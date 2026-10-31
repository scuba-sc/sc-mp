#pragma once
#include "sco/net/core.h"
#include "bounded_mem.h"
#include "receipt_protocol.h"
#include <array>
#include <memory>
#include <cstdio>
#include <cstring>
#include <stdexcept>
using namespace sco::net;
#ifndef CHECK
#define CHECK(x) do {if(!(x))throw std::runtime_error(#x);}while(0)
#endif
using Bytes=std::array<uint8_t,40>;
inline void put(Bytes& b,size_t at,uint64_t n){for(int i=0;i<8;++i)b[at+i]=uint8_t(n>>(8*i));}
inline uint64_t get(const uint8_t* b,size_t at){uint64_t n=0;for(int i=0;i<8;++i)n|=uint64_t(b[at+i])<<(8*i);return n;}
namespace receipt_fixture {
struct Fixture {
 boundedtest::MemNet net{91};
 std::array<std::unique_ptr<boundedtest::MemTransport>,3> transports;
 std::array<std::unique_ptr<Core>,3> cores;
 receipt::Authority authority;
 std::array<std::map<uint64_t,receipt::Result>,3> results;
 size_t receiptSendFailures=0;
 void emit(receipt::Result r){Bytes b{};put(b,0,r.peer);put(b,8,r.epoch);put(b,16,r.id);put(b,24,unsigned(r.status));put(b,32,uint32_t(r.value));if(cores[0]->Send("mp.result",b.data(),b.size())!=SendResult::Ok)++receiptSendFailures;}
 Fixture(){
  for(int i=0;i<3;++i){transports[i]=std::make_unique<boundedtest::MemTransport>(net,i+1);Callbacks cb;
   cb.message=[this,i](const Message&m){if(i==0&&m.channel=="mp.command"&&m.len==24&&get(m.data,16)<=UINT32_MAX){receipt::Command c{get(m.data,0),get(m.data,8),int32_t(uint32_t(get(m.data,16)))};emit(authority.admit(m.from,c));}
    if(i!=0&&m.channel=="mp.result"&&m.from==kHostPeer&&m.len==40&&get(m.data,0)==cores[i]->Self()&&get(m.data,8)==authority.epoch&&get(m.data,16)!=0&&get(m.data,24)<=unsigned(receipt::Status::Disconnected)&&get(m.data,32)<=UINT32_MAX){receipt::Result r{get(m.data,0),get(m.data,8),get(m.data,16),receipt::Status(get(m.data,24)),int32_t(uint32_t(get(m.data,32)))};if(r.status==receipt::Status::Conflict)return;auto& out=results[i];if(out.count(r.id)&&receipt::terminal(out.at(r.id).status)&&out.at(r.id).status!=receipt::Status::Old)return;if(out.size()==32&&!out.count(r.id))out.erase(out.begin());out[r.id]=r;}};
   if(i==0)cb.peer=[this](PeerEvent e,PeerId p,std::string_view){if(e==PeerEvent::Left)authority.disconnect(p);};
   cores[i]=std::make_unique<Core>(*transports[i],cb);CHECK(cores[i]->RegisterChannel("mp.command",kReliable|kToHost,24));CHECK(cores[i]->RegisterChannel("mp.result",kReliable|kFromHost,40));}
  Options o;o.passphrase="synthetic-passphrase";o.pbkdf2Iters=2;o.playerName="host";CHECK(cores[0]->Host(o,net.now));for(int i=1;i<3;++i){o.playerName="peer";CHECK(cores[i]->Join(o,transports[0]->Self(),net.now));}pump(3000);CHECK(cores[1]->Active());CHECK(cores[2]->Active());
 }
 void pump(unsigned ms){for(unsigned k=0;k<ms;k+=5){net.now+=5;for(auto&c:cores)c->Pump(net.now);}}
 void send(int i,uint64_t id,int delta){Bytes b{};put(b,0,authority.epoch);put(b,8,id);put(b,16,uint32_t(delta));CHECK(cores[i]->Send("mp.command",b.data(),24)==SendResult::Ok);}
 void step(){for(auto r:authority.step())emit(r);}
};
}
