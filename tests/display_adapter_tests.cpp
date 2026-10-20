#include "display_adapter.hpp"
#include <cassert>
#include <iostream>
#include <type_traits>
using namespace pitatuner;
namespace d=pitatuner::display;
struct Fake final:pita::Backend {
 std::optional<pita::Ns> value=100; std::uint64_t revision=0; unsigned writes=0;
 std::optional<pita::Ns> read() override { return value; }
 std::uint64_t externalRevision() const override { return revision; }
 void write(pita::Ns n) override { value=n; ++writes; }
};
struct Fixture {
 Fake backend; DeliveryGate gate; pita::Key key{"source-A",1,1};
 ControlSession control{backend,key,{-1000,1000,1,true},gate};
 pita::Estimate estimate{key,1,70,1000,true,true,true,false};
 Fixture() {
  assert(control.registerConfirmedBaseline({pita::Mode::MeasuredReference,100,20,0,1},true));
  assert(control.acceptEstimate(estimate)); assert(control.propose(10));
 }
 d::Input input() {
  d::Input in(pita::Mode::MeasuredReference); in.selection=key; in.latestSequence=1;
  in.observedAt=10; in.now=20; in.control=control.view(); in.estimate=estimate;
  in.deliveryState=pita::delivery::State::Ready;
  waveform::Track pre{{key.uuid,key.session,key.epoch},1000,0,1000000,{{0,1000000,0.5F,true},{1,2000000,-0.5F,true}}};
  waveform::Request request{{1000000,3000000,2},pre,pre,100,100,50};
  in.wave=d::WaveInput{key,1,1,100,100,50,waveform::build(request)};
  return in;
 }
};
int main() {
 static_assert(std::is_const<typename d::ImmutableSnapshot::element_type>::value,"const snapshots");
 static_assert(!std::is_default_constructible<d::Input>::value,"explicit baseline mode required");
 unsigned cases=0;
 { Fixture f; auto in=f.input(); auto s=d::build(in);
  assert(s->state==d::State::Ready && s->delay==70 && s->current==100 && s->target==50);
  assert(s->residualCurrent==50 && s->residualTarget==0 && s->recommendationEnabled);
  assert(s->waveforms.size()==4 && s->waveforms[0].bins[0].drawable);
  in.wave->frame.series[0].bins[0].minimum=99.0F; in.control.offset=999;
  assert(s->current==100 && s->waveforms[0].bins[0].minimum==0.5F && f.backend.writes==0); ++cases;
 }
 { Fixture f; auto in=f.input(); in.now=1000; auto s=d::build(in);
  assert(s->state==d::State::Stale && !s->delay && !s->target && s->current==100 && s->waveforms.empty()); ++cases;
 }
 { Fixture f; auto in=f.input(); in.estimate->key.session=0; auto s=d::build(in);
  assert(s->state==d::State::WrongKey && !s->target && s->waveforms.empty());
  in=f.input(); in.control.key.epoch=2; s=d::build(in); assert(!s->current && !s->manualPrerequisites); ++cases;
 }
 { Fixture f; for(auto seq:{std::uint64_t{0},std::uint64_t{2},std::uint64_t{UINT64_MAX}}) { auto in=f.input(); in.estimate->seq=seq;
   auto s=d::build(in); assert(s->state==d::State::OldSequence && !s->delay && !s->recommendationEnabled); } ++cases;
 }
 { Fixture f; for(int k=0;k<4;++k) { auto in=f.input();
   if(k==0) in.estimate->valid=false;
   if(k==1) in.estimate->active=false;
   if(k==2) in.estimate->muted=true;
   if(k==3) in.estimate->confident=false;
   auto s=d::build(in); assert(!s->delay && !s->target && s->current==100 && s->manualPrerequisites && s->waveforms.size()==(k==2?0:2)); } ++cases;
 }
 { Fixture f; f.control.externalChange(); auto in=f.input(); auto s=d::build(in);
  assert(s->state==d::State::Protected && !s->current && !s->manualPrerequisites && !s->target); ++cases;
 }
 { Fixture f; f.backend.value.reset(); assert(!f.control.acknowledgeRead()); auto s=d::build(f.input());
  assert(!s->current && !s->recommendationEnabled);
  auto in=f.input(); in.control.protectedState=false; s=d::build(in); assert(s->state==d::State::NoReadback); ++cases;
 }
 { Fixture f; auto in=f.input(); in.selectedMode=pita::Mode::Unchosen; auto s=d::build(in);
  assert(s->state==d::State::NoBaseline && !s->target && s->delay==70 && s->waveforms.size()==2);
  in.selectedMode=pita::Mode::ExplicitPreTarget; s=d::build(in);
  assert(s->state==d::State::ModeMismatch && !s->target && s->waveforms.size()==2); ++cases;
 }
 { Fixture f; auto in=f.input(); in.wave->key.session=9; assert(d::build(in)->waveforms.empty());
  in=f.input(); in.wave->sequence=2; assert(d::build(in)->waveforms.empty());
  in=f.input(); in.wave->currentOffset=101; assert(d::build(in)->waveforms.size()==2);
  in=f.input(); in.wave->candidateOffset=51; assert(d::build(in)->waveforms.size()==3);
  in=f.input(); in.wave->frame.error=waveform::Error::ShiftOverflow; assert(d::build(in)->waveforms.empty()); ++cases;
 }
 { Fixture f; auto in=f.input(); auto& b=in.wave->frame.series[0].bins[0]; b.missing=1;
  auto s=d::build(in); assert(!s->waveforms[0].bins[0].drawable && !s->waveforms[0].bins[0].minimum);
  b.missing=0; b.invalid=1; assert(!d::build(in)->waveforms[0].bins[0].drawable); ++cases;
 }
 { Fixture f; auto e=f.estimate; e.seq=2; e.delay=INT64_MIN;
  assert(f.control.acceptEstimate(e)); assert(!f.control.propose(20));
  auto in=f.input(); in.estimate=e; in.latestSequence=2; auto s=d::build(in);
  assert(s->state==d::State::NoProposal && !s->target && s->delay==INT64_MIN && !s->recommendationEnabled); ++cases;
 }
 { Fixture f; auto in=f.input(); in.now=INT64_MAX; assert(d::build(in)->state==d::State::Stale);
  in.now=9; auto s=d::build(in); assert(s->state==d::State::InvalidObservation && !s->current); ++cases;
 }
 { Fixture f; auto in=f.input(); in.control.proposal->estimateSeq=2;
  assert(!d::build(in)->target); in=f.input(); in.control.proposal->freshUntil=999; assert(!d::build(in)->target); ++cases;
 }
 { Fixture f; auto in=f.input(); in.control.closed=true; auto s=d::build(in);
  assert(s->state==d::State::Closed && !s->current && !s->manualPrerequisites); ++cases;
 }
 { Fixture f; pita::delivery::Queue<1> q; pita::delivery::Key key{1,1,1,1};
  assert(q.select(key)); auto ticket=*q.request(); assert(q.recover({ticket,1,100})); assert(q.acknowledge(ticket));
  assert(q.enqueue({key,pita::delivery::Kind::Notification,2})==pita::delivery::Result::Accepted);
  assert(q.enqueue({key,pita::delivery::Kind::Notification,3})==pita::delivery::Result::Overflow);
  auto in=f.input(); in.deliveryState=q.state(); auto s=d::build(in);
  assert(s->state==d::State::DeliveryBlocked && !s->current && !s->manualPrerequisites && s->waveforms.empty()); ++cases;
 }
 { Fixture f; auto in=f.input();in.estimate->valid=false;in.estimate->active=false;in.estimate->confident=false;auto view=d::build(in);assert(view->state==d::State::Silent&&view->confidence==d::Confidence::Silent&&!view->delay&&!view->target&&view->waveforms.size()==2&&f.backend.writes==0);++cases; }
 { Fixture f; auto in=f.input();in.estimate->valid=false;in.estimate->active=false;in.estimate->muted=true;auto view=d::build(in);assert(view->state==d::State::Muted&&view->confidence==d::Confidence::Muted&&!view->delay&&!view->target&&view->waveforms.empty()&&f.backend.writes==0);++cases; }
 std::cout<<"PASS "<<cases<<" display adapter regression groups; synthetic input; no OBS/Qt/Windows/product integration\n";
}

