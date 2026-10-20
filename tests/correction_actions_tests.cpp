#ifdef NDEBUG
#error correction_actions_tests requires active assertions
#endif
#include "correction_actions.hpp"
#include <cassert>
#include <iostream>
struct Fake:pita::Backend{pita::Ns value=100000000;unsigned writes=0,reads=0;std::uint64_t rev=0;bool mismatch=false;std::optional<pita::Ns> read()override{++reads;return value;}std::uint64_t externalRevision()const override{return rev;}void write(pita::Ns n)override{++writes;value=mismatch?n+1:n;}};
struct Fixture{Fake b;pitatuner::DeliveryGate gate;pita::Key key{"fixture",1,1};pitatuner::ControlSession c{b,key,{-500000000,500000000,77,true},gate};std::uint64_t ids=0,bases=0;pitatuner::CorrectionActions a{c,ids,bases};pita::Estimate e{key,1,20000000,1000,true,true,true,false};Fixture(){assert(c.acceptEstimate(e));}};
int main(){
 unsigned groups=0;
 {Fixture f;assert(!f.a.confirm(pita::Mode::ExplicitPreTarget,f.e,1,false));assert(!f.c.view().baseline&&f.b.writes==0);++groups;}
 {Fixture f;assert(f.a.confirm(pita::Mode::ExplicitPreTarget,f.e,1,true));auto p=f.c.propose(1);assert(p&&p->target==80000000);auto reads=f.b.reads;assert(f.a.operation(5,f.key,p,1,"")==pita::Result::Applied);assert(f.b.value==80000000&&f.b.writes==1&&f.b.reads>=reads+2);assert(f.a.operation(5,f.key,p,1,"")==pita::Result::Noop&&f.b.writes==1);assert(f.a.operation(6,f.key,{},1,"")==pita::Result::Applied&&f.b.value==100000000);assert(f.a.operation(6,f.key,{},1,"")==pita::Result::Rejected&&f.b.writes==2);++groups;}
 {Fixture f;assert(f.a.confirm(pita::Mode::MeasuredReference,f.e,1,true));assert(f.c.view().baseline->d0==20000000);f.e.seq=2;f.e.delay=50000000;assert(f.c.acceptEstimate(f.e));auto p=f.c.propose(1);assert(p&&p->target==70000000);++groups;}
 {Fixture f;assert(f.a.operation(2,f.key,{},1,"")==pita::Result::Applied&&f.b.value==101000000);assert(f.a.operation(0,f.key,{},1,"")==pita::Result::Applied&&f.b.value==96000000);assert(f.a.operation(4,f.key,{},1,"80.000001")==pita::Result::Applied&&f.b.value==80000001);assert(f.a.operation(4,f.key,{},1,"1e2")==pita::Result::Rejected);++groups;}
 {Fixture f;assert(f.a.operation(2,f.key,{},1,"")==pita::Result::Applied);++f.b.rev;f.c.externalChange();auto writes=f.b.writes;assert(f.a.operation(6,f.key,{},1,"")==pita::Result::Rejected&&f.b.writes==writes&&!f.c.view().canUndo);++groups;}
 {Fixture f;auto wrong=f.key;++wrong.session;assert(f.a.operation(2,wrong,{},1,"")==pita::Result::Rejected&&f.b.writes==0);f.c.close();assert(f.a.operation(2,f.key,{},1,"")==pita::Result::Rejected);++groups;}
 {Fixture f;f.b.mismatch=true;assert(f.a.operation(2,f.key,{},1,"")==pita::Result::Protected&&!f.c.view().canUndo);++groups;}
 {Fixture f;auto stale=f.e;stale.freshUntil=1;assert(!f.a.confirm(pita::Mode::ExplicitPreTarget,stale,1,true));auto wrong=f.e;++wrong.key.epoch;assert(!f.a.confirm(pita::Mode::ExplicitPreTarget,wrong,1,true));assert(f.b.writes==0);++groups;}
 {Fixture f;assert(f.a.operation(7,f.key,{},1,"")==pita::Result::Applied&&f.b.value==0&&f.b.writes==1&&f.c.view().canUndo);assert(f.a.operation(6,f.key,{},1,"")==pita::Result::Applied&&f.b.value==100000000&&!f.c.view().canUndo);++groups;}
 {Fixture f;assert(f.a.operation(7,f.key,{},1,"")==pita::Result::Applied);++f.b.rev;f.b.value=50000000;f.c.externalChange();assert(f.a.operation(6,f.key,{},1,"")==pita::Result::Rejected&&f.b.value==50000000&&f.b.writes==1);++groups;}
 {Fixture f;assert(f.a.confirm(pita::Mode::ExplicitPreTarget,f.e,1,true));auto old=f.c.propose(1);f.e.seq=2;f.e.delay=50000000;assert(f.c.acceptEstimate(f.e));assert(f.a.operation(5,f.key,old,1,"")==pita::Result::Applied&&f.b.value==50000000);++groups;}
 {Fake b;pitatuner::DeliveryGate gate;pita::Key key{"unverified",1,1};pitatuner::ControlSession control(b,key,{-500000000,500000000,0,false},gate);std::uint64_t ids=0,bases=0;pitatuner::CorrectionActions a(control,ids,bases);assert(a.operation(7,key,{},1,"")==pita::Result::Rejected&&b.value==100000000&&b.writes==0);++groups;}
 {Fixture f;Fake other;const auto untouched=other.value;f.b.mismatch=true;assert(f.a.operation(7,f.key,{},1,"")==pita::Result::Protected&&!f.c.view().canUndo&&other.value==untouched&&other.writes==0);++groups;}
 {Fixture f;auto other=f.key;++other.session;assert(f.a.operation(7,other,{},1,"")==pita::Result::Rejected&&f.b.writes==0);f.c.close();assert(f.a.operation(7,f.key,{},1,"")==pita::Result::Rejected&&f.b.writes==0);++groups;}
 {Fixture f;assert(f.a.confirm(pita::Mode::ExplicitPreTarget,f.e,1,true));assert(f.a.operation(5,f.key,{},1,"")==pita::Result::Applied);assert(f.c.view().canUndo&&f.c.view().baseline);assert(f.a.operation(5,f.key,{},2000000001LL,"")==pita::Result::Rejected);assert(f.a.operation(6,f.key,{},2000000001LL,"")==pita::Result::Applied);assert(f.b.value==100000000&&f.c.view().baseline);++groups;}
 std::cout<<"correction action adapter groups="<<groups<<" PASS; Fake range only, no OBS effect grant\n";
}
