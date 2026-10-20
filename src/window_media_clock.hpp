#pragma once
#include "capture_format.hpp"
#include <array>
#include <algorithm>
#include <cstdint>
#include <optional>
namespace pitatuner::capture::windowclock {
// Worker-only bounded observations; never changes the PCM frame clock per packet.
struct Result{uint64_t origin{},spread{},packets{};bool confident{},discontinuity{};};
class Window {
 std::array<uint64_t,1024> origins_{};size_t count_{};uint64_t previousOrigin_{};uint32_t previousFrames_{};bool overflow_{},jump_{};
public:
 void observe(uint64_t timestamp,uint64_t frameOffset,uint32_t frames,uint32_t rate){
  const auto elapsed=format::frameNs(frameOffset,rate);if(timestamp<elapsed){jump_=true;return;}const auto origin=timestamp-elapsed;
  if(count_){const auto step=origin>previousOrigin_?origin-previousOrigin_:previousOrigin_-origin;const auto gapBoundary=std::max<uint64_t>(5000000,format::frameNs(previousFrames_,rate)/2);if(step>=gapBoundary)jump_=true;}
  previousOrigin_=origin;previousFrames_=frames;
  if(count_==origins_.size()){overflow_=true;return;}origins_[count_++]=origin;
 }
 Result finish()const{
  if(!count_)return{0,0,0,false,true};auto sorted=origins_;std::sort(sorted.begin(),sorted.begin()+count_);const auto spread=sorted[count_-1]-sorted[0];
  // At most 5ms observed intercept spread per tap: pair envelope <=10ms.
  // This is a measurement-quality budget, not proof of no upstream loss.
  return{sorted[count_/2],spread,count_,count_>=2&&!overflow_&&!jump_&&spread<=5000000,jump_};
 }
 void reset(){count_=0;previousOrigin_=0;previousFrames_=0;overflow_=jump_=false;}
};
}
