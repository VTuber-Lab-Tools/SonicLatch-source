#pragma once
#include <atomic>
#include <cstdint>
// Owner must unregister callbacks / retain their context until all entry calls
// have returned. This gate orders accepted PCM publication before terminal done;
// it cannot by itself make a freed callback context safe to access.
class CallbackGate {
 static constexpr uint64_t closed=uint64_t{1}<<63;
 std::atomic<uint64_t> state{0};
public:
 bool enter() noexcept {const auto prior=state.fetch_add(1,std::memory_order_acq_rel);if(prior&closed){leave();return false;}return true;}
 void leave() noexcept {state.fetch_sub(1,std::memory_order_release);}
 void close() noexcept {state.fetch_or(closed,std::memory_order_acq_rel);}
 bool quiet() const noexcept {return (state.load(std::memory_order_acquire)&~closed)==0;}
 bool isClosed() const noexcept {return (state.load(std::memory_order_acquire)&closed)!=0;}
 struct Lease {CallbackGate& gate;bool accepted;explicit Lease(CallbackGate& g):gate(g),accepted(g.enter()){}~Lease(){if(accepted)gate.leave();}Lease(const Lease&)=delete;Lease& operator=(const Lease&)=delete;};
};
