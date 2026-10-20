#include "correction_control.hpp"
#include <iostream>
#include <cstdlib>
struct Fake: pita::Backend {
 pita::Ns actual=0,target=0;unsigned writes=0,cancels=0;uint64_t revision=0;bool pending=false,ack=false;
 std::optional<pita::Ns> read()override{return actual;}uint64_t externalRevision()const override{return revision;}
 void write(pita::Ns n)override{target=n;actual=n-1;pending=true;++writes;}
 bool writePending()const override{return pending;}
 void pollWrite(pita::Ns)override{if(ack){actual=target;pending=false;++writes;}}
 void cancelWrite()override{if(pending){actual=0;pending=false;++writes;++cancels;}}
};
unsigned checks;void ck(bool x){++checks;if(!x){std::cerr<<"FAIL "<<checks<<'\n';std::abort();}}
int main(){using namespace pita;Key key{"one",1,1};
 for(int failure=0;failure<9;++failure){Fake f;pitatuner::DeliveryGate gate;pitatuner::ControlSession c(f,key,{-400000000,0,1,true},gate);Estimate e{key,1,393000000,1000,true,true,true,false};
  ck(c.acceptEstimate(e));ck(c.registerConfirmedBaseline({Mode::ExplicitPreTarget,0,0,0,1},true));auto p=c.propose(1);ck(bool(p));ck(c.apply(key,1,*p,1)==Result::Pending);ck(c.view().pending&&!c.view().canUndo&&c.view().offset==0);ck(!c.acknowledgeRead());
  if(failure==1)e.valid=false;if(failure==2)e.confident=false;if(failure==3)e.active=false;if(failure==4)e.muted=true;if(failure==7)e.freshUntil=2000;e.seq=2;ck(c.acceptEstimate(e));f.ack=true;
  if(failure==6){c.close();ck(f.cancels==1&&!f.pending&&c.view().closed);continue;}
  auto r=c.pollPending((failure==5||failure==7)?1000:2,failure!=8);ck(r.has_value());ck(*r==(failure?Result::Protected:Result::Applied));ck(f.actual==(failure?0:-393000000));ck(c.view().canUndo==!failure);ck(!c.view().pending);ck(f.writes==2);ck(f.cancels==unsigned(failure!=0));
 }
 // Freshness has no authority over explicit Manual or Undo. Same-source live
 // packet requirement is a backend bridge-specific check, not model freshness.
 Fake f;pitatuner::DeliveryGate g;pitatuner::ControlSession c(f,key,{-400000000,0,1,true},g);ck(c.deliver({key,1,pitatuner::Operation::Manual,-393000000})==Result::Pending);f.ack=true;ck(c.pollPending(INT64_MAX)==Result::Applied);ck(c.view().canUndo);ck(c.deliver({key,2,pitatuner::Operation::Undo})==Result::Pending);ck(c.pollPending(INT64_MAX)==Result::Applied);ck(f.actual==0&&!c.view().canUndo);
 std::cout<<"PASS "<<checks<<" async model Auto freshness/invalid/confidence/active/mute/close/Manual/Undo contracts\n";
}
