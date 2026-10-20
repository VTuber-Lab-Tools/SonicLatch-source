#include "qt_metrics.hpp"
#include "correction_actions.hpp"
#include "correction_profile.hpp"
#include <QApplication>
#include <QFocusEvent>
#include <QKeyEvent>
#include <iostream>
#include <stdexcept>
struct RangeBackend final:pita::Backend{pita::Ns value=3000000000;unsigned writes{};std::optional<pita::Ns> read()override{return value;}uint64_t externalRevision()const override{return 0;}void write(pita::Ns n)override{value=n;++writes;}};
int main(int argc,char** argv){QApplication app(argc,argv);try{
 unsigned checks{};auto check=[&](bool ok){++checks;if(!ok)throw std::runtime_error("range readback UI check "+std::to_string(checks));};
 QWidget root;auto* layout=new QGridLayout(&root);pitatuner::qt::Metrics metrics(&root,layout,[](pitatuner::qt::Text t){return t==pitatuner::qt::Text::Milliseconds?QStringLiteral("%1 ms"):QString::number(int(t));});
 RangeBackend backend;pitatuner::DeliveryGate gate;const pita::Key key{"range-qt",1,1};const auto range=pitatuner::qualification::select(true,true,backend.value);pitatuner::ControlSession control(backend,key,range,gate,true);uint64_t ids{},bases{};pitatuner::CorrectionActions actions(control,ids,bases);unsigned commits{};pita::Result result=pita::Result::Noop;
 metrics.manualEditor()->setRange(double(range.min)/1e6,double(range.max)/1e6);metrics.connectActions(&root,[&](size_t action){++commits;result=actions.operation(action,key,{},0,metrics.enteredMilliseconds());});metrics.authority(true,false,false);
 const auto finish=[&]{QKeyEvent keyPress(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(metrics.manualEditor(),&keyPress);};
 metrics.readOffset(backend.value);metrics.seedManualOffset(backend.value);check(metrics.value(1)=="3000.000 ms");check(metrics.enteredMilliseconds()=="3000.000000");check(metrics.manualEditor()->maximum()==1000);finish();check(commits==0&&backend.writes==0&&backend.value==3000000000);
 metrics.manualEditor()->stepDown();check(metrics.enteredMilliseconds()=="3000.000000"&&backend.writes==0); // no arrow-induced coercion
 auto* line=metrics.manualEditor()->findChild<QLineEdit*>();check(line);line->setText("1001");finish();check(commits==1&&result==pita::Result::Rejected&&backend.writes==0&&metrics.enteredMilliseconds()=="1001");
 QFocusEvent out(QEvent::FocusOut);QApplication::sendEvent(metrics.manualEditor(),&out);check(commits==1&&backend.writes==0);
 metrics.discardManualDraft();check(metrics.enteredMilliseconds()=="3000.000000");finish();check(commits==1&&backend.writes==0);
 line->setText("1000");finish();check(commits==2&&result==pita::Result::Applied&&backend.value==1000000000&&backend.writes==1);
 metrics.readOffset(backend.value);metrics.seedManualOffset(backend.value);check(metrics.value(1)=="1000.000 ms"&&metrics.enteredMilliseconds()=="1000.000000");finish();check(commits==2&&backend.writes==1);
 check(actions.operation(6,key,{},0,"")==pita::Result::Protected&&backend.value==1000000000&&backend.writes==1);
 std::cout<<"PASS "<<checks<<" range readback literal/commit/reject/no-coercion Qt checks; fake backend only\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
