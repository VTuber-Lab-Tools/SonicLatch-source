#pragma once
#include "capture_format.hpp"
#include <cstdint>

namespace pitatuner::capture::timeline {
// Provisional measurement uncertainty budget, NOT a WASAPI clock guarantee.
// Observed input jitter reached 0.824ms. Limit each tap to 2ms, so differential
// clock uncertainty remains <=4ms inside the provisional +/-10ms goal.
// Retain the original anchor: repeated small steps may not hide accumulated loss.
inline constexpr uint64_t uncertaintyNs=2000000ULL;
inline uint64_t distance(uint64_t a,uint64_t b){return a>b?a-b:b-a;}
inline bool continuous(uint64_t actual,uint64_t expected,uint64_t previous,
 uint32_t previousFrames,uint32_t rate,bool newPacket){
 if(distance(actual,expected)>uncertaintyNs)return false;
 if(newPacket&&previousFrames){
  if(actual<=previous)return false;
  if(distance(actual,previous+format::frameNs(previousFrames,rate))>uncertaintyNs)return false;
 }
 return true;
}
// Arbitrary timestamps need not lie exactly on a common sample grid. Nearest
// sample projection has <=half-sample rounding uncertainty, not 2ns precision.
inline bool sampleProjection(uint64_t actual,uint64_t mapped,uint32_t rate){return distance(actual,mapped)<=(format::sampleCeilNs(rate)+1)/2;}
inline bool reconfigureReady(bool alive,bool enabled,bool muted,uint64_t lastEvent,uint64_t now){return alive&&enabled&&!muted&&now>=lastEvent&&now-lastEvent>=250000000ULL;}
}
