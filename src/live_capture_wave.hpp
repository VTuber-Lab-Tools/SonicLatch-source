#pragma once
#include "capture_format.hpp"
#include "display_sample_history.hpp"
#include "../integrations/waveform-model/waveform-model-draft/waveform_model.hpp"
#include <array>
#include <cmath>
#include <algorithm>
#include <limits>

namespace pitatuner::capture::livewave {
// Worker-owned, bounded raw-media-time envelope. Every input sample contributes;
// the 1 ms cells are display aggregation, never interpolation or peak alignment.
class Window {
 static constexpr uint64_t cellNs=1000000, cells=8192;
 struct Cell {uint64_t slot=UINT64_MAX,count{},invalid{};float lo{},hi{};};
 std::array<std::array<Cell,cells>,2> data_{};displayonly::SampleHistory samplesHistory_;
 uint64_t newest_[2]{},samples_[2]{},publishedSamples_[2]{};uint32_t publishHz_;size_t visibleCells_;
public:
 explicit Window(uint32_t hz=20,size_t visible=2560):publishHz_(std::clamp<uint32_t>(hz,1,120)),visibleCells_(std::clamp<size_t>(visible,1,cells)){}
 void clear(size_t tap){samplesHistory_.clear(tap);for(auto& c:data_[tap])c={};newest_[tap]=samples_[tap]=publishedSamples_[tap]=0;}
 void add(size_t tap,uint64_t timestamp,const float* pcm,size_t frames,uint32_t rate){
  samplesHistory_.add(tap,timestamp,pcm,frames,rate);
  for(size_t i=0;i<frames;i++){
   const auto delta=format::frameNs(i,rate);if(delta>UINT64_MAX-timestamp)continue;
   const auto slot=(timestamp+delta)/cellNs;if(slot>=UINT64_MAX/cellNs)continue;auto& c=data_[tap][slot%cells];
   if(c.slot!=slot)c={slot};
   const auto value=pcm[i];if(!std::isfinite(value)){++c.invalid;continue;}
   if(!c.count)c.lo=c.hi=value;else{c.lo=std::min(c.lo,value);c.hi=std::max(c.hi,value);}++c.count;
   newest_[tap]=std::max(newest_[tap],slot+1);
  }
  samples_[tap]+=frames;
 }
 bool ready(uint32_t rate)const{return newest_[0]&&newest_[1]&&
  (publishedSamples_[0]==0||std::min(samples_[0]-publishedSamples_[0],samples_[1]-publishedSamples_[1])>=rate/publishHz_);}
 // Raw display can zoom out to 10 s, with 1 s of older samples for offsets.
 // Frame bounds, estimator and 1 ms envelope publication remain unchanged.
 std::shared_ptr<const displayonly::SampleFrame> sampleSnapshot(waveform::Axis axis)const{return samplesHistory_.snapshot(axis.end_ns>10000000000ULL?axis.end_ns-10000000000ULL:0,axis.end_ns,1000000000ULL);}
 waveform::Frame publish(uint32_t unusedRate=0){
  const auto end=std::max(newest_[0],newest_[1]);const auto begin=end>visibleCells_?end-visibleCells_:0;
  waveform::Frame f;f.axis={begin*cellNs,end*cellNs,static_cast<size_t>(end-begin)};
  for(size_t t=0;t<2;t++){
   waveform::Series series{};series.kind=t?waveform::Kind::PostMeasured:waveform::Kind::PreMeasured;series.bins.reserve(end-begin);
   for(auto slot=begin;slot<end;slot++){
    waveform::Bin b{};b.begin_ns=slot*cellNs;b.end_ns=(slot+1)*cellNs;const auto& c=data_[t][slot%cells];
    if(c.slot==slot){b.finite=c.count;b.invalid=c.invalid;if(c.count){b.minimum=c.lo;b.maximum=c.hi;}}
    series.bins.push_back(b);
   }
   f.series.push_back(std::move(series));publishedSamples_[t]=samples_[t];
  }
  return f;
 }
};
}
