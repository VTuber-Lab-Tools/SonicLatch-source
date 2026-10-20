#include "qt_compact_dock.hpp"
#include "qt_advanced_audio.hpp"
#include <QApplication>
#include <QKeyEvent>
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv){QApplication app(argc,argv);using namespace pitatuner::qt;unsigned checks=0;auto check=[&](bool ok){++checks;if(!ok)throw std::runtime_error("audio details UI regression check="+std::to_string(checks));};try{
 QWidget root;auto* old=new QGridLayout(&root);Metrics metrics(&root,old,[](Text t){switch(t){case Text::Settings:return QStringLiteral("Settings");case Text::Apply:return QStringLiteral("Auto adjust");case Text::Undo:return QStringLiteral("Undo");case Text::Reset:return QStringLiteral("Reset");case Text::Milliseconds:return QStringLiteral("%1 ms");default:return QString::number(int(t));}});
 auto* picker=new QComboBox;auto* read=new QPushButton("Read");auto* ref=new QPushButton("Reference");auto* pre=new QWidget;auto* post=new QWidget;pre->setMinimumHeight(80);post->setMinimumHeight(80);
 CompactDock content(&metrics,picker,read,pre,post,{ref});content.resize(360,800);content.show();for(int i=0;i<5;i++)app.processEvents();metrics.authority(true,true,true);metrics.seedManualOffset(100000000);
 unsigned applied=0,steps=0,opened=0;metrics.connectActions(&content,[&](size_t index){if(index<4)++steps;else if(index==4)++applied;});
 metrics.operationButton(0)->click();check(steps==1);metrics.operationButton(3)->click();metrics.operationButton(1)->click();metrics.operationButton(2)->click();check(steps==4&&applied==0);check(metrics.manualEditor()->value()==100);
 check(content.settingsButton()==metrics.operationButton(4));check(metrics.manualEditor()->isVisible());check(read->isHidden()&&ref->isHidden()&&content.settingsPanel()->isHidden());
 QObject host;auto* action=new QAction(QString::fromUtf8("オーディオの詳細プロパティ(&A)"),&host);action->setObjectName("actionAdvAudioProperties");QObject::connect(action,&QAction::triggered,&content,[&]{++opened;QFocusEvent event(QEvent::FocusOut);QApplication::sendEvent(metrics.manualEditor(),&event);});
 check(bindAdvancedAudioButton(content.settingsButton(),&host,&content,[&]{metrics.seedManualOffset(100000000);return true;}));check(content.settingsButton()->text()==QString::fromUtf8("オーディオの詳細プロパティ"));check(content.settingsButton()->focusPolicy()==Qt::NoFocus);
 metrics.manualEditor()->setValue(99);content.settingsButton()->click();app.processEvents();check(opened==1&&applied==0&&metrics.manualEditor()->value()==100);check(content.settingsPanel()->isHidden());
 check(!dynamic_cast<WrapButton*>(content.settingsButton())->prominent());check(dynamic_cast<WrapButton*>(metrics.operationButton(5))->prominent());
 metrics.manualEditor()->setValue(99);QMetaObject::invokeMethod(metrics.manualEditor(),"editingFinished",Qt::DirectConnection);check(applied==1);QMetaObject::invokeMethod(metrics.manualEditor(),"editingFinished",Qt::DirectConnection);check(applied==1);
 metrics.authority(false,false,false);check(content.settingsButton()->isEnabled());check(metrics.writesDisabled());content.settingsButton()->click();check(opened==2&&applied==1);check(metrics.writesDisabled());
 action->setEnabled(false);check(!content.settingsButton()->isEnabled());content.settingsButton()->click();check(opened==2);action->setEnabled(true);check(content.settingsButton()->isEnabled());action->setText("Advanced Audio Properties");check(content.settingsButton()->text()==action->text());delete action;check(!content.settingsButton()->isEnabled());content.settingsButton()->click();check(opened==2);
 QPushButton missing;check(!bindAdvancedAudioButton(&missing,nullptr,&content,[]{return true;}));check(!missing.isEnabled());
 QObject fallbackHost;QAction fallback("Audio",&fallbackHost);fallback.setObjectName("actionMixerToolbarAdvAudio");QPushButton fallbackButton;unsigned fallbackCalls=0;QObject::connect(&fallback,&QAction::triggered,&content,[&]{++fallbackCalls;});check(bindAdvancedAudioButton(&fallbackButton,&fallbackHost,&content,[]{return false;}));fallbackButton.click();check(fallbackCalls==0);
 // Volatile strict readiness never determines the visual enable flag. A click
 // rechecks it; becoming ready later does not queue an automatic write.
 metrics.authority(true,true,false);bool strictReady=false;unsigned autoWrites=0;metrics.connectActions(&content,[&](size_t n){if(n==5&&strictReady)++autoWrites;});for(unsigned i=0;i<8;++i){strictReady=i%2;metrics.authority(true,true,false);check(metrics.operationButton(5)->isEnabled());metrics.operationButton(5)->click();}check(autoWrites==4);strictReady=false;metrics.operationButton(5)->click();strictReady=true;check(autoWrites==4);metrics.authority(false,false,false);check(!metrics.operationButton(5)->isEnabled());
 check(content.grab().save("simple-settings-ui.png"));std::cout<<"PASS "<<checks<<" gray host Audio Details QAction/Enter-only manual/draft discard/no write/disabled+destroyed action/Auto visual stability and fresh-click gate checks; fake host, no OBS runtime\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
