// SPDX-License-Identifier: GPL-3.0-only
// External consumer: host-owned G13 control, sco.net channels, copied spatial
// observations and owner-scoped ghosts. Never sockets, private engine or G2.
#include <sco_api.h>
#include <sco_net.h>
#include <sco_storage.h>
#include <sco_widgets.h>
#include <sc_private_session.h>
#include <sc_spatial.h>
#include <sc_game_spatial.h>
#include <sc_actors.h>
#include <sc_entities.h>
#include <sc_vehicles.h>
#include "multiplayer_wire.h"
#include "relay_wire.h"
#include "motion.h"
#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
#define METHOD(t,m) ((t) && (t)->size >= offsetof(std::remove_pointer_t<decltype(t)>,m)+sizeof((t)->m) && (t)->m)
const sco_api* api=nullptr;
sco_plugin* self=nullptr;
const sco_net_v1* net=nullptr;
const sc_private_session_v1* broker=nullptr;
const sco_storage_v1* store=nullptr;
const sco_widgets_v1* widgets=nullptr;
const sc_spatial_v1* spatial=nullptr;
const sc_game_spatial_v1* poses=nullptr;
const sc_actors_v1* actors=nullptr;
const sc_entities_v1* entities=nullptr;
const sc_vehicles_v1* vehicles=nullptr;
const sco_plugin_info info={sizeof(sco_plugin_info),SCO_API_MAJOR,SCO_API_MINOR,"multiplayer","1.0.0","scubamount"};
namespace rw=sco::relay::wire;
namespace mpprotocol=sco::relay::wire::protocol;
// A pose from this plugin must fit the relay's near_up / near.<slot> envelope (relay_wire.h), or nearby-only forwarding would drop it.
static_assert(mpwire::kMaxPose<=rw::kPoseMaxBytes,"a pose from multiplayer_wire.h must fit the relay wire's pose limit");
enum ChannelIndex : unsigned { kPoseChannel,kSpawnChannel,kDespawnChannel,kRelayChannel,kNearUpChannel,kProtocolChannel,kChatChannel,kChannelCount };
constexpr const char* channels[]={"multiplayer.pose","multiplayer.spawn","multiplayer.despawn","multiplayer.relay","multiplayer.near_up",mpprotocol::kChannel,mpwire::kChatChannel};
static_assert(sizeof(channels)/sizeof(channels[0])==kChannelCount,"one name per channel");
constexpr uint64_t kHostPeer=1; // sco.net: the session host is always peer 1
constexpr const char* panelId="multiplayer.session";
constexpr const char* defaultAvatar="PU_Human-Microtech-DeskWorker-Male-DistroHub_Civilian_01";
struct Settings {
    char name[65]="Pilot",address[16]="",avatar[64]="",pass[257]="";
    int hostPort=64091,joinPort=64091;
    bool showShips=true,remember=false;
} cfg;
constexpr unsigned kProfiles=8;
struct Profile { char address[16]=""; int port=64091; };
std::array<Profile,kProfiles> profiles{};
int selectedProfile=-1;
bool profilesLoaded=false;
char reply[256]="No session. Host/Join require fresh local activation; default loopback only.";
char reason[128]="";
bool panelLive=false,wasActive=false,announced=false,protocolWarned=false,poseWarned=false;
unsigned registeredChannels=0;
bool ProtocolAvailable() { return registeredChannels==kChannelCount && METHOD(net,send_channel) && METHOD(net,get_peers) && METHOD(net,self_peer) && METHOD(net,is_active); }
uint32_t sequence=0;
uint64_t lastSend=0,shipId=0,lastProtocol=0,protocolWaitSince=0;
struct ProtocolPeer { uint64_t id; uint8_t role; };
std::vector<ProtocolPeer> protocolPeers;
std::vector<uint64_t> protocolContradictions;
mpwire::Pose previousSent{};
bool havePreviousSent=false;
char announcedShip[64]="";
struct Ghost { uint64_t id=0,since=0,lastAttempt=0; char cls[64]=""; bool actor=false; };
struct Peer {
    uint64_t id=0;
    char name[65]="",avatarClass[64]="",shipClass[64]="";
    mpmotion::Track track; // this player's last poses: the ghosts are drawn from them (motion.h)
    Ghost avatar{},ship{};
};
std::vector<Peer> peers;
struct Removal { uint64_t id; bool actor; };
std::vector<Removal> cleanup; // bounded by ghost budget, never pruned by ambiguous alive=0
// A sco-relay server hosts the session and says so with multiplayer.relay: who is in, in which slot.
// It is a server, never a player: it gets no peer record and is not counted or listed.
struct Relay {
    bool seen=false,nearby=false;
    uint16_t maxPlayers=0;
    int slot=-1;uint8_t generation=0; // this player's seat in the last roster; slot -1: not listed
    bool joinLogged=false;            // the "joined relay server" line was written (the first roster that listed this player)
    std::vector<uint64_t> players;    // sorted peer ids of that roster
    unsigned bad=0;                   // malformed relay messages this session
    bool nearWarned=false;            // the "cannot listen on multiplayer.near.<slot>" warning was written this session
} relay;
// Text chat (multiplayer.chat, multiplayer_wire.h), for one session only: Leave, the end of the session and unload clear it (Clear).
// What a player says is copied into rows and shown. It is never logged, never used as a format string, never run as a command.
struct ChatSenderWindow { uint64_t id=0; mpwire::RateWindow<mpwire::kChatAcceptPerWindow> window; };
struct ChatState {
    std::vector<std::string> rows;                           // "name: text", oldest first, at most mpwire::kChatHistoryRows
    char draft[SCO_WIDGETS_MAX_TEXT+1]="";                   // what the message box holds
    mpwire::RateWindow<mpwire::kChatSendsPerWindow> sent;    // this player's own sends
    std::vector<ChatSenderWindow> senders;                   // how much each verified sender has had read lately
    unsigned overRate=0,notAccepted=0;                       // lines dropped for rate; lines from nobody verified or not the contract
} chat;
uint64_t Now() { return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); }
void Wipe(char* s,size_t n) { volatile char* p=s;while(n--) *p++=0; }
template<size_t N> bool Text(char (&out)[N],const char* value,size_t max=N-1) {
    if(!value) return false;
    size_t n=0;for(;n<=max && value[n];++n) if(static_cast<unsigned char>(value[n])<0x20) return false;
    if(n>max || n>=N) return false;
    std::memcpy(out,value,n);out[n]=0;return true;
}
// One line in mod.log as "[multiplayer] text". Never a passphrase, an address or a player's name.
void Log(sco_log_level level,const char* format,...) {
    if(!METHOD(api,log) || !self) return;
    char text[320];va_list args;va_start(args,format);
    std::vsnprintf(text,sizeof(text),format,args);va_end(args);
    text[sizeof(text)-1]=0;api->log(self,level,text);
}
unsigned long long Ull(uint64_t v) { return static_cast<unsigned long long>(v); }
template<class T> const T* Query(const char* name,uint32_t version) {
    const void* table=nullptr;
    return METHOD(api,query_service) && api->query_service(name,version,&table)==SCO_OK ? static_cast<const T*>(table):nullptr;
}
void Services() {
    broker=Query<sc_private_session_v1>(SC_PRIVATE_SESSION_NAME,SC_PRIVATE_SESSION_VERSION);
    spatial=Query<sc_spatial_v1>(SC_SPATIAL_SERVICE_NAME,SC_SPATIAL_SERVICE_VERSION);
    poses=Query<sc_game_spatial_v1>(SC_GAME_SPATIAL_NAME,SC_GAME_SPATIAL_VERSION);
    actors=Query<sc_actors_v1>(SC_ACTORS_NAME,SC_ACTORS_VERSION_1_0);
    entities=Query<sc_entities_v1>(SC_ENTITIES_NAME,SC_ENTITIES_VERSION_1_0);
    vehicles=Query<sc_vehicles_v1>(SC_VEHICLES_SERVICE_NAME,SC_VEHICLES_SERVICE_VERSION);
}
bool Active() { return METHOD(net,is_active) && net->is_active(); }
sc_private_session_state_v1 State() {
    sc_private_session_state_v1 out{};out.size=sizeof(out);
    if(!METHOD(broker,get_state) || broker->get_state(&out)!=SCO_OK) out.state=SC_PRIVATE_SESSION_IDLE;
    return out;
}
void UpdatePanel();
sco_result ReadValue(const char* key,char* out,uint32_t cap) {
    if(!METHOD(store,get) || cap<2) return SCO_UNAVAILABLE;
    std::vector<char> buf(cap,0);uint32_t size=cap-1;
    sco_result r=store->get(self,key,buf.data(),&size);
    if(r==SCO_OK && (size>=cap || std::memchr(buf.data(),0,size))) r=SCO_BAD_ARG;
    if(r==SCO_OK) { std::memcpy(out,buf.data(),size);out[size]=0; }
    Wipe(buf.data(),buf.size());return r;
}
bool Read(const char* key,char* out,uint32_t cap) { return ReadValue(key,out,cap)==SCO_OK; }
int ReadInt(const char* key,int fallback,int minimum,int maximum) {
    char text[24];if(!Read(key,text,sizeof(text))) return fallback;
    char* end=nullptr;const long v=std::strtol(text,&end,10);
    return !text[0] || *end || v<minimum || v>maximum ? fallback:static_cast<int>(v);
}
void ProfileKey(char* out,size_t cap,unsigned i,const char* field) { std::snprintf(out,cap,"profile%u.%s",i,field); }
void LoadProfiles() {
    for(auto& p:profiles) p=Profile();
    selectedProfile=-1;profilesLoaded=true;
    for(unsigned i=0;i<kProfiles;++i) {
        char key[40],text[257]{};ProfileKey(key,sizeof(key),i,"address");
        sco_result r=ReadValue(key,text,sizeof(text));
        if(r==SCO_NOT_FOUND) continue;
        if(r!=SCO_OK || !text[0] || !Text(profiles[i].address,text)) { profilesLoaded=false;Wipe(text,sizeof(text));continue; }
        ProfileKey(key,sizeof(key),i,"port");r=ReadValue(key,text,sizeof(text));
        if(r==SCO_OK) {
            char* end=nullptr;const long port=std::strtol(text,&end,10);
            if(!text[0] || *end || port<1024 || port>65535) { profiles[i]=Profile();profilesLoaded=false; }
            else profiles[i].port=static_cast<int>(port);
        } else if(r!=SCO_NOT_FOUND) { profiles[i]=Profile();profilesLoaded=false; }
        Wipe(text,sizeof(text));
    }
}
sco_result SaveProfile() {
    if(!profilesLoaded || !cfg.address[0] || !METHOD(store,put) || !METHOD(store,begin) || !METHOD(store,commit) || !METHOD(store,rollback)) return SCO_UNAVAILABLE;
    int slot=-1;
    for(unsigned i=0;i<kProfiles;++i) if(!std::strcmp(profiles[i].address,cfg.address) && profiles[i].port==cfg.joinPort) { slot=static_cast<int>(i);break; }
    if(slot<0) for(unsigned i=0;i<kProfiles;++i) if(!profiles[i].address[0]) { slot=static_cast<int>(i);break; }
    if(slot<0) return SCO_TOO_MANY;
    char key[40],port[24];std::snprintf(port,sizeof(port),"%d",cfg.joinPort);
    sco_result r=store->begin(self);if(r!=SCO_OK) return r;
    const auto put=[&](const char* field,const char* value) { ProfileKey(key,sizeof(key),static_cast<unsigned>(slot),field);if(r==SCO_OK) r=store->put(self,key,value,static_cast<uint32_t>(std::strlen(value))); };
    put("address",cfg.address);put("port",port);put("pass",cfg.remember?cfg.pass:"");
    if(r==SCO_OK) r=store->commit(self);
    if(r!=SCO_OK) { store->rollback(self);return r; }
    Profile& p=profiles[static_cast<unsigned>(slot)];Text(p.address,cfg.address);p.port=cfg.joinPort;selectedProfile=slot;return SCO_OK;
}
void SelectProfile(int slot) {
    if(slot<0 || slot>=static_cast<int>(kProfiles) || !profiles[static_cast<unsigned>(slot)].address[0]) return;
    char key[40],text[257]{};ProfileKey(key,sizeof(key),static_cast<unsigned>(slot),"pass");
    const sco_result r=ReadValue(key,text,sizeof(text));char pass[sizeof(cfg.pass)]{};
    if((r!=SCO_OK && r!=SCO_NOT_FOUND) || (r==SCO_OK && !Text(pass,text))) {
        Wipe(text,sizeof(text));Wipe(pass,sizeof(pass));std::snprintf(reply,sizeof(reply),"Server profile could not be read");return;
    }
    const Profile& p=profiles[static_cast<unsigned>(slot)];selectedProfile=slot;Text(cfg.address,p.address);cfg.joinPort=p.port;
    Wipe(cfg.pass,sizeof(cfg.pass));Text(cfg.pass,pass);cfg.remember=pass[0]!=0;Wipe(text,sizeof(text));Wipe(pass,sizeof(pass));
}
void LoadSettings() {
    cfg=Settings();Text(cfg.avatar,defaultAvatar);
    char text[257];
    if(Read("name",text,sizeof(text)) && text[0]) Text(cfg.name,text);
    if(Read("join_address",text,sizeof(text))) Text(cfg.address,text);
    if(Read("avatar_class",text,sizeof(text)) && mpwire::ClassOk(text,std::strlen(text))) Text(cfg.avatar,text);
    cfg.hostPort=ReadInt("host_port",64091,1024,65535);
    cfg.joinPort=ReadInt("join_port",64091,1024,65535);
    cfg.showShips=ReadInt("show_ships",1,0,1)!=0;
    cfg.remember=false;
    Wipe(text,sizeof(text));
}
sco_result SaveSettings() {
    if(!METHOD(store,put) || !METHOD(store,del) || !METHOD(store,begin) || !METHOD(store,commit) || !METHOD(store,rollback)) return SCO_UNAVAILABLE;
    sco_result r=store->begin(self);if(r!=SCO_OK) return r;
    const auto put=[&](const char* key,const char* value) { if(r==SCO_OK) r=store->put(self,key,value,static_cast<uint32_t>(std::strlen(value))); };
    const auto number=[&](const char* key,int v) { char s[24];std::snprintf(s,sizeof(s),"%d",v);put(key,s); };
    put("name",cfg.name);put("join_address",cfg.address);put("avatar_class",cfg.avatar);
    number("host_port",cfg.hostPort);number("join_port",cfg.joinPort);number("show_ships",cfg.showShips?1:0);number("remember_passphrase",cfg.remember?1:0);
    if(r==SCO_OK) { r=store->del(self,"passphrase");if(r==SCO_NOT_FOUND) r=SCO_OK; }
    if(r==SCO_OK) r=store->commit(self);
    if(r!=SCO_OK) store->rollback(self);
    return r;
}
void RetryCleanup() {
    for(auto it=cleanup.begin();it!=cleanup.end();) {
        sco_result r=SCO_UNAVAILABLE;
        if(it->actor && METHOD(actors,despawn)) r=actors->despawn(self,it->id);
        if(!it->actor && METHOD(entities,despawn)) r=entities->despawn(self,it->id);
        if(r==SCO_OK) it=cleanup.erase(it);else ++it;
    }
}
void Remove(Ghost& g) {
    if(g.id) cleanup.push_back({g.id,g.actor});
    g=Ghost();
}
void OnNear(uint64_t sender,const void* data,uint32_t len,void*);
void UpdateNearChannel();
char nearChannel[48]=""; // multiplayer.near.<slot> while this player listens on it, else empty
// Nearby-only: the relay server sends a player the poses of the players near it on a channel of its own slot,
// multiplayer.near.<slot>, FROM_HOST. The slot is the one the latest roster gave this player.
void SetNearChannel(const char* wanted) {
    if(!std::strcmp(wanted,nearChannel)) return;
    if(nearChannel[0]) { if(METHOD(net,unregister_channel)) net->unregister_channel(self,nearChannel);nearChannel[0]=0; }
    if(!wanted[0] || !METHOD(net,register_channel)) return;
    const sco_result r=net->register_channel(self,wanted,SCO_NET_FROM_HOST,static_cast<uint32_t>(rw::kNearDownMaxBytes),OnNear,nullptr);
    if(r==SCO_OK) std::snprintf(nearChannel,sizeof(nearChannel),"%s",wanted);
    else if(!relay.nearWarned) {
        // Once per session: every roster (every announce interval) tries again, and a refusal would repeat for the whole session.
        // Only this player's own pose falls back (SendPose sends it on multiplayer.pose); the copies the server forwards
        // to this slot go nowhere, so this player sees nobody it is told about that way.
        relay.nearWarned=true;
        Log(SCO_LOG_WARN,"cannot listen on %s (%d): this player sends its poses on multiplayer.pose and will not see the players the server forwards on that channel",wanted,static_cast<int>(r));
    }
}
void ResetRelay() {
    if(relay.seen) Log(SCO_LOG_INFO,"left the relay session");
    SetNearChannel("");
    relay=Relay();
}
void Clear() { havePreviousSent=false;for(auto& p:peers) { Remove(p.avatar);Remove(p.ship); }peers.clear();protocolPeers.clear();protocolContradictions.clear();lastProtocol=0;protocolWaitSince=0;protocolWarned=false;poseWarned=false;RetryCleanup();announced=false;announcedShip[0]=0;shipId=0;ResetRelay();chat=ChatState(); }
// A temporary loss of host evidence is not a session reset: preserve contradictions.
void ClearAdmission() {
    const auto contradictions=protocolContradictions;const bool warned=protocolWarned, poseWarning=poseWarned;
    ChatState kept=std::move(chat); // a lost verification is not a new session: what was said stays
    Clear();protocolContradictions=contradictions;protocolWarned=warned;poseWarned=poseWarning;chat=std::move(kept);
}
Peer* Find(uint64_t id) { for(auto& p:peers) if(p.id==id) return &p;return nullptr; }
// The session roster. sco.net 1.0 sessions hold at most SCO_NET_MAX_PEERS; a larger one answers SCO_TOO_MANY with the member count,
// and the call is repeated with room for all of them. Bounded: at most kMaxMembers entries and kMemberTries calls. Any other answer,
// or a count that does not grow past the room offered or exceeds the cap, is no list at all.
constexpr uint32_t kMaxMembers=64,kMemberTries=3;
bool Members(std::vector<sco_net_peer>& out) {
    if(!METHOD(net,get_peers)) return false;
    uint32_t room=SCO_NET_MAX_PEERS;
    for(uint32_t attempt=0;attempt<kMemberTries;++attempt) {
        out.assign(room,sco_net_peer{});uint32_t count=room;
        const sco_result r=net->get_peers(out.data(),&count);
        if(r==SCO_OK) { if(count>room) return false;out.resize(count);return true; }
        if(r!=SCO_TOO_MANY || count<=room || count>kMaxMembers) return false;
        room=count;
    }
    return false;
}
uint8_t ProtocolRole(uint64_t id) {
    for(const auto& p:protocolPeers) if(p.id==id) return p.role;
    return 0;
}
const char* SessionMode(const sc_private_session_state_v1& state) {
    if(!Active()) {
        if(state.state==SC_PRIVATE_SESSION_IDLE) return "no private session";
        if(state.state==SC_PRIVATE_SESSION_HOSTING) return "listen host starting";
        if(state.state==SC_PRIVATE_SESSION_JOINING) return "join pending";
        return "session inactive";
    }
    if(!METHOD(net,self_peer)) return "session role unavailable";
    const uint64_t localPeer=net->self_peer();
    if(!localPeer) return "session identity pending";
    if(localPeer==kHostPeer) return "listen host (co-presence only; game state remains local)";
    const uint8_t hostRole=ProtocolRole(kHostPeer);
    if(hostRole==mpprotocol::kRelay) return "relay-announced host (message forwarding only)";
    if(hostRole==mpprotocol::kPlayer) return "player-hosted co-presence (game state remains local)";
    return "host role unverified";
}
bool HostCompatible() {
    return METHOD(net,self_peer) && (net->self_peer()==kHostPeer || ProtocolRole(kHostPeer)!=0);
}
bool MembershipConsistent() {
    if(!Active()) return true;
    if(!METHOD(net,self_peer) || !net->self_peer()) return false;
    std::vector<sco_net_peer> members;if(!Members(members)) return false;
    const auto present=[&members](uint64_t id){return std::any_of(members.begin(),members.end(),[id](const sco_net_peer& p){return p.peer_id==id;});};
    if(!present(net->self_peer()) || (net->self_peer()!=kHostPeer && !present(kHostPeer))) return false;
    for(const auto& p:protocolPeers) if(!present(p.id)) return false;
    return true;
}
size_t UnverifiedCount() {
    if(!Active()) return 0;
    if(!MembershipConsistent()) return 1;
    std::vector<sco_net_peer> members;if(!Members(members)) return 1;
    if(std::none_of(members.begin(),members.end(),[](const sco_net_peer& p){return p.peer_id==net->self_peer();})) return 1;
    size_t count=0;for(const auto& m:members) if(m.peer_id!=net->self_peer() && !ProtocolRole(m.peer_id)) ++count;
    if(net->self_peer()!=kHostPeer && !HostCompatible() && std::none_of(members.begin(),members.end(),[](const sco_net_peer& p){return p.peer_id==kHostPeer;})) ++count;
    return count;
}
const char* ProtocolWait() {
    if(!UnverifiedCount()) return "";
    return mpprotocol::WaitTimedOut(protocolWaitSince,Now())?"; protocol verification timed out or incompatible; synchronize upgrades":"; waiting for matching protocol; incompatible peers excluded";
}
void ForgetProtocol(uint64_t id) {
    protocolPeers.erase(std::remove_if(protocolPeers.begin(),protocolPeers.end(),[id](const ProtocolPeer& p){return p.id==id;}),protocolPeers.end());
}
void SendProtocol() {
    if(!METHOD(net,send_channel)) return;
    const uint8_t message[]={mpprotocol::kGeneration,mpprotocol::kPlayer};
    net->send_channel(self,mpprotocol::kChannel,message,static_cast<uint32_t>(sizeof(message)));
}
void DropPeer(uint64_t id) {
    auto it=std::find_if(peers.begin(),peers.end(),[&](const Peer& p){return p.id==id;});
    if(it!=peers.end()) { Remove(it->avatar);Remove(it->ship);peers.erase(it);RetryCleanup(); }
}
void RejectContradictoryPose(uint64_t id) {
    if(ProtocolRole(id)!=mpprotocol::kPlayer) return;
    std::vector<sco_net_peer> members;if(!Members(members) || std::none_of(members.begin(),members.end(),[id](const sco_net_peer& p){return p.peer_id==id;})) return;
    if(id==kHostPeer) ClearAdmission();else { ForgetProtocol(id);DropPeer(id); }
    protocolContradictions.push_back(id);
    if(!poseWarned) { Log(SCO_LOG_WARN,"peer %llu announced the current protocol but sent a version 1 pose; excluded until it rejoins",Ull(id));poseWarned=true; }
}
Peer* Add(uint64_t id) {
    if(!id || !METHOD(net,self_peer) || id==net->self_peer() || !Active() || !HostCompatible() || ProtocolRole(id)!=mpprotocol::kPlayer) return nullptr;
    if(relay.seen && id==kHostPeer) return nullptr; // the relay server is not a player
    if(auto* p=Find(id)) return p;
    // Only authenticated session members get a record. Never trust raw sender IDs.
    std::vector<sco_net_peer> members;
    if(!Members(members) || peers.size()>=kMaxMembers-1) return nullptr;
    bool member=false;for(const auto& m:members) if(m.peer_id==id) member=true;
    if(!member) return nullptr;
    peers.emplace_back();auto& p=peers.back();p.id=id;
    if(!METHOD(net,get_peer_name) || net->get_peer_name(id,p.name,sizeof(p.name))!=SCO_OK) std::snprintf(p.name,sizeof(p.name),"peer %llu",static_cast<unsigned long long>(id));
    p.name[sizeof(p.name)-1]=0;return &p;
}
void IngestPose(uint64_t sender,const mpwire::Pose& next) {
    Peer* p=Add(sender);if(!p) return;
    const uint64_t arrival=Now();
    p->track.Add(next,arrival,next.senderMs);
}
void OnPose(uint64_t sender,const void* data,uint32_t len,void*) {
    mpwire::Pose next{};if(!mpwire::DecodePose(data,len,next)) return;
    if(next.version!=mpwire::kPoseVersion) { RejectContradictoryPose(sender);return; }
    IngestPose(sender,next);
}
void OnSpawn(uint64_t sender,const void* data,uint32_t len,void*) {
    mpwire::Spawn s{};if(!mpwire::DecodeSpawn(data,len,sender,s)) return;
    Peer* p=Add(sender);if(!p) return;
    char* dst=s.kind==mpwire::kAvatar?p->avatarClass:p->shipClass;
    Ghost& g=s.kind==mpwire::kAvatar?p->avatar:p->ship;
    if(std::strcmp(dst,s.cls)) Remove(g);
    std::memcpy(dst,s.cls,sizeof(s.cls));RetryCleanup();
}
void OnDespawn(uint64_t sender,const void* data,uint32_t len,void*) {
    uint8_t kind=0;if(!mpwire::DecodeDespawn(data,len,sender,kind)) return;
    Peer* p=Find(sender);if(!p) return;
    if(kind==mpwire::kAvatar) { Remove(p->avatar);p->avatarClass[0]=0; }
    else { Remove(p->ship);p->shipClass[0]=0; }
    RetryCleanup();
}
// multiplayer.chat. A line is read only from a player this module has verified: a member of the session now, whose player
// announcement of the current protocol generation was seen, who is not this player and not the relay server (peer 1 with the
// relay role). Add repeats the same tests; it gives the record that names the sender.
Peer* ChatSender(uint64_t id) {
    if(!id || !Active() || !METHOD(net,self_peer) || id==net->self_peer() || !HostCompatible() || ProtocolRole(id)!=mpprotocol::kPlayer) return nullptr;
    if(relay.seen && id==kHostPeer) return nullptr;
    std::vector<sco_net_peer> members;
    if(!Members(members) || std::none_of(members.begin(),members.end(),[id](const sco_net_peer& p){return p.peer_id==id;})) return nullptr;
    return Add(id);
}
// One reading window per verified sender, kept apart from the peer record (which a new announcement drops) so that cannot reset it.
mpwire::RateWindow<mpwire::kChatAcceptPerWindow>* ChatWindow(uint64_t id) {
    for(auto& s:chat.senders) if(s.id==id) return &s.window;
    if(chat.senders.size()>=kMaxMembers) return nullptr; // only members get one and it goes when they leave, so this is not reached
    chat.senders.emplace_back();chat.senders.back().id=id;return &chat.senders.back().window;
}
void ForgetChatSender(uint64_t id) {
    chat.senders.erase(std::remove_if(chat.senders.begin(),chat.senders.end(),[id](const ChatSenderWindow& s){return s.id==id;}),chat.senders.end());
}
void AddChatRow(const std::string& who,const char* text) {
    if(chat.rows.size()>=mpwire::kChatHistoryRows) chat.rows.erase(chat.rows.begin());
    chat.rows.push_back(who+": "+text);
}
// The label in front of a line another player said. A name another player chose may hold anything sco.net lets through, so it
// cannot be trusted to say who spoke: the label always ends in that player's peer id ("Alice #3"), which this player's own
// "You" lines never do, and a name that is not clean text or holds ':' (the separator, which could start a second speaker) is
// not used at all ("peer 3").
std::string ChatName(const Peer& p) {
    const std::string id=std::to_string(p.id);
    if(p.name[0] && !std::strchr(p.name,':') && mpwire::ChatTextOk(p.name,std::strlen(p.name))) return std::string(p.name)+" #"+id;
    return "peer "+id;
}
void OnChat(uint64_t sender,const void* data,uint32_t len,void*) {
    Peer* p=ChatSender(sender);
    mpwire::ChatLine line{};
    if(!p || !mpwire::DecodeChat(data,len,line)) { ++chat.notAccepted;return; }
    mpwire::RateWindow<mpwire::kChatAcceptPerWindow>* window=ChatWindow(sender);
    if(!window || !window->Take(Now())) { ++chat.overRate;return; }
    AddChatRow(ChatName(*p),line.text);
}
// Sending needs a session whose host has announced a compatible protocol (the same evidence a player needs to be seen).
bool ChatReady() { return ProtocolAvailable() && Active() && METHOD(net,self_peer) && net->self_peer() && HostCompatible(); }
void SetDraft(const char* text) {
    const size_t n=text?std::strlen(text):sizeof(chat.draft);
    if(n>=sizeof(chat.draft)) return; // longer than the widget service allows: keep the old draft
    std::memcpy(chat.draft,text,n);chat.draft[n]=0;
}
// The Send button. Nothing is shown, stored or logged unless sco.net took the message; a refusal says why and keeps the draft.
void SendChat() {
    if(!ChatReady()) { std::snprintf(reply,sizeof(reply),"Chat message not sent: no session with compatible players");return; }
    uint8_t message[mpwire::kMaxChat];
    const size_t size=mpwire::EncodeChat(chat.draft,std::strlen(chat.draft),message,sizeof(message));
    if(!size) { std::snprintf(reply,sizeof(reply),"Chat message not sent: write 1 to %u bytes of plain text, without control characters",static_cast<unsigned>(mpwire::kMaxChatText));return; }
    const uint64_t now=Now();
    if(!chat.sent.Room(now)) { std::snprintf(reply,sizeof(reply),"Chat message not sent: at most %u messages in %u seconds",mpwire::kChatSendsPerWindow,static_cast<unsigned>(mpwire::kChatWindowMs/1000));return; }
    const sco_result result=net->send_channel(self,mpwire::kChatChannel,message,static_cast<uint32_t>(size));
    if(result!=SCO_OK) { std::snprintf(reply,sizeof(reply),"Chat message not sent (%d)",static_cast<int>(result));return; }
    chat.sent.Add(now);
    AddChatRow("You",chat.draft);
    chat.draft[0]=0;
    std::snprintf(reply,sizeof(reply),"Chat message sent. It is not encrypted, and players on older plugin builds do not see it.");
}
void NoteBadRelay(const char* what) {
    // One line per session, then only the count (shown by multiplayer.status): a broken or foreign server cannot flood mod.log.
    if(++relay.bad==1) Log(SCO_LOG_WARN,"ignored a malformed %s message from the session host; check that host and players run the same plugin build",what);
}
// multiplayer.relay: the relay server says who is in the session. FROM_HOST, so sco.net delivers it only
// from the host; this checks again. It also tells this player's slot and whether nearby-only is on.
void OnRelay(uint64_t sender,const void* data,uint32_t len,void*) {
    if(sender!=kHostPeer || ProtocolRole(sender)!=mpprotocol::kRelay || !Active() || !METHOD(net,self_peer)) return;
    const uint64_t me=net->self_peer();
    if(!me || me==kHostPeer) return; // the host reads no roster from itself
    rw::RelayRoster roster;
    if(!rw::DecodeRelay(static_cast<const uint8_t*>(data),len,&roster)) { NoteBadRelay("multiplayer.relay");return; }
    const bool first=!relay.seen;
    int slot=-1;uint8_t generation=0;std::vector<uint64_t> ids;
    for(const auto& e:roster.players) { ids.push_back(e.peer);if(e.peer==me) { slot=e.slot;generation=e.generation; } }
    std::sort(ids.begin(),ids.end());
    const bool changed=first || ids!=relay.players || slot!=relay.slot || generation!=relay.generation || roster.nearbyOnly!=relay.nearby;
    relay.seen=true;relay.nearby=roster.nearbyOnly;relay.maxPlayers=roster.maxPlayers;
    relay.slot=slot;relay.generation=generation;relay.players=std::move(ids);
    if(first) {
        DropPeer(kHostPeer); // the join event may have made a record for the server before it said what it is
        Log(SCO_LOG_INFO,"relay server found: peer %llu hosts the session and relays players; it is not shown or counted as a player",Ull(kHostPeer));
    }
    if(changed) {
        if(slot<0) Log(SCO_LOG_WARN,"relay roster: %zu of %u transport members, this player is not listed yet",relay.players.size(),static_cast<unsigned>(relay.maxPlayers));
        else if(!relay.joinLogged) {
            relay.joinLogged=true;
            Log(SCO_LOG_INFO,"joined relay server as peer %llu in slot %d (generation %u): %zu of %u transport members, nearby-only %s",Ull(me),slot,static_cast<unsigned>(generation),relay.players.size(),static_cast<unsigned>(relay.maxPlayers),relay.nearby?"on":"off");
        } else Log(SCO_LOG_INFO,"relay roster: %zu of %u transport members, slot %d (generation %u), nearby-only %s",relay.players.size(),static_cast<unsigned>(relay.maxPlayers),slot,static_cast<unsigned>(generation),relay.nearby?"on":"off");
    }
    UpdateNearChannel();
}
// multiplayer.near.<slot>, FROM_HOST: a pose the server forwarded because its sender is near this player. The
// header names the sender and the generation of this slot; a message for an earlier occupant of the slot is
// dropped, and so is anything not the contract. The pose then goes the way a pose from multiplayer.pose goes.
void OnNear(uint64_t sender,const void* data,uint32_t len,void*) {
    if(sender!=kHostPeer || !relay.seen || relay.slot<0 || !relay.nearby || !nearChannel[0] || !Active()) return;
    rw::NearDown down;
    const rw::NearDownVerdict verdict=rw::DecodeNearDown(static_cast<const uint8_t*>(data),len,relay.generation,&down);
    if(verdict==rw::NearDownVerdict::OtherGeneration) return;
    mpwire::Pose next{};
    if(verdict!=rw::NearDownVerdict::Accepted || !mpwire::DecodePose(down.pose,down.poseLen,next)) { NoteBadRelay("multiplayer.near");return; }
    if(next.version!=mpwire::kPoseVersion) { RejectContradictoryPose(down.sender);return; }
    IngestPose(down.sender,next);
}
// The channel the host never sends on and a player never receives on needs a callback all the same.
void OnNeverReceived(uint64_t,const void*,uint32_t,void*) {}
// Where this player's pose goes: with nearby-only on, to the server on multiplayer.near_up (the server forwards it to
// the players near this one; never also on multiplayer.pose, which would hand it to everyone), else to the other players.
void SendPose(const mpwire::Pose& pose,const uint8_t* data,size_t len) {
    if(relay.seen && relay.nearby && relay.slot>=0 && nearChannel[0]) {
        const auto& top=pose.lv[pose.levels-1]; // the outermost zone is the area; the position is in that zone
        const std::vector<uint8_t> up=rw::EncodeNearUp(rw::Fnv1a64(top.name,std::strlen(top.name)),top.pos[0],top.pos[1],top.pos[2],data,len);
        if(!up.empty()) net->send_channel(self,channels[kNearUpChannel],up.data(),static_cast<uint32_t>(up.size()));
        return;
    }
    net->send_channel(self,channels[kPoseChannel],data,static_cast<uint32_t>(len));
}
void UpdateNearChannel() {
    char wanted[48]="";
    if(relay.seen && relay.nearby && relay.slot>=0) std::snprintf(wanted,sizeof(wanted),"%s%d",rw::kNearDownPrefix,relay.slot);
    if(!std::strcmp(wanted,nearChannel)) return;
    const bool was=nearChannel[0]!=0;
    SetNearChannel(wanted);
    if(nearChannel[0]) Log(SCO_LOG_INFO,"nearby-only: poses go to the relay server on multiplayer.near_up and come back on %s",nearChannel);
    else if(was) Log(SCO_LOG_INFO,"nearby-only is off: poses go to the other players on multiplayer.pose");
}
void SendSpawn(uint8_t kind,const char* cls) {
    if(!METHOD(net,send_channel) || !METHOD(net,self_peer)) return;
    mpwire::Spawn s{};s.kind=kind;s.sessionId=mpwire::SessionId(net->self_peer(),kind);std::snprintf(s.cls,sizeof(s.cls),"%s",cls);
    uint8_t buf[mpwire::kMaxSpawn];const size_t n=mpwire::EncodeSpawn(s,buf,sizeof(buf));
    if(n) net->send_channel(self,channels[kSpawnChannel],buf,static_cast<uint32_t>(n));
}
void SendDespawn(uint8_t kind) {
    if(!Active() || !METHOD(net,send_channel) || !METHOD(net,self_peer)) return;
    // Legacy v1 despawn encoder was inconsistent (16 written, 12 declared).
    // v1 reader expects 16; keep the header and declare the full wire extent.
    uint8_t buf[mpwire::kDespawn];const size_t n=mpwire::EncodeDespawn(kind,mpwire::SessionId(net->self_peer(),kind),buf,sizeof(buf));
    if(n) net->send_channel(self,channels[kDespawnChannel],buf,static_cast<uint32_t>(n));
}
bool Capture(mpwire::Pose& out) {
    if(!METHOD(spatial,player_pose) || !METHOD(poses,zone_record) || !METHOD(poses,pose_to_zone)) return false;
    double pos[3],rot[4];uint64_t zone=0;
    if(!spatial->player_pose(pos,rot,&zone) || !zone || !mpwire::PoseOk(pos,rot)) return false;
    std::array<sc_game_zone,16> chain{};unsigned count=0;uint64_t id=zone;
    while(id) {
        if(count==chain.size()) return false;
        for(unsigned i=0;i<count;++i) if(chain[i].zone_id==id) return false;
        auto& z=chain[count];z.size=sizeof(z);
        if(!poses->zone_record(id,&z) || !z.epoch || !z.root_id || z.zone_id!=id || (z.flags&SC_GAME_ZONE_NAME_TRUNCATED) || !std::memchr(z.name,0,sizeof(z.name))) return false;
        ++count;id=z.parent_id;
    }
    sc_game_zone final{};final.size=sizeof(final);
    if(!poses->zone_record(zone,&final)) return false;
    for(unsigned i=0;i<count;++i) if(chain[i].epoch!=final.epoch) return false;
    out=mpwire::Pose();out.seq=++sequence;
    const unsigned levels=std::min(count,static_cast<unsigned>(mpwire::kMaxLevels));
    // Preserve closest frames and the complete root when the ancestry is longer.
    for(unsigned i=0;i<levels;++i) {
        const unsigned index=(count>levels && i==levels-1)?count-1:i;const auto& z=chain[index];
        auto& l=out.lv[i];const size_t n=std::strlen(z.name);
        if(!mpwire::ZoneNameOk(z.name,n)) return false;
        std::memcpy(l.name,z.name,n+1);
        if(!poses->pose_to_zone(zone,z.zone_id,pos,rot,l.pos,l.rot) || !mpwire::PoseOk(l.pos,l.rot)) return false;
    }
    out.levels=static_cast<uint8_t>(levels);
    shipId=0;
    if(cfg.showShips && METHOD(vehicles,player_ship) && vehicles->player_ship(&shipId)==SCO_OK && shipId && METHOD(entities,get_transform)) {
        double sp[3],sq[4];uint64_t sz=0;
        if(entities->get_transform(shipId,sp,sq,&sz)==SCO_OK && sz && mpwire::PoseOk(sp,sq)) {
            // Ship section starts at 1, outside the player's own innermost frame.
            if(levels>1) {
                out.ship=true;out.shipFirst=1;
                for(unsigned i=1;i<levels;++i) {
                    const unsigned index=(count>levels && i==levels-1)?count-1:i;
                    if(!poses->pose_to_zone(sz,chain[index].zone_id,sp,sq,out.shipPos[i],out.shipRot[i]) || !mpwire::PoseOk(out.shipPos[i],out.shipRot[i])) { out.ship=false;break; }
                }
            }
        }
    }
    return true;
}
void Announce(bool force) {
    if(force || !announced) { SendSpawn(mpwire::kAvatar,cfg.avatar);announced=true; }
    char cls[64]="";uint32_t size=sizeof(cls);
    if(cfg.showShips && shipId && METHOD(entities,class_of) && entities->class_of(shipId,cls,&size)==SCO_OK && std::memchr(cls,0,sizeof(cls)) && mpwire::ClassOk(cls,std::strlen(cls))) {
        if(force || std::strcmp(announcedShip,cls)) { SendSpawn(mpwire::kShip,cls);std::memcpy(announcedShip,cls,sizeof(cls)); }
    } else if(announcedShip[0]) { SendDespawn(mpwire::kShip);announcedShip[0]=0; }
}
void OnProtocol(uint64_t sender,const void* data,uint32_t len,void*) {
    if(!Active() || !METHOD(net,self_peer) || !sender || sender==net->self_peer()) return;
    std::vector<sco_net_peer> members;if(!Members(members)) return;
    if(std::none_of(members.begin(),members.end(),[sender](const sco_net_peer& p){return p.peer_id==sender;})) return;
    if(std::find(protocolContradictions.begin(),protocolContradictions.end(),sender)!=protocolContradictions.end()) return;
    uint8_t role=0;
    if(!mpprotocol::Decode(data,len,&role) || (role==mpprotocol::kRelay && sender!=kHostPeer)) {
        if(ProtocolRole(sender)) { ForgetProtocol(sender);DropPeer(sender);if(sender==kHostPeer) ClearAdmission(); }
        if(!protocolWarned) { Log(SCO_LOG_WARN,"peer %llu has an incompatible or malformed multiplayer protocol announcement; synchronize upgrades",Ull(sender));protocolWarned=true; }return;
    }
    if(ProtocolRole(sender)==role) return;
    if(sender==kHostPeer && ProtocolRole(sender) && ProtocolRole(sender)!=role) ClearAdmission();
    ForgetProtocol(sender);DropPeer(sender);
    protocolPeers.push_back({sender,role});
    if(role==mpprotocol::kPlayer) Add(sender);
    if(sender==kHostPeer) for(const auto& p:protocolPeers) if(p.role==mpprotocol::kPlayer) Add(p.id);
    Announce(true);
}
void OnPeer(const char*,const void* data,void*) {
    if(!data) return;
    const auto* e=static_cast<const sco_net_peer_event*>(data);if(e->size<sizeof(*e)) return;
    // Membership alone is not protocol admission. Forget stale evidence on leave or rejoin.
    if(e->what==SCO_NET_PEER_JOINED) { protocolContradictions.erase(std::remove(protocolContradictions.begin(),protocolContradictions.end(),e->peer.peer_id),protocolContradictions.end());ForgetProtocol(e->peer.peer_id);DropPeer(e->peer.peer_id);if(Active()) { SendProtocol();Announce(true); } }
    if(e->what==SCO_NET_PEER_LEFT) { protocolContradictions.erase(std::remove(protocolContradictions.begin(),protocolContradictions.end(),e->peer.peer_id),protocolContradictions.end());ForgetProtocol(e->peer.peer_id);DropPeer(e->peer.peer_id);ForgetChatSender(e->peer.peer_id);if(e->peer.peer_id==kHostPeer) Clear(); }
}
void OnState(const char*,const void* data,void*) {
    if(!data) return;
    const auto* e=static_cast<const sco_net_state_event*>(data);if(e->size<sizeof(*e)) return;
    if(!e->active) { Clear();std::memcpy(reason,e->reason,sizeof(reason));reason[sizeof(reason)-1]=0; }
    else { reason[0]=0;announced=false;lastSend=0;ResetRelay();chat=ChatState(); }
}
bool Resolve(const Peer& p,bool ship,uint64_t now,uint64_t& zone,double pos[3],double rot[4]) {
    const mpwire::Pose* latest=p.track.Newest();
    if(!latest || !METHOD(poses,find_zone) || !METHOD(poses,zone_record)) return false;
    const unsigned first=ship?latest->shipFirst:0;
    if(ship && !latest->ship) return false;
    for(unsigned i=first;i<latest->levels;++i) {
        sc_game_zone z{};z.size=sizeof(z);
        if(!poses->find_zone(latest->lv[i].name,&z) || !z.zone_id || !z.epoch || !z.root_id || (z.flags&SC_GAME_ZONE_NAME_TRUNCATED)) continue;
        sc_game_zone current{};current.size=sizeof(current);
        if(!poses->zone_record(z.zone_id,&current) || current.epoch!=z.epoch || std::strcmp(current.name,latest->lv[i].name)) return false;
        // Where the ghost is now: between the buffered samples, kept moving for a short while when the newest is late,
        // and at the newest sample at once after a silence, a teleport or a change of frames (motion.h).
        if(!p.track.Sample(now,ship,i,pos,rot)) return false;
        zone=z.zone_id;
        return mpwire::PoseOk(pos,rot);
    }
    return false;
}
void UpdateGhost(Peer& p,bool ship,uint64_t now) {
    Ghost& g=ship?p.ship:p.avatar;const char* cls=ship?p.shipClass:p.avatarClass;
    // Remote avatar class is informational: instantiate only the locally chosen
    // avatar, never a peer-chosen actor archetype. Ship classes remain provider-validated.
    if(!ship && cls[0]) cls=cfg.avatar;
    if(!cls[0] || (ship && !cfg.showShips) || !p.track.Valid() || now-p.track.LastArrivalMs()>10000) { Remove(g);return; }
    uint64_t zone=0;double pos[3],rot[4];
    if(!Resolve(p,ship,now,zone,pos,rot) || !METHOD(spatial,player_pose) || !METHOD(poses,pose_to_zone)) { Remove(g);return; }
    double local[3],q[4],here[3],r[4];uint64_t playerZone=0;
    if(!spatial->player_pose(local,q,&playerZone) || !playerZone || !poses->pose_to_zone(playerZone,zone,local,q,here,r)) { Remove(g);return; }
    double distance=0;for(unsigned i=0;i<3;++i) distance+=(here[i]-pos[i])*(here[i]-pos[i]);
    if(!std::isfinite(distance) || distance>1e8) { Remove(g);return; }
    if(g.id && std::strcmp(g.cls,cls)) Remove(g);
    if(!g.id) {
        // Failed cleanup owns the budget until successfully initiated/provider unload.
        if(peers.size()*2+cleanup.size()>32 || (g.lastAttempt && now-g.lastAttempt<30000)) return;
        g.lastAttempt=now;uint64_t id=0;sco_result result=SCO_UNAVAILABLE;
        if(ship && METHOD(entities,spawn) && METHOD(entities,despawn)) result=entities->spawn(self,cls,zone,pos,rot,&id);
        if(!ship && METHOD(actors,spawn_npc) && METHOD(actors,despawn)) result=actors->spawn_npc(self,cls,zone,pos,&id);
        if(result!=SCO_OK || !id) return;
        g.id=id;g.actor=!ship;g.since=now;std::snprintf(g.cls,sizeof(g.cls),"%s",cls);
    }
    if(!METHOD(entities,alive) || !entities->alive(g.id)) {
        if(now-g.since>60000) Remove(g); // retain ownership removal; never recycle solely on alive=0
        return;
    }
    if(METHOD(entities,set_transform)) entities->set_transform(self,g.id,zone,pos,rot);
}
// What the status line and the panel add when the session host is a relay server.
void RelayNote(char* out,size_t cap) {
    if(!relay.seen) { out[0]=0;return; }
    char seat[24];
    if(relay.slot<0) std::snprintf(seat,sizeof(seat),"not listed");else std::snprintf(seat,sizeof(seat),"slot %d",relay.slot);
    std::snprintf(out,cap,"; via relay server (not a player), %s, nearby-only %s%s",seat,relay.nearby?"on":"off",relay.bad?", malformed relay messages ignored":"");
}
sco_result Status(const sco_arg*,uint32_t nargs,void*,char* out,uint32_t cap) {
    if(nargs) return SCO_BAD_ARG;
    const auto state=State();
    const bool consistent=MembershipConsistent();char note[128]="";if(consistent) RelayNote(note,sizeof(note));
    const size_t unverified=UnverifiedCount();
    char chatNote[96]="";
    if(chat.overRate || chat.notAccepted) std::snprintf(chatNote,sizeof(chatNote),"; chat: %u dropped over the rate limit, %u not accepted",chat.overRate,chat.notAccepted);
    std::snprintf(out,cap,"Multiplayer: G13 %s; protocol %s; transport state %u; mode: %s; %zu other players%s; %zu unverified or incompatible peers%s; %zu cleanup pending; Host/Join UI %s; G2 goto unavailable%s%s%s",METHOD(broker,request_host)?"available":"unavailable (admission/provider)",ProtocolAvailable()?"available":"unavailable (network service)",state.state,SessionMode(state),consistent?peers.size():0,note,unverified,ProtocolWait(),cleanup.size(),panelLive?"available":"unavailable",chatNote,reason[0]?"; ":"",reason);return SCO_OK;
}
sco_result Leave(const sco_arg*,uint32_t nargs,void*,char* out,uint32_t cap) {
    if(nargs) return SCO_BAD_ARG;
    if(Active()) { SendDespawn(mpwire::kAvatar);SendDespawn(mpwire::kShip); }
    const sco_result result=METHOD(broker,leave)?broker->leave(self):SCO_UNAVAILABLE;
    Clear();Wipe(cfg.pass,sizeof(cfg.pass));std::snprintf(out,cap,"%s",result==SCO_OK?"Left private session; ghost cleanup initiated":"Leave unavailable: G13 absent/admission refused or another owner");UpdatePanel();return result;
}
sco_result GoTo(const sco_arg* args,uint32_t nargs,void*,char* out,uint32_t cap) {
    if(nargs!=1 || !args || args[0].type!=SCO_ARG_STRING || !args[0].v.s) return SCO_BAD_ARG;
    std::snprintf(out,cap,"Player relocation unavailable: G2 mutation is not approved; no private fallback");return SCO_UNAVAILABLE;
}
void Control(bool host) {
    Services();
    if(!ProtocolAvailable()) {
        if(!cfg.remember) Wipe(cfg.pass,sizeof(cfg.pass));
        std::snprintf(reply,sizeof(reply),"Host/Join unavailable: multiplayer protocol network service unavailable");
        Log(SCO_LOG_WARN,"host/join refused: multiplayer protocol network service unavailable");return;
    }
    if(!cfg.name[0] || std::strlen(cfg.pass)<SC_PRIVATE_SESSION_MIN_PASSPHRASE || cfg.hostPort<1024 || cfg.hostPort>65535 || cfg.joinPort<1024 || cfg.joinPort>65535) {
        std::snprintf(reply,sizeof(reply),"Pick a name, a 12-256 byte passphrase and ports 1024-65535");return;
    }
    sco_result result=SCO_UNAVAILABLE;
    if(host && METHOD(broker,request_host)) {
        sc_private_session_host_request_v1 r{};r.size=sizeof(r);r.consent=1;r.port=static_cast<uint16_t>(cfg.hostPort);r.max_peers=8;
        r.player_name=cfg.name;r.player_name_len=static_cast<uint32_t>(std::strlen(cfg.name));r.passphrase=cfg.pass;r.passphrase_len=static_cast<uint32_t>(std::strlen(cfg.pass));
        result=broker->request_host(self,&r);
    } else if(!host && METHOD(broker,request_join)) {
        sc_private_session_join_request_v1 r{};r.size=sizeof(r);r.consent=1;r.port=static_cast<uint16_t>(cfg.joinPort);r.address=cfg.address;r.address_len=static_cast<uint32_t>(std::strlen(cfg.address));
        r.player_name=cfg.name;r.player_name_len=static_cast<uint32_t>(std::strlen(cfg.name));r.passphrase=cfg.pass;r.passphrase_len=static_cast<uint32_t>(std::strlen(cfg.pass));
        result=broker->request_join(self,&r);
    }
    const sco_result saved=SaveSettings();
    // Do not retain an unremembered credential in plugin/UI state after request.
    if(!cfg.remember) Wipe(cfg.pass,sizeof(cfg.pass));
    std::snprintf(reply,sizeof(reply),"%s request %s (%d). Acceptance is not authenticated connection.%s",host?"Host":"Join",result==SCO_OK?"accepted":"refused",static_cast<int>(result),saved==SCO_OK?"":" Settings not saved.");
    // Only the outcome: no address, name or passphrase. The host broker audits its own lifecycle.
    Log(result==SCO_OK?SCO_LOG_INFO:SCO_LOG_WARN,"%s request %s (%d)",host?"host":"join",result==SCO_OK?"accepted: waiting for the session":"refused",static_cast<int>(result));
}
void PanelEvent(const char* id,const sco_widget_event* e,void*) {
    if(!panelLive || !id || std::strcmp(id,panelId) || !e || e->size<sizeof(*e)) return;
    bool save=false,refreshPanel=true;
    if(e->kind==SCO_WIDGET_BUTTON) {
        if(e->node_id==1) Control(true);else if(e->node_id==2) Control(false);
        else if(e->node_id==3) Leave(nullptr,0,nullptr,reply,sizeof(reply));
        else if(e->node_id==13) GoTo(nullptr,0,nullptr,reply,sizeof(reply));
        else if(e->node_id==19) std::snprintf(reply,sizeof(reply),"%s",SaveProfile()==SCO_OK?"Server profile saved":"Server profile not saved (empty address, storage unavailable or full)");
        else if(e->node_id==22) SendChat();
    } else if(e->kind==SCO_WIDGET_COMBO && e->node_id==18) {
        SelectProfile(e->index);
    } else if(e->kind==SCO_WIDGET_TEXT_INPUT && e->text) {
        if(e->node_id==4) save=Text(cfg.name,e->text);
        else if(e->node_id==5) save=Text(cfg.address,e->text);
        else if(e->node_id==6) Text(cfg.pass,e->text,255); // host widgets limit
        else if(e->node_id==9 && mpwire::ClassOk(e->text,std::strlen(e->text))) { save=Text(cfg.avatar,e->text);announced=false; }
        else if(e->node_id==21) { SetDraft(e->text);refreshPanel=false; } // preserve later events queued from this panel generation
    } else if(e->kind==SCO_WIDGET_SLIDER && std::isfinite(e->value) && e->value>=1024 && e->value<=65535 && e->value==std::floor(e->value)) {
        if(e->node_id==7) { cfg.hostPort=static_cast<int>(e->value);save=true; }
        else if(e->node_id==8) { cfg.joinPort=static_cast<int>(e->value);save=true; }
    } else if(e->kind==SCO_WIDGET_TOGGLE && (e->value==0 || e->value==1)) {
        if(e->node_id==10) { cfg.showShips=e->value!=0;save=true; }
        else if(e->node_id==11) { cfg.remember=e->value!=0;save=true; }
    }
    if(save && SaveSettings()!=SCO_OK) std::snprintf(reply,sizeof(reply),"Settings not saved: sco.storage unavailable/refused; session-only values retained");
    if(refreshPanel) UpdatePanel();
}
constexpr unsigned kPanelNodes=23; // 1-19: the session controls as before; 20-23: the chat history, the message box, Send and the notice
void UpdatePanel() {
    if(!METHOD(widgets,set_panel) || !METHOD(widgets,remove_panel)) { panelLive=false;return; }
    const auto state=State();const bool consistent=MembershipConsistent();char note[128]="";if(consistent) RelayNote(note,sizeof(note));
    char status[256];std::snprintf(status,sizeof(status),"G13 %s; protocol %s; transport state %u; mode: %s; %zu peers%s, %zu unverified or incompatible%s, %zu cleanup pending",METHOD(broker,request_host)?"available":"unavailable",ProtocolAvailable()?"available":"unavailable (network service)",state.state,SessionMode(state),consistent?peers.size():0,note,UnverifiedCount(),ProtocolWait(),cleanup.size());
    std::vector<std::string> peerText;std::vector<const char*> rows;
    for(const auto& p:peers) { if(!consistent) break;char s[200];std::snprintf(s,sizeof(s),"%s: %s; avatar %s, ship %s",p.name,p.track.Valid()?"pose observed (v2 motion)":"waiting for pose",p.avatar.id?"owned ghost":"not shown",p.ship.id?"owned ghost":"not shown");peerText.emplace_back(s); }
    for(const auto& s:peerText) rows.push_back(s.c_str());
    sco_widget_node nodes[kPanelNodes]{};
    for(unsigned i=0;i<kPanelNodes;++i) { nodes[i].size=sizeof(nodes[i]);nodes[i].id=i+1;nodes[i].kind=SCO_WIDGET_LABEL; }
    for(unsigned i=0;i<3;++i) nodes[i].kind=SCO_WIDGET_BUTTON;
    nodes[0].label="Host private session";nodes[1].label="Join private session";nodes[2].label="Leave";
    for(unsigned i:{3u,4u,5u,8u}) nodes[i].kind=SCO_WIDGET_TEXT_INPUT;
    nodes[3].label="Player name";nodes[3].text=cfg.name;nodes[4].label="Host numeric IPv4 address";nodes[4].text=cfg.address;
    nodes[5].label="Passphrase (12+ bytes)";nodes[5].text=std::strlen(cfg.pass)<=SCO_WIDGETS_MAX_TEXT?cfg.pass:"";nodes[5].flags=SCO_WIDGET_PASSWORD;
    for(unsigned i:{6u,7u}) { nodes[i].kind=SCO_WIDGET_SLIDER;nodes[i].minimum=1024;nodes[i].maximum=65535; }
    nodes[6].label="Host port";nodes[6].value=cfg.hostPort;nodes[7].label="Join port";nodes[7].value=cfg.joinPort;
    nodes[8].label="Local avatar archetype (npcs.txt class)";nodes[8].text=cfg.avatar;
    nodes[9].kind=SCO_WIDGET_TOGGLE;nodes[9].label="Show ships";nodes[9].value=cfg.showShips?1:0;
    nodes[10].kind=SCO_WIDGET_TOGGLE;nodes[10].label="Remember passphrase in plaintext local DB (unsafe)";nodes[10].value=cfg.remember?1:0;
    nodes[11].label=status;
    nodes[12].kind=SCO_WIDGET_BUTTON;nodes[12].label="Go to player (G2 unavailable)";nodes[12].flags=SCO_WIDGET_DISABLED;
    nodes[13].label=reply;nodes[14].label="No socket at load. Default loopback; host must explicitly configure private LAN bind + exact peers. Offline/EAC admission required.";
    nodes[15].kind=SCO_WIDGET_LIST;nodes[15].label="Other players";nodes[15].items=rows.empty()?nullptr:rows.data();nodes[15].item_count=static_cast<uint32_t>(rows.size());nodes[15].index=-1;
    nodes[16].label="Visual co-presence only: no shared physics, damage, missions or inventory. No auto-retry or internet allowlist.";
    std::array<std::string,kProfiles> profileLabels;std::array<const char*,kProfiles> profileItems{};
    for(unsigned i=0;i<kProfiles;++i) { char text[48];std::snprintf(text,sizeof(text),"%s%s%d",profiles[i].address[0]?profiles[i].address:"Empty slot ",profiles[i].address[0]?":":"",profiles[i].address[0]?profiles[i].port:static_cast<int>(i+1));profileLabels[i]=text;profileItems[i]=profileLabels[i].c_str(); }
    nodes[17].kind=SCO_WIDGET_COMBO;nodes[17].label="Saved servers";nodes[17].items=profileItems.data();nodes[17].item_count=kProfiles;nodes[17].index=selectedProfile;
    nodes[18].kind=SCO_WIDGET_BUTTON;nodes[18].label="Save server profile";
    // Chat: the history (the rows live in chat.rows until the call below returns), the message box, Send, and what chat is not.
    std::vector<const char*> chatRows;
    for(const auto& line:chat.rows) chatRows.push_back(line.c_str());
    nodes[19].kind=SCO_WIDGET_LIST;nodes[19].label="Chat";nodes[19].items=chatRows.empty()?nullptr:chatRows.data();nodes[19].item_count=static_cast<uint32_t>(chatRows.size());nodes[19].index=-1;
    nodes[20].kind=SCO_WIDGET_TEXT_INPUT;nodes[20].label="Chat message (1-160 bytes, plain text)";nodes[20].text=chat.draft;
    nodes[21].kind=SCO_WIDGET_BUTTON;nodes[21].label="Send chat message";nodes[21].flags=ChatReady()?0:SCO_WIDGET_DISABLED;
    nodes[22].label="Chat is plain text and not encrypted: use a VPN. Players on older plugin builds do not see it.";
    sco_widget_node_1_2 tagged[kPanelNodes]{};
    const char* tags[]={"host","join","leave","name","address","passphrase","host-port","join-port","avatar","ships","remember","status","goto","reply","notice","players","scope","profiles","save-profile","chat","chat-message","chat-send","chat-notice"};
    static_assert(sizeof(tags)/sizeof(tags[0])==kPanelNodes,"one tag per panel node");
    for(unsigned i=0;i<kPanelNodes;++i) { tagged[i].base.base=nodes[i];tagged[i].base.base.size=sizeof(tagged[i]);tagged[i].tag=tags[i]; }
    panelLive=widgets->set_panel(self,panelId,"Multiplayer",&tagged[0].base.base,kPanelNodes,sizeof(tagged[0]),PanelEvent,nullptr)==SCO_OK;
}
// Drop all previously trusted state when membership cannot be established, but retain the
// wait timer so the panel eventually reports a timeout even while the list stays unusable.
void ClearUnverified() {
    const uint64_t since=protocolWaitSince?protocolWaitSince:Now();
    ClearAdmission();protocolWaitSince=since;
}
void Tick(const char*,const void*,void*) {
    Services();RetryCleanup();
    const bool active=Active() && State().state!=SC_PRIVATE_SESSION_IDLE;
    if(!active) { if(wasActive || !peers.empty()) Clear();wasActive=false;UpdatePanel();return; }
    wasActive=true;
    if(!ProtocolAvailable()) { ClearUnverified();UpdatePanel();return; }
    std::vector<sco_net_peer> members;
    if(!METHOD(net,self_peer) || !Members(members) || std::none_of(members.begin(),members.end(),[](const sco_net_peer& p){return p.peer_id==net->self_peer();})) {
        ClearUnverified();UpdatePanel();return;
    }
    if(net->self_peer()!=kHostPeer && std::none_of(members.begin(),members.end(),[](const sco_net_peer& p){return p.peer_id==kHostPeer;})) ClearUnverified();
    for(size_t i=0;i<protocolPeers.size();) {
        const uint64_t id=protocolPeers[i].id;
        if(std::none_of(members.begin(),members.end(),[id](const sco_net_peer& p){return p.peer_id==id;})) { DropPeer(id);ForgetProtocol(id);ForgetChatSender(id); }
        else ++i;
    }
    const uint64_t now=Now();
    if(UnverifiedCount()) { if(!protocolWaitSince) protocolWaitSince=now; } else protocolWaitSince=0;
    if(!lastProtocol || now-lastProtocol>=3000) { SendProtocol();Announce(true);lastProtocol=now; }
    if(!lastSend || now-lastSend>=100) {
        lastSend=now;mpwire::Pose pose{};
        if(Capture(pose) && METHOD(net,send_channel)) {
            pose.senderMs=Now();
            if(havePreviousSent && pose.senderMs>previousSent.senderMs && pose.senderMs-previousSent.senderMs<=mpmotion::kStaleMs && mpmotion::SameFrame(previousSent,pose)) {
                const double dt=(pose.senderMs-previousSent.senderMs)/1000.0;
                double speed2=0, v[3];
                for(unsigned i=0;i<3;++i) { v[i]=(pose.lv[0].pos[i]-previousSent.lv[0].pos[i])/dt;speed2+=v[i]*v[i]; }
                if(speed2<=mpmotion::kTeleportSpeed*mpmotion::kTeleportSpeed)
                    for(unsigned i=0;i<3;++i) pose.velocity[i]=static_cast<float>(v[i]);
            }
            uint8_t data[mpwire::kMaxPose];size_t len=mpwire::EncodePose(pose,data,sizeof(data));
            if(len) { SendPose(pose,data,len);previousSent=pose;havePreviousSent=true; }
        }
        Announce(false);
    }
    for(auto& p:peers) { UpdateGhost(p,true,now);UpdateGhost(p,false,now); }
    RetryCleanup();UpdatePanel();
}
void Unload() {
    if(!api) return;
    if(METHOD(broker,leave)) broker->leave(self);
    Clear();RetryCleanup(); // any refusal stays provider-owned through ordinary owner release
    if(METHOD(widgets,remove_panel)) widgets->remove_panel(self,panelId);
    if(METHOD(net,unregister_channel)) for(unsigned i=0;i<registeredChannels;++i) net->unregister_channel(self,channels[i]);
    if(METHOD(api,unsubscribe)) { api->unsubscribe(self,"tick",Tick);api->unsubscribe(self,SCO_NET_EVENT_STATE,OnState);api->unsubscribe(self,SCO_NET_EVENT_PEER,OnPeer); }
    Wipe(cfg.pass,sizeof(cfg.pass));cleanup.clear();peers.clear();panelLive=wasActive=false;registeredChannels=0;
    api=nullptr;self=nullptr;net=nullptr;broker=nullptr;store=nullptr;widgets=nullptr;spatial=nullptr;poses=nullptr;actors=nullptr;entities=nullptr;vehicles=nullptr;
}
}
extern "C" {
SCO_EXPORT const sco_plugin_info* sco_plugin_query() { return &info; }
SCO_EXPORT sco_result sco_plugin_load(const sco_api* a,sco_plugin* s) {
    if(!a || !s || a->major!=SCO_API_MAJOR || !METHOD(a,register_command) || !METHOD(a,subscribe) || !METHOD(a,unsubscribe)) return SCO_BAD_ARG;
    api=a;self=s;Services();net=Query<sco_net_v1>(SCO_NET_NAME,SCO_NET_VERSION_1_0);
    store=Query<sco_storage_v1>(SCO_STORAGE_NAME,SCO_STORAGE_VERSION_1_1);widgets=Query<sco_widgets_v1>(SCO_WIDGETS_NAME,SCO_WIDGETS_VERSION_1_2);LoadSettings();LoadProfiles();
    const char* names[]={"multiplayer.status","multiplayer.leave","multiplayer.goto"};const sco_command_fn callbacks[]={Status,Leave,GoTo};
    sco_arg_def who{};who.name="player";who.type=SCO_ARG_STRING;
    sco_result r=SCO_OK;
    for(unsigned i=0;i<3 && r==SCO_OK;++i) { sco_command c{};c.size=sizeof(c);c.name=names[i];c.title=names[i];c.fn=callbacks[i];if(i==2) { c.args=&who;c.nargs=1;c.arg_def_size=sizeof(who); }r=api->register_command(self,&c); }
    if(r==SCO_OK && METHOD(net,register_channel) && METHOD(net,unregister_channel)) {
        // Pose is unreliable and relayed between players; spawn and despawn are reliable. multiplayer.relay is
        // reliable and FROM_HOST: only a session host that speaks the relay contract (sco-relay) ever sends it.
        // multiplayer.near_up is unreliable and TO_HOST (the server reads it, it is not relayed); with nearby-only on, a
        // player's own multiplayer.near.<slot> is registered later, when the roster has told its slot.
        // multiplayer.chat is reliable and goes to every other player, relayed by whichever peer hosts the session (nearby-only
        // does not touch it). Players on a build without it never register the channel and so receive nothing.
        const uint32_t flags[kChannelCount]={0,SCO_NET_RELIABLE,SCO_NET_RELIABLE,SCO_NET_RELIABLE|SCO_NET_FROM_HOST,SCO_NET_TO_HOST,SCO_NET_RELIABLE,SCO_NET_RELIABLE};
        const uint32_t sizes[kChannelCount]={mpwire::kMaxPose,mpwire::kMaxSpawn,mpwire::kDespawn,static_cast<uint32_t>(rw::kRelayMaxBytes),static_cast<uint32_t>(rw::kNearUpMaxBytes),static_cast<uint32_t>(mpprotocol::kBytes),static_cast<uint32_t>(mpwire::kMaxChat)};
        const sco_net_on_message callbacksNet[kChannelCount]={OnPose,OnSpawn,OnDespawn,OnRelay,OnNeverReceived,OnProtocol,OnChat};
        for(unsigned i=0;i<kChannelCount && r==SCO_OK;++i) { r=net->register_channel(self,channels[i],flags[i],sizes[i],callbacksNet[i],nullptr);if(r==SCO_OK) ++registeredChannels; }
    }
    if(r==SCO_OK) r=api->subscribe(self,"tick",Tick,nullptr);
    if(r==SCO_OK) r=api->subscribe(self,SCO_NET_EVENT_STATE,OnState,nullptr);
    if(r==SCO_OK) r=api->subscribe(self,SCO_NET_EVENT_PEER,OnPeer,nullptr);
    if(r!=SCO_OK) { Unload();return r; }
    UpdatePanel();return SCO_OK;
}
SCO_EXPORT void sco_plugin_unload() { Unload(); }
}
