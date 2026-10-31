#pragma once
#include <cstdint>
#include <deque>
#include <map>
#include <vector>
#include <stdexcept>
namespace receipt {
enum class Status:uint8_t { Queued, Applied, Invalid, Old, Stale, Conflict, Overload, Gap, Disconnected };
inline bool terminal(Status s){return s!=Status::Queued && s!=Status::Overload && s!=Status::Gap;}
struct Command {uint64_t epoch, id; int32_t delta;};
struct Result {uint64_t peer, epoch, id; Status status; int32_t value;};
struct Authority {
 static constexpr size_t queueLimit=16, receiptLimit=8, peerLimit=16;
 static uint64_t incarnation(){static uint64_t n=0;if(n==UINT64_MAX)throw std::runtime_error("epoch exhausted");return ++n;}
 uint64_t epoch=incarnation(); int value=0;
 struct Entry {Command command;Result result;};
 struct Peer {uint64_t high=0;std::deque<Entry> receipts;};
 struct Pending {uint64_t peer;Command command;};
 std::map<uint64_t,Peer> peers;
 std::deque<Pending> pending;
 Result admit(uint64_t peer,Command c){
  auto result=[&](Status s){return Result{peer,c.epoch,c.id,s,value};};
  if(c.epoch!=epoch)return result(Status::Stale);
  if(!peer || !c.id)return result(Status::Invalid);
  if(!peers.count(peer)){
   if(c.id!=1)return result(Status::Gap);
   if(pending.size()==queueLimit)return result(Status::Overload);
   if(peers.size()==peerLimit)return result(Status::Overload);
   peers.emplace(peer,Peer{});
  }
  auto& p=peers.at(peer);
  for(auto& e:p.receipts)if(e.command.id==c.id)return e.command.delta==c.delta?e.result:result(Status::Conflict);
  for(auto& q:pending)if(q.peer==peer&&q.command.id==c.id)return result(q.command.delta==c.delta?Status::Queued:Status::Conflict);
  if(c.id<=p.high)return result(Status::Old);
  if(p.high==UINT64_MAX || c.id!=p.high+1)return result(Status::Gap);
  if(pending.size()==queueLimit)return result(Status::Overload);
  p.high=c.id;
  pending.push_back({peer,c});return result(Status::Queued);
 }
 std::vector<Result> step(){
  std::vector<Result> out;
  while(!pending.empty()){
   auto q=pending.front();pending.pop_front();auto c=q.command;
   bool valid=c.delta>=-100&&c.delta<=100&&int64_t(value)+c.delta>=-1000000&&int64_t(value)+c.delta<=1000000;
   if(valid)value+=c.delta;
   Result r{q.peer,epoch,c.id,valid?Status::Applied:Status::Invalid,value};
   auto& p=peers.at(q.peer);p.receipts.push_back({c,r});if(p.receipts.size()>receiptLimit)p.receipts.pop_front();out.push_back(r);
  }return out;
 }
 void disconnect(uint64_t peer){
  // Keep the high-water tombstone for this incarnation: never reopen evicted command IDs.
  for(auto i=pending.begin();i!=pending.end();)if(i->peer==peer){auto& p=peers.at(peer);p.receipts.push_back({i->command,{peer,epoch,i->command.id,Status::Disconnected,value}});if(p.receipts.size()>receiptLimit)p.receipts.pop_front();i=pending.erase(i);}else ++i;
 }
};
}
