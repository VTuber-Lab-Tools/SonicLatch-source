#include "display_adapter.hpp"
#include <limits>

namespace pitatuner::display {
namespace {
bool validKey(const pita::Key& k) { return !k.uuid.empty() && k.session && k.epoch; }
bool chosen(pita::Mode m) {
 return m==pita::Mode::MeasuredReference || m==pita::Mode::ExplicitPreTarget;
}
}
ImmutableSnapshot build(const Input& in) {
 Snapshot s;
 s.key=in.selection; s.sequence=in.latestSequence; s.observedAt=in.observedAt;
 s.evaluatedAt=in.now; s.selectedMode=in.selectedMode;
 auto finish=[&](State state) -> ImmutableSnapshot {
  s.state=state; return std::make_shared<const Snapshot>(std::move(s));
 };
 if(in.control.closed) return finish(State::Closed);
 if(!validKey(in.selection) || !(in.control.key==in.selection)) return finish(State::WrongKey);
 if(in.deliveryState!=pita::delivery::State::Ready) return finish(State::DeliveryBlocked);
 if(in.control.protectedState) return finish(State::Protected);
 if(!in.control.offset) return finish(State::NoReadback);
 if(in.now<0 || in.observedAt<0 || in.now<in.observedAt) return finish(State::InvalidObservation);
 s.current=in.control.offset; s.manualPrerequisites=true;
 if(!in.estimate) return finish(State::NoEstimate);
 const auto& e=*in.estimate;
 if(!(e.key==in.selection)) return finish(State::WrongKey);
 if(!e.seq || e.seq==UINT64_MAX || e.seq!=in.latestSequence) return finish(State::OldSequence);
 s.freshUntil=e.freshUntil;
 // Unavailable signal states must replace an earlier valid display even when
 // correlation produced no numeric estimate. No delay is exposed in these states.
 // Fresh measured PCM may be displayed even when correlation/clock quality
 // cannot authorize a numeric estimate. Never include estimated projections.
 auto measuredOnly=[&]{
  if(in.now>=e.freshUntil||!in.wave||!(in.wave->key==in.selection)||in.wave->sequence!=e.seq||!in.wave->frame)return;
  s.axis=in.wave->frame.axis;
  for(const auto& src:in.wave->frame.series){if(src.kind!=waveform::Kind::PreMeasured&&src.kind!=waveform::Kind::PostMeasured)continue;
   Series dst{src.kind,src.shift_ns,src.clipped_left,src.clipped_right,{}};
   for(const auto& b:src.bins){const bool draw=b.drawable();dst.bins.push_back({b.begin_ns,b.end_ns,draw?b.minimum:std::nullopt,draw?b.maximum:std::nullopt,draw});}s.waveforms.push_back(std::move(dst));}
 };
 if(e.muted) { s.confidence=Confidence::Muted; return finish(State::Muted); }
 if(!e.active) { measuredOnly();s.confidence=Confidence::Silent; return finish(State::Silent); }
 if(!e.valid) { measuredOnly();s.confidence=Confidence::Invalid; return finish(State::InvalidEstimate); }
 if(!e.confident) { measuredOnly();s.confidence=Confidence::Low; return finish(State::LowConfidence); }
 if(in.now>=e.freshUntil) return finish(State::Stale);
 s.confidence=Confidence::Stable; s.delay=e.delay;
 // Fresh, same-key estimate and committed readback were validated above.
 // This display value does not depend on proposal availability or grant a write.
 s.filterResidualPre=pita::add(e.delay,*s.current);
 // Measured waveforms can be shown without a baseline. Estimated projections
 // require exact baseline/current/candidate provenance from this observation.
 const bool baseOK=in.control.baseline && chosen(in.selectedMode) &&
  in.control.baseline->mode==in.selectedMode;
 const auto& p=in.control.proposal;
 const bool referenceOK=p&&(p->absolutePreTarget?p->baselineId==0:(baseOK&&p->baselineId==in.control.baseline->id));
 const bool proposalOK=referenceOK && p->key==in.selection && p->estimateSeq==e.seq &&
  p->before==*s.current &&
  p->freshUntil==e.freshUntil && in.now<p->freshUntil;
 if(proposalOK) {
  s.target=p->target; s.residualCurrent=p->residualNow; s.residualTarget=p->residualTarget;
  s.proposalId=p->id; s.recommendationEnabled=p->id!=0 && p->target!=p->before;
 }
 if(in.wave && in.wave->key==in.selection && in.wave->sequence==e.seq && in.wave->frame) {
  const auto& w=*in.wave; s.axis=w.frame.axis;
  const bool projectionOK=baseOK && w.baselineId==in.control.baseline->id &&
   w.baselineOffset==in.control.baseline->s0 && w.currentOffset==*s.current;
  for(const auto& src:w.frame.series) {
   const bool measured=src.kind==waveform::Kind::PreMeasured || src.kind==waveform::Kind::PostMeasured;
   const bool current=src.kind==waveform::Kind::PostEstimatedCurrentPosition;
   const bool candidate=src.kind==waveform::Kind::PostEstimatedCandidatePosition;
   if(!measured && !(projectionOK && (current || (candidate && proposalOK && w.candidateOffset==s.target)))) continue;
   Series dst{src.kind,src.shift_ns,src.clipped_left,src.clipped_right,{}};
   dst.bins.reserve(src.bins.size());
   for(const auto& b:src.bins) {
    const bool draw=b.drawable();
    dst.bins.push_back({b.begin_ns,b.end_ns,draw?b.minimum:std::nullopt,draw?b.maximum:std::nullopt,draw});
   }
   s.waveforms.push_back(std::move(dst));
  }
 }
 if(proposalOK&&p->absolutePreTarget) return finish(State::Ready);
 if(!in.control.baseline || in.selectedMode==pita::Mode::Unchosen) return finish(State::NoBaseline);
 if(!baseOK) return finish(State::ModeMismatch);
 if(!proposalOK) return finish(State::NoProposal);
 return finish(State::Ready);
}
} // namespace
