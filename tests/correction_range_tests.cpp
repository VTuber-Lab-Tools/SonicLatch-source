#include "correction_actions.hpp"
#include "correction_profile.hpp"
#include "readback_guard.hpp"
#include "display_adapter.hpp"
#include "measurement.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using pita::Ns;
using pita::Result;
constexpr Ns ms=1000000,now=1000000000,fresh=9000000000;
unsigned checks=0;
void check(bool ok,const char* name){++checks;if(!ok)throw std::runtime_error(name);}
struct Backend final:pita::Backend {
 Ns value{};uint64_t revision{};std::vector<Ns> writes;
 explicit Backend(Ns initial):value(initial){}
 std::optional<Ns> read()override{return value;}
 uint64_t externalRevision()const override{return revision;}
 void write(Ns target)override{writes.push_back(target);value=target;}
};
struct Fixture {
 Backend backend;pitatuner::DeliveryGate gate;pita::Key key{"range-test",1,1};
 pitatuner::ControlSession control;uint64_t ids{},bases{},seq{};
 pitatuner::CorrectionActions actions;
 explicit Fixture(Ns initial=0):backend(initial),control(backend,key,pitatuner::qualification::select(true,true,initial),gate,true),actions(control,ids,bases){}
 Result click(size_t action,const std::string& input={}){return actions.operation(action,key,{},now,input);}
 pita::Estimate accept(Ns delay){pita::Estimate estimate{key,++seq,delay,fresh,true,true,true,false};check(control.acceptEstimate(estimate),"accept synthetic estimate");return estimate;}
 Result automatic(Ns delay){accept(delay);return click(5);}
 void value(Ns expected){check(backend.value==expected&&control.view().offset==expected,"exact committed readback, no clamp");}
};
void profile(){
 const auto range=pitatuner::qualification::select(true,true,0);
 check(range.min==-950*ms&&range.max==1000*ms,"literal product limits");
 check(range.hostContract&&!range.verified,"input contract does not claim output proof");
 for(const Ns n:{-950*ms,-950*ms+1,Ns{0},995*ms,1000*ms-1,1000*ms})check(range.permits(n),"inclusive target allowed");
 for(const Ns n:{-950*ms-1,1000*ms+1,1001*ms,20000*ms,INT64_MIN,INT64_MAX})check(!range.permits(n),"outside target forbidden");
 check(!pitatuner::qualification::select(false,true,0).permits(0),"missing API blocks writes");
 check(!pitatuner::qualification::select(true,false,0).permits(0),"unsupported format blocks writes");
 check(!pitatuner::qualification::targetAllowed(range,0,ms,false),"zero bridge activity guard unchanged");
 check(pitatuner::qualification::targetAllowed(range,3000*ms,0,false),"Reset from old out-of-range value allowed");
 check(pitatuner::qualification::targetAllowed(range,1001*ms,1000*ms,false),"inward step target allowed");
 check(!pitatuner::qualification::targetAllowed(range,1000*ms,1001*ms,true),"active stream cannot bypass upper bound");
}
void manualAndSteps(){
 Fixture f;check(f.click(4,"1000")==Result::Applied,"+1000ms input succeeds");f.value(1000*ms);
 const auto writes=f.backend.writes.size();
 for(const char* text:{"1001","1000.000001","20000","-950.000001","-951","NaN"}){
  check(f.click(4,text)==Result::Rejected,"invalid literal input rejected");f.value(1000*ms);check(f.backend.writes.size()==writes,"rejection no setter");
 }
 check(f.click(6)==Result::Applied,"rejected input preserves last successful Undo");f.value(0);
 check(f.click(4,"995")==Result::Applied,"+995ms input succeeds");
 check(f.click(3)==Result::Applied,"+995 then +5 reaches +1000");f.value(1000*ms);
 const auto atHigh=f.backend.writes.size();check(f.click(2)==Result::Rejected,"+1000 then +1 rejected");check(f.click(3)==Result::Rejected,"+1000 then +5 rejected");check(f.backend.writes.size()==atHigh,"upper step rejection no setter");f.value(1000*ms);
 check(f.click(6)==Result::Applied,"Undo returns +1000 to +995");f.value(995*ms);
 check(f.click(4,"-950")==Result::Applied,"lower bound remains -950");f.value(-950*ms);
 const auto atLow=f.backend.writes.size();check(f.click(0)==Result::Rejected&&f.click(1)==Result::Rejected,"outward negative steps rejected");check(f.backend.writes.size()==atLow,"lower rejection no setter");
 check(f.click(2)==Result::Applied,"lower inward +1 succeeds");f.value(-949*ms);
 check(f.click(1)==Result::Applied,"return to lower edge succeeds");f.value(-950*ms);
 check(f.click(7)==Result::Applied,"Reset after boundary succeeds");f.value(0);
}
void autoGuard(){
 // Deliberately injected estimates exercise target range checks only. They do
 // not expand or certify the real estimator's unchanged +/-500ms search limit.
 Fixture f(3000*ms);
 check(f.automatic(-1000*ms)==Result::Applied,"synthetic Auto +1000 target succeeds");f.value(1000*ms);
 const auto writes=f.backend.writes.size();
 check(f.automatic(-1001*ms)==Result::Rejected,"synthetic Auto +1001 target rejected");
 check(f.automatic(-1000*ms-1)==Result::Rejected,"synthetic Auto +1000ms plus 1ns rejected");
 check(f.backend.writes.size()==writes,"out-of-range Auto no setter");f.value(1000*ms);
 check(f.automatic(950*ms)==Result::Applied,"synthetic Auto -950 boundary unchanged");f.value(-950*ms);
 check(f.automatic(951*ms)==Result::Rejected,"synthetic Auto below -950 rejected");
 check(f.automatic(INT64_MIN)==Result::Rejected,"Auto overflow rejected");
 check(f.automatic(50*ms)==Result::Applied,"ordinary D50 Auto succeeds");f.value(-50*ms);
 check(f.automatic(50*ms)==Result::Noop,"repeated absolute Auto stays no-op");f.value(-50*ms);
 // Raising the requested search limit is still rejected by measurement.hpp.
 std::vector<float> pcm(4800,.25f);
 const auto estimate=latency::estimate({pcm,0,48000,1,false},{pcm,0,48000,1,false},501);
 check(!estimate.valid&&std::string(estimate.reason)=="window","measurement range cannot exceed 500ms");
}
void legacyReadAndUndo(){
 for(const Ns previous:{1001*ms,3000*ms,20000*ms}){
  for(const size_t action:{size_t{4},size_t{5},size_t{7}}){
   Fixture f(previous);check(!f.control.view().protectedState,"old value readable without write authority expansion");f.value(previous);
   check(pitatuner::readback::follow(f.backend,f.control,f.key)==previous,"old value readback not clamped");
   auto estimate=f.accept(50*ms);pitatuner::display::Input input(pita::Mode::Unchosen);input.selection=f.key;input.latestSequence=estimate.seq;input.observedAt=now;input.now=now;input.control=f.control.view();input.estimate=estimate;input.deliveryState=pita::delivery::State::Ready;
   const auto display=pitatuner::display::build(input);check(display->current==previous&&display->filterResidualPre==previous+50*ms,"display preserves old S and D+S");
   check(f.backend.writes.empty(),"read/display never clamp by writing");
   const auto result=action==5?f.automatic(50*ms):f.click(action,action==4?"1000":"");
   check(result==Result::Applied,"old value can be reset or set within range");
   const Ns target=action==4?1000*ms:action==5?-50*ms:0;f.value(target);
   const auto writes=f.backend.writes.size();check(f.click(6)==Result::Protected,"Undo to old out-of-range value protected");
   f.value(target);check(f.backend.writes.size()==writes&&!f.control.view().canUndo,"old Undo cannot write or survive rejection");
   check(pitatuner::readback::follow(f.backend,f.control,f.key)==target&&!f.control.view().protectedState,"fresh read recovers existing protection path");
   check(f.click(4,"995")==Result::Applied,"ordinary operation available after read");f.value(995*ms);
  }
 }
 Fixture near(1001*ms);check(near.click(1)==Result::Applied,"old +1001 to +1000 inward -1 succeeds");near.value(1000*ms);
 Fixture far(3000*ms);check(far.click(0)==Result::Rejected,"old +3000 inward -5 still outside rejected");far.value(3000*ms);check(far.backend.writes.empty(),"far old value is not silently clamped");
}
}
int main(){try{profile();manualAndSteps();autoGuard();legacyReadAndUndo();std::cout<<"PASS "<<checks<<" correction write-range checks (-950..+1000ms); pure fake backend only, no OBS/Qt runtime\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL check "<<checks<<": "<<e.what()<<'\n';return 1;}}
