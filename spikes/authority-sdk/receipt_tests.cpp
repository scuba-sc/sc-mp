#include "receipt_protocol.h"
#include <stdexcept>
#include <cstdio>
#include <thread>
#include <mutex>
#include <set>
#include <type_traits>
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
 {receipt::Authority b;CHECK(b.admit(2,{b.epoch,1,3}).status==receipt::Status::Queued);b.disconnect(2);CHECK(b.admit(2,{b.epoch,2,3}).status==receipt::Status::Disconnected);CHECK(b.admit(2,{b.epoch,1,3}).status==receipt::Status::Disconnected);CHECK(b.pending.empty());b.step();CHECK(b.value==0);std::puts("PASS disconnected peer cannot submit new commands");}
 {receipt::Authority b;for(uint64_t id=1;id<=9;++id){CHECK(b.admit(2,{b.epoch,id,1}).status==receipt::Status::Queued);b.step();}CHECK(b.admit(2,{b.epoch,10,1}).status==receipt::Status::Queued);CHECK(b.admit(3,{b.epoch,1,2}).status==receipt::Status::Queued);b.disconnect(2);b.disconnect(2);CHECK(b.admit(2,{b.epoch,1,1}).status==receipt::Status::Old);CHECK(b.admit(2,{b.epoch,9,1}).status==receipt::Status::Applied);CHECK(b.admit(2,{b.epoch,10,1}).status==receipt::Status::Disconnected);CHECK(b.admit(2,{b.epoch,10,2}).status==receipt::Status::Conflict);CHECK(b.admit(2,{b.epoch,11,1}).status==receipt::Status::Disconnected);CHECK(b.pending.size()==1);b.step();CHECK(b.value==11);CHECK(b.admit(3,{b.epoch,1,2}).status==receipt::Status::Applied);std::puts("PASS disconnect preserves retained outcomes and other peers");}
 {static_assert(!std::is_copy_constructible_v<receipt::Authority>);static_assert(!std::is_move_constructible_v<receipt::Authority>);std::mutex mu;std::set<uint64_t> epochs;std::vector<std::thread> threads;for(int t=0;t<8;++t)threads.emplace_back([&]{for(int i=0;i<1000;++i){receipt::Authority b;std::lock_guard<std::mutex> lock(mu);epochs.insert(b.epoch);}});for(auto& thread:threads)thread.join();CHECK(epochs.size()==8000);std::puts("PASS concurrent incarnation allocation is unique");}
 {std::atomic<uint64_t> next{UINT64_MAX-3};std::atomic<unsigned> exhausted{0};std::mutex mu;std::set<uint64_t> epochs;std::vector<std::thread> threads;for(int t=0;t<8;++t)threads.emplace_back([&]{try{auto epoch=receipt::detail::allocateIncarnation(next);std::lock_guard<std::mutex> lock(mu);epochs.insert(epoch);}catch(const std::runtime_error&){++exhausted;}});for(auto& thread:threads)thread.join();CHECK(epochs.size()==3);CHECK(*epochs.begin()==UINT64_MAX-2);CHECK(*epochs.rbegin()==UINT64_MAX);CHECK(exhausted==5);CHECK(next==UINT64_MAX);std::puts("PASS concurrent incarnation exhaustion refuses wrap");}
 return 0;
}catch(const std::exception&e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
