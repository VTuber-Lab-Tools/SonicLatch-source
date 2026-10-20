#pragma once
#include "display_adapter.hpp"

namespace pitatuner::display {
// Values supplied by the serialized control owner, in its notification order.
// No new source token, epoch, IntentID or clock allocator lives here.
struct Context {
 pita::Key key;
 pita::delivery::Ticket ticket;
 pita::delivery::State state{pita::delivery::State::Unbound};
 std::uint64_t notificationSequence{}, observedNotificationSequence{}, latestAcceptedEstimateSequence{};
 bool notificationsDrained{}, controlClosed{}, controlProtected{};
};
struct Packet {
 ImmutableSnapshot snapshot;
 pita::delivery::Ticket ticket;
 std::uint64_t notificationSequence{};
};
enum class Hidden { None, NoSnapshot, ContextBlocked, WrongContext, ClockFault, Expired };
struct Presentation { ImmutableSnapshot snapshot; Hidden hidden{Hidden::NoSnapshot}; };

// UI-thread/serialized consumer only. Immutable snapshots do not self-expire:
// call present with the owner's current context and monotonic time at every use.
// This class has no write, Undo or operation-permission interface.
class Consumer {
 std::optional<Context> context_;
 ImmutableSnapshot latest_;
 std::optional<pita::Ns> lastNow_, lastObservation_;
 std::uint64_t lastSequence_{};
 bool clockFault_{};
 // Reason only: an expired snapshot is still released, never kept as display data.
 bool expired_{};
 static bool ticketEqual(const pita::delivery::Ticket& a,const pita::delivery::Ticket& b) {
  return a.key==b.key && a.generation==b.generation;
 }
 static bool valid(const Context& c) {
  return !c.key.uuid.empty() && c.key.session && c.key.epoch && c.ticket.generation &&
   c.ticket.key.session==c.key.session && c.ticket.key.epoch==c.key.epoch &&
   (c.ticket.key.source_hi || c.ticket.key.source_lo) && c.observedNotificationSequence>=c.notificationSequence;
 }
 static bool ready(const Context& c) {
  return c.state==pita::delivery::State::Ready && c.notificationsDrained &&
   !c.controlClosed && !c.controlProtected && c.observedNotificationSequence==c.notificationSequence && c.notificationSequence && c.notificationSequence!=UINT64_MAX;
 }
 bool clock(pita::Ns now) {
  if(now<0 || (lastNow_ && now<*lastNow_)) {clockFault_=true;latest_.reset();}
  lastNow_=now;
  return !clockFault_;
 }
 bool observe(const Context& c) {
  if(!valid(c))return false;
  if(context_) {
   const auto& old=*context_;
   if(!(c.key==old.key)) {
    const bool session=c.key.session>old.key.session;
    const bool epoch=c.key.session==old.key.session && c.key.uuid==old.key.uuid && c.key.epoch>old.key.epoch;
    if(!session&&!epoch)return false;
    latest_.reset();expired_=false;lastObservation_.reset();lastSequence_=0;
   } else {
    if((old.controlClosed&&!c.controlClosed) || (old.state==pita::delivery::State::Shutdown && c.state!=old.state) ||
       (old.state==pita::delivery::State::NeedsNewKey && c.state!=old.state))return false;
    if(!(c.ticket.key==old.ticket.key) || c.ticket.generation<old.ticket.generation ||
       c.notificationSequence<old.notificationSequence || c.observedNotificationSequence<old.observedNotificationSequence || c.latestAcceptedEstimateSequence<old.latestAcceptedEstimateSequence)return false;
    // A new delivery/notification lineage supersedes the previous expiry reason.
    // An estimate watermark alone does not: its replacement packet must be accepted.
    if(c.ticket.generation!=old.ticket.generation || c.notificationSequence!=old.notificationSequence || c.observedNotificationSequence!=old.observedNotificationSequence)expired_=false;
    if(c.ticket.generation!=old.ticket.generation || c.notificationSequence!=old.notificationSequence || c.observedNotificationSequence!=old.observedNotificationSequence || c.latestAcceptedEstimateSequence!=old.latestAcceptedEstimateSequence || !ready(c))latest_.reset();
   }
  }
  context_=c;
  if(!ready(c))latest_.reset();
  return true;
 }
public:
 // Drop values after an owner-side write without resetting replay watermarks.
 void discardSnapshot(){latest_.reset();expired_=false;}
 bool receive(const Packet& p,const Context& current,pita::Ns now) {
  if(!clock(now)||!observe(current)||!ready(current)||!p.snapshot)return false;
  const auto& s=*p.snapshot;
  if(!(s.key==current.key)||!ticketEqual(p.ticket,current.ticket)||p.notificationSequence!=current.notificationSequence)return false;
  if(s.sequence!=current.latestAcceptedEstimateSequence || s.sequence==UINT64_MAX || s.observedAt<0 || s.evaluatedAt<s.observedAt || s.evaluatedAt>now ||
     (s.freshUntil && now>=*s.freshUntil))return false;
  if(lastObservation_ && (s.sequence<lastSequence_ || s.observedAt<*lastObservation_ ||
     (s.sequence==lastSequence_ && s.observedAt<=*lastObservation_)))return false;
  latest_=p.snapshot;expired_=false;lastSequence_=s.sequence;lastObservation_=s.observedAt;return true;
 }
 Presentation present(const Context& current,pita::Ns now) {
  if(!clock(now))return {{},Hidden::ClockFault};
  if(!observe(current))return {{},Hidden::WrongContext};
  if(!ready(current))return {{},Hidden::ContextBlocked};
  if(!latest_)return {{},expired_?Hidden::Expired:Hidden::NoSnapshot};
  if(latest_->freshUntil && now>=*latest_->freshUntil) {
   latest_.reset();expired_=true;return {{},Hidden::Expired};
  }
  return {latest_,Hidden::None};
 }
};
} // namespace pitatuner::display
