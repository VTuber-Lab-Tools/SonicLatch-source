#pragma once
#include <atomic>
#include <initializer_list>
#include <cstdint>
#include <limits>
#include <optional>
namespace soniclatch::bridge {
// One owner arms after the first setter/readback. PRE samples this generation;
// only that PRE's same TLS invocation can acknowledge it after OBS buffering.
class PacketAck {
 std::atomic<std::uint64_t> armed_{0},ack_{0};std::uint64_t next_=0;
public:
 std::uint64_t arm(){if(next_==UINT64_MAX)return 0;const auto n=++next_;armed_.store(n,std::memory_order_release);return n;}
 std::uint64_t pre()const{return armed_.load(std::memory_order_acquire);}
 void post(std::uint64_t token,bool sameInvocation,bool valid){if(token&&sameInvocation&&valid&&armed_.load(std::memory_order_acquire)==token)ack_.store(token,std::memory_order_release);}
 bool complete(std::uint64_t token)const{return token&&armed_.load(std::memory_order_acquire)==token&&ack_.load(std::memory_order_acquire)==token;}
 void cancel(){armed_.store(0,std::memory_order_release);}
};
template<class Permits> std::optional<std::int64_t> intermediate(std::int64_t target,Permits permits){
 if(!target||!permits(target))return{};
 // Same sign, exactly 1 ns from target, both values admitted by the same policy.
 for(int step:{-1,1}){
  if((step<0&&target==INT64_MIN)||(step>0&&target==INT64_MAX))continue;
  const auto candidate=target+step;
  if(candidate&&((candidate<0)==(target<0))&&permits(candidate))return candidate;
 }
 return{};
}
}
