#pragma once
#include <cstdint>
namespace pitatuner::displayonly {
// Drawing only: no capture, estimator, owner timer, authority or write actions.
class FrameCadence {
 unsigned fps_{60};uint64_t next_{};
public:
 unsigned fps()const{return fps_;}
 void setFps(unsigned fps){fps_=fps==30?30:60;reset();}
 void reset(){next_=0;}
 bool due(uint64_t now){
  if(next_&&now<next_)return false;
  const uint64_t period=1000000000ULL/fps_;
  // Keep fractional cadence across a 16 ms owner timer. Restart only after
  // a genuine stall; do not repay missed frames with a burst.
  if(!next_||now-next_>=period)next_=now+period;else next_+=period;
  return true;
 }
};
}
