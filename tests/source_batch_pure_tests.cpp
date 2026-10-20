#include "display_sample_history.hpp"
#include "display_wave_gain.hpp"
#include "capture_recovery_policy.hpp"
#include "paired_media_clock.hpp"
#include "measurement.hpp"
#include "latest_analysis_lane.hpp"
#include <iostream>
#include <stdexcept>
#include <cstring>
using namespace pitatuner;
int main(){unsigned count=0;auto check=[&](bool good){++count;if(!good)throw std::runtime_error("batch pure check="+std::to_string(count));};try{
 // Enumerate the complete byte-sized fault space, preserving every other fault.
 for(uint32_t mask=0;mask<256;mask++)check(capture::recovery::refreshable(mask)==(mask==1||mask==64||mask==65));
 for(uint32_t rate:{44100U,48000U}){
  displayonly::SampleHistory history;std::vector<float> pcm(rate/100),backup;const uint64_t origin=10000000000ULL;
  for(unsigned packet=0;packet<600;packet++){
   for(size_t i=0;i<pcm.size();i++)pcm[i]=float(int((packet*pcm.size()+i)%101)-50)/50.f;
   if(packet==599){pcm[17]=-.91f;pcm[19]=.93f;}backup=pcm;
   history.add(0,origin+uint64_t(packet)*10000000ULL,pcm.data(),pcm.size(),rate);
   history.add(1,origin+uint64_t(packet)*10000000ULL,pcm.data(),pcm.size(),rate);check(pcm==backup);
  }
  auto frame=history.snapshot(origin+1000000000ULL,origin+6000000000ULL);
  check(frame->end-frame->begin==5000000000ULL&&frame->droppedChunks==0);
  size_t retained=0;for(auto& chunk:frame->tracks[0])retained+=chunk->samples.size();check(retained==size_t(rate)*5);
  // Independent per-sample oracle at wide and deep zoom, with a fractional-bin
  // left edge: unlike 1 ms pre-aggregation, each impulse belongs to one pixel.
  for(size_t width:{size_t(1),size_t(37),size_t(360),size_t(8192)})for(uint64_t span:{5000000000ULL,16000000ULL,1000000ULL}){
   const uint64_t end=frame->end-1000,begin=end-span;auto actual=displayonly::sampleColumns(*frame,0,begin,end,width);
   std::vector<displayonly::SampleColumn> expected(width);
   for(const auto& chunk:frame->tracks[0])for(size_t i=0;i<chunk->samples.size();i++){
    const uint64_t timestamp=chunk->timestamp+(uint64_t(i)*1000000000ULL)/rate;if(timestamp<begin||timestamp>=end)continue;
    const auto column=size_t((timestamp-begin)*width/span);const float value=chunk->samples[i];auto& e=expected[column];if(!e.valid)e={true,value,value};else{e.lo=std::min(e.lo,value);e.hi=std::max(e.hi,value);}
   }
   for(size_t i=0;i<width;i++)check(actual[i].valid==expected[i].valid&&(!actual[i].valid||(actual[i].lo==expected[i].lo&&actual[i].hi==expected[i].hi)));
  }
  auto retainedSnapshot=frame;const auto oldPeak=displayonly::samplePeak(*frame,0);history.clear(0);check(history.snapshot(frame->begin,frame->end)->tracks[0].empty());check(displayonly::samplePeak(*retainedSnapshot,0)==oldPeak);
 }
 // Two opposite extrema in a single millisecond must remain in distinct pixels.
 displayonly::SampleHistory impulse;float values[48]{};values[1]=-.7f;values[47]=.9f;impulse.add(0,10000000000ULL,values,48,48000);
 auto samples=impulse.snapshot(10000000000ULL,10001000000ULL);auto cols=displayonly::sampleColumns(*samples,0,samples->begin,samples->end,100);check(cols[2].lo==-.7f&&cols[97].hi==.9f);check(cols[0].hi==0&&!cols[1].valid);
 // Nonfinite data never contaminates display gain or erases other finite extrema.
 values[0]=NAN;values[1]=.5f;displayonly::SampleHistory finite;finite.add(0,10000000000ULL,values,48,48000);auto f=finite.snapshot(10000000000ULL,10001000000ULL);auto one=displayonly::sampleColumns(*f,0,f->begin,f->end,1);check(one[0].valid&&std::isfinite(one[0].hi));
 displayonly::Gain gain;for(unsigned i=1;i<300;i++)check(std::isfinite(gain.update(displayonly::samplePeak(*f,0),uint64_t(i)*16000000)));
 // Production NCC: amplitude matching stays outside estimation. Original PCM
 // remains unchanged and a positive timestamp-only delay is measured precisely.
 std::vector<float> pre(96000),post(96000);uint32_t rng=12345;for(size_t i=0;i<pre.size();i++){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;pre[i]=float(int32_t(rng))/4294967296.f*.02f;post[i]=pre[i]*10;}
 const auto before=pre;const auto started=capture::steadyNs();auto e=latency::estimate({pre,1000000000,48000,1,false},{post,1392940000,48000,1,false});check(e.valid&&std::abs(e.delayMs-392.94)<.001&&pre==before);const auto cost=capture::steadyNs()-started;
 std::cout<<"PASS "<<count<<" checks: fault-mask recovery policy, 44.1/48k 5s immutable sample history, exact pixel extrema at deep zoom, display-only gain, NCC nonmutation; NCC single-channel ms="<<double(cost)/1e6<<"; synthetic cloud only, not OBS/Qt/Windows QA\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
