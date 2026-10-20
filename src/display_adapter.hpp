#pragma once
#include "correction_control.hpp"
#include "../integrations/waveform-model/waveform-model-draft/waveform_model.hpp"
#include "../integrations/event-delivery/event-queue-draft/event_queue.hpp"
#include <memory>

namespace pitatuner::display {
// All values belong to one serialized control-owner observation. No backend reads.
struct WaveInput {
 pita::Key key;
 std::uint64_t sequence{}, baselineId{};
 pita::Ns baselineOffset{}, currentOffset{};
 std::optional<pita::Ns> candidateOffset;
 waveform::Frame frame;
};
struct Input {
 pita::Key selection;
 std::uint64_t latestSequence{};
 pita::Ns observedAt{}, now{};
 View control;
 std::optional<pita::Estimate> estimate;
 std::optional<WaveInput> wave;
 pita::delivery::State deliveryState{pita::delivery::State::Unbound};
 // Required explicit choice. Unchosen is a valid safe state, never calibrated here.
 pita::Mode selectedMode;
 explicit Input(pita::Mode mode):selectedMode(mode) {}
};
enum class State { Ready, Closed, WrongKey, DeliveryBlocked, Protected, NoReadback,
 InvalidObservation, NoEstimate, OldSequence, InvalidEstimate, Silent, Muted,
 LowConfidence, Stale, NoBaseline, ModeMismatch, NoProposal };
enum class Confidence { Unknown, Invalid, Silent, Muted, Low, Stable };
struct Bin {
 std::uint64_t begin_ns{}, end_ns{};
 std::optional<float> minimum, maximum;
 bool drawable{};
};
struct Series {
 waveform::Kind kind;
 std::int64_t shift_ns{};
 std::size_t clipped_left{}, clipped_right{};
 std::vector<Bin> bins;
};
struct Snapshot {
 pita::Key key;
 std::uint64_t sequence{};
 pita::Ns observedAt{}, evaluatedAt{};
 std::optional<pita::Ns> freshUntil;
 State state{State::NoEstimate};
 Confidence confidence{Confidence::Unknown};
 pita::Mode selectedMode{pita::Mode::Unchosen};
 // nullopt means dash; never coerce missing observations to zero.
 std::optional<pita::Ns> delay, current, target, residualCurrent, residualTarget;
 // Display-only PRE-relative calculation D + committed S, not output/AV sync.
 // Kept separate from baseline-relative proposal residuals above.
 std::optional<pita::Ns> filterResidualPre;
 std::optional<std::uint64_t> proposalId;
 bool recommendationEnabled{};
 // Common prerequisites only: NOT permission to execute any write/Undo.
 bool manualPrerequisites{};
 waveform::Axis axis{};
 std::vector<Series> waveforms;
};
using ImmutableSnapshot = std::shared_ptr<const Snapshot>;
ImmutableSnapshot build(const Input&);
} // namespace pitatuner::display
