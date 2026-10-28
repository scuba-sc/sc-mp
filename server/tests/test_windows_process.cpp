// SPDX-License-Identifier: GPL-3.0-only
// Real child relay, authenticated loopback clients and an operator stop event.
#include <winsock2.h>
#include <windows.h>
#include "sco/net/core.h"
#include "sco/net/transport.h"
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

uint64_t Now() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
class Socket final : public sco::net::Transport {
public:
    SOCKET fd = INVALID_SOCKET;
    WSAEVENT event = WSA_INVALID_EVENT;
    Socket() {
        fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP); assert(fd != INVALID_SOCKET);
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        assert(bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0);
        event = WSACreateEvent(); assert(event != WSA_INVALID_EVENT);
        assert(WSAEventSelect(fd, event, FD_READ) == 0);
    }
    ~Socket() override { closesocket(fd); WSACloseEvent(event); }
    bool Send(const sco::net::Endpoint& to, const uint8_t* bytes, size_t count) override {
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(to.port); std::memcpy(&a.sin_addr, to.addr, 4);
        return sendto(fd, reinterpret_cast<const char*>(bytes), static_cast<int>(count), 0,
            reinterpret_cast<sockaddr*>(&a), sizeof a) == static_cast<int>(count);
    }
    bool Receive(sco::net::Endpoint* from, uint8_t* bytes, size_t cap, size_t* count) override {
        sockaddr_in a{}; int n = sizeof a;
        const int r = recvfrom(fd, reinterpret_cast<char*>(bytes), static_cast<int>(cap), 0,
            reinterpret_cast<sockaddr*>(&a), &n);
        if (r < 0) return false;
        *from = {}; from->family = sco::net::Endpoint::kIpv4; from->port = ntohs(a.sin_port);
        std::memcpy(from->addr, &a.sin_addr, 4); *count = static_cast<size_t>(r); return true;
    }
    uint16_t Port() const { sockaddr_in a{}; int n=sizeof a; assert(getsockname(fd,reinterpret_cast<sockaddr*>(&a),&n)==0); return ntohs(a.sin_port); }
    void Reset() { WSANETWORKEVENTS events{}; assert(WSAEnumNetworkEvents(fd,event,&events)==0); }
};
struct Relay {
    HANDLE stop = nullptr, process = nullptr, output = nullptr;
    std::thread reader;
    std::mutex mutex; std::condition_variable changed; std::string log; bool ended = false;
    Relay(const std::wstring& executable, const std::filesystem::path& config) {
        const std::wstring name=L"Local\\sc-mp-proof-"+std::to_wstring(GetCurrentProcessId());
        stop=CreateEventW(nullptr,TRUE,FALSE,name.c_str()); assert(stop);
        SECURITY_ATTRIBUTES security{sizeof security,nullptr,TRUE}; HANDLE write=nullptr;
        assert(CreatePipe(&output,&write,&security,0)); SetHandleInformation(output,HANDLE_FLAG_INHERIT,0);
        STARTUPINFOW startup{}; startup.cb=sizeof startup; startup.dwFlags=STARTF_USESTDHANDLES;
        startup.hStdOutput=write; startup.hStdError=write;
        startup.hStdInput=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr);
        std::wstring command=L"\""+executable+L"\" \""+config.wstring()+L"\" --stop-event \""+name+L"\"";
        PROCESS_INFORMATION pi{};
        assert(CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&pi));
        process=pi.hProcess; CloseHandle(pi.hThread); CloseHandle(write); CloseHandle(startup.hStdInput);
        reader=std::thread([this]{char b[1024];DWORD n=0;while(ReadFile(output,b,sizeof b,&n,nullptr)&&n){
            {std::lock_guard lock(mutex);log.append(b,n);}changed.notify_all();}
            {std::lock_guard lock(mutex);ended=true;}changed.notify_all();});
    }
    void Ready() { std::unique_lock lock(mutex);assert(changed.wait_for(lock,std::chrono::seconds(15),[&]{return ended||log.find("session is up")!=std::string::npos;}));assert(!ended); }
    void Stop() { assert(SetEvent(stop));assert(WaitForSingleObject(process,15000)==WAIT_OBJECT_0);DWORD code=1;assert(GetExitCodeProcess(process,&code)&&code==0); }
    ~Relay(){SetEvent(stop);if(WaitForSingleObject(process,3000)==WAIT_TIMEOUT)TerminateProcess(process,1);if(reader.joinable())reader.join();CloseHandle(output);CloseHandle(process);CloseHandle(stop);}
};
int wmain(int argc,wchar_t** argv) {
    assert(argc==2);WSADATA data{};assert(WSAStartup(MAKEWORD(2,2),&data)==0);
    Socket reserved; const auto port=reserved.Port(); closesocket(reserved.fd);reserved.fd=INVALID_SOCKET;
    const auto root=std::filesystem::temp_directory_path()/("sc-mp-process-"+std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(root);const auto config=root/"relay.conf";
    {std::ofstream f(config);f<<"passphrase = local proof passphrase\nmax_players = 2\nport = "<<port<<"\n";}
    {
        Relay relay(argv[1],config);relay.Ready();
        Socket a,b,bad; bool got=false,wrong=false,downA=false,downB=false;
        sco::net::Callbacks ca,cb,cc;
        ca.state=[&](bool up,std::string_view){if(!up)downA=true;};
        cb.state=[&](bool up,std::string_view){if(!up)downB=true;};
        cb.message=[&](const sco::net::Message&m){if(m.channel=="proof.pose"){assert(m.from!=sco::net::kHostPeer);assert(m.len==3&&m.data[0]==7);got=true;}};
        cc.state=[&](bool up,std::string_view){if(!up)wrong=true;};
        sco::net::Core first(a,ca),second(b,cb),refused(bad,cc);
        assert(first.RegisterChannel("proof.pose",sco::net::kReliable,32));assert(second.RegisterChannel("proof.pose",sco::net::kReliable,32));
        sco::net::Endpoint target{};target.family=4;target.addr[0]=127;target.addr[3]=1;target.port=port;
        sco::net::Options options;options.passphrase="local proof passphrase";options.playerName="probe_one";
        assert(first.Join(options,target,Now()));options.playerName="probe_two";assert(second.Join(options,target,Now()));
        bool sent=false,joined=false;const uint64_t deadline=Now()+15000;
        WSAEVENT events[]{a.event,b.event,bad.event};
        while(!got||!wrong){
            first.Pump(Now());second.Pump(Now());refused.Pump(Now());
            if(!joined&&first.Active()&&second.Active()&&first.Peers().size()==3&&second.Peers().size()==3){
                options.playerName="bad_probe";options.passphrase="incorrect proof passphrase";
                assert(refused.Join(options,target,Now()));joined=true;
            }
            if(joined&&!sent){const uint8_t payload[]{7,8,9};sent=first.Send("proof.pose",payload,3)==sco::net::SendResult::Ok;}
            if(got&&wrong)break;
            const auto now=Now();assert(now<deadline);
            const auto wait=WSAWaitForMultipleEvents(3,events,FALSE,static_cast<DWORD>(deadline-now),FALSE);
            assert(wait>=WSA_WAIT_EVENT_0&&wait<WSA_WAIT_EVENT_0+3);
            const auto index=wait-WSA_WAIT_EVENT_0;if(index==0)a.Reset();else if(index==1)b.Reset();else bad.Reset();
        }
        relay.Stop();
        const auto end=Now()+5000;
        while(!downA||!downB){first.Pump(Now());second.Pump(Now());if(downA&&downB)break;const auto now=Now();assert(now<end);
            const auto wait=WSAWaitForMultipleEvents(2,events,FALSE,static_cast<DWORD>(end-now),FALSE);
            assert(wait>=WSA_WAIT_EVENT_0&&wait<WSA_WAIT_EVENT_0+2);if(wait==WSA_WAIT_EVENT_0)a.Reset();else b.Reset();}
    }
    std::filesystem::remove_all(root);WSACleanup();
    std::puts("RELAY_PROCESS_PASS real child, two authenticated clients, routing, wrong credentials and clean shutdown");
}
