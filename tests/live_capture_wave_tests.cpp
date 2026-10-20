#include "live_capture_wave.hpp"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
using namespace pitatuner::capture;
int main(){unsigned checks=0;auto check=[&](bool b){++checks;if(!b)throw std::runtime_error("live wave check="+std::to_string(checks));};try{
 auto wStorage=std::make_unique<livewave::Window>();auto& w=*wStorage;std::array<float,480> a{},b{};a.fill(.01f);b.fill(.1f);a[79]=-.3f;a[87]=.4f;b[20]=-.9f;b[42]=.8f;
 check(!w.ready(48000));w.add(0,10000000000ULL,a.data(),a.size(),48000);check(!w.ready(48000));w.add(1,10000000000ULL,b.data(),b.size(),48000);check(w.ready(48000));auto first=w.publish();check(first.series.size()==2&&first.axis.end_ns==10010000000ULL);check(first.series[0].kind==pitatuner::waveform::Kind::PreMeasured&&first.series[1].kind==pitatuner::waveform::Kind::PostMeasured);
 auto& pre=first.series[0].bins;auto& post=first.series[1].bins;check(*pre[pre.size()-9].minimum==-.3f&&*pre[pre.size()-9].maximum==.4f);check(*post[post.size()-10].minimum==-.9f&&*post[post.size()-10].maximum==.8f);check(!pre[0].drawable());check(!w.ready(48000));
 for(int i=1;i<=4;i++){for(size_t t=0;t<2;t++)w.add(t,10000000000ULL+i*10000000ULL,a.data(),a.size(),48000);check(!w.ready(48000));}
 for(size_t t=0;t<2;t++)w.add(t,10050000000ULL,a.data(),a.size(),48000);check(w.ready(48000));auto next=w.publish();check(next.axis.end_ns-first.axis.end_ns==50000000ULL);
 // No new sound: no publication and no artificial movement.
 check(!w.ready(48000));
 // Real media-time offset remains on the common axis, with actual gaps.
 auto shiftStorage=std::make_unique<livewave::Window>();auto& shift=*shiftStorage;shift.add(0,20000000000ULL,a.data(),480,48000);shift.add(1,20020000000ULL,b.data(),480,48000);auto sf=shift.publish();check(sf.axis.end_ns==20030000000ULL);check(sf.series[0].bins[sf.series[0].bins.size()-30].drawable());check(!sf.series[0].bins.back().drawable());check(sf.series[1].bins[sf.series[1].bins.size()-10].drawable());
 // Nonfinite samples invalidate their real cell, never silently fill it.
 a[0]=std::numeric_limits<float>::quiet_NaN();auto badStorage=std::make_unique<livewave::Window>();auto& bad=*badStorage;bad.add(0,30000000000ULL,a.data(),480,48000);bad.add(1,30000000000ULL,b.data(),480,48000);auto bf=bad.publish();check(!bf.series[0].bins[bf.series[0].bins.size()-10].drawable());
 // Ring wraps with bounded storage; no old tap data after clear.
 for(int i=0;i<1000;i++){for(size_t t=0;t<2;t++)w.add(t,40000000000ULL+i*10000000ULL,b.data(),480,48000);if(w.ready(48000)){auto f=w.publish();check(f.axis.end_ns-f.axis.begin_ns==2560000000ULL&&f.series[0].bins.size()==2560);}}
 w.clear(0);check(!w.ready(48000));
 std::cout<<"PASS "<<checks<<" actual-sample envelope/cadence/raw-media-time/gap/finite/bounded-ring checks; no OBS runtime\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
