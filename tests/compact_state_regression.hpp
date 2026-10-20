#pragma once
#include "qt_compact_dock.hpp"
#include <QApplication>

// Synthetic presentation/widget regression only. No source, consumer clock,
// backend, waveform producer or correction authority is exercised here.
namespace pitatuner::qt::test {
template<class Check>
void compactStateRegression(QApplication& app, Metrics& metrics,
                            CompactDock* content, QScrollArea* scroll,
                            const Translate& tr, Check check) {
 const auto settle = [&] { for (int i=0; i<6; ++i) app.processEvents(); };
 const auto fit = [&] {
  settle();
  check(content->width() <= scroll->viewport()->width(), "state transition horizontal fit");
  for (size_t i : {size_t(4), size_t(5)}) {
   auto* label=metrics.metricValue(i);
   check(label->height() >= label->heightForWidth(label->width()), "state transition full warning height");
  }
  check(metrics.writesDisabled(), "state presentation grants no write authority");
 };
 const auto empty = [&] {
  for (size_t i=0; i<4; ++i)
   check(metrics.value(i)==nsValue({}), "all four missing metrics are dashes, not zero or stale values");
  check(metrics.unknown(), "empty presentation confidence is unknown");
  fit();
 };
 const auto seed = [&] {
  auto fresh=std::make_shared<display::Snapshot>();
  fresh->state=display::State::Ready;
  fresh->confidence=display::Confidence::Stable;
  fresh->delay=20000000; fresh->current=120000000;
  fresh->target=90000000; fresh->residualCurrent=30000000;
  metrics.update({fresh,display::Hidden::None});
  for (size_t i=0; i<4; ++i)
   check(metrics.value(i)!=nsValue({}), "synthetic populated fixture displays all four numbers");
  check(metrics.value(4)==tr(Text::Stable), "synthetic populated fixture has stable confidence");
  fit();
 };
 // The owner already supplies these empty presentations. This tests only their
 // rendering after populated numbers, not owner/consumer invalidation itself.
 for (auto hidden : {display::Hidden::NoSnapshot, display::Hidden::ContextBlocked,
                     display::Hidden::WrongContext, display::Hidden::ClockFault,
                     display::Hidden::Expired}) {
  seed();
  for (int repeat=0; repeat<3; ++repeat) {
   metrics.update({{},hidden});
   empty();
   if (hidden==display::Hidden::Expired)
    check(metrics.stateIs(Text::Expired), "expired presentation retains existing localized reason");
   if (hidden==display::Hidden::ContextBlocked)
    check(metrics.stateIs(Text::Protected), "blocked presentation retains existing localized reason");
  }
 }
 // Covers initial/no-result and fresh -> clear -> fresh -> clear presentation.
 seed(); metrics.update({}); empty();
 seed(); metrics.update({}); empty();
 seed(); metrics.update({},Text::Muted); empty();
 check(metrics.stateIs(Text::Muted), "unavailable muted source uses existing localized reason");
 // Existing adapter state/translation pairs, without inventing new UI text.
 for (auto state : {display::State::Silent,display::State::Muted,display::State::InvalidEstimate}) {
  seed();
  auto unavailable=std::make_shared<display::Snapshot>(); unavailable->state=state;
  metrics.update({unavailable,display::Hidden::None}); empty();
  const auto reason=state==display::State::Silent?Text::Silent:
                    state==display::State::Muted?Text::Muted:Text::Invalid;
  check(metrics.stateIs(reason), "unavailable snapshot keeps the existing localized reason");
 }
 // Low-confidence text and no-baseline are already supported; test reflow only.
 auto low=std::make_shared<display::Snapshot>();
 low->state=display::State::LowConfidence; low->confidence=display::Confidence::Low;
 metrics.update({low,display::Hidden::None}); fit();
 check(metrics.value(4)==tr(Text::Low), "low confidence uses existing translation");
 auto noBaseline=std::make_shared<display::Snapshot>();
 noBaseline->state=display::State::NoBaseline;
 metrics.update({noBaseline,display::Hidden::None}); fit();
 check(metrics.stateIs(Text::NoBaseline), "no baseline uses existing translation");
 check(metrics.value(2)==nsValue({})&&metrics.value(3)==nsValue({}), "no baseline has no target or residual");
 metrics.update({}); empty();
}
} // namespace pitatuner::qt::test
