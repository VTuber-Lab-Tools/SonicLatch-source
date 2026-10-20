#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace pitatuner::qt::waveb {
// Display coordinates only. No PCM resampling, interpolation, or correction API.
class Viewport {
 double durationMs_{}, minimumFraction_{1}, start_{}, span_{1},initialSpanMs_{};
public:
 static constexpr std::size_t minimumVisibleBins = 16;
 explicit Viewport(double initialSpanMs=0):initialSpanMs_(initialSpanMs){}
 void clear() { durationMs_=0;minimumFraction_=1;start_=0;span_=1; }
 bool ready() const { return durationMs_>0; }
 bool update(double durationMs,double widestBinMs) {
  if(!std::isfinite(durationMs)||durationMs<=0||!std::isfinite(widestBinMs)||widestBinMs<=0||widestBinMs>durationMs){clear();return false;}
  const bool initial=!ready();durationMs_=durationMs;
  if(initial&&initialSpanMs_>0){span_=std::min(1.,initialSpanMs_/durationMs_);start_=1.-span_;}
  // Keep at least 16 source-bin widths visible; coarsest plotted series wins.
  minimumFraction_=std::min(1.,double(minimumVisibleBins)*(widestBinMs/durationMs));
  const auto next=std::clamp(span_,minimumFraction_,1.);
  if(next!=span_)start_+=(span_-next)*.5;
  span_=next;start_=std::clamp(start_,0.,1.-span_);
  return true;
 }
 bool zoom(int angleDeltaY,double cursorFraction) {
  if(!ready()||!angleDeltaY||!std::isfinite(cursorFraction))return false;
  const auto anchor=std::clamp(cursorFraction,0.,1.);
  const auto steps=std::clamp(double(angleDeltaY)/120.,-128.,128.);
  // One standard wheel notch: 1.25x magnification (80% of current time span).
  const auto next=std::clamp(span_*std::pow(.8,steps),minimumFraction_,1.);
  if(next==span_)return false;
  const auto fixed=start_+anchor*span_;
  start_=std::clamp(fixed-anchor*next,0.,1.-next);span_=next;
  return true;
 }
 double lowerMs() const { return start_*durationMs_; }
 double upperMs() const { return (start_+span_)*durationMs_; }
 double minimumSpanMs() const { return minimumFraction_*durationMs_; }
 double magnification() const { return 1./span_; }
};

// Increase only the existing renderer's reduction budget, never its source data.
// At maximum detail geometry() emits the original bins with their actual centers.
inline double detailWidth(double fullMs,double visibleMs,double pixels,std::size_t bins) {
 if(!std::isfinite(fullMs)||fullMs<=0||!std::isfinite(visibleMs)||visibleMs<=0||!std::isfinite(pixels)||pixels<=0||!bins)return 0;
 const auto originalBinWidth=3.*double(bins);
 const auto enlarged=pixels*std::max(1.,fullMs/visibleMs);
 return std::min(originalBinWidth,enlarged);
}
} // namespace pitatuner::qt::waveb
