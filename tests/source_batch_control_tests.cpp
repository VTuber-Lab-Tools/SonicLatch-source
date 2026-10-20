#include "capture_recovery_policy.hpp"
#include "correction_actions.hpp"
#include "first_auto_guard.hpp"
#include "last_successful_delay.hpp"
#include <iostream>
#include <stdexcept>
struct Fake:pita::Backend {pita::Ns value=100000000;unsigned writes{};uint64_t revision{};std::optional<pita::Ns> read()override{return value;}uint64_t externalRevision()const override{return revision;}void write(pita::Ns n)override{value=n;++writes;}};
int main(){unsigned checks=0;auto check=[&](bool b){++checks;if(!b)throw std::runtime_error("batch control "+std::to_string(checks));};try{
 constexpr uint64_t t=1000000000;
 for(uint32_t mask=0;mask<256;++mask)for(unsigned states=0;states<8;++states)for(uint64_t elapsed:{0ULL,249999999ULL,250000000ULL,450000000ULL}){
  bool alive=states&1,enabled=states&2,muted=states&4;
  const bool expected=(mask==1||mask==64||mask==65)&&alive&&enabled&&!muted&&elapsed>=250000000;
  check(pitatuner::capture::recovery::ready(mask,alive,enabled,muted,t,t+elapsed)==expected);
 }
 check(!pitatuner::capture::recovery::ready(1,true,true,false,0,t));check(!pitatuner::capture::recovery::ready(1,true,true,false,t+1,t));
 pita::Key key{"same-uuid",1,1};pita::Range range{-500000000,500000000,77,true};Fake backend;pitatuner::DeliveryGate gate;pitatuner::ControlSession control(backend,key,range,gate);uint64_t ids=0,bases=0;pitatuner::CorrectionActions actions(control,ids,bases);
 // Manual/Undo require no active/fresh estimate. Auto still does.
 check(!pitatuner::autoguard::firstTarget(control.view(),{},range,0,t));
 check(actions.operation(2,key,{},t,"")==pita::Result::Applied&&backend.value==101000000);
 check(actions.operation(6,key,{},t,"")==pita::Result::Applied&&backend.value==100000000);
 check(actions.operation(5,key,{},t,"")==pita::Result::Rejected&&backend.writes==2);
 pita::Estimate e{key,1,20000000,2000000000,true,true,true,false};check(control.acceptEstimate(e));
 check(pitatuner::autoguard::firstTarget(control.view(),e,range,1,t)==80000000);
 auto invalid=e;invalid.valid=false;check(!pitatuner::autoguard::firstTarget(control.view(),invalid,range,1,t));
 invalid=e;invalid.active=false;check(!pitatuner::autoguard::firstTarget(control.view(),invalid,range,1,t));
 check(!pitatuner::autoguard::firstTarget(control.view(),e,range,1,e.freshUntil));
 check(actions.confirm(pita::Mode::ExplicitPreTarget,e,t,true));check(actions.operation(5,key,{},t,"")==pita::Result::Applied&&backend.value==80000000);
 const auto writes=backend.writes;check(actions.operation(5,key,{},e.freshUntil,"")==pita::Result::Rejected&&backend.writes==writes);
 ++backend.revision;check(actions.operation(2,key,{},t,"")==pita::Result::Protected&&backend.writes==writes&&!control.view().canUndo);
 control.close();pita::Key next{"same-uuid",2,1};pitatuner::ControlSession fresh(backend,next,range,gate);check(!fresh.acceptEstimate(e)&&!fresh.view().canUndo&&!fresh.view().baseline&&fresh.view().offset==80000000);
 pitatuner::LastSuccessfulDelay held;held.observe(key,392940000,t,true);check(!held.held(next,t+1));check(held.diagnosticForSource(next.uuid,t+1)->delay==392940000);check(!held.diagnosticForSource("another-uuid",t+1));held.observe(next,0,t+2,false);check(held.diagnosticForSource(next.uuid,t+3)->ageNs==3);
 std::cout<<"PASS "<<checks<<" recovery debounce/mask/control no-measurement manual+Undo/Auto freshness/external revision/new session/diagnostic-only held checks; synthetic backend only\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
