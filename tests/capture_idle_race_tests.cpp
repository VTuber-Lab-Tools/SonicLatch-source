#include "paired_media_clock.hpp"
#include <iostream>
#include <stdexcept>
using namespace pitatuner::capture::pairedclock;
int main(){unsigned checks=0;auto check=[&](bool b){++checks;if(!b)throw std::runtime_error("idle arrival-race regression");};try{
 constexpr uint64_t t=10000000000ULL;
 check(!active(t+1,t+2,t));check(!idleElapsed(t+1,t+2,t));
 check(!active(t-100,t+1,t));check(!idleElapsed(t-100,t+1,t));
 check(active(t,t,t)&&!idleElapsed(t,t,t));
 check(!idleElapsed(t-captureIdleNs+1,t,t));
 check(idleElapsed(t-captureIdleNs,t,t));check(idleElapsed(t,t-captureIdleNs,t));
 check(!idleElapsed(0,t,t)&&!idleElapsed(t,0,t));
 check(!idleElapsed(UINT64_MAX-1,UINT64_MAX,1));
 // A stale real tap is still detected even if the other tap just advanced.
 check(idleElapsed(t-captureIdleNs,t+1,t));
 // The old !active rule wrongly treated this normal arrival interleaving idle.
 check(!active(t-100,t+1,t)&&!idleElapsed(t-100,t+1,t));
 std::cout<<"PASS "<<checks<<" future-arrival/no-underflow/250ms-boundary checks; inactive remains fail-closed, future arrival never creates idle\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
