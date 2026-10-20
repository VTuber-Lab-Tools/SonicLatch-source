#pragma once
#include "display_sample_history.hpp"
namespace pitatuner::displayonly {
// Display coordinates only. Never mutate samples, capture timestamps or audio.
inline std::optional<uint64_t> shiftedTime(uint64_t time,int64_t shift){
 if(shift>=0){const auto n=uint64_t(shift);if(time>UINT64_MAX-n)return {};return time+n;}
 const auto n=uint64_t(-(shift+1))+1;if(time<n)return {};return time-n;
}
inline std::optional<uint64_t> unshiftedTime(uint64_t time,int64_t shift){
 if(shift>=0){const auto n=uint64_t(shift);if(time<n)return {};return time-n;}
 const auto n=uint64_t(-(shift+1))+1;if(time>UINT64_MAX-n)return {};return time+n;
}
struct DisplayWindow {uint64_t begin{},end{};};
inline std::optional<DisplayWindow> displayWindow(uint64_t begin,uint64_t end,int64_t postShift){
 if(end<=begin)return {};
 const auto lag=std::min<int64_t>(postShift,0);
 const auto first=shiftedTime(begin,lag),last=shiftedTime(end,lag);
 // Both taps share this exact interval. An unrepresentable interval is blank;
 // never clamp one edge, change the lag or stretch the remaining timestamps.
 if(!first||!last)return {};
 return DisplayWindow{*first,*last};
}
inline std::vector<SampleColumn> offsetSampleColumns(const SampleFrame& frame,size_t tap,uint64_t begin,uint64_t end,size_t width,int64_t shift){
 width=std::clamp<size_t>(width,1,8192);
 const auto rawBegin=unshiftedTime(begin,shift),rawEnd=unshiftedTime(end,shift);
 // Equal translations preserve interval size and the exact common pixel bins.
 if(rawBegin&&rawEnd)return sampleColumns(frame,tap,*rawBegin,*rawEnd,width);
 std::vector<SampleColumn> out(width);if(tap>=2||end<=begin||end-begin>60000000000ULL)return out;
 // Only the uint64 edges need clipping. Do not stretch a clipped raw interval.
 for(const auto& chunk:frame.tracks[tap])if(chunk&&chunk->rate){for(size_t i=0;i<chunk->samples.size();++i){
  const auto offset=capture::format::frameNs(i,chunk->rate);if(chunk->timestamp>UINT64_MAX-offset)continue;
  const auto time=shiftedTime(chunk->timestamp+offset,shift);if(!time||*time<begin||*time>=end)continue;
  const float value=chunk->samples[i];if(!std::isfinite(value))continue;
  auto& column=out[static_cast<size_t>((*time-begin)*width/(end-begin))];
  if(!column.valid)column={true,value,value};else{column.lo=std::min(column.lo,value);column.hi=std::max(column.hi,value);}
 }}return out;
}
}
