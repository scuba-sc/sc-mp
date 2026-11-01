// SPDX-License-Identifier: GPL-3.0-only
// Actual independently loaded module, synthetic public services only. No sockets,
// game, private host API or authentic EAC/admission evidence.
#include <sco_api.h>
#include <sco_net.h>
#include <sco_storage.h>
#include <sco_widgets.h>
#include <sc_private_session.h>
#include <sc_spatial.h>
#include <sc_game_spatial.h>
#include <sc_entities.h>
#include <sc_actors.h>
#include "../plugins/multiplayer/multiplayer_wire.h"
#include "../plugins/multiplayer/relay_wire.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <initializer_list>
#include <algorithm>
#include <chrono>
#include <map>
#include <string>
#include <thread>
#include <vector>
#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace {
unsigned passed=0,failed=0;
#define CHECK(x) do { if(x) ++passed;else { ++failed;std::printf("FAIL %d: %s\n",__LINE__,#x); } } while(0)
sco_plugin* owner=reinterpret_cast<sco_plugin*>(uintptr_t{42});
std::map<std::string,sco_command> commands;
std::map<std::string,sco_event_fn> events;
std::map<std::string,sco_net_on_message> messages;
std::map<std::string,std::string> db,prior;
std::string failGetKey;
unsigned hostCalls=0,joinCalls=0,leaveCalls=0,spawnCalls=0,removeCalls=0,transformCalls=0,sendCalls=0;
bool brokerPresent=true,networkActive=false,admitted=true,inTx=false,widgetsPresent=true,storagePresent=true,failPut=false;
uint32_t widgetVersion=0,storageVersion=0;
uint32_t state=0,memberTotal=2,getPeersCalls=0;
unsigned panelUpdates=0;
sco_widget_event_fn panelCallback=nullptr;
bool maskSeen=false,rememberSeen=false,hostSeen=false,joinSeen=false,panelRemoved=false;
std::string passInPanel,addressInPanel;
int profileIndex=-1;
namespace rw=sco::relay::wire;
namespace mpprotocol=sco::relay::wire::protocol;
uint64_t selfId=1;                               // this player's peer id in the synthetic session
std::vector<uint64_t> members={1,2};             // the session's peers, self among them
bool serverIsRelay=false;                        // peer 1 is a relay server (its name says so)
std::vector<std::string> modLog,panelRows;       // mod.log lines the module wrote; the panel's "Other players" rows
std::string panelStatus,panelReply;
std::map<std::string,uint32_t> channelFlags,channelMax;
std::map<std::string,unsigned> sentCount;
std::map<std::string,std::vector<uint8_t>> sentLast;
bool refuseProtocol=false;
std::vector<std::string> chatRows;               // the panel's chat history (node 20)
std::string chatDraft,chatNotice;                // the message box (node 21) and the not-encrypted notice (node 23)
bool chatSendDisabled=false;                     // Send (node 22) is greyed out
bool failChatSend=false;                         // sco.net refuses to take a chat message
std::map<uint64_t,std::string> peerNames;        // names sco.net reports; unset: "Remote", or "Relay Server" for peer 1 of a relay
bool refuseNear=false;                           // sco.net refuses to register a multiplayer.near.<slot> channel
using Bytes=std::vector<uint8_t>;
void LogFn(sco_plugin* s,sco_log_level,const char* text) { CHECK(s==owner && text);modLog.emplace_back(text?text:""); }
unsigned LogCount(const char* part) { unsigned n=0;for(const auto& l:modLog) if(l.find(part)!=std::string::npos) ++n;return n; }
sco_result Register(sco_plugin* s,const sco_command* c) {
    if(s!=owner || !c || c->size<sizeof(*c) || !c->fn || !c->name) return SCO_BAD_ARG;
    if(c->nargs && (!c->args || c->arg_def_size!=sizeof(sco_arg_def))) return SCO_BAD_ARG;
    if(commands.count(c->name)) return SCO_BAD_ARG;
    commands[c->name]=*c;return SCO_OK;
}
sco_result Subscribe(sco_plugin*,const char* event,sco_event_fn fn,void*) { events[event]=fn;return SCO_OK; }
sco_result Unsubscribe(sco_plugin*,const char* event,sco_event_fn) { events.erase(event);return SCO_OK; }
sco_result SetPanel(sco_plugin* s,const char* id,const char*,const sco_widget_node* nodes,uint32_t n,uint32_t stride,sco_widget_event_fn cb,void*) {
    if(s!=owner || std::strcmp(id,"multiplayer.session") || stride!=sizeof(sco_widget_node_1_2)) return SCO_BAD_ARG;
    ++panelUpdates;
    panelCallback=cb;hostSeen=joinSeen=maskSeen=rememberSeen=false;CHECK(n==23); // nodes 1-19 as before, then the chat's 20-23
    for(uint32_t i=0;i<n;++i) {
        const auto* ext=reinterpret_cast<const sco_widget_node_1_2*>(reinterpret_cast<const char*>(nodes)+i*stride);
        CHECK(ext->base.base.size==sizeof(*ext) && ext->tag && *ext->tag);
        const sco_widget_node* node=&ext->base.base;
        if(node->id==1 && node->kind==SCO_WIDGET_BUTTON) hostSeen=true;
        if(node->id==2 && node->kind==SCO_WIDGET_BUTTON) joinSeen=true;
        if(node->id==5) addressInPanel=node->text;
        if(node->id==6) { maskSeen=(node->flags&SCO_WIDGET_PASSWORD)!=0;passInPanel=node->text; }
        if(node->id==11) rememberSeen=node->value!=0;
        if(node->id==12) panelStatus=node->label;
        if(node->id==14) panelReply=node->label;
        if(node->id==16) { panelRows.clear();for(uint32_t j=0;j<node->item_count;++j) panelRows.emplace_back(node->items[j]); }
        if(node->id==18) profileIndex=node->index;
        if(node->id==20) { chatRows.clear();for(uint32_t j=0;j<node->item_count;++j) chatRows.emplace_back(node->items[j]);CHECK(node->kind==SCO_WIDGET_LIST); }
        if(node->id==21) { chatDraft=node->text?node->text:"";CHECK(node->kind==SCO_WIDGET_TEXT_INPUT && node->label && *node->label); }
        if(node->id==22) { chatSendDisabled=(node->flags&SCO_WIDGET_DISABLED)!=0;CHECK(node->kind==SCO_WIDGET_BUTTON && node->label && *node->label); }
        if(node->id==23) chatNotice=node->label?node->label:"";
    }
    return SCO_OK;
}
sco_result RemovePanel(sco_plugin*,const char*) { panelCallback=nullptr;panelRemoved=true;return SCO_OK; }
sco_result Put(sco_plugin*,const char* key,const void* value,uint32_t size) { if(failPut && std::strstr(key,".port")) return SCO_UNAVAILABLE;db[key]=std::string(static_cast<const char*>(value),size);return SCO_OK; }
sco_result Get(sco_plugin*,const char* key,void* out,uint32_t* size) {
    if(failGetKey==key) return SCO_UNAVAILABLE;
    auto it=db.find(key);if(it==db.end()) { *size=0;return SCO_NOT_FOUND; }
    const auto needed=static_cast<uint32_t>(it->second.size());if(*size<needed) { *size=needed;return SCO_TOO_MANY; }
    std::memcpy(out,it->second.data(),needed);*size=needed;return SCO_OK;
}
sco_result Delete(sco_plugin*,const char* key) { return db.erase(key)?SCO_OK:SCO_NOT_FOUND; }
sco_result Begin(sco_plugin*) { if(inTx) return SCO_BAD_ARG;prior=db;inTx=true;return SCO_OK; }
sco_result Commit(sco_plugin*) { if(!inTx) return SCO_BAD_ARG;inTx=false;return SCO_OK; }
sco_result Rollback(sco_plugin*) { db=prior;inTx=false;return SCO_OK; }
sco_result Host(sco_plugin* s,const sc_private_session_host_request_v1* r) {
    ++hostCalls;CHECK(s==owner && r && r->size==sizeof(*r) && r->consent==1 && r->_pad==0);
    CHECK(r->passphrase_len>=12 && r->passphrase_len<=256 && r->player_name_len==5 && !std::memcmp(r->player_name,"Pilot",5));
    CHECK(r->port==64091 && r->max_peers==8);
    if(!admitted) return SCO_UNAVAILABLE;
    networkActive=true;state=SC_PRIVATE_SESSION_HOSTING;return SCO_OK;
}
sco_result Join(sco_plugin* s,const sc_private_session_join_request_v1* r) {
    ++joinCalls;CHECK(s==owner && r && r->size==sizeof(*r) && r->consent==1 && r->_pad==0);
    CHECK(r->address_len==9 && !std::memcmp(r->address,"127.0.0.1",9));
    CHECK(r->passphrase_len>=12 && r->player_name_len==5);
    if(!admitted) return SCO_UNAVAILABLE;
    networkActive=true;state=SC_PRIVATE_SESSION_CONNECTED;return SCO_OK;
}
sco_result Leave(sco_plugin*) { ++leaveCalls;networkActive=false;state=0;return SCO_OK; }
sco_result State(sc_private_session_state_v1* out) { if(!admitted) return SCO_UNAVAILABLE;out->state=state;out->peer_count=networkActive?2:0;return SCO_OK; }
int Active() { return networkActive?1:0; }
uint64_t Self() { return networkActive?selfId:0; }
// Faithful to sco_net.h: up to the capacity are written, *count is the member total, SCO_TOO_MANY when they do not all fit.
// The members are `members` (ids as set by the relay tests), padded with synthetic ids up to `memberTotal` (the over-16 tests).
sco_result GetPeers(sco_net_peer* out,uint32_t* count) {
    ++getPeersCalls;const uint32_t cap=*count;
    const uint32_t have=networkActive?std::max(memberTotal,static_cast<uint32_t>(members.size())):0;
    for(uint32_t i=0;i<cap && i<have;++i) out[i]={i<members.size()?members[i]:i+1,0};
    *count=have;return have>cap?SCO_TOO_MANY:SCO_OK;
}
sco_result Name(uint64_t id,char* out,uint32_t cap) {
    const auto it=peerNames.find(id);
    std::snprintf(out,cap,"%s",it!=peerNames.end()?it->second.c_str():(id==1 && serverIsRelay?"Relay Server":"Remote"));return SCO_OK;
}
sco_result Send(sco_plugin*,const char* channel,const void* data,uint32_t len) {
    if(failChatSend && !std::strcmp(channel,"multiplayer.chat")) return SCO_TOO_MANY;
    ++sendCalls;++sentCount[channel];
    sentLast[channel]=Bytes(static_cast<const uint8_t*>(data),static_cast<const uint8_t*>(data)+len);return SCO_OK;
}
sco_result Channel(sco_plugin*,const char* id,uint32_t flags,uint32_t max,sco_net_on_message cb,void*) {
    if(refuseProtocol && !std::strcmp(id,mpprotocol::kChannel)) return SCO_BAD_ARG;
    if(refuseNear && !std::strncmp(id,"multiplayer.near.",17)) return SCO_BAD_ARG;
    channelFlags[id]=flags;channelMax[id]=max;
    if(!std::strcmp(id,"multiplayer.pose")) CHECK(!flags && max==mpwire::kMaxPose);
    else if(!std::strcmp(id,"multiplayer.spawn")) CHECK(flags==SCO_NET_RELIABLE && max==mpwire::kMaxSpawn);
    else if(!std::strcmp(id,"multiplayer.despawn")) CHECK(flags==SCO_NET_RELIABLE && max==16);
    else if(!std::strcmp(id,"multiplayer.relay")) CHECK(flags==(SCO_NET_RELIABLE|SCO_NET_FROM_HOST) && max==rw::kRelayMaxBytes);
    else if(!std::strcmp(id,mpprotocol::kChannel)) CHECK(flags==SCO_NET_RELIABLE && max==mpprotocol::kBytes);
    else if(!std::strcmp(id,"multiplayer.chat")) CHECK(flags==SCO_NET_RELIABLE && max==mpwire::kMaxChat);
    else if(!std::strcmp(id,"multiplayer.near_up")) CHECK(flags==SCO_NET_TO_HOST && max==rw::kNearUpMaxBytes);
    else if(!std::strncmp(id,"multiplayer.near.",17)) CHECK(flags==SCO_NET_FROM_HOST && max==rw::kNearDownMaxBytes);
    else CHECK(!"a channel the module should not register");
    messages[id]=cb;return SCO_OK;
}
sco_result Unchannel(sco_plugin*,const char* id) { messages.erase(id);return SCO_OK; }
int PlayerPose(double p[3],double q[4],uint64_t* z) { p[0]=p[1]=p[2]=0;q[0]=q[1]=q[2]=0;q[3]=1;*z=7;return 1; }
int Record(uint64_t id,sc_game_zone* out) { if(id!=7) return 0;out->epoch=9;out->zone_id=7;out->root_id=7;out->parent_id=0;std::snprintf(out->name,sizeof(out->name),"Synthetic_Zone");return 1; }
int FindZone(const char* name,sc_game_zone* out) { return !std::strcmp(name,"Synthetic_Zone")?Record(7,out):0; }
int Convert(uint64_t,uint64_t,const double p[3],const double q[4],double op[3],double oq[4]) { std::memcpy(op,p,24);std::memcpy(oq,q,32);return 1; }
sco_result SpawnActor(sco_plugin* s,const char* cls,uint64_t z,const double[3],uint64_t* id) { CHECK(s==owner && z==7 && std::strstr(cls,"Civilian"));++spawnCalls;*id=100+spawnCalls;return SCO_OK; }
sco_result RemoveActor(sco_plugin* s,uint64_t id) { CHECK(s==owner && id>=101);++removeCalls;return SCO_OK; }
int Alive(uint64_t) { return 1; }
double lastGhostX=0;
sco_result Transform(sco_plugin* s,uint64_t id,uint64_t z,const double p[3],const double[4]) { CHECK(s==owner && id>=101 && z==7);++transformCalls;lastGhostX=p[0];return SCO_OK; }
sco_net_v1 net{};sco_widgets_v1 widgets{};sco_storage_v1 storage{};sc_private_session_v1 broker{};
sc_spatial_v1 spatial{};sc_game_spatial_v1 poses{};sc_actors_v1 actors{};sc_entities_v1 entities{};
sco_result Query(const char* name,uint32_t version,const void** out) {
    *out=nullptr;
    if(!std::strcmp(name,SCO_NET_NAME)) *out=&net;
    else if(!std::strcmp(name,SCO_WIDGETS_NAME)) { widgetVersion=version;if(widgetsPresent && version==SCO_WIDGETS_VERSION_1_2) *out=&widgets; }
    else if(!std::strcmp(name,SCO_STORAGE_NAME)) { storageVersion=version;if(storagePresent && version==SCO_STORAGE_VERSION_1_1) *out=&storage; }
    else if(!std::strcmp(name,SC_PRIVATE_SESSION_NAME) && brokerPresent) *out=&broker;
    else if(!std::strcmp(name,SC_SPATIAL_SERVICE_NAME)) *out=&spatial;
    else if(!std::strcmp(name,SC_GAME_SPATIAL_NAME)) *out=&poses;
    else if(!std::strcmp(name,SC_ACTORS_NAME)) *out=&actors;
    else if(!std::strcmp(name,SC_ENTITIES_NAME)) *out=&entities;
    return *out?SCO_OK:SCO_NOT_FOUND;
}
void UI(uint32_t id,uint32_t kind,const char* text=nullptr,double value=0) {
    CHECK(panelCallback!=nullptr);if(!panelCallback) return;
    if(id==21 && kind==SCO_WIDGET_TEXT_INPUT && text) chatDraft=text; // the widget owns the live edit until the panel is refreshed
    sco_widget_event event{};event.size=sizeof(event);event.node_id=id;event.kind=kind;event.text=text;event.value=value;event.index=static_cast<int32_t>(value);
    panelCallback("multiplayer.session",&event,nullptr);
}
void Tick() { CHECK(events.count("tick"));if(events.count("tick")) events["tick"]("tick",nullptr,nullptr); }
void StateEvent(bool active) {
    sco_net_state_event e{};e.size=sizeof(e);e.active=active?1:0;if(!active) std::snprintf(e.reason,sizeof(e.reason),"the host ended the session");
    CHECK(events.count(SCO_NET_EVENT_STATE));if(events.count(SCO_NET_EVENT_STATE)) events[SCO_NET_EVENT_STATE](SCO_NET_EVENT_STATE,&e,nullptr);
}
void PeerEvent(uint32_t what,uint64_t id) {
    sco_net_peer_event e{};e.size=sizeof(e);e.what=what;e.peer.peer_id=id;
    CHECK(events.count(SCO_NET_EVENT_PEER));if(events.count(SCO_NET_EVENT_PEER)) events[SCO_NET_EVENT_PEER](SCO_NET_EVENT_PEER,&e,nullptr);
}
void Deliver(const char* channel,uint64_t from,const Bytes& bytes) {
    CHECK(messages.count(channel));if(messages.count(channel)) messages.at(channel)(from,bytes.data(),static_cast<uint32_t>(bytes.size()),nullptr);
}
std::string StatusText() { char out[512]="";commands.at("multiplayer.status").fn(nullptr,0,nullptr,out,sizeof(out));return out; }
Bytes PoseBytes(uint32_t seq,double x) {
    mpwire::Pose pose{};pose.seq=seq;pose.levels=1;std::snprintf(pose.lv[0].name,sizeof(pose.lv[0].name),"Synthetic_Zone");pose.lv[0].rot[3]=1;pose.lv[0].pos[0]=x;
    uint8_t data[mpwire::kMaxPose];const size_t n=mpwire::EncodePose(pose,data,sizeof(data));return Bytes(data,data+n);
}
Bytes SpawnBytes(uint64_t peer) {
    mpwire::Spawn spawn{};spawn.kind=mpwire::kAvatar;spawn.sessionId=mpwire::SessionId(peer,mpwire::kAvatar);std::snprintf(spawn.cls,sizeof(spawn.cls),"Synthetic_Human");
    uint8_t data[mpwire::kMaxSpawn];const size_t n=mpwire::EncodeSpawn(spawn,data,sizeof(data));return Bytes(data,data+n);
}
Bytes RelayBytes(bool nearby,uint16_t maxPlayers,std::initializer_list<rw::RelayEntry> players) {
    rw::RelayRoster roster;roster.nearbyOnly=nearby;roster.maxPlayers=maxPlayers;roster.players.assign(players.begin(),players.end());return rw::EncodeRelay(roster);
}
void Reset() { commands.clear();events.clear();messages.clear();panelRemoved=false;networkActive=false;state=0; }
// ---- multiplayer.chat: the layout in multiplayer_wire.h. No module, no clock, no session. ----------------------
// A chat message as a sender writes it: version 1, a zero byte, the length in two little-endian bytes, then the text.
Bytes ChatBytes(const std::string& text) {
    uint8_t data[mpwire::kMaxChat];const size_t n=mpwire::EncodeChat(text.data(),text.size(),data,sizeof(data));
    return Bytes(data,data+n);
}
// The same laid out by hand, so a test can break one field at a time.
Bytes ChatRaw(uint8_t version,uint8_t reserved,uint16_t length,const Bytes& text) {
    Bytes out({version,reserved,static_cast<uint8_t>(length&0xFF),static_cast<uint8_t>(length>>8)});
    out.insert(out.end(),text.begin(),text.end());return out;
}
bool ChatReads(const Bytes& message,std::string* text=nullptr) {
    mpwire::ChatLine line{};
    if(!mpwire::DecodeChat(message.data(),message.size(),line)) return false;
    if(text) *text=std::string(line.text,line.length);
    return true;
}
bool ChatTextPasses(const Bytes& text) { return mpwire::ChatTextOk(reinterpret_cast<const char*>(text.data()),text.size()); }
// ---- multiplayer.chat: the rates, with a clock the test steps by hand ---------------------------------------------
void ChatRateChecks() {
    CHECK(mpwire::kChatWindowMs==5000 && mpwire::kChatSendsPerWindow==3 && mpwire::kChatAcceptPerWindow==6 && mpwire::kChatHistoryRows==32);
    mpwire::RateWindow<3> sends; // what one player may send in 5000 ms
    CHECK(sends.Take(1000) && sends.Take(1001) && sends.Take(1002));
    CHECK(!sends.Take(1003) && !sends.Room(5999));     // 4999 ms after the first: still inside the window
    CHECK(sends.Room(6000));                           // 5000 ms after the first: forgotten
    CHECK(sends.Take(6000) && !sends.Take(6000));      // 1001, 1002 and 6000 fill it again
    CHECK(sends.Take(6001) && !sends.Take(6001));      // 1001 is forgotten at 6001
    CHECK(sends.Take(100000) && sends.Take(100000) && sends.Take(100000) && !sends.Take(100000)); // a long silence forgets all
    mpwire::RateWindow<6> reads; // what one sender may have read in 5000 ms
    for(uint64_t t:{0,1000,2000,3000,4000,4500}) CHECK(reads.Take(t));
    CHECK(!reads.Take(4999) && !reads.Room(4999));
    CHECK(reads.Take(5000) && !reads.Take(5000));      // 0 is forgotten at 5000, then it is full again
    CHECK(!reads.Take(5999) && reads.Take(6000));      // 1000 is forgotten at 6000
}
// ---- multiplayer.chat in a session: a listen host, then a relay server, with synthetic players ------------------------
// Every line said here holds "zq", so the test can look for it where it must never be (mod.log, the status line).
void ChatSessions(const sco_api* api,sco_result (*load)(const sco_api*,sco_plugin*),void (*unload)()) {
    const auto savedMembers=members;const auto savedTotal=memberTotal;const auto savedSelf=selfId;const bool savedRelay=serverIsRelay;
    const Bytes player({mpprotocol::kGeneration,mpprotocol::kPlayer}),relayRole({mpprotocol::kGeneration,mpprotocol::kRelay});
    char output[512];
    const auto type=[](const std::string& text) { UI(21,SCO_WIDGET_TEXT_INPUT,text.c_str()); };
    const auto send=[&](const std::string& text) { type(text);UI(22,SCO_WIDGET_BUTTON); };
    const auto said=[](uint64_t from,const std::string& text) { Deliver("multiplayer.chat",from,ChatBytes(text)); };
    const auto sent=[]() { return sentCount["multiplayer.chat"]; };
    const auto notSent=[]() { return panelReply.find("not sent")!=std::string::npos; };
    unsigned dropped=0,ignored=0; // what the status line must say about the lines this player did not read
    const auto counts=[&]() { return StatusText().find("chat: "+std::to_string(dropped)+" dropped over the rate limit, "+std::to_string(ignored)+" not accepted")!=std::string::npos; };

    // ---- a listen host: this player is peer 1 ----
    Reset();modLog.clear();serverIsRelay=false;selfId=1;members={1,2};memberTotal=2;sentCount.clear();sentLast.clear();failChatSend=false;peerNames.clear();
    CHECK(load(api,owner)==SCO_OK);
    CHECK(messages.count("multiplayer.chat")==1 && channelFlags.at("multiplayer.chat")==SCO_NET_RELIABLE && channelMax.at("multiplayer.chat")==mpwire::kMaxChat);
    CHECK(chatRows.empty() && chatDraft.empty() && chatSendDisabled && chatNotice.find("not encrypted")!=std::string::npos);
    // No session: Send sends nothing and says so, and the draft stays until the session ends or starts.
    type("zq typed before the session");CHECK(chatDraft=="zq typed before the session");
    UI(22,SCO_WIDGET_BUTTON);CHECK(sent()==0 && notSent() && chatRows.empty() && chatDraft=="zq typed before the session");
    networkActive=true;state=SC_PRIVATE_SESSION_HOSTING;StateEvent(true);PeerEvent(SCO_NET_PEER_JOINED,2);Tick();
    CHECK(chatDraft.empty() && !chatSendDisabled); // a new session starts with nothing typed
    // A player nobody has heard announce is not read.
    said(2,"zq too early");++ignored;Tick();
    CHECK(chatRows.empty() && counts());
    Deliver(mpprotocol::kChannel,2,player);
    said(2,"zq hello there");Tick();
    CHECK(chatRows==std::vector<std::string>({"Remote #2: zq hello there"}));
    // Sending: the bytes that go out are the layout's, and the line is shown only once sco.net took it.
    send("zq general kenobi");
    CHECK(sent()==1 && sentLast["multiplayer.chat"]==ChatBytes("zq general kenobi"));
    CHECK(chatRows.size()==2 && chatRows[1]=="You: zq general kenobi" && chatDraft.empty() && panelReply.find("Chat message sent")!=std::string::npos);
    // A draft that is not sendable stays in the box and sends nothing, and costs none of the 3 sends.
    for(const std::string& bad:{std::string(),std::string(161,'x'),std::string("zq tab\there"),std::string("zq bad \xFF utf8"),std::string("zq \xE2\x80\xAE override")}) {
        send(bad);
        CHECK(sent()==1 && chatRows.size()==2 && chatDraft==bad && notSent());
    }
    // sco.net refuses: nothing is echoed, the draft stays, and the failed try is not counted against the 3.
    failChatSend=true;send("zq will fail");failChatSend=false;
    CHECK(sent()==1 && chatRows.size()==2 && chatDraft=="zq will fail" && panelReply.find("not sent (")!=std::string::npos);
    UI(22,SCO_WIDGET_BUTTON);
    CHECK(sent()==2 && chatRows.size()==3 && chatRows[2]=="You: zq will fail" && chatDraft.empty());
    send("zq third");CHECK(sent()==3 && chatRows.size()==4);
    send("zq fourth");  // 3 sends in 5000 ms: the fourth waits
    CHECK(sent()==3 && chatRows.size()==4 && chatDraft=="zq fourth" && panelReply.find("at most 3 messages in 5 seconds")!=std::string::npos);
    type("");
    // Reading is limited to 6 lines from one sender in 5000 ms; the extras are dropped and counted.
    for(int i=0;i<5;++i) said(2,"zq line "+std::to_string(i)); // the sixth with "hello there"
    said(2,"zq line 5");said(2,"zq line 6");dropped+=2;Tick();
    CHECK(chatRows.size()==9 && chatRows.back()=="Remote #2: zq line 4" && counts());
    // Lines that are not the contract are dropped and counted, and do not use up the sender's 6.
    Bytes trailing=ChatBytes("zq fine");trailing.push_back('!');
    Bytes truncated=ChatBytes("zq fine");truncated.pop_back();
    for(const Bytes& bad:{trailing,truncated,ChatRaw(1,0,0,Bytes()),ChatRaw(1,0,161,Bytes(161,uint8_t('a'))),ChatRaw(2,0,2,Bytes({'h','i'})),ChatRaw(1,1,2,Bytes({'h','i'})),
                          ChatRaw(1,0,3,Bytes({'z','q',0xFF})),ChatRaw(1,0,5,Bytes({'z','q',0x01,'a','b'})),ChatRaw(1,0,4,Bytes({'z',0xE2,0x80,0xAE})),Bytes(),Bytes(165,uint8_t('z'))}) {
        Deliver("multiplayer.chat",2,bad);++ignored;
    }
    Tick();CHECK(chatRows.size()==9 && counts());
    // Text that looks like a command, a format string or markup is only text: shown as it came, run as nothing.
    members={1,2,3};memberTotal=3;PeerEvent(SCO_NET_PEER_JOINED,3);Deliver(mpprotocol::kChannel,3,player);
    const unsigned leavesBefore=leaveCalls,hostsBefore=hostCalls,joinsBefore=joinCalls;
    for(const std::string& text:{"zq /multiplayer.leave","zq multiplayer.goto Remote","zq %s%n%x %d","zq !leave","zq {host} ${HOME} `id`"}) said(3,text);
    Tick();
    CHECK(chatRows.size()==14 && chatRows[9]=="Remote #3: zq /multiplayer.leave" && chatRows[10]=="Remote #3: zq multiplayer.goto Remote");
    CHECK(chatRows[11]=="Remote #3: zq %s%n%x %d" && chatRows[12]=="Remote #3: zq !leave" && chatRows.back()=="Remote #3: zq {host} ${HOME} `id`");
    CHECK(leaveCalls==leavesBefore && hostCalls==hostsBefore && joinCalls==joinsBefore && networkActive && commands.size()==3);
    CHECK(sent()==3); // the lines only came in: chat never answers one
    // The history keeps the last 32 lines.
    members={1,2,3,4,5,6,7,8};memberTotal=8;
    peerNames[5]="You";peerNames[6]="Alice: brb. Bob"; // names that would pass for this player's own lines, or start a second speaker
    for(uint64_t id=4;id<=8;++id) {
        PeerEvent(SCO_NET_PEER_JOINED,id);Deliver(mpprotocol::kChannel,id,player);
        for(int i=0;i<6;++i) said(id,"zq fill "+std::to_string(id)+"-"+std::to_string(i));
    }
    Tick();
    CHECK(chatRows.size()==32 && chatRows.back()=="Remote #8: zq fill 8-5" && std::find(chatRows.begin(),chatRows.end(),"Remote #2: zq hello there")==chatRows.end());
    // A name cannot forge a speaker: "You" gets its peer id like any name, so it never reads as this player's own "You: " lines, and a
    // name with the separator is not used, so it cannot start a second speaker. Every label ends in the real speaker's peer id.
    CHECK(std::find(chatRows.begin(),chatRows.end(),"You #5: zq fill 5-0")!=chatRows.end());
    CHECK(std::find(chatRows.begin(),chatRows.end(),"peer 6: zq fill 6-0")!=chatRows.end());
    CHECK(std::none_of(chatRows.begin(),chatRows.end(),[](const std::string& row) { return row.rfind("You: ",0)==0 || row.find("Alice")!=std::string::npos; }));
    peerNames.clear();
    // A player who left is not read, and one who comes back is not read until it announces again.
    members={1,2,4,5,6,7,8};memberTotal=7;PeerEvent(SCO_NET_PEER_LEFT,3);
    said(3,"zq after leaving");++ignored;
    members={1,2,3,4,5,6,7,8};memberTotal=8;PeerEvent(SCO_NET_PEER_JOINED,3);
    said(3,"zq back but not announced");++ignored;
    Deliver(mpprotocol::kChannel,99,player);said(99,"zq not a member");++ignored;
    Tick();CHECK(chatRows.back()=="Remote #8: zq fill 8-5" && counts());
    Deliver(mpprotocol::kChannel,3,player);said(3,"zq announced again");Tick();
    CHECK(chatRows.back()=="Remote #3: zq announced again" && chatRows.size()==32);
    // Leave clears the history and the draft; nothing is read afterwards.
    type("zq draft before leave");CHECK(chatDraft=="zq draft before leave");
    CHECK(commands.at("multiplayer.leave").fn(nullptr,0,nullptr,output,sizeof(output))==SCO_OK);
    CHECK(chatRows.empty() && chatDraft.empty() && chatSendDisabled && StatusText().find("chat:")==std::string::npos);
    said(2,"zq after leave");Tick();CHECK(chatRows.empty());
    // The end of a session clears them too.
    members={1,2};memberTotal=2;networkActive=true;state=SC_PRIVATE_SESSION_HOSTING;StateEvent(true);PeerEvent(SCO_NET_PEER_JOINED,2);Deliver(mpprotocol::kChannel,2,player);
    said(2,"zq second session");type("zq draft at the end");Tick();
    CHECK(chatRows.size()==1 && chatDraft=="zq draft at the end");
    networkActive=false;state=SC_PRIVATE_SESSION_IDLE;StateEvent(false);Tick();
    CHECK(chatRows.empty() && chatDraft.empty());
    // Unloading clears them: the module's state outlives unload here, so a reload would show them if they stayed.
    networkActive=true;state=SC_PRIVATE_SESSION_HOSTING;StateEvent(true);PeerEvent(SCO_NET_PEER_JOINED,2);Deliver(mpprotocol::kChannel,2,player);
    said(2,"zq before unload");type("zq draft before unload");Tick();CHECK(chatRows.size()==1 && chatDraft=="zq draft before unload");
    unload();CHECK(messages.empty() && events.empty());
    Reset();CHECK(load(api,owner)==SCO_OK);CHECK(chatRows.empty() && chatDraft.empty());
    CHECK(LogCount("zq")==0 && StatusText().find("zq")==std::string::npos && panelStatus.find("zq")==std::string::npos); // no chat text in mod.log or the status
    unload();

    // ---- a relay server hosts: this player is peer 3, the server is peer 1 and no player ----
    Reset();modLog.clear();serverIsRelay=true;selfId=3;members={3,1,2};memberTotal=3;sentCount.clear();sentLast.clear();dropped=ignored=0;
    CHECK(load(api,owner)==SCO_OK);
    networkActive=true;state=SC_PRIVATE_SESSION_CONNECTED;StateEvent(true);PeerEvent(SCO_NET_PEER_JOINED,1);PeerEvent(SCO_NET_PEER_JOINED,2);
    // Player 2 has announced, the server has not: the host's role is unknown, so chat is neither read nor sent.
    Deliver(mpprotocol::kChannel,2,player);said(2,"zq before the server announced");++ignored;
    send("zq too early");CHECK(sent()==0 && notSent() && chatDraft=="zq too early" && panelReply.find("no session with compatible players")!=std::string::npos);
    type("");
    Deliver(mpprotocol::kChannel,1,relayRole);
    Deliver("multiplayer.relay",1,RelayBytes(false,15,{{2,0,1},{3,1,4}}));
    said(2,"zq hello from a player");Tick();
    CHECK(chatRows==std::vector<std::string>({"Remote #2: zq hello from a player"}) && counts());
    // The server is never a player: nothing it sends as chat is read, whatever its layout.
    said(1,"zq from the server");++ignored;Tick();
    CHECK(chatRows.size()==1 && counts());
    send("zq reply to the room");
    CHECK(sent()==1 && sentLast["multiplayer.chat"]==ChatBytes("zq reply to the room") && chatRows.size()==2 && chatRows[1]=="You: zq reply to the room");
    // Nearby-only is on: poses go to the server on another channel, chat goes where it always went.
    Deliver("multiplayer.relay",1,RelayBytes(true,15,{{2,0,1},{3,1,4}}));
    CHECK(messages.count("multiplayer.near.1")==1);
    said(2,"zq nearby-only is on");send("zq nearby send");Tick();
    CHECK(sent()==2 && sentLast["multiplayer.chat"]==ChatBytes("zq nearby send") && chatRows.size()==4 && chatRows[2]=="Remote #2: zq nearby-only is on");
    // A name that is not clean text is not put in front of what its owner says.
    members={3,1,2,4};memberTotal=4;peerNames[4]=std::string("zq name ")+"\xE2\x80\xAE";
    PeerEvent(SCO_NET_PEER_JOINED,4);Deliver(mpprotocol::kChannel,4,player);said(4,"zq odd name");Tick();
    CHECK(chatRows.back()=="peer 4: zq odd name");
    // A player who left is not read.
    members={3,1,4};memberTotal=3;PeerEvent(SCO_NET_PEER_LEFT,2);said(2,"zq after leaving");++ignored;Tick();
    CHECK(chatRows.size()==5 && counts());
    // The session ends: the history goes with it.
    networkActive=false;state=SC_PRIVATE_SESSION_IDLE;StateEvent(false);Tick();
    CHECK(chatRows.empty() && chatDraft.empty());
    CHECK(LogCount("zq")==0 && StatusText().find("zq")==std::string::npos);
    unload();CHECK(messages.empty() && events.empty());
    // ---- a listen host seen from a joiner: peer 1 is a player here, and what it says is read ----
    Reset();modLog.clear();serverIsRelay=false;selfId=2;members={2,1,3};memberTotal=3;sentCount.clear();sentLast.clear();dropped=ignored=0;
    CHECK(load(api,owner)==SCO_OK);
    networkActive=true;state=SC_PRIVATE_SESSION_CONNECTED;StateEvent(true);PeerEvent(SCO_NET_PEER_JOINED,1);PeerEvent(SCO_NET_PEER_JOINED,3);
    // Until the host has announced itself as a player, nothing is read from anyone and nothing is sent.
    Deliver(mpprotocol::kChannel,3,player);said(3,"zq before the host announced");++ignored;said(1,"zq host before it announced");++ignored;
    send("zq too early");CHECK(sent()==0 && notSent() && chatRows.empty());type("");
    Deliver(mpprotocol::kChannel,1,player);
    said(1,"zq from the listen host");said(3,"zq from the other joiner");Tick();
    CHECK(chatRows==std::vector<std::string>({"Remote #1: zq from the listen host","Remote #3: zq from the other joiner"}) && counts());
    send("zq joiner reply");
    CHECK(sent()==1 && sentLast["multiplayer.chat"]==ChatBytes("zq joiner reply") && chatRows.size()==3 && chatRows[2]=="You: zq joiner reply");
    CHECK(LogCount("zq")==0 && StatusText().find("zq")==std::string::npos);
    unload();CHECK(messages.empty() && events.empty());
    // Editing the chat box must not replace the panel before a queued Send click from that panel pass.
    Reset();modLog.clear();serverIsRelay=false;selfId=1;members={1};memberTotal=1;sentCount.clear();sentLast.clear();
    CHECK(load(api,owner)==SCO_OK);networkActive=true;state=SC_PRIVATE_SESSION_HOSTING;StateEvent(true);
    const unsigned beforeEdit=panelUpdates;
    type("zq edit then send in one panel pass");
    CHECK(chatDraft=="zq edit then send in one panel pass" && panelUpdates==beforeEdit);
    UI(22,SCO_WIDGET_BUTTON);
    CHECK(sent()==1 && chatRows.back()=="You: zq edit then send in one panel pass");
    unload();
    members=savedMembers;memberTotal=savedTotal;selfId=savedSelf;serverIsRelay=savedRelay;failChatSend=false;peerNames.clear();
}
void ChatLayoutChecks() {
    CHECK(std::strcmp(mpwire::kChatChannel,"multiplayer.chat")==0);
    CHECK(mpwire::kChatVersion==1 && mpwire::kChatHeader==4 && mpwire::kMaxChatText==160 && mpwire::kMaxChat==164);
    // Golden bytes: "hi" is 01 00 02 00 68 69, and it reads back.
    std::string text;
    CHECK(ChatBytes("hi")==Bytes({0x01,0x00,0x02,0x00,0x68,0x69}));
    CHECK(ChatReads(Bytes({0x01,0x00,0x02,0x00,0x68,0x69}),&text) && text=="hi");
    // Length: 1 to 160 bytes of text, and exactly 4 + n bytes in all.
    const Bytes full=ChatBytes(std::string(160,'a'));
    CHECK(full.size()==164 && full[2]==0xA0 && full[3]==0x00 && ChatReads(full,&text) && text.size()==160);
    CHECK(ChatBytes(std::string(161,'a')).empty() && ChatBytes("").empty());
    CHECK(ChatReads(ChatRaw(1,0,1,Bytes({'a'})),&text) && text=="a");
    CHECK(!ChatReads(ChatRaw(1,0,0,Bytes())));                                        // no text
    CHECK(!ChatReads(ChatRaw(1,0,161,Bytes(161,uint8_t('a')))));                    // one byte too many
    CHECK(!ChatReads(ChatRaw(1,0,0x0102,Bytes(258,uint8_t('a')))));                 // the high length byte counts
    CHECK(!ChatReads(ChatRaw(1,0,2,Bytes({'h'}))));                                  // cut short
    CHECK(!ChatReads(ChatRaw(1,0,2,Bytes({'h','i','!'}))));                          // a trailing byte
    CHECK(!ChatReads(ChatRaw(1,0,3,Bytes({'h','i'}))) && !ChatReads(ChatRaw(1,0,1,Bytes({'h','i'}))));
    CHECK(!ChatReads(Bytes(165,uint8_t(0))));                                          // longer than any message
    CHECK(!ChatReads(Bytes()) && !ChatReads(Bytes({1,0,1})) && !ChatReads(Bytes({1,0,0,0})));
    // Version 1 only, and the reserved byte is zero: anything else is another layout.
    CHECK(ChatReads(ChatRaw(1,0,2,Bytes({'h','i'}))));
    CHECK(!ChatReads(ChatRaw(0,0,2,Bytes({'h','i'}))) && !ChatReads(ChatRaw(2,0,2,Bytes({'h','i'}))) && !ChatReads(ChatRaw(255,0,2,Bytes({'h','i'}))));
    CHECK(!ChatReads(ChatRaw(1,1,2,Bytes({'h','i'}))) && !ChatReads(ChatRaw(1,255,2,Bytes({'h','i'}))));
    // A buffer too small for the message is refused, not cut.
    uint8_t small[3];CHECK(mpwire::EncodeChat("hi",2,small,sizeof(small))==0 && mpwire::EncodeChat(nullptr,2,small,sizeof(small))==0);
    // UTF-8 that is fine.
    CHECK(ChatTextPasses(Bytes({'a',' ','b','~'})));
    CHECK(ChatTextPasses(Bytes({0xC3,0xA9})));                  // e acute
    CHECK(ChatTextPasses(Bytes({0xE2,0x82,0xAC})));             // euro sign
    CHECK(ChatTextPasses(Bytes({0xF0,0x9F,0x98,0x80})));        // a four-byte character
    CHECK(ChatTextPasses(Bytes({0xC2,0xA0})));                  // no-break space: the first character after C1
    CHECK(ChatTextPasses(Bytes({0xED,0x9F,0xBF})));             // U+D7FF, just below the surrogates
    CHECK(ChatTextPasses(Bytes({0xEE,0x80,0x80})));             // U+E000, just above them
    CHECK(ChatTextPasses(Bytes({0xF4,0x8F,0xBF,0xBF})));        // U+10FFFF, the last character
    CHECK(ChatTextPasses(Bytes({0xE2,0x80,0x8E})));             // a left-to-right mark is not an embedding, override or isolate
    CHECK(ChatTextPasses(Bytes({0xE2,0x80,0xAF})) && ChatTextPasses(Bytes({0xE2,0x81,0xAA})));   // U+202F and U+206A, beside the refused ones
    // Control characters: C0, DEL, C1.
    for(unsigned c=0;c<0x20;++c) CHECK(!ChatTextPasses(Bytes({'a',static_cast<uint8_t>(c),'b'})));
    CHECK(!ChatTextPasses(Bytes({0x7F})) && ChatTextPasses(Bytes({0x7E})));
    CHECK(!ChatTextPasses(Bytes({0xC2,0x80})) && !ChatTextPasses(Bytes({0xC2,0x8D})) && !ChatTextPasses(Bytes({0xC2,0x9F})));
    // Bidirectional embedding, override and isolate controls.
    for(uint8_t last=0xAA;last<=0xAE;++last) CHECK(!ChatTextPasses(Bytes({'a',0xE2,0x80,last})));
    for(uint8_t last=0xA6;last<=0xA9;++last) CHECK(!ChatTextPasses(Bytes({'a',0xE2,0x81,last})));
    // UTF-8 that is not well formed: a stray continuation byte, overlong forms, surrogates, beyond U+10FFFF, bytes that
    // never occur, a character cut off by the end, a lead byte not followed by continuation bytes.
    for(const Bytes& bad:{Bytes({0x80}),Bytes({0xBF}),Bytes({0xC0,0x80}),Bytes({0xC0,0xAF}),Bytes({0xC1,0xBF}),Bytes({0xE0,0x80,0xAF}),
                          Bytes({0xE0,0x9F,0xBF}),Bytes({0xF0,0x80,0x80,0x80}),Bytes({0xF0,0x8F,0xBF,0xBF}),Bytes({0xED,0xA0,0x80}),
                          Bytes({0xED,0xBF,0xBF}),Bytes({0xF4,0x90,0x80,0x80}),Bytes({0xF5,0x80,0x80,0x80}),Bytes({0xFE}),Bytes({0xFF}),
                          Bytes({'a',0xC3}),Bytes({'a',0xE2,0x82}),Bytes({'a',0xF0,0x9F,0x98}),Bytes({0xC3,0x28}),
                          Bytes({0xE2,0x28,0xAC}),Bytes({0xE2,0x82,0x28}),Bytes({0xF0,0x9F,0x28,0x80})})
        CHECK(!ChatTextPasses(bad) && !ChatReads(ChatRaw(1,0,static_cast<uint16_t>(bad.size()),bad)));
    // Length counts bytes, not characters: 80 two-byte characters fill the line, and one more byte does not fit.
    std::string accents;for(int i=0;i<80;++i) accents.append("\xC3\xA9");
    CHECK(accents.size()==160 && ChatBytes(accents).size()==164 && ChatReads(ChatBytes(accents),&text) && text==accents);
    CHECK(ChatBytes(accents+"a").empty() && ChatBytes(std::string(159,'a')+"\xC3\xA9").empty());
    std::string euros;for(int i=0;i<53;++i) euros.append("\xE2\x82\xAC");
    CHECK(euros.size()==159 && ChatBytes(euros+"a").size()==164 && ChatBytes(euros+"\xE2\x82\xAC").empty());
    Bytes cutAtEnd(159,uint8_t('a'));cutAtEnd.push_back(0xC3);                       // 160 bytes, the last one the lead of a character
    CHECK(!ChatReads(ChatRaw(1,0,160,cutAtEnd)));
    CHECK(ChatBytes(std::string("a\0b",3)).empty() && ChatBytes("a\nb").empty() && ChatBytes("a\tb").empty());
}
}
int main(int argc,char** argv) {
    if(argc!=2) return 2;
#if defined(_WIN32)
    HMODULE module=LoadLibraryA(argv[1]);if(!module) return 2;
    auto symbol=[&](const char* n){return reinterpret_cast<void*>(GetProcAddress(module,n));};
#else
    void* module=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!module) { std::puts(dlerror());return 2; }
    auto symbol=[&](const char* n){return dlsym(module,n);};
