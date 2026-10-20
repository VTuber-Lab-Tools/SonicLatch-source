#pragma once
#include "capture_format.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <vector>

namespace pitatuner::displayonly {
// Display-only copies. Immutable chunks are shared, never returned to capture,
// estimator, OBS gain or correction. Samples retain their media timestamps.
struct SampleChunk {
 uint64_t timestamp{};uint32_t rate{};std::vector<float> samples;std::optional<double> peak;
 uint64_t end()const{return timestamp+capture::format::frameNs(samples.size(),rate);}
};
struct SampleFrame {
 uint64_t begin{},end{},droppedChunks{};
 std::array<std::vector<std::shared_ptr<const SampleChunk>>,2> tracks;
};
struct SampleColumn {bool valid{};float lo{},hi{};};
inline std::vector<SampleColumn> sampleColumns(const SampleFrame& frame,size_t tap,uint64_t begin,uint64_t end,size_t width){
 width=std::clamp<size_t>(width,1,8192);std::vector<SampleColumn> columns(width);
 if(tap>=2||end<=begin||end-begin>60000000000ULL)return columns;
 for(const auto& chunk:frame.tracks[tap]){
  if(!chunk||!chunk->rate||chunk->timestamp>=end||chunk->end()<=begin)continue;
  // Seek directly to the visible samples, preserving the exact half-open
  // timestamp rule floor(i * 1e9 / rate); no interpolation or PCM mutation.
  const auto first=begin>chunk->timestamp?std::min<uint64_t>(chunk->samples.size(),((begin-chunk->timestamp)*chunk->rate+999999999ULL)/1000000000ULL):0;
  const auto last=end<chunk->end()?std::min<uint64_t>(chunk->samples.size(),((end-chunk->timestamp)*chunk->rate+999999999ULL)/1000000000ULL):chunk->samples.size();
  for(size_t i=first;i<last;++i){
   const auto time=chunk->timestamp+capture::format::frameNs(i,chunk->rate);
   if(time<begin||time>=end)continue;
   const float value=chunk->samples[i];if(!std::isfinite(value))continue;
   // Integer half-open time bins, anchored to the actual visible interval.
   // Max 60 s * 8192 cannot overflow uint64_t; no envelope midpoint remapping.
   const auto at=static_cast<size_t>((time-begin)*width/(end-begin));auto& column=columns[at];
   if(!column.valid)column={true,value,value};else{column.lo=std::min(column.lo,value);column.hi=std::max(column.hi,value);}
  }
 }
 return columns;
}
inline double samplePeak(const SampleFrame& frame,size_t tap){
 if(tap>=2||frame.end<=frame.begin)return 0.;double peak=0.;
 for(const auto& chunk:frame.tracks[tap]){
  if(!chunk||!chunk->rate||chunk->timestamp>=frame.end||chunk->end()<=frame.begin)continue;
  if(chunk->peak&&chunk->timestamp>=frame.begin&&chunk->end()<=frame.end){peak=std::max(peak,*chunk->peak);continue;}
  for(size_t i=0;i<chunk->samples.size();++i){const auto at=chunk->timestamp+capture::format::frameNs(i,chunk->rate);const auto value=chunk->samples[i];if(at>=frame.begin&&at<frame.end&&std::isfinite(value))peak=std::max(peak,std::abs(double(value)));}
 }
 return peak;
}
class SampleHistory {
 static constexpr uint64_t retainedNs=11000000000ULL;
 static constexpr size_t maxChunks=32768,maxSamples=48000*11+1024;
 std::array<std::deque<std::shared_ptr<const SampleChunk>>,2> chunks_;
 std::array<size_t,2> samples_{};std::array<uint64_t,2> newest_{};uint64_t dropped_{};
 void prune(size_t tap){auto& chunks=chunks_[tap];const auto cutoff=newest_[tap]>retainedNs?newest_[tap]-retainedNs:0;
  // Timestamp jitter can reorder chunks. Retain every chunk intersecting 11 s (plus one boundary chunk).
  for(auto it=chunks.begin();it!=chunks.end();)if((*it)->end()<=cutoff){samples_[tap]-=(*it)->samples.size();it=chunks.erase(it);}else ++it;
  while(chunks.size()>maxChunks||samples_[tap]>maxSamples){samples_[tap]-=chunks.front()->samples.size();chunks.pop_front();++dropped_;}
 }
public:
 void clear(size_t tap){chunks_[tap].clear();samples_[tap]=0;newest_[tap]=0;}
 void add(size_t tap,uint64_t timestamp,const float* data,size_t frames,uint32_t rate){
  if(tap>=2||!data||!frames||frames>1024||!capture::format::supportedRate(rate))return;
  const auto duration=capture::format::frameNs(frames,rate);if(timestamp>UINT64_MAX-duration)return;
  auto chunk=std::make_shared<SampleChunk>();chunk->timestamp=timestamp;chunk->rate=rate;chunk->samples.assign(data,data+frames);double peak=0.;for(float value:chunk->samples)if(std::isfinite(value))peak=std::max(peak,std::abs(double(value)));chunk->peak=peak;
  newest_[tap]=std::max(newest_[tap],chunk->end());samples_[tap]+=frames;chunks_[tap].push_back(std::move(chunk));prune(tap);
 }
 std::shared_ptr<const SampleFrame> snapshot(uint64_t begin,uint64_t end,uint64_t lookbackNs=0)const{
  auto frame=std::make_shared<SampleFrame>();frame->begin=begin;frame->end=end;frame->droppedChunks=dropped_;
  // Extra retained samples support display offsets without widening the viewport.
  const auto chunkBegin=begin>lookbackNs?begin-lookbackNs:0;
  for(size_t tap=0;tap<2;++tap)for(const auto& chunk:chunks_[tap])if(chunk->end()>chunkBegin&&chunk->timestamp<end)frame->tracks[tap].push_back(chunk);
  return frame;
 }
};
}
