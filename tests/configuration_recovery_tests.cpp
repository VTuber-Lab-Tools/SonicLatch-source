#define SONICLATCH_CAPTURE_REPLAY_TEST
#include "obs_source_capture.hpp"
#include "correction_actions.hpp"
#include <iostream>
#include <stdexcept>
struct OffsetFake:pita::Backend{pita::Ns value=100000000;unsigned writes{};std::optional<pita::Ns> read()override{return value;}uint64_t externalRevision()const override{return 0;}void write(pita::Ns n)override{++writes;value=n;}};
int main(){unsigned checks=0;auto check=[&](bool b){++checks;if(!b)throw std::runtime_error("configuration recovery check="+std::to_string(checks));};try{
 using B=pitatuner::capture::Binding;using F=pitatuner::capture::Fault;const auto config=uint32_t(F::Configuration),mute=uint32_t(F::Muted);constexpr uint64_t t=1000000000;
 const auto actual=B::probeConfigurationSignal();check(actual.mask==config&&actual.at>0&&!actual.accepting);
 check(!B::configurationRecoveryReady(config,true,true,false,t,t+249999999));check(B::configurationRecoveryReady(config,true,true,false,t,t+250000000));
 check(!B::configurationRecoveryReady(config,true,true,false,t+200000000,t+250000000));check(B::configurationRecoveryReady(config,true,true,false,t+200000000,t+450000000));
 check(!B::configurationRecoveryReady(config,true,true,false,t+1,t));check(!B::configurationRecoveryReady(config,true,true,false,0,t));
 check(!B::configurationRecoveryReady(config,false,true,false,t,t+250000000));check(!B::configurationRecoveryReady(config,true,false,false,t,t+250000000));check(!B::configurationRecoveryReady(config,true,true,true,t,t+250000000));
 check(B::configurationRecoveryReady(mute,true,true,false,t,t+250000000));check(B::configurationRecoveryReady(config|mute,true,true,false,t,t+250000000));check(!B::configurationRecoveryReady(0,true,true,false,t,t+250000000));
 for(auto other:{F::Removed,F::Shape,F::Overflow,F::AudioConfiguration,F::Timeline,F::Analysis}){check(!B::configurationRecoveryReady(uint32_t(other),true,true,false,t,t+250000000));check(!B::configurationRecoveryReady(config|uint32_t(other),true,true,false,t,t+250000000));}
 // Exact same UUID, a fresh source-session identity, and no carried old Undo,
 // proposal/baseline/estimate. Getter state remains the user's current offset.
 OffsetFake backend;pitatuner::DeliveryGate gate;pitatuner::capture::SessionIds sessions;const auto first=sessions.claim(),second=sessions.claim();check(first&&second&&*first!=*second);pita::Key oldKey{"same-selected-uuid",*first,1},newKey{"same-selected-uuid",*second,1};uint64_t ids=0,bases=0;pitatuner::ControlSession old(backend,oldKey,{-500000000,500000000,77,true},gate);pitatuner::CorrectionActions oldActions(old,ids,bases);check(oldActions.operation(2,oldKey,{},0,"")==pita::Result::Applied&&old.view().canUndo);const auto current=backend.value;const auto writes=backend.writes;old.close();pitatuner::ControlSession fresh(backend,newKey,{-500000000,500000000,77,true},gate);pitatuner::CorrectionActions freshActions(fresh,ids,bases);check(!fresh.view().canUndo&&!fresh.view().baseline&&!fresh.view().proposal&&fresh.view().offset==current);check(oldActions.operation(6,oldKey,{},0,"")==pita::Result::Rejected);check(freshActions.operation(6,newKey,{},0,"")==pita::Result::Rejected);pita::Estimate stale{oldKey,1,20000000,2000000000,true,true,true,false};check(!fresh.acceptEstimate(stale));auto measured=stale;measured.key=newKey;check(fresh.acceptEstimate(measured));check(!fresh.propose(t));check(backend.value==current&&backend.writes==writes);
 std::cout<<"PASS "<<checks<<" actual update-signal/debounce/terminal-fault/same-UUID-new-session/old-estimate-and-Undo-revocation checks; fake offset, no OBS gain changed; actual OBS rebind QA pending\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
