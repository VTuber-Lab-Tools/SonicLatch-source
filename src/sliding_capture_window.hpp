#pragma once
#include "measurement.hpp"
#include "capture_format.hpp"
#include "capture_packet_split.hpp"
#include "window_media_clock.hpp"
#include "paired_media_clock.hpp"
#include <deque>
#include <stdexcept>
namespace pitatuner::capture::sliding {
inline constexpr uint64_t windowNs=2000000000ULL,hopNs=250000000ULL;
inline size_t frames(uint32_t rate){return format::windowFrames(rate);}
inline size_t hop(uint32_t rate){return size_t(rate)/4;}
struct Segment {uint64_t timestamp{},arrival{},call{};uint32_t frames{};bool gap{},muted{};uint64_t callFrameOffset{};};
class Tap {
 size_t capacity_;uint32_t rate_;bool overflow_{};
public:
 std::array<std::vector<float>,2> pcm;std::deque<Segment> metadata;uint64_t total{};
 Tap(size_t capacity,uint32_t rate):capacity_(capacity),rate_(rate){for(auto& p:pcm)p.reserve(capacity);}
 size_t size()const{return pcm[0].size();}bool full()const{return size()==capacity_;}bool overflow()const{return overflow_;}
 void clear(){for(auto& p:pcm)p.clear();metadata.clear();overflow_=false;}
 void append(const latency::AudioBlock& block,size_t offset,size_t count,bool gap){
  if(count>capacity_-size())throw std::logic_error("sliding capacity");
  for(size_t c=0;c<2;c++)pcm[c].insert(pcm[c].end(),block.pcm[c].begin()+offset,block.pcm[c].begin()+offset+count);
  if(metadata.size()<1024)metadata.push_back({packet::advance(block.mediaTimestampNs,block.invocationFrameOffset,offset,rate_),block.arrivalMonotonicNs,block.invocationId,static_cast<uint32_t>(count),gap,block.muted,uint64_t(block.invocationFrameOffset)+offset});else overflow_=true;
  total+=count;
 }
 void discard(size_t count){
  if(count>size())throw std::logic_error("sliding discard");for(auto& p:pcm)p.erase(p.begin(),p.begin()+count);
  while(count&&!metadata.empty()){auto& m=metadata.front();const auto n=std::min<size_t>(count,m.frames);count-=n;if(n==m.frames)metadata.pop_front();else{m.timestamp=packet::advance(m.timestamp,m.callFrameOffset,n,rate_);m.callFrameOffset+=n;m.frames-=static_cast<uint32_t>(n);m.gap=false;}}
  // Metadata overflow cannot be repaired by inventing timestamps. Remain invalid
  // until the next explicit stream/session clear.
 }
 void observe(size_t side,windowclock::Window& clock,pairedclock::Window& pair)const{
  size_t offset=0,groupOffset=0;const Segment* first=nullptr;uint64_t previousEnd=0;uint32_t groupFrames=0;
  for(const auto& m:metadata){
   // Split fragments are one original clock observation. Counting each as an
   // independent packet would manufacture confidence from a single callback.
   if(first&&(m.call!=first->call||m.callFrameOffset!=previousEnd)){
    clock.observe(first->timestamp,groupOffset,groupFrames,rate_);first=nullptr;
   }
   if(!first){first=&m;groupOffset=offset;groupFrames=0;}
   groupFrames+=m.frames;previousEnd=m.callFrameOffset+m.frames;
   pair.observe(side,{m.call,m.timestamp,offset,m.frames,m.callFrameOffset});offset+=m.frames;
  }
  if(first)clock.observe(first->timestamp,groupOffset,groupFrames,rate_);
 }
 bool gap()const{return overflow_||std::any_of(metadata.begin(),metadata.end(),[](auto& x){return x.gap;});}
 bool muted()const{return std::any_of(metadata.begin(),metadata.end(),[](auto& x){return x.muted;});}
 uint64_t firstArrival()const{return metadata.empty()?0:metadata.front().arrival;}
 uint64_t lastArrival()const{return metadata.empty()?0:metadata.back().arrival;}
};
}
