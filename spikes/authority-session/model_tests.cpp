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
}
