#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>

namespace pitatuner::capture::diagnostic {
// Metadata only. One producer per tap; append-only, bounded, never overwritten.
struct Packet { uint64_t timestamp{},arrival{};uint32_t frames{};bool muted{}; };
template<size_t Capacity=128> class Trace {
 std::array<Packet,Capacity> packets_{};std::atomic<size_t> published_{};
public:
 void append(Packet value) noexcept {const auto n=published_.load(std::memory_order_relaxed);if(n==Capacity)return;packets_[n]=value;published_.store(n+1,std::memory_order_release);}
 size_t size() const noexcept{return published_.load(std::memory_order_acquire);}
 Packet at(size_t i)const noexcept{return packets_[i];} // i < previously acquired size
};
struct Failure {
 uint64_t expected{},actual{},difference{},first{},totalFrames{},sequence{},arrival{},previousMedia{},previousArrival{};
 uint32_t frames{},consumed{},rate{},previousFrames{};bool before{};
};
class FirstFailure {
 Failure value_{};std::atomic<bool> ready_{};
public:
 // The sole worker calls publish once, before raising the Timeline fault.
 void publish(Failure value)noexcept{value_=value;ready_.store(true,std::memory_order_release);}
 std::optional<Failure> read()const noexcept{if(!ready_.load(std::memory_order_acquire))return{};return value_;}
};
inline constexpr uint64_t observationNs=500000000ULL;
inline bool observing(uint64_t faultAt,uint64_t now)noexcept{return now>=faultAt&&now-faultAt<observationNs;}
// Owner must consume the trace before destroying its session, even if the
// observation deadline passes between takeTimelineTrace and pollRecovery.
inline bool recoveryReady(uint64_t faultAt,uint64_t now,bool traceTaken)noexcept{return faultAt&&traceTaken&&!observing(faultAt,now);}
}
