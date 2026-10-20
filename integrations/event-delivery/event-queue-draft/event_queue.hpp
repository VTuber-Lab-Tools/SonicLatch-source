#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace pita::delivery {
// Stable opaque source identity. Resolve UUID -> this token off the RT path;
// mapping must be collision-free and tokens must never be reused.
struct Key {
  std::uint64_t source_hi=0, source_lo=0, session=0, epoch=0;
  bool operator==(const Key &b) const noexcept {
    return source_hi==b.source_hi && source_lo==b.source_lo && session==b.session && epoch==b.epoch;
  }
};
enum class Kind { Notification, Intent };
enum class Action { Manual, Step, Undo, ApplyProposal };
struct Event {
  Key key{};
  Kind kind=Kind::Notification;
  std::uint64_t sequence=0; // Notification stream sequence OR global Intent ID.
  std::uint64_t observed_sequence=0; // Intent's observed notification version.
  Action action=Action::Manual;
  std::int64_t value=0; // ns for manual/step, otherwise adapter-defined unused.
  std::uint64_t proposal=0;
};
static_assert(std::is_trivially_copyable<Event>::value, "bounded value payload only");
enum class State { Unbound, NeedsSnapshot, NeedsAck, Ready, NeedsNewKey, Shutdown };
enum class Result { Accepted, WrongKey, OldSequence, NotReady, Overflow, Closed, Invalid, Exhausted };
struct Ticket { Key key{}; std::uint64_t generation=0; };
struct Snapshot { Ticket ticket{}; std::uint64_t sequence=0; std::int64_t offset=0; };

// SEQUENTIAL reference state machine. NOT thread-safe; not a production RT queue.
// All calls (including lifetime/selection changes) belong to ONE serialized owner.
// No callback, allocation, locking, waiting, string or OBS/Qt access inside methods.
template<std::size_t Capacity> class Queue {
  static_assert(Capacity>0, "positive bounded capacity");
  std::array<Event, Capacity> entries_{};
  std::size_t head_=0, size_=0;
  Key key_{};
  State state_=State::Unbound;
  std::uint64_t generation_=0, seen_notification_=0, delivered_notification_=0, seen_intent_=0;
  std::optional<std::int64_t> current_offset_{};
  void clear() noexcept { head_=0; size_=0; }
  bool advance_generation() noexcept {
    if(generation_==UINT64_MAX) { shutdown(); return false; }
    ++generation_; return true;
  }
  void fault() noexcept {
    clear(); current_offset_.reset();
    if(advance_generation()) state_=State::NeedsSnapshot;
  }
public:
  State state() const noexcept { return state_; }
  std::size_t size() const noexcept { return size_; }
  bool fail_closed() const noexcept { return state_!=State::Ready; }
  bool snapshot_required() const noexcept { return state_==State::NeedsSnapshot; }
  std::optional<std::int64_t> recovery_offset() const noexcept { return current_offset_; }
  std::uint64_t delivered_sequence() const noexcept { return delivered_notification_; }
  std::optional<Ticket> request() const noexcept {
    if(state_!=State::NeedsSnapshot) return {};
    return Ticket{key_,generation_};
  }
  // Session allocator is global to this control lifetime. Same-session source
  // changes are forbidden. Same-source epoch must strictly increase.
  bool select(Key k) noexcept {
    if(state_==State::Shutdown || k.session==0 || k.epoch==0 || (k.source_hi==0 && k.source_lo==0)) return false;
    if(state_!=State::Unbound) {
      bool new_session=k.session>key_.session;
      bool new_epoch=k.session==key_.session && k.source_hi==key_.source_hi && k.source_lo==key_.source_lo && k.epoch>key_.epoch;
      if(!new_session && !new_epoch) return false;
    }
    key_=k; seen_notification_=delivered_notification_=0; fault();
    return state_!=State::Shutdown;
  }
  Result enqueue(const Event &e) noexcept {
    if(state_==State::Shutdown) return Result::Closed;
    if(!(e.key==key_)) return Result::WrongKey;
    if(state_==State::Unbound) return Result::NotReady;
    if(e.kind!=Kind::Notification && e.kind!=Kind::Intent) return Result::Invalid;
    if(e.kind==Kind::Intent && e.action!=Action::Manual && e.action!=Action::Step && e.action!=Action::Undo && e.action!=Action::ApplyProposal) return Result::Invalid;
    if(state_==State::NeedsNewKey) return Result::Exhausted;
    auto &watermark=e.kind==Kind::Notification ? seen_notification_ : seen_intent_;
    if(e.sequence==0 || e.sequence<=watermark) return Result::OldSequence;
    // Consume even when overflow rejects delivery. Never replay that ID later.
    watermark=e.sequence;
    if(e.kind==Kind::Notification && e.sequence==UINT64_MAX) {
      fault();
      if(state_!=State::Shutdown) state_=State::NeedsNewKey;
      return Result::Exhausted; // Maximum is never delivered as usable authority.
    }
    if(state_!=State::Ready) {
      if(e.kind==Kind::Notification) fault(); // A racing update invalidates the recovery ticket.
      return Result::NotReady;
    }
    if(e.kind==Kind::Notification) current_offset_.reset();
    if(size_==Capacity) { fault(); return Result::Overflow; }
    entries_[(head_+size_)%Capacity]=e; ++size_;
    return Result::Accepted;
  }
  // Returns eligible events only. Stale intents are discarded, not reissued.
  std::optional<Event> pop() noexcept {
    if(state_!=State::Ready) return {};
    while(size_) {
      Event e=entries_[head_]; head_=(head_+1)%Capacity; --size_;
      if(!(e.key==key_)) continue;
      if(e.kind==Kind::Notification) {
        delivered_notification_=e.sequence;
        return e;
      }
      // Conservatively reject if ANY newer notification has arrived, including
      // one queued behind the intent. Handler must immediately recheck backend.
      if(e.observed_sequence!=seen_notification_ || e.observed_sequence!=delivered_notification_) continue;
      return e;
    }
    return {};
  }
  // Async snapshot response must match the outstanding recovery ticket and be
  // newer than every accepted/dropped notification. Zero/rollback/wrap rejected.
  bool recover(const Snapshot &s) noexcept {
    if(state_!=State::NeedsSnapshot || !(s.ticket.key==key_) || s.ticket.generation!=generation_ || s.sequence==0 || s.sequence<=seen_notification_) return false;
    if(s.sequence==UINT64_MAX) {
      seen_notification_=s.sequence; fault();
      if(state_!=State::Shutdown) state_=State::NeedsNewKey;
      return false;
    }
    clear(); seen_notification_=delivered_notification_=s.sequence;
    current_offset_=s.offset; state_=State::NeedsAck; return true;
  }
  // Adapter calls only following explicit user read/ack AND backend revalidation.
  // This does NOT restore an old baseline/proposal/Undo or authorize a write.
  bool acknowledge(Ticket t) noexcept {
    if(state_!=State::NeedsAck || !(t.key==key_) || t.generation!=generation_) return false;
    state_=State::Ready; return true;
  }
  // Loss reports, same-value external writes, source removal, and invalidated
  // snapshots use this out-of-band serialized signal; it must not be queued.
  void invalidate() noexcept {
    if(state_!=State::Shutdown && state_!=State::Unbound && state_!=State::NeedsNewKey) fault();
  }
  void shutdown() noexcept { clear(); current_offset_.reset(); state_=State::Shutdown; }
};
} // namespace pita::delivery
