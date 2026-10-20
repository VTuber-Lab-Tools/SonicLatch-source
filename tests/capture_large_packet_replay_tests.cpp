#define SONICLATCH_CAPTURE_REPLAY_TEST 1
#include "obs_source_capture.hpp"
#include <iostream>
#include <stdexcept>
using namespace pitatuner;
int main(){unsigned checks=0;auto check=[&](bool good,const char* label){++checks;if(!good)throw std::runtime_error(std::string(label)+" check="+std::to_string(checks));};try{
 using Packet=capture::Binding::ReplayPacket;
 for(uint32_t rate:{44100U,48000U}){
  for(uint32_t frames:{1023U,1024U,1025U,1114U,1152U,2048U,2049U})for(int64_t delay:{-20000000LL,0LL,20000000LL}){
   std::vector<Packet> packets;for(uint64_t i=0;i<12;i++){const auto ts=3000000000+capture::format::frameNs(i*frames,rate);packets.push_back({ts,frames,0,false,frames,uint64_t(int64_t(ts)+delay)});}
   const auto result=capture::Binding::replay(packets,0,12*frames,false,false,rate);
   check(!result.faults&&!result.drops&&result.windows.size()==1,"large callbacks actual copy/ring/analyze accepted");const auto& m=result.windows[0];
   check(m.valid&&m.timelineOk&&m.commonClockMatched&&m.wave,"large callbacks valid measurement and waveform");
   check(m.matchedCallPairs==12&&m.pairedFrames==12*frames,"call identity retained, no fragment pair inflation");
   check(m.captureGeneration==1&&m.preFrames==12*frames&&m.postFrames==12*frames,"generation and frame count retained");
   for(const auto& ch:m.channels)check(std::abs(ch.delayMs-double(delay)/1e6)<1000.0/rate,"D preserved within one sample");
  }
  std::vector<Packet> different;for(uint64_t i=0;i<12;i++)different.push_back({3000000000+capture::format::frameNs(i*1114,rate),1114,0,false,1152,3020000000+capture::format::frameNs(i*1152,rate)});
  const auto unequal=capture::Binding::replay(different,0,8192,false,false,rate);check(!unequal.faults&&!unequal.drops&&unequal.windows.size()==1,"different PRE POST lengths accepted");
  check(unequal.windows[0].valid&&unequal.windows[0].commonClockMatched&&unequal.windows[0].pairedFrames<=8192,"different lengths use actual overlap");check(std::abs(unequal.windows[0].channels[0].delayMs-20)<1000.0/rate,"different lengths preserve D");
  const auto gap=capture::Binding::replay(different,0,8192,false,true,rate);check(gap.windows.size()==1&&!gap.windows[0].valid&&!gap.windows[0].wave,"split sequence gap still fails closed");
  auto bad=different;bad[0].nonfinite=true;const auto nonfinite=capture::Binding::replay(bad,0,8192,false,false,rate);check(nonfinite.windows.size()==1&&!nonfinite.windows[0].valid,"nonfinite still rejects D");
  check(std::string(nonfinite.windows[0].channels[0].reason)=="nonfinite","nonfinite reason retained");
  const auto one=capture::Binding::replay({{3000000000,8192}},20000000,8192,false,false,rate);check(!one.faults&&one.windows.size()==1&&!one.windows[0].valid&&!one.windows[0].clockConfident&&one.windows[0].matchedCallPairs==1,"single callback fragments cannot manufacture clock confidence");
 }
 const auto overflow=capture::Binding::replay({{3000000000,187*1024}},0,8192);check(overflow.faults==uint32_t(capture::Fault::Overflow)&&overflow.drops==1&&overflow.windows.empty(),"oversized atomic callback fails closed as Overflow");
 const auto shape=capture::Binding::replay({{3000000000,0}},0,8192);check(shape.faults==uint32_t(capture::Fault::Shape)&&shape.windows.empty(),"zero shape still stops");
 std::vector<Packet> restart;for(uint64_t i=0;i<12;i++)restart.push_back({3000000000+capture::format::frameNs(i*1152,44100),1152});auto later=restart;for(auto& p:later)p.timestamp+=1000000000;later.front().pauseMs=270;restart.insert(restart.end(),later.begin(),later.end());
 const auto resumed=capture::Binding::replay(restart,20000000,12*1152,false,false,44100);check(!resumed.faults&&resumed.windows.size()==2&&resumed.windows[0].valid&&resumed.windows[1].valid,"large callback idle recovery");check(resumed.windows[0].captureGeneration!=resumed.windows[1].captureGeneration,"generation not shared across idle restart");
 std::cout<<"PASS "<<checks<<" production PRE/copy/batch-ring/analyze large-packet replay checks, 44.1/48k, 1023/1024/1025/1114/1152/2048/2049, unequal lengths, signed D, generation, overflow, nonfinite; declaration-only OBS shim, no OBS runtime\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
