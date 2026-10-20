#include "paired_media_clock.hpp"
#include "latest_window_fixture.hpp"
#include <iostream>
#include <string>
#include <stdexcept>
using namespace pitatuner::capture;
int main(){unsigned checks=0;auto check=[&](bool b){++checks;if(!b)throw std::runtime_error("paired clock/freshness check="+std::to_string(checks));};try{
 pairedclock::Window w;for(uint64_t i=0;i<256;i++){const int64_t jitter=i%7==0?35000000:i%2?2000000:-2000000;auto ts=uint64_t(int64_t(1000000000+i*10000000)+jitter);w.observe(0,{i+1,ts,i*480,480});w.observe(1,{i+1,ts+20000000,i*480,480});}auto r=w.finish(48000);check(r.matched&&!r.discontinuity&&r.pairs==256&&r.matchedFrames==122880&&r.windowDeltaNs==20000000&&r.deltaSpreadNs==0);
 w.reset();for(uint64_t i=0;i<256;i++){auto ts=1000000000+i*10000000;w.observe(0,{i+1,ts,i*480,480});w.observe(1,{i+2,ts+10000000,i*480,480});}r=w.finish(48000);check(r.matched&&r.windowDeltaNs==10000000); // different window start, actual call token aligns it
 w.reset();for(uint64_t i=0;i<256;i++){auto ts=1000000000+i*10000000;w.observe(0,{i+1,ts,i*480,480});w.observe(1,{0,ts,i*480,480});}check(!w.finish(48000).matched); // no correspondence, same local seq would not suffice
 w.reset();for(uint64_t i=0;i<256;i++){auto ts=1000000000+i*10000000;w.observe(0,{i+1,ts,i*480,480});w.observe(1,{i+1,ts+(i%2?1000000:0),i*480,480});}check(!w.finish(48000).matched); // differential jitter is not cancelled
 w.reset();for(uint64_t i=0;i<256;i++){auto ts=1000000000+i*10000000+(i>=128?100000000:0);w.observe(0,{i+1,ts,i*480,480});w.observe(1,{i+1,ts+20000000,i*480,480});}check(w.finish(48000).discontinuity&&!w.finish(48000).matched);
 // These are explicitly synthetic paired traces derived from each recorded
 // window spread. The real log does not contain the full packet timestamps.
 for(uint64_t spread:latestRecordedWindowSpreads){w.reset();for(uint64_t i=0;i<256;i++){const auto ts=1000000000+i*10000000+spread*i/255;w.observe(0,{i+1,ts,i*480,480});w.observe(1,{i+1,ts+20000000,i*480,480});}auto result=w.finish(48000);check(result.matched&&result.windowDeltaNs==20000000&&result.deltaSpreadNs==0);}
 const uint64_t arrival=1000000000;check(pairedclock::active(arrival,arrival,arrival+249999999));check(!pairedclock::active(arrival,arrival,arrival+250000000));check(!pairedclock::active(0,arrival,arrival));check(!pairedclock::active(arrival,arrival,arrival-1));
 check(pairedclock::publicationFreshnessNs>2560000000);for(uint64_t now=arrival;now<arrival+60000000000ULL;now+=50000000){const uint64_t latest=arrival+(now-arrival)/2560000000ULL*2560000000ULL;check(now<latest+pairedclock::publicationFreshnessNs);check(pairedclock::active(now,now,now));} // 1-minute publication contract, active callbacks
 std::cout<<"PASS "<<checks<<" recorded_window_spans="<<latestRecordedWindowSpreads.size()<<" pairing/differential-jitter/gross-jump/1-minute-freshness/idle checks; no OBS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
