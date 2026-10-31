// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <limits>
#include <string>

namespace authority {
struct Result { const char* status; uint64_t id; uint64_t entity; int64_t value; uint64_t revision; };
struct Receipt { uint64_t id = 0, entity = 0; int64_t delta = 0; Result result{"",0,0,0,0}; };
struct Peer { uint64_t id = 0, entity = 0, high = 0; bool active = false; std::array<Receipt,8> receipts{}; };
class Session {
 public:
 explicit Session(std::string epoch, uint64_t initial_revision = 0) : epoch_(std::move(epoch)), revision_(initial_revision) {}
 uint64_t join(uint64_t peer) {
  if (!peer || find(peer)) return 0;
  for (auto& p : peers_) if (!p.id) { p.id=peer; p.entity=next_entity_++; p.active=true; return p.entity; }
  return 0;
 }
 void leave(uint64_t peer) { if (auto* p=find(peer)) p->active=false; }
 Result command(uint64_t peer, const std::string& epoch, uint64_t id, uint64_t entity, int64_t delta) {
  auto* p=find(peer);
  auto reply=[&](const char* s) { return Result{s,id,entity,p ? p->value : 0,revision_}; };
  if(epoch != epoch_) return reply("Stale");
  if(!p || !p->active) return reply("Disconnected");
  if(!id) return reply("Invalid");
  if(id<=p->high) {
   const auto& r=p->receipts[(id-1)%8];
   if(r.id!=id) return reply("Old");
   if(r.entity!=entity || r.delta!=delta) return reply("Conflict");
   return r.result;
  }
  if(p->high==std::numeric_limits<uint64_t>::max() || id!=p->high+1) return reply("Gap");
  const char* status="Applied";
  if(entity!=p->entity) status="Forbidden";
  else if(delta < -100 || delta > 100 || p->value+delta < -1000000 || p->value+delta > 1000000) status="Invalid";
  else if(revision_==std::numeric_limits<uint64_t>::max()) status="Exhausted";
  else { p->value+=delta; ++revision_; }
  p->high=id;
  Result result=reply(status);
  p->receipts[(id-1)%8]=Receipt{id,entity,delta,result};
  return result;
 }
 Result snapshot(uint64_t peer) { auto* p=find(peer); return {p&&p->active?"Ok":"Disconnected",0,p?p->entity:0,p?p->value:0,revision_}; }
 const std::string& epoch() const { return epoch_; }
 private:
 struct Slot : Peer { int64_t value=0; };
 Slot* find(uint64_t id) { for(auto& p:peers_) if(p.id==id && id) return &p; return nullptr; }
 std::string epoch_; std::array<Slot,16> peers_{}; uint64_t next_entity_=1, revision_=0;
};
}
