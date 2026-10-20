#include "qt_metrics.hpp"
#include "last_successful_delay.hpp"
#include "qt_waveform_overlay.hpp"
#include <QApplication>
#include <QImage>
#include <QGridLayout>
#include <QElapsedTimer>
#include <QVBoxLayout>
#include <iostream>
#include <stdexcept>
using namespace pitatuner;
// Controllable asynchronous backend for UI clear -> Pending -> completion paths.
struct PendingUiBackend final:pita::Backend {
 pita::Ns actual{},target{};uint64_t revision{};unsigned writes{};bool pending{},ack{},timeout{};
 std::optional<pita::Ns> read()override{return actual;}uint64_t externalRevision()const override{return revision;}
 void write(pita::Ns n)override{target=n;actual=n-1;pending=true;++writes;}
 bool writePending()const override{return pending;}
 void pollWrite(pita::Ns)override{if(timeout){cancelWrite();return;}if(ack){actual=target;pending=false;++writes;}}
 void cancelWrite()override{if(pending&&revision==0&&actual==target-1){actual=0;++writes;}pending=false;}
};
int main(int argc,char** argv){QApplication app(argc,argv);unsigned checks=0;auto check=[&](bool value){++checks;if(!value)throw std::runtime_error("batch Qt check="+std::to_string(checks));};try{
 LastSuccessfulDelay diagnostic;diagnostic.observe({"saved-uuid",1,1},392940000,1000000000,true);
 check(!diagnostic.held({"saved-uuid",2,1},2000000000));check(diagnostic.diagnosticForSource("saved-uuid",2000000000)->ageNs==1000000000);
 check(!diagnostic.diagnosticForSource("other-uuid",2000000000));diagnostic.observe({"saved-uuid",2,1},0,2100000000,false);check(diagnostic.diagnosticForSource("saved-uuid",2200000000)->delay==392940000);
 QWidget root;auto* grid=new QGridLayout(&root);qt::Metrics metrics(&root,grid,[](qt::Text role){return role==qt::Text::Milliseconds?QStringLiteral("%1 ms"):QString::number(static_cast<int>(role));});
 // First Auto confirmation clears all labels synchronously before writeTo.
 // Exercise the same Metrics::readOffset(Model committed offset) used by the
 // owner Pending branch, before any event processing or acknowledgement.
 for(unsigned outcome=0;outcome<4;++outcome){PendingUiBackend backend;DeliveryGate gate;const pita::Key key{"pending-ui",1,1};ControlSession control(backend,key,{-400000000,0,1,true},gate);
  metrics.authority(false,false,false);metrics.update({});check(metrics.value(1)==qt::nsValue({}));
  check(control.deliver({key,1,Operation::Manual,-393000000})==pita::Result::Pending);
  metrics.readOffset(control.view().offset);metrics.authority(false,false,false);
  check(metrics.value(1)==QStringLiteral("0.000 ms")&&backend.actual==-393000001&&control.view().pending&&metrics.writesDisabled());
  const auto textChanges=metrics.textUpdates();for(unsigned frame=0;frame<20;++frame){check(control.pollPending(1)==pita::Result::Pending);metrics.readOffset(control.view().offset);}
  check(metrics.value(1)==QStringLiteral("0.000 ms")&&metrics.textUpdates()==textChanges&&backend.writes==1&&!control.view().canUndo);
  if(outcome==0){backend.ack=true;check(control.pollPending(2)==pita::Result::Applied);metrics.update({});metrics.readOffset(control.view().offset);check(metrics.value(1)==QStringLiteral("-393.000 ms")&&control.view().canUndo);}
  else {if(outcome==1)control.close();if(outcome==2){backend.timeout=true;check(control.pollPending(2)==pita::Result::Protected);}if(outcome==3){backend.actual=-390000000;++backend.revision;control.externalChange();}
   metrics.authority(false,false,false);metrics.update({});check(metrics.value(1)==qt::nsValue({})&&metrics.writesDisabled()&&!control.view().pending&&!control.view().canUndo);check(backend.actual==(outcome==3?-390000000:0));
   if(outcome==3){check(control.acknowledgeRead());metrics.readOffset(control.view().offset);check(metrics.value(1)==QStringLiteral("-390.000 ms")&&!control.view().canUndo&&backend.writes==1);}
  }
 }
 // The entire final presentation is committed once. Repeating a failed result
 // must not alternate held D -> blank -> held D or readback S -> blank -> S.
 const qt::Metrics::HeldDelay held{392940000,1000000000};metrics.update({}, {},held,pita::Ns(120000000));
 const auto writes=metrics.textUpdates();for(unsigned frame=0;frame<180;++frame)metrics.update({}, {},held,pita::Ns(120000000));
 check(metrics.textUpdates()==writes);check(metrics.value(0).contains("392.940")&&metrics.value(0).contains("1 s"));check(metrics.value(1).contains("120.000"));
 metrics.update({}, {},qt::Metrics::HeldDelay{392940000,1100000000},pita::Ns(120000000));check(metrics.textUpdates()==writes);
 metrics.update({}, {},qt::Metrics::HeldDelay{392940000,2000000000},pita::Ns(120000000));check(metrics.textUpdates()==writes+1);
 unsigned manual=0;metrics.connectActions(&root,[&](size_t action){if(action==4)++manual;});metrics.authority(true,false,false);metrics.seedManualOffset(120000000);
 QMetaObject::invokeMethod(metrics.manualEditor(),"editingFinished",Qt::DirectConnection);check(manual==0);
 metrics.manualEditor()->setValue(119);QMetaObject::invokeMethod(metrics.manualEditor(),"editingFinished",Qt::DirectConnection);check(manual==1);
 QMetaObject::invokeMethod(metrics.manualEditor(),"editingFinished",Qt::DirectConnection);metrics.operationButton(4)->click();check(manual==1);
 metrics.manualEditor()->setValue(118);metrics.authority(false,false,false);metrics.authority(true,false,false);QMetaObject::invokeMethod(metrics.manualEditor(),"editingFinished",Qt::DirectConnection);check(manual==1);
 // Production WrapLabel must preserve rich-text spans instead of painting HTML
 // literally. Compare identical opaque widgets against Qt's native renderer.
 qt::WrapLabel rich(&root);QLabel native(&root);const auto html=QStringLiteral("<span style='color:#00dc50'>PRE ×2</span> · <span style='color:#0050ff'>POST ×1</span>");
 for(QLabel* label:{static_cast<QLabel*>(&rich),&native}){label->setTextFormat(Qt::RichText);label->setWordWrap(true);label->setText(html);label->setAutoFillBackground(true);label->resize(300,60);}
 check(rich.heightForWidth(160)==native.heightForWidth(160));check(rich.grab().toImage()==native.grab().toImage());
 QWidget display;auto* layout=new QVBoxLayout(&display);auto* plot=new QCustomPlot(&display);auto* legend=new qt::WrapLabel(&display);layout->addWidget(plot);layout->addWidget(legend);display.resize(800,400);display.show();app.processEvents();qt::waveb::style(plot,"PRE / POST");plot->xAxis->setRange(0,5000);qt::overlay::Renderer renderer(plot,legend,"PRE","POST");
 displayonly::SampleHistory history;std::array<float,480> pcm{};for(unsigned packet=0;packet<500;++packet){for(size_t i=0;i<pcm.size();++i)pcm[i]=float(int(i%23)-11)*.001f;history.add(0,10000000000ULL+uint64_t(packet)*10000000ULL,pcm.data(),pcm.size(),48000);for(auto& value:pcm)value*=10;history.add(1,10000000000ULL+uint64_t(packet)*10000000ULL,pcm.data(),pcm.size(),48000);}
 auto frame=history.snapshot(10000000000ULL,15000000000ULL);pita::Key key{"batch-qt",1,1};QElapsedTimer timer;timer.start();check(renderer.update(frame,key,1000000000));const auto updateNs=timer.nsecsElapsed();auto* pre=renderer.curve(0);auto* post=renderer.curve(1);check(pre&&post&&plot->plottableCount()==2);check(!renderer.update(frame,key,1010000000));
 plot->xAxis->setRange(4990,5000);check(renderer.update(frame,key,1020000000));check(renderer.curve(0)==pre&&renderer.curve(1)==post);check(pre->data()->size()<=plot->axisRect()->width()*3);check(!renderer.update(frame,key,1030000000));
 plot->replot(QCustomPlot::rpQueuedReplot);app.processEvents();check(!pre->data()->isEmpty()&&!post->data()->isEmpty());check(renderer.clear()&&pre->data()->isEmpty()&&plot->plottableCount()==2);
 std::cout<<"PASS "<<checks<<" clear-to-Pending/committed-S/repeated-Pending/cancel/timeout/foreign-display/final-label-only/held-age/manual-commit-dedup/5s-raw-column/persistent-curve/zoom checks; first 5s renderer update ms="<<double(updateNs)/1e6<<"; synthetic Qt only, not OBS or screen FPS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
