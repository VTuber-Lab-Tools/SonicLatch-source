#include "capture_timeline_policy.hpp"
#include "measurement.hpp"
#include "../integrations/waveform-model/waveform-model-draft/waveform_model.hpp"
#include <iostream>
#include <stdexcept>
#include <cmath>
using namespace pitatuner::capture;
int main(){unsigned checks=0;auto check=[&](bool v){++checks;if(!v)throw std::runtime_error("timeline policy regression");};try{
 const uint64_t anchor=1000000000ULL;uint64_t previous=anchor;
 // Actual PRE timestamp sequence from 18:51:10, offsets only; no audio content.
 const uint64_t offsets[]={0,10326100,20514700,29825200,40077500,50388200,59615600,69940100,80227700,90405900,99680400,109879800,120077300,129753200,139875800,150138100,160413900,169683900,179890400,190102800,200401700,209652400,219830100,230041000,240375300,250622800,259873300,270233800,280335400,290589600,300168000};
 for(size_t i=0;i<std::size(offsets);i++){auto actual=anchor+offsets[i];check(timeline::continuous(actual,anchor+format::frameNs(i*480,48000),previous,i?480:0,48000,true));previous=actual;}
 check(!timeline::continuous(anchor+3000000,anchor,0,0,48000,true));
 check(!timeline::continuous(anchor,anchor,anchor,480,48000,true));
 check(!timeline::continuous(anchor-1,anchor,anchor,480,48000,true));
 check(!timeline::continuous(anchor+20000000,anchor+10000000,anchor,480,48000,true)); // lost 10ms packet
 check(timeline::continuous(anchor+2000000,anchor,0,0,48000,true));
 check(!timeline::continuous(anchor+2000001,anchor,0,0,48000,true));
 // Per-step 0.5ms drift must fail at original-anchor cumulative 2.5ms.
 for(uint64_t i=1;i<=5;i++)check(timeline::continuous(anchor+i*10500000,anchor+i*10000000,anchor+(i-1)*10500000,480,48000,true)==(i<=4));
 for(uint32_t rate:{44100U,48000U})for(uint64_t delta:{0ULL,1ULL,326100ULL,823800ULL,70012345ULL}){auto mapped=format::frameNs(format::nearestFrame(delta,rate),rate);check(timeline::sampleProjection(delta,mapped,rate));}
 check(!timeline::sampleProjection(0,20000,48000));
 check(!timeline::reconfigureReady(true,true,true,0,300000000)); // muted: wait
 check(timeline::reconfigureReady(true,true,false,0,300000000)); // unmute: new binding
 check(!timeline::reconfigureReady(false,true,false,0,300000000));
 check(!timeline::reconfigureReady(true,false,false,0,300000000));
 check(!timeline::reconfigureReady(true,true,false,100000000,349999999));
 check(timeline::reconfigureReady(true,true,false,100000000,350000000));
 std::vector<float> pcm(8192);uint32_t rng=123456;for(auto& x:pcm){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;x=float(int(rng%20001)-10000)/10000.f;}
 for(int delay:{0,20,50,100,200}){auto e=latency::estimate({pcm,1000000000,48000,1,false},{pcm,1000000000+int64_t(delay)*1000000+326100,48000,1,false});check(e.valid&&std::abs(e.delayMs-delay)<=0.327);}
 // Independent firstMedia may differ by 2ms under opposite +/-1ms jitter.
 // Never align PRE/POST by their independently assigned callback sequence.
 for(int delay:{0,20,50}){
  const uint64_t first[2]={1001000000,999000000+uint64_t(delay)*1000000};
  for(size_t side=0;side<2;side++){uint64_t prev=0;for(uint64_t packet=0;packet<100;packet++){
   const int64_t jitter=packet%2?1000000:-1000000;const auto actual=uint64_t(int64_t(1000000000+uint64_t(side*delay)*1000000+packet*10000000)+jitter);
   const auto firstPacket=uint64_t(int64_t(1000000000+uint64_t(side*delay)*1000000)-1000000);
   check(timeline::continuous(actual,firstPacket+packet*10000000,prev,packet?480:0,48000,true));prev=actual;
  }}
  auto e=latency::estimate({pcm,int64_t(first[0]),48000,1,false},{pcm,int64_t(first[1]),48000,1,false});check(e.valid&&std::abs(e.delayMs-delay)<=2.001);
  const auto origin=std::min(first[0],first[1]);pitatuner::waveform::Track tracks[2];
  for(size_t side=0;side<2;side++){auto& track=tracks[side];track.key={"synthetic",1,1};track.rate=48000;track.origin_ns=origin;const auto start=format::nearestFrame(first[side]-origin,48000);check(timeline::sampleProjection(first[side]-origin,format::frameNs(start,48000),48000));for(size_t i=0;i<pcm.size();i++)track.points.push_back({start+i,origin+format::frameNs(start+i,48000),pcm[i],true});}
  const auto end=std::max(tracks[0].points.back().timestamp_ns,tracks[1].points.back().timestamp_ns)+format::sampleCeilNs(48000);
  auto frame=pitatuner::waveform::build({{origin,end,800},std::move(tracks[0]),std::move(tracks[1]),0,0,{}});check(bool(frame)&&frame.series.size()==3);
 }
 latency::SpscRing<int,4> bounded;check(bounded.push(1));check(bounded.push(2));check(bounded.push(3));check(!bounded.push(4)); // overflow remains observable
 std::cout<<"PASS "<<checks<<" timeline/real-trace/known-delay/reconfigure checks; no OBS runtime\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
