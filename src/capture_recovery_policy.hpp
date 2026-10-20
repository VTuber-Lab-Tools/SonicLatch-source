#pragma once
#include <cstdint>
#include "capture_timeline_policy.hpp"
namespace pitatuner::capture::recovery {
// Only ordinary configuration/mute events may start a fresh acquisition.
// Removed/shape/overflow/audio-format/timeline/analysis faults remain protected.
inline constexpr std::uint32_t configuration=1u, muted=64u;
inline constexpr bool refreshable(std::uint32_t faults) {
 return faults != 0 && (faults & ~(configuration | muted)) == 0;
}
inline bool ready(uint32_t reason,bool alive,bool enabled,bool isMuted,uint64_t lastEvent,uint64_t now){
 return refreshable(reason)&&lastEvent&&timeline::reconfigureReady(alive,enabled,isMuted,lastEvent,now);
}
}
