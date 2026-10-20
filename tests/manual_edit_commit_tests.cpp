#include "qt_metrics.hpp"
#include <QApplication>
#include <iostream>
#include <stdexcept>
#include <QKeyEvent>
#include <QLineEdit>
#include "correction_actions.hpp"
#include "correction_profile.hpp"
struct FakeOffset:pita::Backend{pita::Ns value=100000000;unsigned writes{};std::optional<pita::Ns> read()override{return value;}uint64_t externalRevision()const override{return 0;}void write(pita::Ns v)override{value=v;++writes;}};
int main(int argc,char** argv){QApplication app(argc,argv);using namespace pitatuner;using namespace pitatuner::qt;unsigned checks=0;std::string context="existing edit regression";auto check=[&](bool b){++checks;if(!b)throw std::runtime_error("manual edit commit regression check="+std::to_string(checks)+" "+context);};try{
 QWidget root;auto* layout=new QGridLayout(&root);Metrics metrics(&root,layout,[](Text t){return t==Text::Milliseconds?QStringLiteral("%1 ms"):QString::number(int(t));});unsigned manual=0,steps=0;std::string last;
 metrics.connectActions(&root,[&](size_t n){if(n==4){++manual;last=metrics.enteredMilliseconds();}else if(n<4)++steps;});metrics.authority(true,true,true);
 metrics.seedManualOffset(100000000);check(manual==0);QMetaObject::invokeMethod(metrics.manualEditor(),"editingFinished",Qt::DirectConnection);check(manual==0);
 metrics.manualEditor()->setValue(95);check(manual==0);QMetaObject::invokeMethod(metrics.manualEditor(),"editingFinished",Qt::DirectConnection);check(manual==1&&last=="95.000000");
 QMetaObject::invokeMethod(metrics.manualEditor(),"editingFinished",Qt::DirectConnection);metrics.operationButton(4)->click();check(manual==1); // Enter then focus-out/Settings: one operation
 metrics.manualEditor()->setValue(94);metrics.operationButton(4)->click();check(manual==1);QMetaObject::invokeMethod(metrics.manualEditor(),"editingFinished",Qt::DirectConnection);check(manual==2&&last=="94.000000");QMetaObject::invokeMethod(metrics.manualEditor(),"editingFinished",Qt::DirectConnection);check(manual==2);
 metrics.manualEditor()->setValue(93);metrics.seedManualOffset(123000000);QMetaObject::invokeMethod(metrics.manualEditor(),"editingFinished",Qt::DirectConnection);check(manual==2);check(metrics.manualEditor()->value()==123); // external readback discards pending draft without writing
 metrics.manualEditor()->setValue(122);metrics.authority(false,false,false);QMetaObject::invokeMethod(metrics.manualEditor(),"editingFinished",Qt::DirectConnection);check(manual==2);metrics.seedManualOffset(124000000);metrics.authority(true,true,true);QMetaObject::invokeMethod(metrics.manualEditor(),"editingFinished",Qt::DirectConnection);check(manual==2);
 metrics.operationButton(0)->click();metrics.operationButton(3)->click();check(steps==2&&manual==2);check(!metrics.manualEditor()->keyboardTracking());
 auto* line=metrics.manualEditor()->findChild<QLineEdit*>();check(line);line->setText(QStringLiteral("91.000000"));check(manual==2);QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(metrics.manualEditor(),&enter);check(manual==3&&last=="91.000000");
 // The real correction model gives one edit one transaction and one Undo.
 QWidget secondRoot;auto* secondLayout=new QGridLayout(&secondRoot);Metrics second(&secondRoot,secondLayout,[](Text){return QStringLiteral("%1");});FakeOffset backend;pitatuner::DeliveryGate gate;pita::Key key{"fake-edit",1,1};pitatuner::ControlSession control(backend,key,{-500000000,500000000,77,true},gate);uint64_t ids=0,bases=0;pitatuner::CorrectionActions actions(control,ids,bases);
 second.connectActions(&secondRoot,[&](size_t n){actions.operation(n,key,{},0,second.enteredMilliseconds());});second.authority(true,false,false);second.seedManualOffset(100000000);second.manualEditor()->setValue(95);QMetaObject::invokeMethod(second.manualEditor(),"editingFinished",Qt::DirectConnection);check(backend.writes==1&&backend.value==95000000&&control.view().canUndo);QMetaObject::invokeMethod(second.manualEditor(),"editingFinished",Qt::DirectConnection);second.operationButton(4)->click();check(backend.writes==1);second.seedManualOffset(95000000);check(backend.writes==1);check(actions.operation(6,key,{},0,"")==pita::Result::Applied);check(backend.writes==2&&backend.value==100000000&&!control.view().canUndo);
 // Native key events must keep a literal draft, including out-of-range text.
 // R3 QDoubleSpinBox silently changed 100 -> 00 and -400 -> -40 while typing.
 QWidget thirdRoot;auto* thirdLayout=new QGridLayout(&thirdRoot);Metrics third(&thirdRoot,thirdLayout,[](Text t){return t==Text::Milliseconds?QStringLiteral("%1 ms"):QString::number(int(t));});
 FakeOffset literalBackend;literalBackend.value=-392958333;DeliveryGate literalGate;const pita::Key literalKey{"literal-edit",1,1};const auto range=qualification::select(true,true,0);ControlSession literalControl(literalBackend,literalKey,range,literalGate);uint64_t literalIds=0,literalBases=0;CorrectionActions literalActions(literalControl,literalIds,literalBases);unsigned commits=0;std::string committedText;std::optional<pita::Ns> committedNumber;
 third.manualEditor()->setRange(-399,0);third.connectActions(&thirdRoot,[&](size_t n){if(n!=4)return;++commits;committedText=third.enteredMilliseconds();committedNumber=pita::parse_ms(committedText);literalActions.operation(n,literalKey,{},0,committedText);});third.authority(true,false,false);third.seedManualOffset(literalBackend.value);
 auto* literalLine=third.manualEditor()->findChild<QLineEdit*>();check(literalLine);
 auto type=[&](const QString& text){literalLine->selectAll();QKeyEvent erase(QEvent::KeyPress,Qt::Key_Backspace,Qt::NoModifier);QApplication::sendEvent(third.manualEditor(),&erase);for(const auto c:text){QKeyEvent key(QEvent::KeyPress,0,Qt::NoModifier,QString(c));QApplication::sendEvent(third.manualEditor(),&key);}};
 auto finish=[&]{QKeyEvent key(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(third.manualEditor(),&key);};
 for(const auto& input:std::array<QString,9>{"-393","-390.125","0","100","-400","","-","-390.","12x"}){
  context="literal input=["+input.toStdString()+"]";third.seedManualOffset(literalBackend.value);const auto oldWrites=literalBackend.writes,oldCommits=commits;type(input);context+=" actual=["+third.enteredMilliseconds()+"]";check(third.enteredMilliseconds()==input.toStdString());check(literalBackend.writes==oldWrites&&commits==oldCommits);
  const auto parsed=pita::parse_ms(input.toStdString());const bool permitted=parsed&&range.permits(*parsed);finish();check(commits==oldCommits+1&&committedText==input.toStdString());check(third.enteredMilliseconds()==input.toStdString());
  if(permitted)check(literalBackend.value==*parsed);else check(literalBackend.writes==oldWrites);
  QFocusEvent focusOut(QEvent::FocusOut);QApplication::sendEvent(third.manualEditor(),&focusOut);third.operationButton(4)->click();check(commits==oldCommits+1&&third.enteredMilliseconds()==input.toStdString());
 }
 // Invalid literal cannot become a legal zero through native spinner arrows.
 type("100");const auto beforeArrow=third.enteredMilliseconds();third.manualEditor()->stepUp();check(third.enteredMilliseconds()==beforeArrow);
 type("-390.125");third.seedManualOffset(-392000000);const auto beforeExternal=commits;finish();check(commits==beforeExternal&&third.enteredMilliseconds()=="-392.000000");
 type("-393");third.authority(false,false,false);finish();check(commits==beforeExternal);third.seedManualOffset(-392000000);third.authority(true,false,false);finish();check(commits==beforeExternal);
 // Same-value readback must still discard a different raw draft.
 type("100");third.seedManualOffset(-392000000);check(third.enteredMilliseconds()=="-392.000000");finish();check(commits==beforeExternal);
 std::cout<<"PASS "<<checks<<" literal key input/range rejection/no coercion/external draft discard/manual edit/Enter/dedup/seed-blocking/one-Undo checks; fake range only, no OBS writes\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
