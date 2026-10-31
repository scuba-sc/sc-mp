#include "receipt_protocol.h"
#include <stdexcept>
#include <cstdio>
#define CHECK(x) do { if(!(x)) throw std::runtime_error(#x); } while(0)
int main(){try{
 receipt::Authority a;
 auto r=a.admit(2,{a.epoch,1,8});
 CHECK(r.status==receipt::Status::Queued); CHECK(a.value==0);
 a.step(); r=a.admit(2,{a.epoch,1,8});
 CHECK(r.status==receipt::Status::Applied); CHECK(a.value==8);
 std::puts("PASS application receipt distinguishes queued from applied");
 {receipt::Authority b;for(uint64_t id=1;id<=17;++id){auto x=b.admit(2,{b.epoch,id,1});CHECK(x.status==(id<=16?receipt::Status::Queued:receipt::Status::Overload));}CHECK(b.value==0);b.step();CHECK(b.value==16);CHECK(b.admit(2,{b.epoch,1,1}).status==receipt::Status::Old);CHECK(b.value==16);CHECK(b.admit(2,{b.epoch,17,1}).status==receipt::Status::Queued);b.step();CHECK(b.value==17);CHECK(b.peers.at(2).receipts.size()==8);std::puts("PASS bounded receipts old refusal overload recovery");}
 {receipt::Authority b;CHECK(b.admit(2,{b.epoch,2,1}).status==receipt::Status::Gap);CHECK(b.admit(2,{b.epoch,1,101}).status==receipt::Status::Queued);b.step();CHECK(b.admit(2,{b.epoch,1,101}).status==receipt::Status::Invalid);CHECK(b.admit(2,{b.epoch,1,1}).status==receipt::Status::Conflict);CHECK(b.value==0);CHECK(b.admit(2,{b.epoch+1,2,1}).status==receipt::Status::Stale);CHECK(b.admit(3,{b.epoch,1,1}).status==receipt::Status::Queued);b.disconnect(3);CHECK(b.admit(3,{b.epoch,1,1}).status==receipt::Status::Disconnected);b.step();CHECK(b.value==0);std::puts("PASS permanent rejection conflict gap session scope disconnect");}
 {receipt::Authority b;for(uint64_t p=1;p<=16;++p)CHECK(b.admit(p,{b.epoch,2,1}).status==receipt::Status::Gap);CHECK(b.peers.empty());CHECK(b.admit(17,{b.epoch,1,1}).status==receipt::Status::Queued);std::puts("PASS gap refusals do not allocate peer tombstones");}
 {receipt::Authority b;for(uint64_t id=1;id<=16;++id)CHECK(b.admit(1,{b.epoch,id,1}).status==receipt::Status::Queued);for(uint64_t p=2;p<=17;++p)CHECK(b.admit(p,{b.epoch,1,1}).status==receipt::Status::Overload);CHECK(b.peers.size()==1);b.step();CHECK(b.admit(18,{b.epoch,1,1}).status==receipt::Status::Queued);std::puts("PASS queue refusals do not allocate peer tombstones");}
 return 0;
}catch(const std::exception&e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
