#include <stdexcept>
#include "sliding_capture_window.hpp"
#include "raw_capture_wave.hpp"
#include "latest_analysis_lane.hpp"
#include "last_successful_delay.hpp"
#include <iostream>
#include <condition_variable>
#include <string>
using namespace pitatuner::capture;
int main(){unsigned checks=0;auto check=[&](bool b){++checks;if(!b)throw std::runtime_error("overlap check="+std::to_string(checks));};try{
 for(uint32_t rate:{44100U,48000U}){
  check(sliding::frames(rate)==rate*2&&sliding::hop(rate)*4==rate);
  sliding::Tap tap(sliding::frames(rate),rate);latency::AudioBlock block{};block.frames=rate/100;block.mediaTimestampNs=1000000000;block.arrivalMonotonicNs=1;for(size_t c=0;c<2;c++)for(size_t i=0;i<block.frames;i++)block.pcm[c][i]=float(i%19)/19.f;
  for(int i=0;i<200;i++){block.invocationId=i+1;block.mediaTimestampNs=1000000000ULL+i*10000000ULL;tap.append(block,0,block.frames,false);}check(tap.full()&&tap.metadata.size()==200);
  for(int hop=0;hop<8;hop++){check(tap.metadata.front().timestamp==1000000000ULL+uint64_t(hop)*250000000ULL);tap.discard(sliding::hop(rate));for(int i=0;i<25;i++){block.mediaTimestampNs+=10000000;tap.append(block,0,block.frames,false);}check(tap.full());}check(!tap.gap());
 }
 // At 48 kHz every retained point is exactly the corresponding real sample.
 rawwave::Window raw;latency::AudioBlock sound{};sound.frames=480;for(size_t i=0;i<480;i++)sound.pcm[0][i]=float(i)/1000.f;unsigned publications=0;uint64_t previousEnd=0;
 for(unsigned i=0;i<600;i++){for(size_t side=0;side<2;side++)raw.add(side,10000000000ULL+i*10000000ULL+side*20000000ULL,sound.pcm[0].data(),480,48000);if(raw.ready(48000)){auto frame=raw.publish(48000);check(frame.series.size()==2&&frame.axis.end_ns>previousEnd);previousEnd=frame.axis.end_ns;for(auto& series:frame.series)for(auto& bin:series.bins){check(bin.finite==1&&bin.minimum==bin.maximum);const auto within=(bin.begin_ns%10000000ULL)*48000/1000000000ULL;check(std::abs(*bin.minimum-float(within)/1000.f)<.002f);}++publications;}}
 check(publications>=359&&publications<=361);check(!raw.ready(48000));raw.clear(0);check(!raw.ready(48000));
 // A slow analysis lane is bounded and declares skipped pending jobs.
 RuntimeStats stats;std::mutex mutex;std::condition_variable cv;bool started=false,release=false;unsigned last=0;
 {LatestAnalysisLane<unsigned> lane(stats,[&](unsigned& value){if(value==1){std::unique_lock lock(mutex);started=true;cv.notify_one();cv.wait(lock,[&]{return release;});}last=value;},[]{});lane.submit(1);{std::unique_lock lock(mutex);check(cv.wait_for(lock,std::chrono::seconds(2),[&]{return started;}));}for(unsigned i=2;i<=20;i++)lane.submit(i);{std::lock_guard lock(mutex);release=true;}cv.notify_one();}
 check(stats.submitted==20&&stats.replaced==18&&stats.completed==2&&last==20&&stats.computeMaxNs>0);
 // Holding the previous success never substitutes a failed new estimate.
 pitatuner::LastSuccessfulDelay hold;pita::Key key{"same-uuid",1,1};check(!hold.held(key,1));hold.observe(key,400000000,1000000000,true);hold.observe(key,0,1250000000,false);auto old=hold.held(key,1500000000);check(old&&old->delay==400000000&&old->ageNs==500000000);check(!hold.held({"other-uuid",1,1},1500000000)&&!hold.held({"same-uuid",2,1},1500000000));check(!hold.held(key,999999999));hold.observe(key,390000000,1750000000,true);check(hold.held(key,2000000000)->delay==390000000);hold.clear();check(!hold.held(key,2000000000));
 // Benchmark exact production NCC for 2 s windows, without weakening confidence.
 std::vector<float> a(96000),b(96000);uint32_t rng=20261019;for(auto& x:a){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;x=float(int32_t(rng))/4294967296.f;}uint64_t maximum=0,total=0;for(unsigned i=0;i<4;i++){const auto lag=size_t(i)*2400;std::fill(b.begin(),b.end(),0.f);std::copy(a.begin(),a.end()-lag,b.begin()+lag);const auto begin=steadyNs();auto estimate=latency::estimate({a,1000000000,48000,1,false},{b,1000000000,48000,1,false});const auto elapsed=steadyNs()-begin;maximum=std::max(maximum,elapsed);total+=elapsed;check(estimate.valid&&std::abs(estimate.delayMs-i*50.)<.001);}
 std::cout<<"PASS "<<checks<<" overlap/raw-decimation/bounded-lane/held-age checks; raw_publications_6s="<<publications<<" NCC_1channel_avg_ms="<<double(total)/4e6<<" max_ms="<<double(maximum)/1e6<<"; synthetic, no OBS or screen QA\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
