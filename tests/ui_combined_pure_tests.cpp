#include "display_sample_history.hpp"
#include "display_offset_columns.hpp"
#include "live_capture_wave.hpp"
#include "waveform_viewport.hpp"
#include "waveform_cadence.hpp"
#include "sliding_capture_window.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace pitatuner;
int main(){unsigned checks=0;auto check=[&](bool yes){++checks;if(!yes)throw std::runtime_error("ui combined pure check="+std::to_string(checks));};try{
 // Preserve the analysis window and hop; display time does not configure them.
 check(capture::sliding::frames(48000)==96000&&capture::sliding::hop(48000)==12000);
 check(capture::sliding::frames(44100)==88200&&capture::sliding::hop(44100)==11025);
 qt::waveb::Viewport view(5000);check(view.update(10000,1));check(view.lowerMs()==5000&&view.upperMs()==10000);
 check(view.zoom(-120,.5)&&std::abs(view.upperMs()-view.lowerMs()-6250)<.0001);
 for(unsigned i=0;i<50;++i)view.zoom(-120,.5);check(view.lowerMs()==0&&view.upperMs()==10000);check(!view.zoom(-120,.5));
 for(unsigned i=0;i<50;++i)view.zoom(120,.5);check(std::abs(view.upperMs()-view.lowerMs()-16)<.0001);
 const auto low=view.lowerMs();view.update(10000,1);check(view.lowerMs()==low);view.clear();view.update(10000,1);check(view.lowerMs()==5000&&view.upperMs()==10000);
 for(unsigned fps:{60U,30U}){displayonly::FrameCadence cadence;check(cadence.fps()==60);cadence.setFps(fps);unsigned draws=0;
  for(uint64_t now=1000000000;now<6000000000;now+=16000000)if(cadence.due(now))++draws;
  check(draws>=fps*5-1&&draws<=fps*5+1);check(cadence.due(20000000000ULL));check(!cadence.due(20000000001ULL));cadence.reset();check(cadence.due(20000000002ULL));
 }
 for(uint32_t rate:{44100U,48000U}){
  displayonly::SampleHistory history;std::vector<float> pcm(rate/100),backup;const uint64_t origin=10000000000ULL;
  for(unsigned packet=0;packet<1200;++packet){for(size_t i=0;i<pcm.size();++i)pcm[i]=float(int((packet*pcm.size()+i)%101)-50)/53;backup=pcm;history.add(0,origin+uint64_t(packet)*10000000,pcm.data(),pcm.size(),rate);history.add(1,origin+uint64_t(packet)*10000000,pcm.data(),pcm.size(),rate);check(pcm==backup);}
  auto frame=history.snapshot(origin+2000000000,origin+12000000000);size_t samples=0;for(const auto& chunk:frame->tracks[0])samples+=chunk->samples.size();check(samples==size_t(rate)*10&&frame->droppedChunks==0);check(history.snapshot(origin,origin+1000000000)->tracks[0].empty());
  for(size_t width:{size_t(1),size_t(37),size_t(360),size_t(8192)})for(uint64_t span:{9999999000ULL,5000000000ULL,16000000ULL,1000000ULL}){
   const uint64_t end=frame->end-1000,begin=end-span;const auto actual=displayonly::sampleColumns(*frame,0,begin,end,width);std::vector<displayonly::SampleColumn> oracle(width);double peak=0;
   for(const auto& chunk:frame->tracks[0])for(size_t i=0;i<chunk->samples.size();++i){const uint64_t stamp=chunk->timestamp+(uint64_t(i)*1000000000)/rate;if(stamp<begin||stamp>=end)continue;const float value=chunk->samples[i];const auto at=size_t((stamp-begin)*width/span);auto& column=oracle[at];if(!column.valid)column={true,value,value};else{column.lo=std::min(column.lo,value);column.hi=std::max(column.hi,value);}peak=std::max(peak,std::abs(double(value)));}
   for(size_t i=0;i<width;++i)check(actual[i].valid==oracle[i].valid&&(!actual[i].valid||(actual[i].lo==oracle[i].lo&&actual[i].hi==oracle[i].hi)));
   auto partial=*frame;partial.begin=begin;partial.end=end;check(displayonly::samplePeak(partial,0)==peak);
  }
  const auto oldPeak=displayonly::samplePeak(*frame,0);history.clear(0);check(history.snapshot(frame->begin,frame->end)->tracks[0].empty());check(displayonly::samplePeak(*frame,0)==oldPeak);
 }
 // Raw POST is offset only in display coordinates, with exact shared bins.
 for(uint32_t rate:{44100U,48000U})for(uint64_t base:{0ULL,10000000000ULL}){
  displayonly::SampleFrame frame;frame.begin=base;frame.end=base+2000000000ULL;
  auto pre=std::make_shared<displayonly::SampleChunk>();pre->timestamp=base;pre->rate=rate;pre->samples.resize(rate/100);
  for(size_t i=0;i<pre->samples.size();++i)pre->samples[i]=i%17==0?.8f:-.2f;
  auto post=std::make_shared<displayonly::SampleChunk>(*pre);post->timestamp+=393000000ULL;frame.tracks[0].push_back(pre);frame.tracks[1].push_back(post);const auto saved=post->samples;
  for(size_t width:{size_t(37),size_t(1000),size_t(8192)}){
   const auto before=displayonly::sampleColumns(frame,0,base,frame.end,width);const auto aligned=displayonly::offsetSampleColumns(frame,1,base,frame.end,width,-393000000);
   for(size_t i=0;i<width;++i)check(before[i].valid==aligned[i].valid&&(!before[i].valid||(before[i].lo==aligned[i].lo&&before[i].hi==aligned[i].hi)));
   for(int64_t shift:{int64_t(-393000000),int64_t(0),int64_t(100000000)}){
    const auto actual=displayonly::offsetSampleColumns(frame,1,base,frame.end,width,shift);std::vector<displayonly::SampleColumn> oracle(width);
    for(size_t i=0;i<post->samples.size();++i){const auto time=displayonly::shiftedTime(post->timestamp+uint64_t(i)*1000000000ULL/rate,shift);if(!time||*time<base||*time>=frame.end)continue;auto& c=oracle[size_t((*time-base)*width/(frame.end-base))];const float v=post->samples[i];if(!c.valid)c={true,v,v};else{c.lo=std::min(c.lo,v);c.hi=std::max(c.hi,v);}}
    for(size_t i=0;i<width;++i)check(actual[i].valid==oracle[i].valid&&(!actual[i].valid||(actual[i].lo==oracle[i].lo&&actual[i].hi==oracle[i].hi)));
   }
  }
  check(post->samples==saved&&post->timestamp==base+393000000ULL);check(frame.tracks[0][0]==pre&&frame.tracks[1][0]==post);
 }
 check(!displayonly::shiftedTime(0,-1));check(!displayonly::shiftedTime(UINT64_MAX,1));
 check(displayonly::shiftedTime(uint64_t(INT64_MAX)+1,INT64_MIN)==0);check(!displayonly::unshiftedTime(UINT64_MAX,-1));
 check(displayonly::unshiftedTime(0,INT64_MIN)==uint64_t(INT64_MAX)+1);
 // Production display snapshot retains eleven seconds behind ten-second frame
 // bounds; the existing envelope stays five seconds. Use the production heap.
 auto live=std::make_unique<capture::livewave::Window>(120,5000);std::array<float,480> pcm{};
 for(unsigned i=0;i<1100;++i)for(size_t tap=0;tap<2;++tap)live->add(tap,10000000000ULL+uint64_t(i)*10000000,pcm.data(),pcm.size(),48000);
 auto envelope=live->publish();auto raw=live->sampleSnapshot(envelope.axis);check(envelope.axis.end_ns-envelope.axis.begin_ns==5000000000ULL);check(raw->end-raw->begin==10000000000ULL);size_t retained=0;for(auto& c:raw->tracks[0])retained+=c->samples.size();check(retained==528000);
 std::cout<<"PASS "<<checks<<" display-offset/nonmutation/shared-bin + combined UI pure checks: 11s history/10s viewport/5s default, exact 44.1/48k columns/peaks, 30/60 cadence, 2s/250ms invariants; not Qt or Windows\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