#endif
    auto query=reinterpret_cast<const sco_plugin_info* (*)()>(symbol("sco_plugin_query"));
    auto load=reinterpret_cast<sco_result (*)(const sco_api*,sco_plugin*)>(symbol("sco_plugin_load"));
    auto unload=reinterpret_cast<void (*)()>(symbol("sco_plugin_unload"));
    CHECK(query && load && unload);if(!query || !load || !unload) return 2;
    CHECK(!std::strcmp(query()->name,"multiplayer"));
    ChatLayoutChecks();ChatRateChecks();
    sco_api api{};api.size=sizeof(api);api.major=SCO_API_MAJOR;api.minor=SCO_API_MINOR;api.register_command=Register;api.subscribe=Subscribe;api.unsubscribe=Unsubscribe;api.query_service=Query;api.log=LogFn;
    net.size=sizeof(net);net.is_active=Active;net.self_peer=Self;net.get_peers=GetPeers;net.get_peer_name=Name;net.send_channel=Send;net.register_channel=Channel;net.unregister_channel=Unchannel;
    widgets.size=sizeof(widgets);widgets.set_panel=SetPanel;widgets.remove_panel=RemovePanel;
    storage.size=sizeof(storage);storage.put=Put;storage.get=Get;storage.del=Delete;storage.begin=Begin;storage.commit=Commit;storage.rollback=Rollback;
    broker.size=sizeof(broker);broker.request_host=Host;broker.request_join=Join;broker.leave=Leave;broker.get_state=State;
    spatial.size=sizeof(spatial);spatial.player_pose=PlayerPose;
    poses.size=sizeof(poses);poses.zone_record=Record;poses.find_zone=FindZone;poses.pose_to_zone=Convert;
    actors.size=sizeof(actors);actors.spawn_npc=SpawnActor;actors.despawn=RemoveActor;
    entities.size=sizeof(entities);entities.alive=Alive;entities.set_transform=Transform;
    db={{"name","Pilot"},{"join_address","127.0.0.1"},{"host_port","64091"},{"join_port","64091"},{"show_ships","1"},{"remember_passphrase","0"},{"passphrase","ignored stale synthetic secret"}};
    CHECK(load(nullptr,owner)==SCO_BAD_ARG);
    CHECK(load(&api,owner)==SCO_OK);CHECK(commands.size()==3 && messages.size()==7 && events.size()==3);
    CHECK(widgetVersion==SCO_WIDGETS_VERSION_1_2 && storageVersion==SCO_STORAGE_VERSION_1_1);
    CHECK(hostSeen && joinSeen && maskSeen && !rememberSeen && passInPanel.empty());
    CHECK(StatusText().find("mode: no private session")!=std::string::npos);
    CHECK(panelStatus.find("mode: no private session")!=std::string::npos);
    CHECK(hostCalls==0 && joinCalls==0 && sendCalls==0);Tick();CHECK(hostCalls==0 && joinCalls==0);
    char output[512];sco_arg who{};who.type=SCO_ARG_STRING;who.v.s="Remote";
    CHECK(commands.at("multiplayer.goto").fn(&who,1,nullptr,output,sizeof(output))==SCO_UNAVAILABLE);CHECK(std::strstr(output,"G2"));
    UI(1,SCO_WIDGET_BUTTON);CHECK(hostCalls==0); // empty/short passphrase refused locally
    UI(6,SCO_WIDGET_TEXT_INPUT,"synthetic passphrase");CHECK(hostCalls==0);
    UI(1,SCO_WIDGET_BUTTON);CHECK(hostCalls==1 && passInPanel.empty());CHECK(!db.count("passphrase"));
    CHECK(StatusText().find("mode: listen host (co-presence only; game state remains local)")!=std::string::npos);
    CHECK(panelStatus.find("mode: listen host (co-presence only; game state remains local)")!=std::string::npos);
    const char* keys[]={"name","join_address","avatar_class","host_port","join_port","show_ships","remember_passphrase"};
    for(const auto* key:keys) CHECK(db.count(key));
    mpwire::Spawn spawn{};spawn.kind=mpwire::kAvatar;spawn.sessionId=mpwire::SessionId(2,mpwire::kAvatar);std::snprintf(spawn.cls,sizeof(spawn.cls),"Synthetic_Human");
    uint8_t data[mpwire::kMaxPose];size_t len=mpwire::EncodeSpawn(spawn,data,sizeof(data));
    messages.at("multiplayer.spawn")(999,data,static_cast<uint32_t>(len),nullptr);Tick();CHECK(spawnCalls==0);
    Deliver(mpprotocol::kChannel,2,Bytes({mpprotocol::kGeneration,mpprotocol::kPlayer}));
    messages.at("multiplayer.spawn")(2,data,static_cast<uint32_t>(len),nullptr);
    mpwire::Pose pose{};pose.seq=1;pose.levels=1;std::snprintf(pose.lv[0].name,sizeof(pose.lv[0].name),"Synthetic_Zone");pose.lv[0].rot[3]=1;pose.lv[0].pos[0]=3;
    len=mpwire::EncodePose(pose,data,sizeof(data));messages.at("multiplayer.pose")(2,data,static_cast<uint32_t>(len),nullptr);
    Tick();CHECK(spawnCalls==1 && transformCalls>=1);
    data[0]=99;messages.at("multiplayer.pose")(2,data,static_cast<uint32_t>(len),nullptr);Tick();CHECK(spawnCalls==1 && panelRows.size()==1);
    len=mpwire::EncodeDespawn(mpwire::kAvatar,mpwire::SessionId(2,mpwire::kAvatar),data,sizeof(data));CHECK(len==16);
    messages.at("multiplayer.despawn")(2,data,static_cast<uint32_t>(len),nullptr);CHECK(removeCalls==1);
    {   // More than 16 members: the list is read by growing the buffer, bounded at 64 members
        const auto others=[&](unsigned n){ char want[32];std::snprintf(want,sizeof(want),"; %u other players;",n);return std::strstr(output,want)!=nullptr; };
        const auto poseFrom=[&](uint64_t id){ messages.at("multiplayer.pose")(id,data,static_cast<uint32_t>(mpwire::EncodePose(pose,data,sizeof(data))),nullptr); };
        memberTotal=20;getPeersCalls=0;Deliver(mpprotocol::kChannel,20,Bytes({mpprotocol::kGeneration,mpprotocol::kPlayer}));
        CHECK(getPeersCalls==4);poseFrom(20); // 16 offered, TOO_MANY with 20, then 20 offered
        commands.at("multiplayer.status").fn(nullptr,0,nullptr,output,sizeof(output));CHECK(others(2));
        memberTotal=65;getPeersCalls=0;Deliver(mpprotocol::kChannel,21,Bytes({mpprotocol::kGeneration,mpprotocol::kPlayer})); // over the cap: no list, no record, one call only
        CHECK(getPeersCalls==1);commands.at("multiplayer.status").fn(nullptr,0,nullptr,output,sizeof(output));CHECK(others(0));
        memberTotal=64;getPeersCalls=0;Deliver(mpprotocol::kChannel,64,Bytes({mpprotocol::kGeneration,mpprotocol::kPlayer}));CHECK(getPeersCalls==4);poseFrom(64); // exactly the cap is read
        commands.at("multiplayer.status").fn(nullptr,0,nullptr,output,sizeof(output));CHECK(others(3));
        memberTotal=2;Tick();
    }
    UI(6,SCO_WIDGET_TEXT_INPUT,"secret before command leave");
    CHECK(!panelRows.empty() && !passInPanel.empty());
    CHECK(commands.at("multiplayer.leave").fn(nullptr,0,nullptr,output,sizeof(output))==SCO_OK);CHECK(!networkActive);
    CHECK(panelRows.empty() && passInPanel.empty() && panelStatus.find("0 peers")!=std::string::npos);
    UI(6,SCO_WIDGET_TEXT_INPUT,"synthetic passphrase");UI(2,SCO_WIDGET_BUTTON);CHECK(joinCalls==1 && passInPanel.empty());
    UI(11,SCO_WIDGET_TOGGLE,nullptr,1);UI(6,SCO_WIDGET_TEXT_INPUT,"synthetic remembered passphrase");UI(1,SCO_WIDGET_BUTTON);
    CHECK(rememberSeen);UI(19,SCO_WIDGET_BUTTON);CHECK(db.count("profile0.address") && db.at("profile0.address")=="127.0.0.1");
    UI(5,SCO_WIDGET_TEXT_INPUT,"127.0.0.2");{ const auto before=db;failPut=true;UI(19,SCO_WIDGET_BUTTON);failPut=false;CHECK(db==before && !inTx); }UI(18,SCO_WIDGET_COMBO,nullptr,0);CHECK(passInPanel=="synthetic remembered passphrase");
    UI(3,SCO_WIDGET_BUTTON);CHECK(passInPanel.empty());
    UI(11,SCO_WIDGET_TOGGLE,nullptr,0);CHECK(!db.count("passphrase"));
    unload();CHECK(panelRemoved && messages.empty() && events.empty());unload();
    Reset();CHECK(load(&api,owner)==SCO_OK);UI(18,SCO_WIDGET_COMBO,nullptr,0);CHECK(passInPanel=="synthetic remembered passphrase");unload();
    { // A failed read must never turn a populated slot into an overwrite target.
        Reset();const auto before=db;failGetKey="profile0.address";CHECK(load(&api,owner)==SCO_OK);
        UI(5,SCO_WIDGET_TEXT_INPUT,"127.0.0.9");UI(19,SCO_WIDGET_BUTTON);CHECK(db.at("profile0.address")==before.at("profile0.address"));
        failGetKey.clear();unload();
    }
    { // Selection must not alter the current secret on transient read failure.
        Reset();CHECK(load(&api,owner)==SCO_OK);UI(6,SCO_WIDGET_TEXT_INPUT,"current secret");
        UI(5,SCO_WIDGET_TEXT_INPUT,"127.0.0.7");const int beforeIndex=profileIndex;
        failGetKey="profile0.pass";UI(18,SCO_WIDGET_COMBO,nullptr,0);CHECK(passInPanel=="current secret" && addressInPanel=="127.0.0.7" && profileIndex==beforeIndex);
        CHECK(panelReply.find("could not be read")!=std::string::npos);
        failGetKey.clear();unload();
    }
    { // Bad or unavailable ports and oversized addresses cannot be reused as empty slots.
        const auto saved=db;
        for(const auto& failure:{std::string("port-error"),std::string("bad-port"),std::string("long-address")}) {
            db=saved;Reset();
            if(failure=="port-error") failGetKey="profile0.port";
            else if(failure=="bad-port") db["profile0.port"]="invalid";
            else db["profile0.address"]=std::string(300,'x');
            CHECK(load(&api,owner)==SCO_OK);const auto original=db.at("profile0.address");
            UI(5,SCO_WIDGET_TEXT_INPUT,"127.0.0.8");UI(19,SCO_WIDGET_BUTTON);CHECK(db.at("profile0.address")==original);
            failGetKey.clear();unload();
        }
        db=saved;
    }
    { // A genuinely missing passphrase selects the profile with an empty secret.
        const auto saved=db;db.erase("profile0.pass");Reset();CHECK(load(&api,owner)==SCO_OK);
        UI(6,SCO_WIDGET_TEXT_INPUT,"current secret");UI(18,SCO_WIDGET_COMBO,nullptr,0);
        CHECK(passInPanel.empty() && !rememberSeen && profileIndex==0);unload();db=saved;
    }
    Reset();widgetsPresent=false;storagePresent=false;CHECK(load(&api,owner)==SCO_OK);CHECK(!panelCallback && commands.size()==3);unload();widgetsPresent=storagePresent=true;
    Reset();brokerPresent=false;const unsigned priorHosts=hostCalls;
    CHECK(load(&api,owner)==SCO_OK);CHECK(commands.size()==3 && maskSeen);UI(6,SCO_WIDGET_TEXT_INPUT,"synthetic passphrase");UI(1,SCO_WIDGET_BUTTON);CHECK(hostCalls==priorHosts);
    CHECK(commands.at("multiplayer.leave").fn(nullptr,0,nullptr,output,sizeof(output))==SCO_UNAVAILABLE);
    unload();Reset();brokerPresent=true;admitted=false;
    CHECK(load(&api,owner)==SCO_OK);UI(6,SCO_WIDGET_TEXT_INPUT,"synthetic passphrase");UI(1,SCO_WIDGET_BUTTON);CHECK(!networkActive);unload();
    CHECK(!mpprotocol::WaitTimedOut(100,10099));
    CHECK(mpprotocol::WaitTimedOut(100,10100));
    CHECK(!mpprotocol::WaitTimedOut(0,10100));
    // Compatibility evidence gates display, including after leave/rejoin. A role can only claim
    // relay for authenticated peer 1; malformed and mismatched bytes revoke admission.
    Reset();modLog.clear();selfId=1;members={1,2};memberTotal=2;admitted=true;
    CHECK(load(&api,owner)==SCO_OK);networkActive=true;state=SC_PRIVATE_SESSION_HOSTING;StateEvent(true);
    PeerEvent(SCO_NET_PEER_JOINED,2);Deliver("multiplayer.spawn",2,SpawnBytes(2));Deliver("multiplayer.pose",2,PoseBytes(1,7));Tick();
    CHECK(panelRows.empty() && StatusText().find("1 unverified or incompatible")!=std::string::npos);
    Deliver(mpprotocol::kChannel,2,Bytes({1,mpprotocol::kPlayer}));
    Deliver(mpprotocol::kChannel,2,Bytes({mpprotocol::kGeneration}));
    Deliver(mpprotocol::kChannel,2,Bytes({mpprotocol::kGeneration,mpprotocol::kRelay}));Tick();
    CHECK(panelRows.empty() && StatusText().find("1 unverified or incompatible")!=std::string::npos);
    std::this_thread::sleep_for(std::chrono::milliseconds(mpprotocol::kWaitMs+10));Tick();
    CHECK(StatusText().find("protocol verification timed out or incompatible")!=std::string::npos && panelRows.empty());
    Deliver(mpprotocol::kChannel,2,Bytes({mpprotocol::kGeneration,mpprotocol::kPlayer}));
    Deliver("multiplayer.spawn",2,SpawnBytes(2));Deliver("multiplayer.pose",2,PoseBytes(2,7));Tick();
    CHECK(panelRows.size()==1 && StatusText().find("0 unverified or incompatible")!=std::string::npos);
    Deliver(mpprotocol::kChannel,2,Bytes({99,mpprotocol::kPlayer}));Tick();CHECK(panelRows.empty());
    Deliver(mpprotocol::kChannel,2,Bytes({mpprotocol::kGeneration,mpprotocol::kPlayer}));Tick();CHECK(panelRows.size()==1);
    Bytes oldPose=PoseBytes(3,11);oldPose[0]=1;oldPose.erase(oldPose.begin()+8,oldPose.begin()+28);
    Deliver("multiplayer.pose",2,oldPose);Tick();CHECK(panelRows.empty() && LogCount("version 1 pose")==1);
    Deliver(mpprotocol::kChannel,2,Bytes({mpprotocol::kGeneration,mpprotocol::kPlayer}));
    Deliver("multiplayer.spawn",2,SpawnBytes(2));Deliver("multiplayer.pose",2,PoseBytes(4,11));Tick();CHECK(panelRows.empty());
    PeerEvent(SCO_NET_PEER_LEFT,2);PeerEvent(SCO_NET_PEER_JOINED,2);
    Deliver(mpprotocol::kChannel,2,Bytes({mpprotocol::kGeneration,mpprotocol::kPlayer}));Tick();CHECK(panelRows.size()==1);
    members={1};memberTotal=1;CHECK(StatusText().find("0 other players")!=std::string::npos);Tick();CHECK(panelRows.empty());
    members={1,2};memberTotal=2;PeerEvent(SCO_NET_PEER_LEFT,2);PeerEvent(SCO_NET_PEER_JOINED,2);Tick();CHECK(panelRows.empty());
    unload();
    // A joining player cannot trust a roster from a relay without its own matching announcement.
    Reset();modLog.clear();selfId=3;members={3,1,2};serverIsRelay=true;
    CHECK(load(&api,owner)==SCO_OK);networkActive=true;state=SC_PRIVATE_SESSION_CONNECTED;StateEvent(true);
    Deliver(mpprotocol::kChannel,2,Bytes({mpprotocol::kGeneration,mpprotocol::kPlayer}));
    Deliver("multiplayer.relay",1,RelayBytes(true,15,{{2,0,1},{3,1,1}}));
    Deliver("multiplayer.spawn",2,SpawnBytes(2));Deliver("multiplayer.pose",2,PoseBytes(1,7));Tick();
    CHECK(panelRows.empty() && StatusText().find("unverified or incompatible")!=std::string::npos && StatusText().find("via relay")==std::string::npos);
    CHECK(StatusText().find("mode: host role unverified")!=std::string::npos);
    Deliver(mpprotocol::kChannel,1,Bytes({1,mpprotocol::kRelay}));
    Deliver(mpprotocol::kChannel,1,Bytes({mpprotocol::kGeneration}));
    Deliver("multiplayer.relay",1,RelayBytes(true,15,{{2,0,1},{3,1,1}}));Tick();CHECK(panelRows.empty() && messages.count("multiplayer.near.1")==0);
    Deliver(mpprotocol::kChannel,1,Bytes({mpprotocol::kGeneration,mpprotocol::kRelay}));
    Deliver("multiplayer.relay",1,RelayBytes(true,15,{{2,0,1},{3,1,1}}));Tick();
    CHECK(messages.count("multiplayer.near.1")==1 && StatusText().find("via relay")!=std::string::npos);
    CHECK(StatusText().find("mode: relay-announced host (message forwarding only)")!=std::string::npos);
    CHECK(panelStatus.find("mode: relay-announced host (message forwarding only)")!=std::string::npos);
    Deliver(mpprotocol::kChannel,1,Bytes({99,mpprotocol::kRelay}));Tick();
    CHECK(messages.count("multiplayer.near.1")==0 && panelRows.empty() && StatusText().find("via relay")==std::string::npos);
    unload();
    Reset();refuseProtocol=true;CHECK(load(&api,owner)!=SCO_OK);CHECK(messages.empty() && events.empty());refuseProtocol=false;
    Reset();net.register_channel=nullptr;CHECK(load(&api,owner)==SCO_OK);CHECK(messages.empty());
    { const unsigned oldHosts=hostCalls,oldJoins=joinCalls;
      UI(6,SCO_WIDGET_TEXT_INPUT,"synthetic passphrase");UI(1,SCO_WIDGET_BUTTON);
      UI(6,SCO_WIDGET_TEXT_INPUT,"synthetic passphrase");UI(2,SCO_WIDGET_BUTTON);
      CHECK(hostCalls==oldHosts && joinCalls==oldJoins && !networkActive);
      CHECK(panelReply.find("protocol network service unavailable")!=std::string::npos);
      CHECK(panelStatus.find("protocol unavailable")!=std::string::npos && StatusText().find("protocol unavailable")!=std::string::npos);
    }
    unload();net.register_channel=Channel;
    Reset();net.send_channel=nullptr;CHECK(load(&api,owner)==SCO_OK);
    { const unsigned oldHosts=hostCalls,oldJoins=joinCalls;
      UI(6,SCO_WIDGET_TEXT_INPUT,"synthetic passphrase");UI(1,SCO_WIDGET_BUTTON);
      UI(6,SCO_WIDGET_TEXT_INPUT,"synthetic passphrase");UI(2,SCO_WIDGET_BUTTON);
      CHECK(hostCalls==oldHosts && joinCalls==oldJoins && !networkActive);
      CHECK(panelStatus.find("protocol unavailable")!=std::string::npos && StatusText().find("protocol unavailable")!=std::string::npos);
    }
    unload();net.send_channel=Send;
    ChatSessions(&api,load,unload);
    // ---- a sco-relay server hosts the session: peer 1 is the server, never a player ----------------------
    // Golden bytes of multiplayer.relay, the same vectors as sco-relay's tests/server/test_relay_wire.cpp.
    CHECK(std::strcmp(rw::kRelayChannel,"multiplayer.relay")==0);
    CHECK(RelayBytes(true,15,{{2,0,1},{5,3,2}})==Bytes({0x01,0x01,0x0F,0x00,0x02,0x00, 0x02,0,0,0,0,0,0,0,0x00,0x00,0x01, 0x05,0,0,0,0,0,0,0,0x03,0x00,0x02}));
    CHECK(RelayBytes(false,15,{})==Bytes({0x01,0x00,0x0F,0x00,0x00,0x00}));
    CHECK(RelayBytes(false,400,{{0x0102030405060708ull,300,255}})==Bytes({0x01,0x00,0x90,0x01,0x01,0x00,0x08,0x07,0x06,0x05,0x04,0x03,0x02,0x01,0x2C,0x01,0xFF}));
    Reset();brokerPresent=true;admitted=true;modLog.clear();serverIsRelay=true;selfId=3;members={3,1,2};
    CHECK(load(&api,owner)==SCO_OK);
    CHECK(messages.count("multiplayer.relay")==1 && channelFlags.at("multiplayer.relay")==(SCO_NET_RELIABLE|SCO_NET_FROM_HOST) && channelMax.at("multiplayer.relay")==rw::kRelayMaxBytes);
    CHECK(modLog.empty()); // loading says nothing and opens nothing
    networkActive=true;state=SC_PRIVATE_SESSION_CONNECTED;
    StateEvent(true);PeerEvent(SCO_NET_PEER_JOINED,1);PeerEvent(SCO_NET_PEER_JOINED,2);
    Deliver(mpprotocol::kChannel,1,Bytes({mpprotocol::kGeneration,mpprotocol::kRelay}));
    Deliver(mpprotocol::kChannel,2,Bytes({mpprotocol::kGeneration,mpprotocol::kPlayer}));Tick();
    // Before the server says what it is, the join event of peer 1 makes no record: only peer 2 is counted.
    CHECK(StatusText().find("1 other players")!=std::string::npos && StatusText().find("via relay")==std::string::npos);
    CHECK(StatusText().find("mode: relay-announced host (message forwarding only)")!=std::string::npos);
    Deliver("multiplayer.spawn",2,SpawnBytes(2));Deliver("multiplayer.pose",2,PoseBytes(1,3));
    const unsigned spawnsBefore=spawnCalls;Tick();CHECK(spawnCalls==spawnsBefore+1);
    // The roster, from the server, lists peers 2 and 3 (this player) and not the server.
    const Bytes roster=RelayBytes(false,15,{{2,0,1},{3,1,4}});
    Deliver("multiplayer.relay",2,RelayBytes(false,15,{{2,0,1},{3,1,4},{9,2,1}})); // not from the host: ignored
    CHECK(LogCount("relay server found")==0);
    Deliver("multiplayer.relay",1,roster);Tick();
    CHECK(LogCount("relay server found")==1 && LogCount("joined relay server as peer 3 in slot 1 (generation 4): 2 of 15 transport members, nearby-only off")==1);
    CHECK(StatusText().find("1 other players; via relay server (not a player), slot 1, nearby-only off")!=std::string::npos);
    CHECK(panelRows.size()==1 && panelRows[0].find("Relay")==std::string::npos && panelStatus.find("1 peers; via relay server")!=std::string::npos);
    Deliver("multiplayer.relay",1,roster);CHECK(LogCount("joined relay")==1 && LogCount("relay roster:")==0); // the same roster again is silent
    // The server sends no pose or spawn and is never made a player, even if it did.
    const unsigned spawnsNow=spawnCalls;
    Deliver("multiplayer.spawn",1,SpawnBytes(1));Deliver("multiplayer.pose",1,PoseBytes(1,5));Tick();
    CHECK(spawnCalls==spawnsNow && panelRows.size()==1 && StatusText().find("1 other players")!=std::string::npos);
    // A roster that is not the contract is dropped, and said once.
    Bytes cut=roster;cut.pop_back();
    Deliver("multiplayer.relay",1,cut);Deliver("multiplayer.relay",1,Bytes({0x02,0x00,0x0F,0x00,0x00,0x00}));
    CHECK(LogCount("ignored a malformed multiplayer.relay")==1 && StatusText().find("malformed relay messages ignored")!=std::string::npos);
    // A new player: the roster changes, and the line says so.
    members={3,1,2,4};Deliver("multiplayer.relay",1,RelayBytes(false,15,{{2,0,1},{3,1,4},{4,2,1}}));
    CHECK(LogCount("relay roster: 3 of 15 transport members, slot 1 (generation 4), nearby-only off")==1);
    // Players leave one at a time; the session ends: the module says it left the relay session and forgets the server.
    const unsigned removesBefore=removeCalls;
    PeerEvent(SCO_NET_PEER_LEFT,2);Tick();CHECK(removeCalls==removesBefore+1 && panelRows.empty());
    networkActive=false;state=SC_PRIVATE_SESSION_IDLE;StateEvent(false);
    CHECK(LogCount("left the relay session")==1 && StatusText().find("via relay")==std::string::npos);
    unload();CHECK(messages.empty() && events.empty());
    // A session host that is a player (the unmodified module hosting) is a player: its first spawn makes its record.
    Reset();modLog.clear();serverIsRelay=false;selfId=2;members={2,1};
    CHECK(load(&api,owner)==SCO_OK);networkActive=true;state=SC_PRIVATE_SESSION_CONNECTED;
    StateEvent(true);PeerEvent(SCO_NET_PEER_JOINED,1);Tick();
    CHECK(StatusText().find("0 other players")!=std::string::npos);
    Deliver(mpprotocol::kChannel,1,Bytes({mpprotocol::kGeneration,mpprotocol::kPlayer}));
    CHECK(StatusText().find("mode: player-hosted co-presence (game state remains local)")!=std::string::npos);
    Deliver("multiplayer.spawn",1,SpawnBytes(1));CHECK(StatusText().find("1 other players")!=std::string::npos && LogCount("relay")==0);
    unload();
    // ---- nearby-only: poses go to the relay server on multiplayer.near_up and come back on multiplayer.near.<slot> ----
    {   // Golden bytes of the two messages, the same vectors as sco-relay's tests/server/test_relay_wire.cpp.
        const Bytes pose3={0xAA,0xBB,0xCC};
        CHECK(rw::EncodeNearUp(0x1122334455667788ull,1.0,-2.0,0.5,pose3.data(),pose3.size())==Bytes({0x01,0,0,0,0,0,0,0, 0x88,0x77,0x66,0x55,0x44,0x33,0x22,0x11, 0,0,0,0,0,0,0xF0,0x3F, 0,0,0,0,0,0,0x00,0xC0, 0,0,0,0,0,0,0xE0,0x3F, 0xAA,0xBB,0xCC}));
        CHECK(rw::EncodeNearDown(2,10,pose3.data(),pose3.size())==Bytes({0x01,0x02,0,0,0,0,0,0, 0x0A,0,0,0,0,0,0,0, 0xAA,0xBB,0xCC}));
        CHECK(rw::Fnv1a64("OOC_Stanton_2b_Daymar",21)==0x7b9a22aa73a012a6ull);
        CHECK(std::strcmp(rw::kNearUpChannel,"multiplayer.near_up")==0 && rw::NearDownChannel(1)=="multiplayer.near.1");
    }
    Reset();brokerPresent=true;admitted=true;modLog.clear();serverIsRelay=true;selfId=3;members={3,1,2,4,5};sentCount.clear();sentLast.clear();
    CHECK(load(&api,owner)==SCO_OK);
    CHECK(messages.size()==7 && channelFlags.at("multiplayer.near_up")==SCO_NET_TO_HOST && channelMax.at("multiplayer.near_up")==rw::kNearUpMaxBytes);
    networkActive=true;state=SC_PRIVATE_SESSION_CONNECTED;StateEvent(true);
    Deliver(mpprotocol::kChannel,1,Bytes({mpprotocol::kGeneration,mpprotocol::kRelay}));
    for(uint64_t id:{uint64_t(4),uint64_t(5)}) { PeerEvent(SCO_NET_PEER_JOINED,id);Deliver(mpprotocol::kChannel,id,Bytes({mpprotocol::kGeneration,mpprotocol::kPlayer}));Deliver("multiplayer.spawn",id,SpawnBytes(id)); }
    Tick(); // no roster yet: the pose goes to the other players, as it does in a session a player hosts
    CHECK(sentCount["multiplayer.pose"]==1 && sentCount["multiplayer.near_up"]==0);
    // The roster: nearby-only on, this player in slot 1, generation 7.
    const Bytes nearRoster=RelayBytes(true,15,{{2,0,1},{3,1,7},{4,2,1},{5,3,1}});
    Deliver("multiplayer.relay",1,nearRoster);
    CHECK(LogCount("joined relay server as peer 3 in slot 1 (generation 7): 4 of 15 transport members, nearby-only on")==1);
    CHECK(LogCount("nearby-only: poses go to the relay server on multiplayer.near_up and come back on multiplayer.near.1")==1);
    CHECK(messages.count("multiplayer.near.1")==1 && channelFlags.at("multiplayer.near.1")==SCO_NET_FROM_HOST && channelMax.at("multiplayer.near.1")==rw::kNearDownMaxBytes);
    CHECK(StatusText().find("slot 1, nearby-only on")!=std::string::npos);
    sentCount.clear();std::this_thread::sleep_for(std::chrono::milliseconds(110));Tick();
    // The pose went up on near_up with the area (a hash of the outermost zone) and the position in it, and not on multiplayer.pose.
    CHECK(sentCount["multiplayer.pose"]==0 && sentCount["multiplayer.near_up"]==1);
    {
        rw::NearUp up;const Bytes& upBytes=sentLast["multiplayer.near_up"];
        CHECK(rw::DecodeNearUp(upBytes.data(),upBytes.size(),&up) && up.area==rw::Fnv1a64("Synthetic_Zone",14) && up.x==0 && up.y==0 && up.z==0);
        mpwire::Pose upPose{};
        CHECK(mpwire::DecodePose(up.pose,up.poseLen,upPose) && upPose.levels==1 && !std::strcmp(upPose.lv[0].name,"Synthetic_Zone") && upPose.seq>=1);
    }
    // Poses come back on this player's own channel. Each of these is dropped; none makes a ghost for peer 4.
    const Bytes poseFour=PoseBytes(5,11);
    const unsigned spawnsBeforeNear=spawnCalls;
    const auto down=[&](uint8_t generation,uint64_t sender) { return rw::EncodeNearDown(generation,sender,poseFour.data(),poseFour.size()); };
    Deliver("multiplayer.near.1",1,down(6,4));      // for an earlier occupant of the slot
    Deliver("multiplayer.near.1",1,down(8,4));      // for a later one
    Deliver("multiplayer.near.1",2,down(7,4));      // not from the host
    Deliver("multiplayer.near.1",1,down(7,3));      // the sender is this player itself
    Deliver("multiplayer.near.1",1,down(7,1));      // the sender is the server
    Deliver("multiplayer.near.1",1,down(7,99));     // the sender is not in the session
    Tick();CHECK(spawnCalls==spawnsBeforeNear && LogCount("ignored a malformed multiplayer.near")==0);
    Bytes dirty=down(7,4);dirty[3]=1;               // a reserved byte that is not zero
    Deliver("multiplayer.near.1",1,dirty);
    const Bytes whole=down(7,4);
    Deliver("multiplayer.near.1",1,Bytes(whole.begin(),whole.begin()+16)); // a header and no pose
    Bytes garbage=down(7,4);garbage[16]=99;         // a pose that is not a pose
    Deliver("multiplayer.near.1",1,garbage);
    Tick();CHECK(spawnCalls==spawnsBeforeNear && LogCount("ignored a malformed multiplayer.near")==1);
    // The right one: generation 7, from the host, naming peer 4.
    Deliver("multiplayer.near.1",1,down(7,4));Tick();
    CHECK(spawnCalls==spawnsBeforeNear+1 && lastGhostX==11);
    Bytes nearOldPose=poseFour;nearOldPose[0]=1;nearOldPose.erase(nearOldPose.begin()+8,nearOldPose.begin()+28);
    Deliver("multiplayer.near.1",1,rw::EncodeNearDown(7,4,nearOldPose.data(),nearOldPose.size()));Tick();
    CHECK(panelRows.size()==1 && LogCount("version 1 pose")==1);
    Deliver(mpprotocol::kChannel,4,Bytes({mpprotocol::kGeneration,mpprotocol::kPlayer}));Tick();CHECK(panelRows.size()==1);
    PeerEvent(SCO_NET_PEER_LEFT,4);PeerEvent(SCO_NET_PEER_JOINED,4);
    Deliver(mpprotocol::kChannel,4,Bytes({mpprotocol::kGeneration,mpprotocol::kPlayer}));Tick();CHECK(panelRows.size()==2);
    // The roster says nearby-only is off: the module stops listening on its slot and sends its pose to the other players again.
    Deliver("multiplayer.relay",1,RelayBytes(false,15,{{2,0,1},{3,1,7},{4,2,1},{5,3,1}}));
    CHECK(messages.count("multiplayer.near.1")==0 && LogCount("nearby-only is off: poses go to the other players on multiplayer.pose")==1);
    sentCount.clear();std::this_thread::sleep_for(std::chrono::milliseconds(110));Tick();
    CHECK(sentCount["multiplayer.pose"]==1 && sentCount["multiplayer.near_up"]==0);
    // On again, in another seat: the old channel goes and the new one comes.
    Deliver("multiplayer.relay",1,RelayBytes(true,15,{{2,0,1},{3,2,1},{4,1,1}}));
    CHECK(messages.count("multiplayer.near.1")==0 && messages.count("multiplayer.near.2")==1);
    CHECK(LogCount("come back on multiplayer.near.2")==1);
    // The session ends: nothing stays registered.
    networkActive=false;state=SC_PRIVATE_SESSION_IDLE;StateEvent(false);
    CHECK(messages.count("multiplayer.near.2")==0 && messages.size()==7);
    unload();CHECK(messages.empty() && events.empty());
    // ---- sco.net refuses the multiplayer.near.<slot> channel: this player's own pose falls back to multiplayer.pose, the
    // module says what that means once per session (every roster retries), and it listens as soon as the channel is accepted.
    {
        const char* line="cannot listen on multiplayer.near.1 (3): this player sends its poses on multiplayer.pose and will not see the players the server forwards on that channel";
        Reset();modLog.clear();serverIsRelay=true;selfId=3;members={3,1,2};sentCount.clear();sentLast.clear();refuseNear=true;
        CHECK(load(&api,owner)==SCO_OK);
        networkActive=true;state=SC_PRIVATE_SESSION_CONNECTED;StateEvent(true);
        Deliver(mpprotocol::kChannel,1,Bytes({mpprotocol::kGeneration,mpprotocol::kRelay}));
        const Bytes nearRosterAgain=RelayBytes(true,15,{{2,0,1},{3,1,4}});
        for(int i=0;i<4;++i) Deliver("multiplayer.relay",1,nearRosterAgain);                     // one roster per announce interval
        Deliver("multiplayer.relay",1,RelayBytes(true,15,{{2,0,1},{3,1,4},{4,2,1}}));          // and one that changes
        CHECK(LogCount(line)==1 && LogCount("cannot listen")==1);
        CHECK(messages.count("multiplayer.near.1")==0 && LogCount("nearby-only: poses go to the relay server")==0);
        sentCount.clear();std::this_thread::sleep_for(std::chrono::milliseconds(110));Tick();
        CHECK(sentCount["multiplayer.pose"]==1 && sentCount["multiplayer.near_up"]==0);          // the own pose falls back
        refuseNear=false;                                                                        // the channel is accepted at the next roster
        Deliver("multiplayer.relay",1,nearRosterAgain);
        CHECK(messages.count("multiplayer.near.1")==1 && LogCount("come back on multiplayer.near.1")==1 && LogCount("cannot listen")==1);
        sentCount.clear();std::this_thread::sleep_for(std::chrono::milliseconds(110));Tick();
        CHECK(sentCount["multiplayer.near_up"]==1 && sentCount["multiplayer.pose"]==0);
        // A new session warns again, once.
        networkActive=false;state=SC_PRIVATE_SESSION_IDLE;StateEvent(false);
        refuseNear=true;networkActive=true;state=SC_PRIVATE_SESSION_CONNECTED;StateEvent(true);
        Deliver(mpprotocol::kChannel,1,Bytes({mpprotocol::kGeneration,mpprotocol::kRelay}));
        for(int i=0;i<3;++i) Deliver("multiplayer.relay",1,nearRosterAgain);
        CHECK(LogCount(line)==2 && messages.count("multiplayer.near.1")==0);
        refuseNear=false;
        unload();CHECK(messages.empty() && events.empty());
    }
#if defined(_WIN32)
    FreeLibrary(module);
#else
    dlclose(module);
#endif
    std::printf("multiplayer module: %u passed, %u failed (synthetic public services; no sockets/game)\n",passed,failed);
    return failed?1:0;
}
