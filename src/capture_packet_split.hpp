#pragma once
#include "measurement.hpp"
#include "capture_format.hpp"
#include <cstring>
namespace pitatuner::capture::packet {
// No allocation or PCM conversion on the callback thread. The storage limit is
// still 1024 frames per ring slot, independently of the OBS callback size.
inline constexpr uint32_t blockFrames=1024;
enum class Result {Accepted,Shape,Overflow};
inline bool valid(const uint8_t*const* planes,uint32_t frames,uint64_t timestamp,uint32_t rate){
 return frames&&planes&&planes[0]&&planes[1]&&format::supportedRate(rate)&&
        format::frameNs(frames,rate)<=UINT64_MAX-timestamp;
}
// A fragment's timestamp always comes from the original callback sample clock.
// Sub-slicing/discarding a fragment must not repeatedly add rounded durations.
inline uint64_t advance(uint64_t timestamp,uint64_t callbackOffset,uint64_t count,uint32_t rate){
 return timestamp+(format::frameNs(callbackOffset+count,rate)-format::frameNs(callbackOffset,rate));
}
template<class Ring,class Fill>
Result enqueue(Ring& ring,const uint8_t*const* planes,uint32_t frames,uint64_t timestamp,uint32_t rate,uint64_t& sequence,Fill fill){
 if(!valid(planes,frames,timestamp,rate))return Result::Shape;
 const size_t chunks=(uint64_t(frames)+blockFrames-1)/blockFrames;
 if(chunks>UINT64_MAX-sequence)return Result::Shape;
 if(!ring.writeBatch(chunks,[&](latency::AudioBlock& block,size_t index){
  const auto offset=uint32_t(index*blockFrames);const auto count=std::min(blockFrames,frames-offset);
  fill(block);block.frames=count;block.sequence=sequence+index;block.invocationFrameOffset=offset;
  block.mediaTimestampNs=timestamp+format::frameNs(offset,rate);
  for(size_t ch=0;ch<2;ch++)std::memcpy(block.pcm[ch].data(),planes[ch]+size_t(offset)*sizeof(float),size_t(count)*sizeof(float));
 }))return Result::Overflow;
 sequence+=chunks;return Result::Accepted;
}
}
