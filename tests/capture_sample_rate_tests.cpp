#include "capture_format.hpp"
#include "measurement.hpp"
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <vector>
int main(){try{unsigned checks=0;auto check=[&](bool v){++checks;if(!v)throw std::runtime_error("sample rate regression");};using namespace pitatuner::capture::format;
 check(windowFrames(44100)==88200);check(windowFrames(48000)==96000);check(windowFrames(96000)==0);check(!supportedRate(0));check(frameNs(112896,44100)==2560000000ULL);check(frameNs(122880,48000)==2560000000ULL);check(sampleCeilNs(44100)==22676);check(sampleCeilNs(48000)==20834);check(frameNs(1,44100)==22675);check(frameNs(1,48000)==20833);
 for(auto r:{44100U,48000U}){
  check(nearestFrame(70000000,r)==(r==44100?3087ULL:3360ULL));
  for(std::uint64_t i=0;i<windowFrames(r);i+=137){const auto stamp=frameNs(i,r);check(nearestFrame(stamp,r)==i);}
  std::vector<float> pre(r),post(r);std::uint32_t rng=20261019;
  for(auto& v:pre){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;v=float(static_cast<std::int32_t>(rng))/4294967296.f;}
  for(auto ms:{0,20,50,100,200}){const auto lag=std::size_t(r)*ms/1000;std::fill(post.begin(),post.end(),0.f);for(std::size_t i=lag;i<post.size();++i)post[i]=pre[i-lag];auto e=latency::estimate({pre,1000000000,r,1,false},{post,1000000000,r,1,false});check(e.valid&&std::abs(e.delayMs-ms)<.1);}
  auto timestampOnly=latency::estimate({pre,1000000000,r,1,false},{pre,1070000000,r,1,false});check(timestampOnly.valid&&std::abs(timestampOnly.delayMs-70)<.1);
 }
 std::cout<<checks<<" rate/timestamp/estimator checks PASS; no OBS runtime\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
