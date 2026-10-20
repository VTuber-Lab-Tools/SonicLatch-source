#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace pitatuner::displayonly {
// UI-only multiplier. Never shared with capture, NCC, correction or backend.
class Gain {
 double value_=1;uint64_t updated_{};bool aboveFloor_{};
public:
 void reset(){value_=1;updated_=0;aboveFloor_=false;}
 double update(double peak,uint64_t now){
  if(!std::isfinite(peak)||peak<.0001)aboveFloor_=false;else if(peak>=.0002)aboveFloor_=true;
  const double target=aboveFloor_?std::clamp(.8/peak,1./64,64.):1.;
  const double seconds=updated_&&now>=updated_?std::min(1.,double(now-updated_)/1e9):1./60;
  const double tau=target<value_?.060:.350;const double alpha=1-std::exp(-seconds/tau);
  value_=std::clamp(std::exp(std::log(value_)+(std::log(target)-std::log(value_))*alpha),1./64,64.);updated_=now;return value_;
 }
 double value()const{return value_;}
};
}
