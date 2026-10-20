#pragma once
#include "../integrations/correction-model/dock-model-draft/model.hpp"
#include <thread>
#include <stdexcept>

namespace pitatuner {
// One instance per ordered control stream, retained across source selections.
// UI delivery retries retain their ID; a new user action receives a fresh ID.
class DeliveryGate {
 std::uint64_t last_=0;
public:
 bool claim(std::uint64_t id) { if(!id||id<=last_)return false;last_=id;return true; }
};
enum class Operation { Manual, Step, Undo, Reset };
struct Intent { pita::Key key; std::uint64_t id=0; Operation operation=Operation::Manual; pita::Ns value=0; };
struct View {
 pita::Key key; bool closed=false,protectedState=false,canUndo=false;
 std::optional<pita::Ns> offset;
 std::optional<pita::Baseline> baseline;
 std::optional<pita::Proposal> proposal;
 bool pending=false;
};
// UI-independent integration boundary. The backend, delivery gate and session
// live on one control thread. Backend lifetime must exceed this object's.
// Construct a new session for a new OBS source binding; close the old one first.
class ControlSession {
 pita::Model model_;
 DeliveryGate& delivery_;
 const std::thread::id owner_=std::this_thread::get_id();
 std::optional<pita::Estimate> latest_;
 std::uint64_t latestSequence_=0;
 bool closed_=false;
 const bool absolutePreTarget_;
 bool available() const { return std::this_thread::get_id()==owner_&&!closed_; }
public:
 ControlSession(pita::Backend& backend,pita::Key key,pita::Range range,DeliveryGate& delivery,bool absolutePreTarget=false)
  :model_(backend,std::move(key),range),delivery_(delivery),absolutePreTarget_(absolutePreTarget){}
 ControlSession(const ControlSession&)=delete;
 ControlSession& operator=(const ControlSession&)=delete;
 View view() const { if(std::this_thread::get_id()!=owner_)throw std::logic_error("control thread required");return {model_.key,closed_,model_.protectedState,model_.undoEntry.has_value(),model_.expected,model_.baseline,model_.offered,model_.pendingWrite()}; }
 std::uint64_t acceptedEstimateSequence() const {if(std::this_thread::get_id()!=owner_)throw std::logic_error("control thread required");return latestSequence_;}
 std::optional<pita::Result> pollPending(pita::Ns now,bool automaticEnvironment=true){return available()?model_.pollPending(latest_,now,automaticEnvironment):std::nullopt;}
 bool acknowledgeRead(){return available()&&model_.acknowledgeRead();}
 bool registerConfirmedBaseline(pita::Baseline baseline,bool confirmed){return available()&&confirmed&&model_.registerBaseline(baseline);}
 bool acceptEstimate(const pita::Estimate& estimate){
  if(!available()||!(estimate.key==model_.key)||estimate.seq==0||estimate.seq<=latestSequence_)return false;
  latestSequence_=estimate.seq;latest_=estimate;model_.offered.reset();return true;
 }
 std::optional<pita::Proposal> propose(pita::Ns now){return available()&&latest_?model_.propose(*latest_,now,absolutePreTarget_):std::optional<pita::Proposal>{};}
 pita::Result apply(const pita::Key& key,std::uint64_t intent,const pita::Proposal& proposal,pita::Ns now){
  if(!available()||!(key==model_.key)||!delivery_.claim(intent)||!latest_)return pita::Result::Rejected;
  return model_.apply(proposal,*latest_,now);
 }
 pita::Result deliver(const Intent& intent){
  if(!available()||!(intent.key==model_.key)||!delivery_.claim(intent.id))return pita::Result::Rejected;
  switch(intent.operation){
   case Operation::Manual:return model_.manual(intent.value,intent.id);
   case Operation::Step:return model_.step(intent.value,intent.id);
   case Operation::Undo:return model_.undo(intent.id);
   case Operation::Reset:return model_.reset(intent.id);
  }
  return pita::Result::Rejected;
 }
 // Called only after the OBS notification has been classified/queued safely.
 // Same-value external revisions also revoke all old baseline/proposal/Undo.
 void externalChange(){if(available()){model_.changeSelection(model_.key);latest_.reset();}}
 void close(){if(!available())return;closed_=true;model_.closing=true;model_.alive=false;model_.changeSelection(model_.key);latest_.reset();}
};
} // namespace pitatuner

