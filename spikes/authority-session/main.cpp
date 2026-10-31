// SPDX-License-Identifier: GPL-3.0-only
// Synthetic fixture only: fixed test credential and intentionally weak PBKDF2 work factor.
#include "session.h"
#include "sco/net/core.h"
#include "sco/net/scope.h"
#include "sco/net/udp.h"
#include <array>
#include <charconv>
#include <chrono>
#include <cstring>
#include <csignal>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <poll.h>
#include <random>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>
using namespace sco::net;
static uint64_t now() { return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); }
static std::string epoch() { std::random_device r; static constexpr char hex[]="0123456789abcdef"; for(int attempt=0;attempt<4;++attempt) { std::string s; for(int i=0;i<16;++i) { auto b=static_cast<unsigned>(r())&255U; s+=hex[b>>4]; s+=hex[b&15]; } if(s!=std::string(32,'0')) return s; } return {}; }
static bool valid_epoch(const std::string& s) { if(s.size()!=32) return false; for(char c:s) if(!((c>='0'&&c<='9')||(c>='a'&&c<='f'))) return false; return true; }
template<class T> static bool number(const std::string& s,T& v) { if(s.empty()) return false; auto r=std::from_chars(s.data(),s.data()+s.size(),v); return r.ec==std::errc{} && r.ptr==s.data()+s.size(); }
static std::vector<std::string> words(const std::string& s) { std::istringstream in(s); std::vector<std::string> v; std::string x; while(in>>x) { if(v.size()==8) return {}; v.push_back(x); } return v; }
static void emit(const char* kind,const char* status,const std::string& ep,uint64_t peer,uint64_t entity,int64_t value,uint64_t rev,uint64_t id=0) {
 std::ostringstream out;
 out << "{\"kind\":\""<<kind<<"\",\"status\":\""<<status<<"\",\"epoch\":\""<<ep<<"\",\"world\":\""<<ep<<"\",\"peer\":"<<peer<<",\"entity\":"<<entity<<",\"value\":"<<value<<",\"revision\":"<<rev<<",\"id\":"<<id<<"}\n";
 const auto line=out.str();
 // A stalled or closed harness cannot hold the session indefinitely. Each line is below PIPE_BUF.
 if(write(STDOUT_FILENO,line.data(),line.size())!=static_cast<ssize_t>(line.size())) _exit(2);
}
struct App {
 bool host, local; std::string ep; authority::Session session; std::array<uint64_t,16> peers{}; std::unique_ptr<udp::Socket> socket; std::unique_ptr<ScopedTransport> scoped; std::unique_ptr<Core> core; bool done=false; uint64_t context_at=0;
 App(bool h,bool l):host(h),local(l),ep(epoch()),session(ep) { if(local) session.join(1); }
 void output(const char* kind,uint64_t peer,const authority::Result& r) { emit(kind,r.status,ep,peer,r.entity,r.value,r.revision,r.id); }
 void send(const char* channel,const std::string& s) { auto result=core->Send(channel,s.data(),static_cast<uint32_t>(s.size())); if(result!=SendResult::Ok) emit("send",SendResultName(result),host?ep:"",core->Self(),0,0,0); }
 void result(uint64_t peer,const authority::Result& r,const char* kind) { std::string s=std::string(kind)+" "+std::to_string(peer)+" "+r.status+" "+std::to_string(r.id)+" "+std::to_string(r.entity)+" "+std::to_string(r.value)+" "+std::to_string(r.revision)+" "+ep; send("authority.out",s); output(kind,peer,r); }
 void contexts() { for(auto p:peers) if(p) { auto r=session.snapshot(p); send("authority.out","context "+std::to_string(p)+" "+std::to_string(r.entity)+" "+ep+" world "+ep); } }
 void message(const Message& m) {
  if(m.len>256) return;
  auto w=words(std::string(reinterpret_cast<const char*>(m.data),m.len));
  if(host && m.channel=="authority.in") {
   if(w.size()==5 && w[0]=="command" && valid_epoch(w[1])) { uint64_t id=0,e=0; int64_t d=0; if(number(w[2],id)&&number(w[3],e)&&number(w[4],d)) result(m.from,session.command(m.from,w[1],id,e,d),"receipt"); }
   else if(w.size()==1 && w[0]=="snapshot") result(m.from,session.snapshot(m.from),"snapshot");
  } else if(!host && m.from==kHostPeer && m.channel=="authority.out") {
   uint64_t p=0,e=0,id=0,rev=0; int64_t value=0;
   if(w.size()==6 && w[0]=="context" && number(w[1],p)&&p==core->Self()&&number(w[2],e)&&valid_epoch(w[3])&&w[4]=="world"&&w[5]==w[3]) { if(ep!=w[3]) { ep=w[3]; emit("context",e?"Ok":"Overload",ep,p,e,0,0); } }
   else if(w.size()==8 && (w[0]=="receipt" || w[0]=="snapshot") && number(w[1],p) && p==core->Self() && number(w[3],id) && number(w[4],e) && number(w[5],value) && number(w[6],rev) && w[7]==ep) {
    const auto& status=w[2];
    if(status=="Applied" || status=="Exhausted" || status=="Invalid" || status=="Forbidden" || status=="Stale" || status=="Disconnected" || status=="Conflict" || status=="Old" || status=="Gap" || status=="Ok") emit(w[0].c_str(),status.c_str(),ep,p,e,value,rev,id);
   }
  }
 }
};
static bool setup(App& a,uint16_t port) {
 std::string why; a.socket=udp::Socket::Open(a.host?port:0,true,&why); if(!a.socket) return false;
 Scope scope; Cidr cidr; if(!ParseCidr("127.0.0.0/8",&cidr)) return false; scope.allowOnly=true; scope.allow.push_back(cidr); scope.maxDatagramsPerSecond=1000;
 a.scoped=std::make_unique<ScopedTransport>(*a.socket,scope);
 Callbacks cb;
 cb.message=[&](const Message& m) { a.message(m); };
 cb.peer=[&](PeerEvent ev,PeerId p,std::string_view) {
  if(!a.host || p==1) return;
  if(ev==PeerEvent::Joined) { auto e=a.session.join(p); for(auto& slot:a.peers) if(!slot) { slot=p; break; } emit("peer",e?"Joined":"Overload",a.ep,p,e,0,0); a.context_at=0; }
  else { a.session.leave(p); for(auto& slot:a.peers) if(slot==p) slot=0; emit("peer","Left",a.ep,p,0,0,0); }
 };
 cb.state=[&](bool active,std::string_view) { if(!active) { if(!a.host) a.ep.clear(); emit("state","Down",a.host?a.ep:"",0,0,0,0); } };
 a.core=std::make_unique<Core>(*a.scoped,cb);
 if(!a.core->RegisterChannel("authority.in",kReliable|kToHost,256) || !a.core->RegisterChannel("authority.out",kReliable|kFromHost,256)) return false;
 Options o; o.playerName=a.host?"server":"client"; o.passphrase="synthetic fixture only"; o.pbkdf2Iters=2; o.maxPeers=16;
 if(a.host) return a.core->Host(o,now());
 Endpoint target; return ParseAddress("127.0.0.1",port,&target) && a.core->Join(o,target,now());
}
static void control(App& a,const std::string& line) {
 auto w=words(line);
 if(w.size()==1 && (w[0]=="quit" || w[0]=="disconnect")) { a.done=true; return; }
 if(w.size()==1 && w[0]=="snapshot") { if(a.local) a.output("snapshot",1,a.session.snapshot(1)); else if(!a.host) a.send("authority.in","snapshot"); else emit("error","NoLocalPeer",a.ep,0,0,0,0); return; }
 if(w.size()==5 && w[0]=="command" && valid_epoch(w[1])) {
  uint64_t id=0,e=0; int64_t d=0;
  if(number(w[2],id)&&number(w[3],e)&&number(w[4],d)) {
   if(a.local) a.output("receipt",1,a.session.command(1,w[1],id,e,d));
   else if(!a.host) a.send("authority.in",line);
   else emit("error","NoLocalPeer",a.ep,0,0,0,0);
   return;
  }
 }
 emit("error","InvalidInput",a.host||a.local?a.ep:"",0,0,0,0);
}
int main(int argc,char** argv) {
 std::signal(SIGPIPE,SIG_IGN);
 const int flags=fcntl(STDOUT_FILENO,F_GETFL);
 if(flags<0 || fcntl(STDOUT_FILENO,F_SETFL,flags|O_NONBLOCK)<0) return 2;
 if(argc<2 || argc>3) return 1;
 std::string mode=argv[1]; if(mode=="sc-world") { emit("error","Unavailable","",0,0,0,0); return 1; }
 bool solo=mode=="solo", listen=mode=="listen", dedicated=mode=="dedicated", client=mode=="client";
 if(!(solo||listen||dedicated||client) || (solo?argc!=2:argc!=3)) return 1;
 unsigned port=0; if(!solo && (!number(std::string(argv[2]),port)||!port||port>65535)) return 1;
 App a(!client,solo||listen);
 if(a.ep.empty()) return 2;
 if(!solo && !setup(a,static_cast<uint16_t>(port))) { emit("error","SetupFailed","",0,0,0,0); return 1; }
 if(a.local) emit("context","Ok",a.ep,1,1,0,0); else if(dedicated) emit("ready","Ok",a.ep,0,0,0,0); else emit("ready","Joining","",0,0,0,0);
 std::string buffer; bool overflow=false;
 while(!a.done) {
  if(a.core) { a.core->Pump(now()); if(a.host && now()>=a.context_at) { a.contexts(); a.context_at=now()+1000; } }
  pollfd fd{STDIN_FILENO,POLLIN,0}; int rc=poll(&fd,1,10);
  if(rc<0) break;
  if(rc>0 && (fd.revents&(POLLIN|POLLHUP))) {
   char bytes[256]; auto n=read(STDIN_FILENO,bytes,sizeof(bytes)); if(n<=0) break;
   for(ssize_t i=0;i<n;++i) { char c=bytes[i]; if(c=='\n') { if(overflow) emit("error","InvalidInput","",0,0,0,0); else control(a,buffer); buffer.clear(); overflow=false; if(a.done) break; } else if(buffer.size()<256 && !overflow && c!='\0' && c!='\r') buffer+=c; else overflow=true; }
  }
 }
 if(a.core) a.core->Leave("fixture shutdown");
 return 0;
}
