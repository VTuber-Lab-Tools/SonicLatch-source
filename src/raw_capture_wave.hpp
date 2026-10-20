#pragma once
#include "live_capture_wave.hpp"
namespace pitatuner::capture::rawwave {
// Retain actual channel-0 samples only, every 32 source frames. No min/max,
// normalization, smoothing, filtering, D/offset shift or invented animation.
class Window {
 struct Point{uint64_t timestamp{},index{};float value{};};
 static constexpr size_t capacity=4096,step=32;std::array<std::array<Point,capacity>,2> points_{};
 uint64_t samples_[2]{},count_[2]{},nextPublish_{},newest_[2]{};
public:
 void clear(size_t tap){samples_[tap]=count_[tap]=newest_[tap]=0;nextPublish_=0;}
 void add(size_t tap,uint64_t timestamp,const float* pcm,size_t frames,uint32_t rate){
  for(size_t i=0;i<frames;i++)if((samples_[tap]+i)%step==0){const auto delta=format::frameNs(i,rate);if(delta>UINT64_MAX-timestamp)continue;const auto time=timestamp+delta;points_[tap][count_[tap]%capacity]={time,samples_[tap]+i,pcm[i]};++count_[tap];newest_[tap]=std::max(newest_[tap],time);}
  samples_[tap]+=frames;
 }
 bool ready(uint32_t)const{return count_[0]&&count_[1]&&std::min(samples_[0],samples_[1])>=nextPublish_;}
 waveform::Frame publish(uint32_t rate){
  const auto have=std::min(samples_[0],samples_[1]);const auto stride=std::max<uint64_t>(1,rate/60);
  if(!nextPublish_)nextPublish_=have+stride;else do{nextPublish_+=stride;}while(nextPublish_<=have);
  const auto maxTime=std::max(newest_[0],newest_[1]);const auto end=maxTime<=UINT64_MAX-format::sampleCeilNs(rate)?maxTime+format::sampleCeilNs(rate):maxTime;
  const auto begin=end>2000000000ULL?end-2000000000ULL:0;waveform::Frame frame;frame.axis={begin,end,capacity};
  for(size_t t=0;t<2;t++){waveform::Series series{};series.kind=t?waveform::Kind::PostMeasured:waveform::Kind::PreMeasured;const auto n=std::min<uint64_t>(count_[t],capacity);series.bins.reserve(n);
   for(auto i=count_[t]-n;i<count_[t];i++){const auto& p=points_[t][i%capacity];if(p.timestamp<begin||p.timestamp>=end)continue;waveform::Bin bin{};bin.begin_ns=p.timestamp;bin.end_ns=p.timestamp<=UINT64_MAX-format::sampleCeilNs(rate)?std::min(end,p.timestamp+format::sampleCeilNs(rate)):end;if(std::isfinite(p.value)){bin.finite=1;bin.minimum=bin.maximum=p.value;}else bin.invalid=1;series.bins.push_back(bin);}frame.series.push_back(std::move(series));}
  return frame;
 }
};
}
