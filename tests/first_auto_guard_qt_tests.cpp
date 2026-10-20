#include "first_auto_guard.hpp"
#include "correction_actions.hpp"
#include <QApplication>
#include <QMessageBox>
#include <QTimer>
#include <iostream>
#include <stdexcept>
struct Fake:pita::Backend {pita::Ns value=100000000;unsigned writes{};uint64_t revision{};std::optional<pita::Ns> read()override{return value;}uint64_t externalRevision()const override{return revision;}void write(pita::Ns n)override{value=n;++writes;}};
struct Fixture{Fake b;pitatuner::DeliveryGate gate;pita::Key key{"fake",1,1};pita::Range range{30000000,100000000,88,true};pitatuner::ControlSession c{b,key,range,gate};uint64_t ids=0,bases=0;pitatuner::CorrectionActions actions{c,ids,bases};std::optional<pita::Estimate> e{pita::Estimate{key,1,20000000,1000000000,true,true,true,false}};Fixture(uint64_t session=1):key{"fake",session,1}{if(!c.acceptEstimate(*e)||actions.operation(4,key,{},0,"95")!=pita::Result::Applied)throw std::runtime_error("fixture setup");}};
int main(int argc,char** argv){QApplication app(argc,argv);unsigned checks=0,dialogs=0;auto check=[&](bool b){++checks;if(!b)throw std::runtime_error("first Auto guard regression");};try{
 auto target=[](Fixture& f){return pitatuner::autoguard::firstTarget(f.c.view(),f.e,f.range,f.c.acceptedEstimateSequence(),0);};
 pita::Result result=pita::Result::Rejected;auto attempt=[&](Fixture*& active,auto mutate,int response){result=pita::Result::Rejected;const auto initial=active->c.view();const auto revision=active->b.revision;if(!target(*active))return false;if(target(*active)==initial.offset){result=pita::Result::Noop;return false;}
  QMessageBox box(QMessageBox::Question,"Auto","Confirm existing baseline",QMessageBox::Yes|QMessageBox::No);box.setDefaultButton(QMessageBox::No);++dialogs;QTimer::singleShot(0,&box,[&]{mutate();box.done(response);});const bool confirmed=box.exec()==QMessageBox::Yes;
  if(!pitatuner::autoguard::unchanged(initial,active->c.view(),revision,active->b.revision,confirmed)||!target(*active))return false;
  if(target(*active)==active->c.view().offset){result=pita::Result::Noop;return false;}
  if(!active->actions.confirm(pita::Mode::ExplicitPreTarget,*active->e,0,true))return false;
  return active->actions.operation(5,active->key,{},0,"")==pita::Result::Applied;
 };
 {Fixture f;f.e->delay=70000000;Fixture* active=&f;check(!target(f));check(!attempt(active,[]{},QMessageBox::Yes));check(dialogs==0&&f.b.writes==1&&!f.c.view().baseline&&f.c.view().canUndo);check(f.actions.operation(6,f.key,{},0,"")==pita::Result::Applied&&f.b.value==100000000);}
 {Fixture f;Fixture* active=&f;check(target(f)==75000000);check(!attempt(active,[]{},QMessageBox::No));check(f.b.writes==1&&f.c.view().canUndo&&!f.c.view().baseline);}
 {Fixture f;Fixture* active=&f;check(!attempt(active,[&]{f.e->seq=2;f.e->delay=70000000;f.c.acceptEstimate(*f.e);},QMessageBox::Yes));check(f.b.writes==1&&f.c.view().canUndo&&!f.c.view().baseline);}
 {Fixture f,other(2);Fixture* active=&f;check(!attempt(active,[&]{active=&other;},QMessageBox::Yes));check(f.b.writes==1&&other.b.writes==1&&!other.c.view().baseline);}
 {Fixture f;Fixture* active=&f;check(!attempt(active,[&]{++f.b.revision;f.c.externalChange();},QMessageBox::Yes));check(f.b.writes==1&&!f.c.view().baseline&&f.c.view().protectedState);}
 {Fixture f;Fixture* active=&f;check(!attempt(active,[&]{f.c.close();},QMessageBox::Yes));check(f.b.writes==1&&!f.c.view().baseline);}
 {Fixture f;Fixture* active=&f;check(attempt(active,[]{},QMessageBox::Yes));check(f.b.writes==2&&f.b.value==75000000&&f.c.view().canUndo);check(f.actions.operation(6,f.key,{},0,"")==pita::Result::Applied&&f.b.value==95000000);}
 {Fixture f;f.e->delay=0;Fixture* active=&f;const auto beforeDialogs=dialogs;check(!attempt(active,[]{},QMessageBox::Yes)&&result==pita::Result::Noop);check(dialogs==beforeDialogs&&f.b.writes==1&&f.c.view().canUndo&&!f.c.view().baseline);check(f.actions.operation(6,f.key,{},0,"")==pita::Result::Applied&&f.b.value==100000000);}
 {Fixture f;Fixture* active=&f;check(!attempt(active,[&]{f.e->seq=2;f.e->delay=0;f.c.acceptEstimate(*f.e);},QMessageBox::Yes)&&result==pita::Result::Noop);check(f.b.writes==1&&f.c.view().canUndo&&!f.c.view().baseline);}
 {Fixture f;auto v=f.c.view();v.offset=INT64_MIN;f.e->delay=1;pita::Range full{INT64_MIN,INT64_MAX,99,true};check(!pitatuner::autoguard::firstTarget(v,f.e,full,1,0));f.range.verified=false;check(!target(f));}
 std::cout<<"PASS "<<checks<<" first-Auto target/Undo/modal checks; dialogs="<<dialogs<<"; Fake range only, no OBS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
