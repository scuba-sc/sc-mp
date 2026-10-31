// SPDX-License-Identifier: GPL-3.0-only
// Synthetic policy consumer. All network identities come from the SDK callback.
#include "fixture.h"
#include "sco_net.h"
#include "../authority-session/session.h"
#include <array>
#include <charconv>
#include <cstddef>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {
template<class T> bool number(const std::string& s,T& v) { if(s.empty()) return false; auto r=std::from_chars(s.data(),s.data()+s.size(),v); return r.ec==std::errc{} && r.ptr==s.data()+s.size(); }
bool valid_epoch(const std::string& s) { if(s.size()!=32) return false; for(char c:s) if(!((c>='0'&&c<='9')||(c>='a'&&c<='f'))) return false; return true; }
std::vector<std::string> words(const std::string& s) { std::istringstream in(s); std::vector<std::string> v; std::string x; while(in>>x) { if(v.size()==8) return {}; v.push_back(x); } return v; }
using Emit=void (*)(const char*,const char*,const char*,uint64_t,uint64_t,int64_t,uint64_t,uint64_t);
const char* result_name(sco_result r) { switch(r) {
 case SCO_OK:return "Ok"; case SCO_UNAVAILABLE:return "Unavailable"; case SCO_NOT_FOUND:return "NotFound";
 case SCO_BAD_ARG:return "BadArg"; case SCO_CRASHED:return "Crashed"; case SCO_WRONG_THREAD:return "WrongThread";
 case SCO_TOO_MANY:return "TooMany"; case SCO_FAILED:return "Failed"; default:return "Unknown";
} }
struct State {
 const sco_net_v1* net=nullptr; sco_plugin* self=nullptr; Emit emit=nullptr; bool host=false,local=false,configured=false; std::string ep; std::unique_ptr<authority::Session> session; std::array<uint64_t,16> peers{}; uint64_t context_at=0;
 void output(const char* kind,uint64_t peer,const authority::Result& r) { emit(kind,r.status,ep.c_str(),peer,r.entity,r.value,r.revision,r.id); }
 void send(const char* ch,const std::string& s) { auto r=net->send_channel(self,ch,s.data(),static_cast<uint32_t>(s.size())); if(r!=SCO_OK) emit("send",result_name(r),host?ep.c_str():"",net->self_peer(),0,0,0,0); }
 void result(uint64_t peer,const authority::Result& r,const char* kind) { std::string s=std::string(kind)+" "+std::to_string(peer)+" "+r.status+" "+std::to_string(r.id)+" "+std::to_string(r.entity)+" "+std::to_string(r.value)+" "+std::to_string(r.revision)+" "+ep; send("authority.out",s); output(kind,peer,r); }
 void contexts() { for(auto p:peers) if(p) { auto r=session->snapshot(p); send("authority.out","context "+std::to_string(p)+" "+std::to_string(r.entity)+" "+ep+" world "+ep); } }
 void message(bool incoming,uint64_t from,const void* data,uint32_t len) {
  if(!configured || len>256 || !data) return;
  auto w=words(std::string(static_cast<const char*>(data),len));
  if(host && incoming) {
   if(w.size()==5 && w[0]=="command" && valid_epoch(w[1])) { uint64_t id=0,e=0; int64_t d=0; if(number(w[2],id)&&number(w[3],e)&&number(w[4],d)) result(from,session->command(from,w[1],id,e,d),"receipt"); }
   else if(w.size()==1 && w[0]=="snapshot") result(from,session->snapshot(from),"snapshot");
  } else if(!host && !incoming && from==1) {
   uint64_t p=0,e=0,id=0,rev=0; int64_t value=0;
   if(w.size()==6 && w[0]=="context" && number(w[1],p)&&p==net->self_peer()&&number(w[2],e)&&valid_epoch(w[3])&&w[4]=="world"&&w[5]==w[3]) { if(ep!=w[3]) { ep=w[3]; emit("context",e?"Ok":"Overload",ep.c_str(),p,e,0,0,0); } }
   else if(w.size()==8 && (w[0]=="receipt" || w[0]=="snapshot") && number(w[1],p) && p==net->self_peer() && number(w[3],id) && number(w[4],e) && number(w[5],value) && number(w[6],rev) && w[7]==ep) {
    const auto& s=w[2]; if(s=="Applied"||s=="Exhausted"||s=="Invalid"||s=="Forbidden"||s=="Stale"||s=="Disconnected"||s=="Conflict"||s=="Old"||s=="Gap"||s=="Ok") emit(w[0].c_str(),s.c_str(),ep.c_str(),p,e,value,rev,id);
   }
  }
 }
};
State g;
void incoming(uint64_t p,const void* b,uint32_t n,void*) { g.message(true,p,b,n); }
void outgoing(uint64_t p,const void* b,uint32_t n,void*) { g.message(false,p,b,n); }
void event(const char* name,const void* data,void*) {
 if(!g.configured || !data) return;
 if(std::string(name)==SCO_NET_EVENT_PEER) { const auto* e=static_cast<const sco_net_peer_event*>(data); if(e->size<sizeof(*e) || !g.host || e->peer.peer_id==1) return; auto p=e->peer.peer_id;
  if(e->what==SCO_NET_PEER_JOINED) { for(auto existing:g.peers) if(existing==p) return; auto entity=g.session->join(p); for(auto& slot:g.peers) if(!slot) {slot=p;break;} g.emit("peer",entity?"Joined":"Overload",g.ep.c_str(),p,entity,0,0,0); g.context_at=0; }
  else if(e->what==SCO_NET_PEER_LEFT) {g.session->leave(p);for(auto& slot:g.peers) if(slot==p) slot=0;g.emit("peer","Left",g.ep.c_str(),p,0,0,0,0);}
 } else if(std::string(name)==SCO_NET_EVENT_STATE) { const auto* e=static_cast<const sco_net_state_event*>(data); if(e->size>=sizeof(*e)&&!e->active) {if(!g.host)g.ep.clear();g.emit("state","Down",g.host?g.ep.c_str():"",0,0,0,0,0);} }
}
bool configure(bool host,bool local,const char* ep,Emit emit) { if(!g.net||g.configured||!ep||!valid_epoch(ep)||!emit) return false; g.host=host;g.local=local;g.ep=host?ep:"";g.emit=emit;g.session=std::make_unique<authority::Session>(ep);g.configured=true;if(local)g.session->join(1);return true; }
void submit(const char* line) { if(!g.configured||!line) return; auto w=words(line);
 if(w.size()==1&&w[0]=="snapshot") {if(g.local)g.output("snapshot",1,g.session->snapshot(1));else if(!g.host)g.send("authority.in","snapshot");else g.emit("error","NoLocalPeer",g.ep.c_str(),0,0,0,0,0);return;}
 if(w.size()==5&&w[0]=="command"&&valid_epoch(w[1])) {uint64_t id=0,e=0;int64_t d=0;if(number(w[2],id)&&number(w[3],e)&&number(w[4],d)) {if(g.local)g.output("receipt",1,g.session->command(1,w[1],id,e,d));else if(!g.host)g.send("authority.in",line);else g.emit("error","NoLocalPeer",g.ep.c_str(),0,0,0,0,0);return;}}
 g.emit("error","InvalidInput",g.host?g.ep.c_str():"",0,0,0,0,0);
}
void tick(uint64_t now) { if(g.configured&&g.host&&now>=g.context_at) {g.contexts();g.context_at=now+1000;} }
bool loaded() {return g.net!=nullptr;}
const FixtureControl control{sizeof(FixtureControl),configure,submit,tick,loaded};
}
const FixtureControl* fixture_control() { return &control; }
const sco_plugin_info* authority_query() { static const sco_plugin_info info{sizeof(info),SCO_API_MAJOR,SCO_API_MINOR,"authority","0.0.1","fixture"};return &info; }
sco_result authority_load(const sco_api* api,sco_plugin* self) {
 if(!api||!self||api->size<offsetof(sco_api,query_service)+sizeof(api->query_service)||!api->query_service||api->size<offsetof(sco_api,subscribe)+sizeof(api->subscribe)||!api->subscribe||api->size<offsetof(sco_api,unsubscribe)+sizeof(api->unsubscribe)||!api->unsubscribe) return SCO_UNAVAILABLE;
 const void* table=nullptr; if(api->query_service(SCO_NET_NAME,SCO_NET_VERSION_1_0,&table)!=SCO_OK||!table) return SCO_UNAVAILABLE;
 const auto* n=static_cast<const sco_net_v1*>(table); if(n->size<sizeof(sco_net_v1)||!n->is_active||!n->get_peers||!n->get_peer_name||!n->send_channel||!n->register_channel||!n->unregister_channel||!n->self_peer) return SCO_UNAVAILABLE;
 g.net=n;g.self=self;
 bool in=false,out=false,peer=false;
 if(n->register_channel(self,"authority.in",SCO_NET_RELIABLE|SCO_NET_TO_HOST,256,incoming,nullptr)==SCO_OK) {
  in=true;
  if(n->register_channel(self,"authority.out",SCO_NET_RELIABLE|SCO_NET_FROM_HOST,256,outgoing,nullptr)==SCO_OK) {
   out=true;
   if(api->subscribe(self,SCO_NET_EVENT_PEER,event,nullptr)==SCO_OK) {
    peer=true;
    if(api->subscribe(self,SCO_NET_EVENT_STATE,event,nullptr)==SCO_OK) return SCO_OK;
   }
  }
 }
 if(peer) api->unsubscribe(self,SCO_NET_EVENT_PEER,event);
 if(out) n->unregister_channel(self,"authority.out");
 if(in) n->unregister_channel(self,"authority.in");
 g=State{};return SCO_UNAVAILABLE;
}
void authority_unload() {g=State{};}
