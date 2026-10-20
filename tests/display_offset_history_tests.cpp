#include "display_offset_columns.hpp"
#include "live_capture_wave.hpp"
#include <iostream>
#include <stdexcept>
using namespace pitatuner;
int main(){unsigned checks=0;auto check=[&](bool ok){++checks;if(!ok)throw std::runtime_error("display history/offset check="+std::to_string(checks));};try{
 for(uint32_t rate:{44100U,48000U}){
  auto live=std::make_unique<capture::livewave::Window>(120,5000);std::vector<float> pcm(rate/100);const uint64_t origin=10000000000ULL;
  for(unsigned packet=0;packet<1300;++packet){for(size_t i=0;i<pcm.size();++i)pcm[i]=float(int((packet*pcm.size()+i)%71)-35)/40.f;const auto backup=pcm;for(size_t tap=0;tap<2;++tap)live->add(tap,origin+uint64_t(packet)*10000000,pcm.data(),pcm.size(),rate);check(pcm==backup);}
  const auto envelope=live->publish();const auto frame=live->sampleSnapshot(envelope.axis);check(envelope.axis.end_ns-envelope.axis.begin_ns==5000000000ULL);check(frame->begin==origin+3000000000ULL&&frame->end==origin+13000000000ULL&&frame->droppedChunks==0);
  for(size_t tap=0;tap<2;++tap){size_t count=0;for(const auto& chunk:frame->tracks[tap])count+=chunk->samples.size();check(count==size_t(rate)*11);check(frame->tracks[tap].front()->timestamp==origin+2000000000ULL);}
  for(int64_t shift:{-950000000LL,0LL,1000000000LL,1500000000LL}){
   const auto range=displayonly::displayWindow(frame->begin,frame->end,shift);check(bool(range));const auto lag=std::min<int64_t>(shift,0);check(range->begin==uint64_t(int64_t(frame->begin)+lag)&&range->end==uint64_t(int64_t(frame->end)+lag));check(range->end-range->begin==10000000000ULL);
   for(size_t width:{size_t(37),size_t(1000),size_t(8192)})for(size_t tap=0;tap<2;++tap){
    const auto columns=displayonly::offsetSampleColumns(*frame,tap,range->begin,range->end,width,tap?shift:0);std::vector<displayonly::SampleColumn> oracle(width);
    // Independent signed arithmetic oracle. Fixture timestamps safely fit int64.
    const int64_t left=int64_t(frame->begin)+lag,right=int64_t(frame->end)+lag;
    for(const auto& chunk:frame->tracks[tap])for(size_t i=0;i<chunk->samples.size();++i){const int64_t time=int64_t(chunk->timestamp)+int64_t(uint64_t(i)*1000000000ULL/rate)+(tap?shift:0);if(time<left||time>=right)continue;const auto at=size_t(uint64_t(time-left)*width/uint64_t(right-left));auto& c=oracle[at];const float value=chunk->samples[i];if(!c.valid)c={true,value,value};else{c.lo=std::min(c.lo,value);c.hi=std::max(c.hi,value);}}
    for(size_t i=0;i<width;++i)check(columns[i].valid==oracle[i].valid&&(!columns[i].valid||(columns[i].lo==oracle[i].lo&&columns[i].hi==oracle[i].hi)));
    if(width==1000){const size_t blank=tap&&shift==1500000000LL?50:0;for(size_t i=0;i<width;++i)check(columns[i].valid==(i>=blank));std::cout<<"EDGE rate="<<rate<<" S_ns="<<shift<<" tap="<<tap<<" left_blank_ms="<<blank*10<<" right_blank_ms=0 span_ms=10000\n";}
   }
  }
  // A boundary chunk can contribute up to 1023 samples beyond the 11 s cutoff.
  // The capacity must not evict that chunk or record a capacity drop.
  displayonly::SampleHistory boundary;std::array<float,1024> full{};full.fill(.25f);
  for(uint64_t i=0;i<700;++i)boundary.add(0,origin+capture::format::frameNs(i*full.size(),rate),full.data(),full.size(),rate);
  const auto end=origin+capture::format::frameNs(700*full.size(),rate);auto retained=boundary.snapshot(end-10000000000ULL,end,1000000000ULL);size_t count=0;for(const auto& chunk:retained->tracks[0])count+=chunk->samples.size();check(count>=size_t(rate)*11&&count<size_t(rate)*11+1024&&retained->droppedChunks==0);check(retained->begin==end-10000000000ULL&&retained->end==end);
  check(boundary.snapshot(origin,end-11000000000ULL)->tracks[0].size()<=1);
  // The extra chunk lookback does not affect the public frame/peak interval.
  displayonly::SampleHistory levels;float high=1.f,low=.25f;levels.add(0,origin-1000000000ULL,&high,1,rate);levels.add(0,origin,&low,1,rate);auto viewed=levels.snapshot(origin,origin+1000000000ULL,1000000000ULL);check(viewed->tracks[0].size()==2&&displayonly::samplePeak(*viewed,0)==.25);
 }
 // Keep the exact common interval or reject it; never wrap or clamp one end.
 check(!displayonly::displayWindow(0,10000000000ULL,-950000000));check(!displayonly::displayWindow(949999999,10000000000ULL,-950000000));
 const auto zero=displayonly::displayWindow(950000000,10950000000ULL,-950000000);check(zero&&zero->begin==0&&zero->end==10000000000ULL);
 check(!displayonly::displayWindow(0,UINT64_MAX,INT64_MIN));const auto low=displayonly::displayWindow(uint64_t(INT64_MAX)+1,UINT64_MAX,INT64_MIN);check(low&&low->begin==0&&low->end==uint64_t(INT64_MAX));
 const auto high=displayonly::displayWindow(UINT64_MAX-10000000000ULL,UINT64_MAX,INT64_MAX);check(high&&high->end-high->begin==10000000000ULL&&high->end==UINT64_MAX);
 check(!displayonly::displayWindow(1,1,0));check(!displayonly::displayWindow(2,1,0));
 std::cout<<"PASS "<<checks<<" production display-window/history/columns checks: 44.1/48k, 11s retention, 10s bounds, negative/positive S, 1024-frame boundary capacity, checked uint64 edges; synthetic cloud C++, not Qt/OBS/Windows\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
