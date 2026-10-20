#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace pitatuner::waveform {
constexpr std::size_t max_points = 1'000'000, max_bins = 8192;
constexpr std::uint64_t max_window_ns = 60'000'000'000ULL;
struct Key { std::string source; std::uint64_t session{}, epoch{}; };
struct Point { std::uint64_t index{}, timestamp_ns{}; float amplitude{}; bool valid{true}; };
// One explicitly selected channel. No averaging, normalization or peak alignment.
struct Track { Key key; std::uint32_t rate{}, channel{}; std::uint64_t origin_ns{}; std::vector<Point> points; };
struct Axis { std::uint64_t begin_ns{}, end_ns{}; std::size_t width{}; };
struct Bin {
 std::uint64_t begin_ns{}, end_ns{}, expected{}, finite{}, invalid{}, missing{};
 std::optional<float> minimum, maximum;
 bool outside_timeline{};
 bool drawable() const { return finite && !invalid && !missing && !outside_timeline; }
};
enum class Kind { PreMeasured, PostMeasured, PostEstimatedCurrentPosition, PostEstimatedCandidatePosition };
struct Series { Kind kind; std::int64_t shift_ns{}; std::vector<Bin> bins; std::size_t clipped_left{}, clipped_right{}; };
enum class Error { None, InvalidAxis, InputLimit, InvalidMetadata, TimestampOverflow, TimestampMismatch, IndexOrder, ShiftOverflow };
struct Request { Axis axis; Track pre, post; std::int64_t baseline_offset{}, current_offset{}; std::optional<std::int64_t> candidate_offset; };
struct Frame { Axis axis; std::vector<Series> series; Error error{Error::None}; explicit operator bool() const { return error == Error::None; } };
// Synchronous worker-side operation; no I/O, OBS, Qt, audio processing or setter.
Frame build(const Request&);
} // namespace
