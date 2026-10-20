#include "capture_packet_split.hpp"
#include "sliding_capture_window.hpp"
#include <bit>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
using namespace pitatuner::capture;
namespace {
unsigned checks=0;
void check(bool good,const char* label){++checks;if(!good)throw std::runtime_error(std::string(label)+" check="+std::to_string(checks));}
float sample(uint64_t index){uint32_t v=uint32_t(index)+12345;v=(v^(v>>16))*0x7feb352dU;v=(v^(v>>15))*0x846ca68bU;v^=v>>16;return float(int(v%20001)-10000)/10000.f;}
struct Input {
 std::vector<float> left,right;const uint8_t* planes[2]{};
 explicit Input(size_t n,uint64_t offset=0):left(n),right(n){for(size_t i=0;i<n;i++){left[i]=sample(offset+i);right[i]=-left[i];}refresh();}
 void refresh(){planes[0]=reinterpret_cast<const uint8_t*>(left.data());planes[1]=reinterpret_cast<const uint8_t*>(right.data());}
};
void metadata(latency::AudioBlock& b,uint32_t rate,uint64_t call,latency::Tap side=latency::Tap::Pre){b.sourceUuid[0]='u';b.sourceType[0]='t';b.sessionId=23;b.configEpoch=7;b.channels=2;b.sampleRate=rate;b.arrivalMonotonicNs=9876543210;b.invocationId=call;b.captureGeneration=11;b.tap=side;b.muted=false;}
void basics(uint32_t rate){
 for(uint32_t frames:{1U,1023U,1024U,1025U,1114U,1152U,2048U,2049U}){
  auto ring=std::make_unique<latency::SpscRing<latency::AudioBlock,8>>();Input input(frames);const auto oldLeft=input.left,oldRight=input.right;uint64_t sequence=100;
  check(packet::enqueue(*ring,input.planes,frames,1000000000,rate,sequence,[&](auto& b){metadata(b,rate,42);})==packet::Result::Accepted,"accepted callback");
  latency::AudioBlock block;uint32_t cursor=0;uint64_t seq=100;
  while(ring->pop(block)){
   check(block.frames==std::min(1024U,frames-cursor),"bounded frame length");check(block.invocationFrameOffset==cursor,"callback offset");check(block.sequence==seq++,"fragment sequence");
   check(block.mediaTimestampNs==1000000000+format::frameNs(cursor,rate),"timestamp exact floor");
   check(block.invocationId==42&&block.captureGeneration==11&&block.arrivalMonotonicNs==9876543210&&block.sessionId==23&&block.configEpoch==7&&block.channels==2&&block.sampleRate==rate&&block.sourceUuid[0]=='u'&&block.sourceType[0]=='t',"callback metadata unchanged");
   for(size_t ch=0;ch<2;ch++)check(!std::memcmp(block.pcm[ch].data(),(ch?input.right:input.left).data()+cursor,block.frames*sizeof(float)),"PCM bit-identical");
   for(uint32_t offset=0;offset<block.frames;offset++)check(packet::advance(block.mediaTimestampNs,block.invocationFrameOffset,offset,rate)==1000000000+format::frameNs(cursor+offset,rate),"nested offset no accumulated floor");
   cursor+=block.frames;
  }
  check(cursor==frames&&sequence==seq,"all frames once");check(input.left==oldLeft&&input.right==oldRight,"input untouched");
 }
 // Repeated small discards preserve the original callback clock exactly.
 Input input(2048);auto ring=std::make_unique<latency::SpscRing<latency::AudioBlock,8>>();uint64_t sequence=0;
 check(packet::enqueue(*ring,input.planes,2048,1000000000,rate,sequence,[&](auto& b){metadata(b,rate,1);})==packet::Result::Accepted,"discard setup");
 sliding::Tap tap(2048,rate);latency::AudioBlock block;while(ring->pop(block)){tap.append(block,0,13,false);tap.append(block,13,block.frames-13,false);}
 for(uint64_t i=0;i<2048;i++){check(tap.metadata.front().timestamp==1000000000+format::frameNs(i,rate),"discard exact timestamp");check(tap.metadata.front().callFrameOffset==i,"discard callback offset");tap.discard(1);}
 check(!tap.size()&&tap.metadata.empty(),"discard complete");
 // Invalid shape must not publish or advance sequence. Unsupported rate and
 // timestamp overflow are rejected without touching the input plane contents.
 auto invalid=[&](const uint8_t*const* planes,uint32_t frames,uint64_t timestamp,uint32_t sampleRate){const auto before=sequence;unsigned fills=0;check(packet::enqueue(*ring,planes,frames,timestamp,sampleRate,sequence,[&](auto&){++fills;})==packet::Result::Shape,"invalid shape rejected");check(!fills&&sequence==before&&!ring->pop(block),"shape publishes nothing");};
 const uint8_t* missing[2]{input.planes[0],nullptr};invalid(nullptr,10,1,rate);invalid(missing,10,1,rate);invalid(input.planes,0,1,rate);invalid(input.planes,1,UINT64_MAX,rate);invalid(input.planes,10,1,0);invalid(input.planes,10,1,96000);
 sequence=UINT64_MAX;invalid(input.planes,1,1,rate);
 // Nonfinite samples are transported unchanged and rejected by the estimator,
 // not silently sanitized into apparently valid audio.
 Input bad(rate/5);bad.left[1100]=std::bit_cast<float>(uint32_t(0x7fc12345));bad.right[1100]=std::numeric_limits<float>::infinity();
 auto big=std::make_unique<latency::SpscRing<latency::AudioBlock,187>>();sequence=0;
 check(packet::enqueue(*big,bad.planes,uint32_t(bad.left.size()),1000000000,rate,sequence,[&](auto& b){metadata(b,rate,1);})==packet::Result::Accepted,"nonfinite transported");
 sliding::Tap badTap(bad.left.size(),rate);while(big->pop(block))badTap.append(block,0,block.frames,false);
 check(!std::memcmp(badTap.pcm[0].data(),bad.left.data(),bad.left.size()*sizeof(float)),"NaN payload retained");
 const auto estimate=latency::estimate({badTap.pcm[0],1000000000,rate,1,false},{badTap.pcm[0],1000000000,rate,1,false});check(!estimate.valid&&std::string(estimate.reason)=="nonfinite","nonfinite never valid estimate");
}
void overflow(){
 auto ring=std::make_unique<latency::SpscRing<latency::AudioBlock,4>>();Input in(2048);uint64_t sequence=0;latency::AudioBlock block;
 check(packet::enqueue(*ring,in.planes,2048,1,48000,sequence,[](auto& b){metadata(b,48000,1);})==packet::Result::Accepted,"overflow setup");
 unsigned fills=0;check(packet::enqueue(*ring,in.planes,2048,1,48000,sequence,[&](auto&){++fills;})==packet::Result::Overflow,"insufficient space atomic reject");check(fills==0&&sequence==2,"overflow no prefix or sequence advance");
 check(ring->pop(block)&&block.sequence==0&&block.invocationId==1,"older first retained");check(ring->pop(block)&&block.sequence==1,"older second retained");check(!ring->pop(block),"no partial overflow callback");
 check(packet::enqueue(*ring,in.planes,2048,1,48000,sequence,[](auto& b){metadata(b,48000,2);})==packet::Result::Accepted,"batch wraps ring");check(ring->pop(block)&&block.sequence==2&&block.invocationFrameOffset==0,"wrapped first");check(ring->pop(block)&&block.sequence==3&&block.invocationFrameOffset==1024,"wrapped second");
 // Nothing is visible until the entire callback is filled, including when the
 // consumer probes during each fill (deterministic publish-boundary test).
 unsigned probes=0;check(ring->writeBatch(3,[&](auto& b,size_t i){b.sequence=i;check(!ring->pop(block),"no premature batch publication");++probes;}),"three-slot batch");check(probes==3,"all fills");for(size_t i=0;i<3;i++)check(ring->pop(block)&&block.sequence==i,"batch order");
 auto full=std::make_unique<latency::SpscRing<latency::AudioBlock,187>>();Input maximum(186*1024);sequence=0;
 check(packet::enqueue(*full,maximum.planes,186*1024,1,44100,sequence,[](auto& b){metadata(b,44100,3);})==packet::Result::Accepted,"exact ring capacity");
 check(packet::enqueue(*full,maximum.planes,1,1,44100,sequence,[](auto&){})==packet::Result::Overflow,"full ring reject");unsigned count=0;while(full->pop(block))++count;check(count==186,"capacity all intact");
 // Deliberately huge advertised frame count never dereferences the short input:
 // bounded reservation fails before the first fill.
 fills=0;check(packet::enqueue(*full,in.planes,UINT32_MAX,1,48000,sequence,[&](auto&){++fills;})==packet::Result::Overflow,"large count bounded by ring");check(!fills&&!full->pop(block),"oversized callback no work or prefix");
}
void pairing(){
 pairedclock::Window pair;
 for(uint64_t call=1;call<=2;call++){
  const uint64_t offset=(call-1)*1114,ts=1000000000+format::frameNs(offset,44100);
  pair.observe(0,{call,ts,offset,1024,0});pair.observe(0,{call,ts+format::frameNs(1024,44100),offset+1024,90,1024});
  pair.observe(1,{call,ts+20000000,offset,800,0});pair.observe(1,{call,ts+20000000+format::frameNs(800,44100),offset+800,314,800});
 }
 auto result=pair.finish(44100);check(result.matched&&result.pairs==2&&result.matchedFrames==2228,"different segmentation joins exact overlaps");check(std::abs(result.windowDeltaNs-20000000)<=1&&result.deltaSpreadNs<=1,"paired floor accuracy");
 pair.reset();for(uint64_t offset=0;offset<2048;offset+=1024){pair.observe(0,{1,1000000000+format::frameNs(offset,48000),offset,1024,offset});pair.observe(1,{1,1020000000+format::frameNs(offset,48000),offset,1024,offset});}
 result=pair.finish(48000);check(!result.matched&&result.pairs==1&&result.matchedFrames==2048,"one callback cannot masquerade as two pairs");
 pair.reset();for(uint64_t call=1;call<=2;call++){pair.observe(0,{call,1000000000+call*30000000,0,512,0});pair.observe(1,{call,1020000000+call*30000000,0,512,1024});}check(!pair.finish(48000).matched&&pair.finish(48000).matchedFrames==0,"disjoint intra-call samples never match");
 pair.reset();for(uint64_t call=1;call<=2;call++){pair.observe(0,{call,1000000000+call*30000000,(call-1)*1024,1024,0});pair.observe(1,{call,1020000000+call*30000000,(call-1)*2048,2048,0});}check(!pair.finish(48000).matched,"insufficient asymmetric coverage rejected");
 pair.reset();for(uint64_t call=1;call<=2;call++){pair.observe(0,{call,1000000000+call*30000000,(call-1)*1024,1024,0});pair.observe(1,{call+20,1020000000+call*30000000,(call-1)*1024,1024,0});}check(!pair.finish(48000).matched,"identity not local sequence");
}
void pipeline(uint32_t rate,bool differentFrames,uint64_t preStart,uint64_t postStart){
 constexpr size_t window=8192;sliding::Tap taps[2]{{window,rate},{window,rate}};auto ring=std::make_unique<latency::SpscRing<latency::AudioBlock,187>>();uint64_t cursor[2]{},sequences[2]{};latency::AudioBlock block;
 for(uint64_t call=1;!taps[0].full()||!taps[1].full();call++)for(size_t side=0;side<2;side++){
  const uint32_t frames=differentFrames?(side?1152:1114):uint32_t(call%3==0?2048:call%2?1114:1152);
  Input in(frames,cursor[side]);const auto base=1000000000+side*20000000;const auto stamp=base+format::frameNs(cursor[side],rate);
  check(packet::enqueue(*ring,in.planes,frames,stamp,rate,sequences[side],[&](auto& b){metadata(b,rate,call,side?latency::Tap::Post:latency::Tap::Pre);})==packet::Result::Accepted,"pipeline copy");
  while(ring->pop(block)){
   const uint64_t start=side?postStart:preStart;const auto global=cursor[side]+block.invocationFrameOffset;
   const size_t skip=global<start?std::min<uint64_t>(block.frames,start-global):0;
   auto count=std::min<size_t>(block.frames-skip,window-taps[side].size());
   if(count){const auto first=std::min<size_t>(317,count);taps[side].append(block,skip,first,false);if(count>first)taps[side].append(block,skip+first,count-first,false);}
  }
  cursor[side]+=frames;
 }
 windowclock::Window clocks[2];pairedclock::Window pair;for(size_t side=0;side<2;side++)taps[side].observe(side,clocks[side],pair);
 const auto result=pair.finish(rate);check(result.matched&&!result.discontinuity&&result.pairs>=2,"pipeline pairing");check(result.matchedFrames<=window,"no duplicate paired frames");
 const int64_t expected=20000000+int64_t(format::frameNs(postStart,rate))-int64_t(format::frameNs(preStart,rate));
 check(std::abs(result.windowDeltaNs-expected)<=2,"window-origin precision <=2ns");check(result.deltaSpreadNs<=2,"no segment rounding drift");
 for(size_t side=0;side<2;side++){check(!taps[side].gap()&&!taps[side].muted(),"pipeline valid metadata");for(size_t i=0;i<window;i++)check(taps[side].pcm[0][i]==sample((side?postStart:preStart)+i),"pipeline exact samples");}
 const auto origin=clocks[0].finish().origin;const auto estimate=latency::estimate({taps[0].pcm[0],int64_t(origin),rate,1,false},{taps[1].pcm[0],int64_t(origin)+result.windowDeltaNs,rate,1,false});
 check(estimate.valid&&std::abs(estimate.delayMs-20)<1000.0/rate,"PRE POST D preserved within one sample");
 check(std::abs(-estimate.delayMs+20)<1000.0/rate,"Auto remains -D");
}
}
int main(){try{for(auto rate:{44100U,48000U}){basics(rate);for(bool different:{false,true}){pipeline(rate,different,0,0);pipeline(rate,different,3,7);}}overflow();pairing();std::cout<<"PASS "<<checks<<" callback splitting / exact PCM / 44.1k+48k / offsets+rounding / unequal PRE POST / identity / atomic overflow / nonfinite checks; pure production code, no OBS runtime\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
