#include "sco/net/core.h"
#include "net_mem.h"
#include <stdexcept>
#include <cstdio>
#include <string>
#include <memory>
#include <array>
using namespace sco::net;
#define REQUIRE(x) do { if(!(x)) throw std::runtime_error(#x); } while(0)
struct Authority {
 int value=0;
 static uint64_t incarnation(){static uint64_t next=0;if(next==UINT64_MAX) throw std::runtime_error("incarnation exhausted");return ++next;}
 uint64_t epoch=incarnation();
 struct Queued {uint64_t epoch;unsigned peer;int delta;};
 std::vector<Queued> pending;
 uint64_t overflow=0;
 bool enqueue(unsigned peer,int delta) {if(pending.size()==16) {++overflow;return false;}pending.push_back({epoch,peer,delta});return true;}
 void restart(){epoch=incarnation();}
 void drain(){for(auto q:pending) if(q.epoch==epoch) submit(q.peer,q.delta);pending.clear();}
 bool submit(unsigned peer,int delta) { if(peer<1 || delta < -100 || delta>100 || value+delta < -1000000 || value+delta>1000000) return false; value+=delta; return true; }
};
struct Fixture {
 nettest::MemNet net{91};
 std::array<std::unique_ptr<nettest::MemTransport>,3> tr;
 std::array<std::unique_ptr<Core>,3> core;
 Authority authority;
 Fixture() {
   for(int i=0;i<3;++i) {
     tr[i]=std::make_unique<nettest::MemTransport>(net,i+1);
     Callbacks cb;
     if(i==0) {
       cb.message=[this](const Message&m){if(m.channel=="mp.intent" && m.len==1) authority.enqueue(static_cast<unsigned>(m.from),m.data[0]);};
       cb.peer=[this](PeerEvent e,PeerId p,std::string_view){if(e==PeerEvent::Left) {auto& q=authority.pending;q.erase(std::remove_if(q.begin(),q.end(),[p](auto x){return x.peer==p;}),q.end());}};
     }
     core[i]=std::make_unique<Core>(*tr[i],cb);
     REQUIRE(core[i]->RegisterChannel("mp.intent",kReliable|kToHost,1));
   }
   Options o; o.passphrase="synthetic-passphrase";o.playerName="host";o.pbkdf2Iters=2;
   REQUIRE(core[0]->Host(o,net.now));
   for(int i=1;i<3;++i){o.playerName="peer"+std::to_string(i);REQUIRE(core[i]->Join(o,tr[0]->Self(),net.now));}
   pump(3000); REQUIRE(core[1]->Active()); REQUIRE(core[2]->Active()); REQUIRE(core[0]->Peers().size()==3);
 }
 void pump(unsigned ms) {for(unsigned k=0;k<ms;k+=5){net.now+=5;for(auto&c:core)c->Pump(net.now);}}
 int run(const std::string&mode) {
   const uint8_t a=8,b=4;
   if(mode=="solo") {REQUIRE(authority.submit(2,a));REQUIRE(authority.submit(3,b));}
   else if(mode=="listen") {REQUIRE(authority.submit(2,a));REQUIRE(core[2]->Send("mp.intent",&b,1)==SendResult::Ok);}
   else {REQUIRE(core[1]->Send("mp.intent",&a,1)==SendResult::Ok);REQUIRE(core[2]->Send("mp.intent",&b,1)==SendResult::Ok);}
   pump(5000);authority.drain();return authority.value;
 }
};
int main(int argc,char**argv) { try {
 if(argc!=2) throw std::runtime_error("usage: authority-cli test|solo|listen|dedicated|sc-world");
 std::string mode=argv[1];
 if(mode=="sc-world") throw std::runtime_error("SC world unavailable: synthetic provider only");
 if(mode!="test") {if(mode!="solo"&&mode!="listen"&&mode!="dedicated") throw std::runtime_error("unknown mode");Fixture f;auto state=f.run(mode);std::printf("{\"mode\":\"%s\",\"provider\":\"synthetic-only\",\"sc_world_available\":false,\"value\":%d}\n",mode.c_str(),state);return state==12?0:1;}
 for(const auto&mode:{"solo","listen","dedicated"}) {Fixture f; REQUIRE(f.run(mode)==12);std::printf("PASS %s state=12\n",mode);}
 {Fixture f; f.net.loss=0.25; f.net.dup=0.30; f.net.maxDelayMs=80; REQUIRE(f.run("dedicated")==12); std::puts("PASS SDK reliable progress seeded loss/reorder/duplicate");}
 {Fixture f; std::vector<uint8_t> captured; f.net.tap=[&](auto&d){Packet p;if(captured.empty() && d.from==f.tr[1]->Self() && d.to==f.tr[0]->Self() && Parse(d.bytes.data(),d.bytes.size(),&p)==ParseError::Ok && p.h.kind==Kind::Data) captured=d.bytes;return true;}; const uint8_t x=8; REQUIRE(f.core[1]->Send("mp.intent",&x,1)==SendResult::Ok); f.pump(400); REQUIRE(!captured.empty()); REQUIRE(f.authority.pending.size()==1); f.authority.drain(); REQUIRE(f.authority.value==8); f.net.tap=nullptr; auto before=f.core[0]->GetStats(); f.net.Inject(f.tr[1]->Self(),f.tr[0]->Self(),captured); f.pump(100); auto after=f.core[0]->GetStats(); REQUIRE(after.replayed==before.replayed+1); REQUIRE(after.badTag==before.badTag); REQUIRE(after.spoofed==before.spoofed); REQUIRE(f.authority.pending.empty()); f.authority.drain(); REQUIRE(f.authority.value==8); std::puts("PASS SDK authenticated DATA replay rejected without duplicate intent");}
 {Authority a; REQUIRE(a.enqueue(2,8)); a.restart();a.drain();REQUIRE(a.value==0);std::puts("PASS stale authority queue");}
 {Fixture f; const uint8_t d=8; REQUIRE(f.core[1]->Send("mp.intent",&d,1)==SendResult::Ok);f.pump(500);REQUIRE(f.authority.value==0);f.core[1]->Leave("reconnect");f.pump(100);f.authority.drain();REQUIRE(f.authority.value==0);std::puts("PASS queued disconnect revoked");}
 {Authority a,b;REQUIRE(a.epoch!=b.epoch);std::puts("PASS process-local authority incarnations differ");}
 {Fixture f; std::vector<uint8_t> captured; f.net.tap=[&](auto&d){Packet p;if(Parse(d.bytes.data(),d.bytes.size(),&p)==ParseError::Ok && p.h.kind==Kind::Data && d.from==f.tr[1]->Self())captured=d.bytes;return true;};const uint8_t d=8;REQUIRE(f.core[1]->Send("mp.intent",&d,1)==SendResult::Ok);f.pump(400);REQUIRE(!captured.empty());f.authority.drain();REQUIRE(f.authority.value==8);auto spoof=f.core[0]->GetStats().spoofed;f.net.Inject(nettest::MemEndpoint(99),f.tr[0]->Self(),captured);f.pump(100);REQUIRE(f.core[0]->GetStats().spoofed>spoof);f.authority.drain();REQUIRE(f.authority.value==8);auto bad=f.core[0]->GetStats().badTag;captured.back()^=1;f.net.Inject(f.tr[1]->Self(),f.tr[0]->Self(),captured);f.pump(100);REQUIRE(f.core[0]->GetStats().badTag>bad);std::puts("PASS SDK source spoof and tamper rejection");}
 {Fixture f;f.core[1]->Leave("wrong credential retry");f.pump(100);Options o;o.playerName="bad";o.passphrase="wrong";o.pbkdf2Iters=2;REQUIRE(f.core[1]->Join(o,f.tr[0]->Self(),f.net.now));f.pump(12000);REQUIRE(!f.core[1]->Active());REQUIRE(f.core[0]->Peers().size()==2);std::puts("PASS SDK wrong passphrase");}
 {Fixture f;const uint8_t d=1;bool full=false;for(int i=0;i<10000;++i){auto r=f.core[1]->Send("mp.intent",&d,1);if(r==SendResult::QueueFull){full=true;break;}}REQUIRE(full);f.core[1]->Leave("shutdown");REQUIRE(f.core[1]->Send("mp.intent",&d,1)==SendResult::NoSession);std::puts("PASS SDK bounded reliable queue and shutdown");}
 {Authority a;for(int i=0;i<16;++i)REQUIRE(a.enqueue(2,1));REQUIRE(!a.enqueue(2,1));a.drain();REQUIRE(a.value==16);std::puts("PASS authority queue bound");}
 {Fixture f;std::vector<uint8_t> old;f.net.tap=[&](auto&d){Packet p;if(d.from==f.tr[1]->Self()&&Parse(d.bytes.data(),d.bytes.size(),&p)==ParseError::Ok&&p.h.kind==Kind::Data)old=d.bytes;return true;};uint8_t x=8;REQUIRE(f.core[1]->Send("mp.intent",&x,1)==SendResult::Ok);f.pump(400);REQUIRE(!old.empty());f.core[1]->Leave("reconnect");f.pump(100);Options o;o.passphrase="synthetic-passphrase";o.playerName="peer1";o.pbkdf2Iters=2;REQUIRE(f.core[1]->Join(o,f.tr[0]->Self(),f.net.now));f.pump(3000);REQUIRE(f.core[1]->Active());auto before=f.core[0]->GetStats();f.net.Inject(f.tr[1]->Self(),f.tr[0]->Self(),old);f.pump(100);auto after=f.core[0]->GetStats();REQUIRE(after.badTag+after.spoofed>before.badTag+before.spoofed);f.authority.drain();REQUIRE(f.authority.value==0);std::puts("PASS reconnect rejects old wire and queued intent");REQUIRE(f.core[1]->Send("mp.intent",&x,1)==SendResult::Ok);f.pump(400);f.authority.drain();REQUIRE(f.authority.value==8);
 f.core[0]->Leave("authority restart");f.authority.restart();f.pump(100);o.playerName="host";REQUIRE(f.core[0]->Host(o,f.net.now));o.playerName="peer1";REQUIRE(f.core[1]->Join(o,f.tr[0]->Self(),f.net.now));f.pump(3000);REQUIRE(f.core[1]->Active());before=f.core[0]->GetStats();f.net.Inject(f.tr[1]->Self(),f.tr[0]->Self(),old);f.pump(100);after=f.core[0]->GetStats();REQUIRE(after.badTag+after.spoofed>before.badTag+before.spoofed);f.authority.drain();REQUIRE(f.authority.value==8);REQUIRE(f.core[1]->Send("mp.intent",&x,1)==SendResult::Ok);f.pump(400);f.authority.drain();REQUIRE(f.authority.value==16);std::puts("PASS coordinated restart rejects old wire and accepts fresh intent");}
 {Fixture f;int dataPackets=0;f.net.tap=[&](auto&d){Packet p;if(d.from==f.tr[1]->Self()&&Parse(d.bytes.data(),d.bytes.size(),&p)==ParseError::Ok&&p.h.kind==Kind::Data){++dataPackets;if(dataPackets==1)return false;if(dataPackets==2){f.net.Inject(d.from,d.to,d.bytes);d.bytes.clear();return false;}}return true;};uint8_t d=1;REQUIRE(f.core[1]->Send("mp.intent",&d,1)==SendResult::Ok);d=2;REQUIRE(f.core[1]->Send("mp.intent",&d,1)==SendResult::Ok);f.pump(5000);REQUIRE(dataPackets>=3);REQUIRE(f.core[1]->GetStats().retransmits>0);REQUIRE(f.authority.pending.size()==2);REQUIRE(f.authority.pending[0].delta==1);REQUIRE(f.authority.pending[1].delta==2);f.authority.drain();REQUIRE(f.authority.value==3);std::puts("PASS forced DATA loss and ordered SDK retransmission");}
 {Fixture f;uint8_t x=1;for(int i=0;i<17;++i)REQUIRE(f.core[1]->Send("mp.intent",&x,1)==SendResult::Ok);f.pump(2000);REQUIRE(f.authority.pending.size()==16);REQUIRE(f.authority.overflow==1);f.authority.drain();REQUIRE(f.authority.value==16);std::puts("PASS dispatch overflow explicit rejection count");}
 return 0;
 } catch(const std::exception&e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;} }
