// SPDX-License-Identifier: GPL-3.0-only
#include "session.h"
#include <cstdlib>
#define CHECK(x) do { if (!(x)) std::abort(); } while (false)
int main() {
 authority::Session full("epoch",std::numeric_limits<uint64_t>::max()); auto f=full.join(2);
 CHECK(std::string(full.command(2,"epoch",1,f,1).status)=="Exhausted");
 CHECK(full.snapshot(2).value==0);
 authority::Session s("epoch"); auto e=s.join(2); CHECK(e && s.join(3)!=e);
 auto r=s.command(2,"epoch",1,e,4); CHECK(std::string(r.status)=="Applied" && r.value==4);
 CHECK(s.command(2,"epoch",1,e,4).revision==r.revision);
 CHECK(std::string(s.command(2,"epoch",1,e,5).status)=="Conflict");
 CHECK(std::string(s.command(2,"old",2,e,5).status)=="Stale");
 CHECK(std::string(s.command(2,"epoch",3,e,5).status)=="Gap");
 CHECK(std::string(s.command(2,"epoch",2,e+1,5).status)=="Forbidden");
 for(uint64_t i=3;i<=10;++i) CHECK(std::string(s.command(2,"epoch",i,e,1).status)=="Applied");
 CHECK(std::string(s.command(2,"epoch",1,e,4).status)=="Old");
 s.leave(2); CHECK(std::string(s.command(2,"epoch",11,e,1).status)=="Disconnected");
 CHECK(!s.join(2)); for(uint64_t i=4;i<=17;++i) CHECK(s.join(i)); CHECK(!s.join(18));

 // Drop the application result to model an applied command whose receipt was
 // not observed by its caller. The authority-side snapshot is not a client
 // outcome or a resume token.
 authority::Session lost("lost-receipt-epoch");
 const uint64_t old_peer=2; const auto old_entity=lost.join(old_peer);
 CHECK(old_entity);
 (void)lost.command(old_peer,"lost-receipt-epoch",1,old_entity,7);
 const auto before_leave=lost.snapshot(old_peer);
 CHECK(std::string(before_leave.status)=="Ok" && before_leave.value==7 && before_leave.revision==1);

 // No later command evicts id 1. Leave still ends receipt retrieval for this peer.
 lost.leave(old_peer);
 const auto replay=lost.command(old_peer,"lost-receipt-epoch",1,old_entity,7);
 CHECK(std::string(replay.status)=="Disconnected");
 CHECK(replay.id==1 && replay.entity==old_entity && replay.value==before_leave.value);
 CHECK(replay.revision==before_leave.revision);
 const auto after_replay=lost.snapshot(old_peer);
 CHECK(std::string(after_replay.status)=="Disconnected");
 CHECK(after_replay.entity==old_entity && after_replay.value==before_leave.value);
 CHECK(after_replay.revision==before_leave.revision);
 CHECK(!lost.join(old_peer));

 // The new peer gets a fresh entity. Reusing id 1 against the former entity
 // is forbidden and leaves the new participant unchanged; its own next id works.
 const uint64_t new_peer=3; const auto new_entity=lost.join(new_peer);
 CHECK(new_entity && new_entity!=old_entity);
 const auto foreign=lost.command(new_peer,"lost-receipt-epoch",1,old_entity,9);
 CHECK(std::string(foreign.status)=="Forbidden");
 CHECK(foreign.id==1 && foreign.value==0 && foreign.revision==before_leave.revision);
 const auto after_foreign=lost.snapshot(new_peer);
 CHECK(std::string(after_foreign.status)=="Ok" && after_foreign.entity==new_entity);
 CHECK(after_foreign.value==0 && after_foreign.revision==before_leave.revision);
 const auto fresh=lost.command(new_peer,"lost-receipt-epoch",2,new_entity,2);
 CHECK(std::string(fresh.status)=="Applied" && fresh.value==2 && fresh.revision==2);

 // The same numeric id can apply for another newly admitted identity only.
 const uint64_t second_peer=4; const auto second_entity=lost.join(second_peer);
 CHECK(second_entity && second_entity!=old_entity && second_entity!=new_entity);
 const auto second_first=lost.command(second_peer,"lost-receipt-epoch",1,second_entity,3);
 CHECK(std::string(second_first.status)=="Applied");
 CHECK(second_first.id==1 && second_first.value==3 && second_first.revision==3);
}
