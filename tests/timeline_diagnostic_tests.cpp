#include "timeline_diagnostic.hpp"
#include <thread>
#include <iostream>
#include <cstdlib>
using namespace pitatuner::capture::diagnostic;
int main(){size_t checks=0;auto check=[&](bool b){++checks;if(!b)std::abort();};
 Trace<128> t;std::thread producer([&]{for(uint64_t i=0;i<10000;i++)t.append({i*100,i*100+7,static_cast<uint32_t>(i+1),false});});
 size_t read=0;while(read<128){const auto n=t.size();for(;read<n;read++){const auto p=t.at(read);check(p.timestamp==read*100);check(p.arrival==read*100+7);check(p.frames==read+1);}}producer.join();check(t.size()==128);check(t.at(127).frames==128);
 FirstFailure f;check(!f.read());std::thread worker([&]{f.publish({100,90,10,40,480,2,900,80,880,480,0,48000,480,true});});while(!f.read())std::this_thread::yield();worker.join();auto v=f.read();check(v->expected==100&&v->actual==90&&v->difference==10&&v->rate==48000&&v->before);
 check(observing(100,100));check(observing(100,500000099));check(!observing(100,500000100));check(!observing(100,99));
 // One tick: take at 499.999ms defers; recovery at exactly 500ms must wait.
 // Next tick consumes/logs the trace, then (and only then) may destroy session.
 const uint64_t faultAt=100;bool traceTaken=false;
 if(!observing(faultAt,faultAt+499999000))traceTaken=true;
 check(!traceTaken);
 check(!recoveryReady(faultAt,faultAt+500000000,traceTaken));
 if(!observing(faultAt,faultAt+500000000))traceTaken=true;
 check(traceTaken&&recoveryReady(faultAt,faultAt+500000000,traceTaken));
 std::cout<<"PASS "<<checks<<" checks; bounded metadata, release/acquire publication, 500ms boundary\n";
}
