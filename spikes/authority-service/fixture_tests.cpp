// SPDX-License-Identifier: GPL-3.0-only
#include "fixture.h"
#include "sco_net.h"
#include "sco/app.h"
#include "sco/net/session.h"
#include <cstdio>
#include <cstddef>
#include <cstring>

namespace {
const sco_net_v1* returned=nullptr;
sco_result query(const char*,uint32_t,const void** out) { *out=returned;return returned?SCO_OK:SCO_NOT_FOUND; }
int register_calls=0, unregister_calls=0, subscribe_calls=0, unsubscribe_calls=0;
int fail_register=0, fail_subscribe=0;
sco_event_fn peer_callback=nullptr;
sco_result send_result=SCO_OK;
int emitted=0;
char emitted_status[32]{};
void emit(const char*,const char* status,const char*,uint64_t,uint64_t,int64_t,uint64_t,uint64_t) {++emitted;std::snprintf(emitted_status,sizeof(emitted_status),"%s",status);}
sco_result subscribe(sco_plugin*,const char* event,sco_event_fn fn,void*) {if(std::strcmp(event,SCO_NET_EVENT_PEER)==0)peer_callback=fn;return ++subscribe_calls==fail_subscribe?SCO_FAILED:SCO_OK;}
sco_result unsubscribe(sco_plugin*,const char*,sco_event_fn) {++unsubscribe_calls;return SCO_OK;}
int active(){return 0;}
sco_result peers(sco_net_peer*,uint32_t*){return SCO_OK;}
sco_result name(uint64_t,char*,uint32_t){return SCO_OK;}
sco_result send(sco_plugin*,const char*,const void*,uint32_t){return send_result;}
sco_result reg(sco_plugin*,const char*,uint32_t,uint32_t,sco_net_on_message,void*){return ++register_calls==fail_register?SCO_FAILED:SCO_OK;}
sco_result unreg(sco_plugin*,const char*){++unregister_calls;return SCO_OK;}
uint64_t self_peer(){return 0;}
void callback(uint64_t,const void*,uint32_t,void*){}
int failures=0;
void check(bool b,const char* msg) {if(!b){std::fprintf(stderr,"FAIL: %s\n",msg);++failures;}}
}
int main(){
 sco_api api{};api.size=sizeof(api);api.query_service=query;api.subscribe=subscribe;api.unsubscribe=unsubscribe;
 auto* fake=reinterpret_cast<sco_plugin*>(static_cast<uintptr_t>(1));
 sco_net_v1 table{sizeof(table),0,active,peers,name,send,reg,unreg,self_peer};
 check(authority_load(nullptr,fake)==SCO_UNAVAILABLE,"null api refused");
 api.size=offsetof(sco_api,query_service);check(authority_load(&api,fake)==SCO_UNAVAILABLE,"short api refused");api.size=sizeof(api);
 returned=nullptr;check(authority_load(&api,fake)==SCO_UNAVAILABLE,"missing net refused");
 returned=&table;table.size=offsetof(sco_net_v1,self_peer);check(authority_load(&api,fake)==SCO_UNAVAILABLE,"short net refused");table.size=sizeof(table);
 table.is_active=nullptr;check(authority_load(&api,fake)==SCO_UNAVAILABLE,"missing is_active refused");table.is_active=active;
 table.get_peers=nullptr;check(authority_load(&api,fake)==SCO_UNAVAILABLE,"missing get_peers refused");table.get_peers=peers;
 table.get_peer_name=nullptr;check(authority_load(&api,fake)==SCO_UNAVAILABLE,"missing get_peer_name refused");table.get_peer_name=name;
 table.send_channel=nullptr;check(authority_load(&api,fake)==SCO_UNAVAILABLE,"missing send refused");table.send_channel=send;
 table.register_channel=nullptr;check(authority_load(&api,fake)==SCO_UNAVAILABLE,"missing register refused");table.register_channel=reg;
 table.unregister_channel=nullptr;check(authority_load(&api,fake)==SCO_UNAVAILABLE,"missing unregister refused");table.unregister_channel=unreg;
 table.self_peer=nullptr;check(authority_load(&api,fake)==SCO_UNAVAILABLE,"missing self refused");table.self_peer=self_peer;
 register_calls=unregister_calls=0;fail_register=2;
 check(authority_load(&api,fake)==SCO_UNAVAILABLE&&unregister_calls==1&&!fixture_control()->loaded(),"second channel failure unregisters first");
 fail_register=0;register_calls=unregister_calls=subscribe_calls=unsubscribe_calls=0;fail_subscribe=2;
 check(authority_load(&api,fake)==SCO_UNAVAILABLE&&unregister_calls==2&&unsubscribe_calls==1&&!fixture_control()->loaded(),"subscription failure removes subscriptions and channels");
 fail_subscribe=0;
 check(authority_load(&api,fake)==SCO_OK&&fixture_control()->loaded(),"complete table accepted");
 check(fixture_control()->configure(true,false,"11111111111111111111111111111111",emit),"host fixture configured");
 sco_net_peer_event joined{sizeof(joined),SCO_NET_PEER_JOINED,{2,0}};
 emitted=0;peer_callback(SCO_NET_EVENT_PEER,&joined,nullptr);peer_callback(SCO_NET_EVENT_PEER,&joined,nullptr);
 check(emitted==1&&std::strcmp(emitted_status,"Joined")==0,"duplicate join ignored");
 authority_unload();check(!fixture_control()->loaded(),"unload invalidates bridge");
 check(authority_load(&api,fake)==SCO_OK,"client fixture loads");
 check(fixture_control()->configure(false,false,"11111111111111111111111111111111",emit),"client fixture configured");
 send_result=SCO_TOO_MANY;emitted=0;fixture_control()->submit("snapshot");
 check(emitted==1&&std::strcmp(emitted_status,"TooMany")==0,"backpressure result preserved");
 send_result=SCO_BAD_ARG;fixture_control()->submit("snapshot");
 check(std::strcmp(emitted_status,"BadArg")==0,"bad argument result preserved");
 send_result=SCO_OK;authority_unload();
 static const sco::plugins::Builtin builtin{"authority",authority_query,authority_load,authority_unload};sco::app::Platform pf;pf.hostVersion="fixture";pf.pluginsEnabled=false;pf.builtins=&builtin;pf.nBuiltins=1;pf.privateSession.enabled=false;
 check(sco::app::Start(pf),"host starts");auto& entries=sco::app::Plugins();check(entries.size()==1&&entries[0].state==sco::plugins::State::Loaded,"actual loader loaded built-in");
 if(entries.size()==1&&entries[0].state==sco::plugins::State::Loaded){auto* owner=entries[0].self;const auto* net=sco::net::Table();
  // The host owns this mutable list; this test invokes the real loader's mid-session unload.
  auto& entry=const_cast<sco::plugins::Plugin&>(entries[0]);sco::plugins::UnloadOne(entry);
  check(entry.state==sco::plugins::State::Unloaded&&!fixture_control()->loaded(),"owner unloaded");
  check(net->register_channel(owner,"authority.test",0,16,callback,nullptr)==SCO_BAD_ARG,"released owner cannot register");
  sco::app::Stop();uint32_t count=0;check(!net->is_active()&&net->get_peers(nullptr,&count)==SCO_UNAVAILABLE,"host Stop invalidates net table");
 } else sco::app::Stop();
 check(sco::app::Start(pf),"host restarts");
 check(fixture_control()->loaded(),"built-in loaded before host Stop");
 const auto* stopped_net=sco::net::Table();
 sco::app::Stop();
 uint32_t stopped_count=0;
 check(!fixture_control()->loaded(),"host Stop unloads built-in and invalidates bridge");
 check(!stopped_net->is_active()&&stopped_net->get_peers(nullptr,&stopped_count)==SCO_UNAVAILABLE,"host Stop withdraws service");
 return failures?1:0;
}
