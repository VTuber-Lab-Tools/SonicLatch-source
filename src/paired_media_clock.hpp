#pragma once
#include "capture_format.hpp"
#include "capture_packet_split.hpp"
#include <array>
#include <algorithm>
#include <cstdint>
namespace pitatuner::capture::pairedclock {
inline constexpr uint64_t largeCommonJumpNs=70000000ULL; // gross reset gate, not precision
struct Observation{uint64_t call{},timestamp{},frameOffset{};uint32_t frames{};uint64_t callFrameOffset{};};
struct Result{bool matched{},discontinuity{};uint64_t pairs{},matchedFrames{},deltaSpreadNs{};int64_t windowDeltaNs{};};
class Window{
 std::array<Observation,1024> observations_[2];size_t count_[2]{};uint64_t total_[2]{};bool overflow_{};
public:
 void observe(size_t side,Observation x){total_[side]+=x.frames;if(count_[side]==1024){overflow_=true;return;}observations_[side][count_[side]++]=x;}
 Result finish(uint32_t rate)const{
  Result r;std::array<int64_t,1024> deltas{};size_t i=0,j=0,n=0;uint64_t lastCall=0;int64_t low=INT64_MAX,high=INT64_MIN;
  while(i<count_[0]&&j<count_[1]){const auto& a=observations_[0][i];const auto& b=observations_[1][j];if(!a.call){++i;continue;}if(!b.call){++j;continue;}if(a.call<b.call){++i;continue;}if(b.call<a.call){++j;continue;}
   // The same invocation can be split differently by the ring/window boundary
   // or by an upstream filter. Compare only overlapping sample offsets, never
   // assume equal fragment lengths or count fragments as independent calls.
   const auto aEnd=a.callFrameOffset+a.frames,bEnd=b.callFrameOffset+b.frames;
   const auto begin=std::max(a.callFrameOffset,b.callFrameOffset),end=std::min(aEnd,bEnd);
   if(begin<end){
    const auto at=packet::advance(a.timestamp,a.callFrameOffset,begin-a.callFrameOffset,rate);
    const auto bt=packet::advance(b.timestamp,b.callFrameOffset,begin-b.callFrameOffset,rate);
    if(at<=INT64_MAX&&bt<=INT64_MAX){const auto raw=int64_t(bt)-int64_t(at);if(raw>=-10000000000LL&&raw<=10000000000LL){
     const auto delta=raw+int64_t(format::frameNs(a.frameOffset+begin-a.callFrameOffset,rate))-int64_t(format::frameNs(b.frameOffset+begin-b.callFrameOffset,rate));
     if(a.call!=lastCall){deltas[n++]=delta;lastCall=a.call;}
     low=std::min(low,delta);high=std::max(high,delta);r.matchedFrames+=end-begin;
    }}
   }
   if(aEnd<=bEnd)++i;
   if(bEnd<=aEnd)++j;
  }
  r.pairs=n;if(!n)return r;std::sort(deltas.begin(),deltas.begin()+n);r.windowDeltaNs=deltas[n/2];r.deltaSpreadNs=uint64_t(high-low);
  for(size_t side=0;side<2;side++)for(size_t k=1;k<count_[side];k++){const auto& a=observations_[side][k-1];const auto& b=observations_[side][k];const auto elapsed=format::frameNs(b.frameOffset-a.frameOffset,rate);if(b.timestamp<a.timestamp){if(a.timestamp-b.timestamp+elapsed>=largeCommonJumpNs)r.discontinuity=true;}else{const auto step=b.timestamp-a.timestamp;const auto error=step>elapsed?step-elapsed:elapsed-step;if(error>=largeCommonJumpNs)r.discontinuity=true;}}
  r.matched=!overflow_&&!r.discontinuity&&n>=2&&r.matchedFrames*100>=total_[0]*95&&r.matchedFrames*100>=total_[1]*95&&r.deltaSpreadNs<=2*format::sampleCeilNs(rate);return r;
 }
 void reset(){count_[0]=count_[1]=0;total_[0]=total_[1]=0;overflow_=false;}
};
inline constexpr uint64_t publicationFreshnessNs=5120000000ULL,captureIdleNs=250000000ULL;
// Inactive is a conservative read result. Recording idle additionally needs
// positive, comparable elapsed time; an arrival newer than the UI's sampled now
// must never underflow or trigger a stream generation reset.
inline bool idleElapsed(uint64_t pre,uint64_t post,uint64_t now){return pre&&post&&((pre<=now&&now-pre>=captureIdleNs)||(post<=now&&now-post>=captureIdleNs));}
inline bool active(uint64_t pre,uint64_t post,uint64_t now){return pre&&post&&pre<=now&&post<=now&&now-pre<captureIdleNs&&now-post<captureIdleNs;}
}
