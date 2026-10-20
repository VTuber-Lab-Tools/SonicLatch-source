#pragma once
#include "correction_control.hpp"
namespace pitatuner {
// A click adapter, not a range grant. The selected ControlSession owns policy,
// authoritative readback, proposal identity, replay rejection and one-step Undo.
class CorrectionActions {
 ControlSession& control_;std::uint64_t& ids_;std::uint64_t& baselines_;
public:
 CorrectionActions(ControlSession& c,std::uint64_t& i,std::uint64_t& b):control_(c),ids_(i),baselines_(b){}
 bool confirm(pita::Mode mode,const pita::Estimate& e,pita::Ns now,bool confirmed){
  const auto v=control_.view();
  if(!confirmed||v.closed||v.protectedState||!v.offset||!(v.key==e.key)||!e.valid||!e.confident||!e.active||e.muted||now>=e.freshUntil||!e.seq||control_.acceptedEstimateSequence()!=e.seq||baselines_==UINT64_MAX)return false;
  if(mode!=pita::Mode::MeasuredReference&&mode!=pita::Mode::ExplicitPreTarget)return false;
  return control_.registerConfirmedBaseline({mode,*v.offset,mode==pita::Mode::MeasuredReference?e.delay:0,0,++baselines_},true);
 }
 pita::Result operation(size_t action,const pita::Key& displayedKey,const std::optional<pita::Proposal>& /*displayed*/, pita::Ns now,const std::string& input){
  if(ids_==UINT64_MAX)return pita::Result::Rejected;
  const auto id=++ids_;
  if(action==5){if(!(control_.view().key==displayedKey))return pita::Result::Rejected;auto proposal=control_.propose(now);return proposal?control_.apply(displayedKey,id,*proposal,now):pita::Result::Rejected;}
  if(action==7)return control_.deliver({displayedKey,id,Operation::Reset,0});
  if(action==6)return control_.deliver({displayedKey,id,Operation::Undo,0});
  if(action<4){constexpr pita::Ns steps[]{-5000000,-1000000,1000000,5000000};return control_.deliver({displayedKey,id,Operation::Step,steps[action]});}
  if(action==4){const auto n=pita::parse_ms(input);return n?control_.deliver({displayedKey,id,Operation::Manual,*n}):pita::Result::Rejected;}
  return pita::Result::Rejected;
 }
};
}
