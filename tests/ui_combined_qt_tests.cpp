#include "qt_compact_dock.hpp"
#include "qt_waveform_zoom.hpp"
#include "qt_waveform_overlay.hpp"
#include "qt_waveform_rate.hpp"
#include "waveform_cadence.hpp"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QScrollBar>
#include <QWheelEvent>
#include <iostream>
#include <stdexcept>
using namespace pitatuner;
inline const char* localeKey(qt::Text role){
 switch(role){
 case qt::Text::Delay:return "LabelDelay";case qt::Text::Current:return "LabelCurrentOffset";case qt::Text::Recommended:return "LabelTargetOffset";case qt::Text::Residual:return "LabelCurrentResidual";case qt::Text::Confidence:return "LabelConfidence";case qt::Text::State:return "LabelMeasurement";case qt::Text::Milliseconds:return "FormatMilliseconds";
 case qt::Text::Unknown:return "ConfidenceUnknown";case qt::Text::Stable:return "ConfidenceStable";case qt::Text::Invalid:return "StatusInvalidEstimate";case qt::Text::Silent:return "StatusSilent";case qt::Text::Muted:return "StatusMuted";case qt::Text::Low:return "ConfidenceLow";case qt::Text::Read:return "BtnRefreshOffset";case qt::Text::Apply:return "BtnRecommend";case qt::Text::Reset:return "BtnResetOffset";case qt::Text::Settings:return "BtnSettings";case qt::Text::Undo:return "BtnUndo";case qt::Text::Manual:return "BtnApplyOffset";case qt::Text::Minus5:return "BtnMinus5";case qt::Text::Minus1:return "BtnMinus1";case qt::Text::Plus1:return "BtnPlus1";case qt::Text::Plus5:return "BtnPlus5";case qt::Text::Waiting:return "StatusMeasuring";case qt::Text::NoBaseline:return "StatusNoBaseline";case qt::Text::Protected:return "StatusProtected";case qt::Text::Expired:return "StatusStale";case qt::Text::Unconnected:return "StatusManualUnavailable";case qt::Text::NoSource:return "StatusNoSource";
 }return "ConfidenceUnknown";
}
int main(int argc,char** argv){QApplication app(argc,argv);unsigned checks=0;std::string context="startup";auto check=[&](bool value,const char* reason){++checks;if(!value)throw std::runtime_error(context+" check="+std::to_string(checks)+" "+reason);};try{
 check(argc==3,"locale directory/output directory required");QDir locales(argv[1]),output(argv[2]);check(output.mkpath("."),"create output");const auto files=locales.entryList({"*.ini"},QDir::Files);check(files.size()==16,"all sixteen product locales");
 const auto settle=[&]{for(unsigned i=0;i<8;++i)app.processEvents();};
 for(const auto& file:files){context=file.toStdString();QSettings catalog(locales.filePath(file),QSettings::IniFormat);
  // OBS catalogs keep their keys under [locale], not QSettings' root group.
  catalog.beginGroup(QFileInfo(file).completeBaseName());
  auto tr=[&](qt::Text role){const auto key=QString::fromLatin1(localeKey(role));const auto value=catalog.value(key).toString();if(value.isEmpty())throw std::runtime_error(file.toStdString()+" missing locale value: "+key.toStdString());return value;};
  check(tr(qt::Text::Milliseconds).contains("%1"),"numeric format retains value placeholder");
  QWidget root;root.resize(360,850);auto* outer=new QVBoxLayout(&root);outer->setContentsMargins(0,0,0,0);auto* old=new QGridLayout;qt::Metrics metrics(&root,old,tr);auto* picker=new qt::WrapComboBox(&root);picker->addItem("Mic / "+QString(80,QChar('W')));auto* read=new QPushButton("Read",&root);auto* ref=new QPushButton("Reference",&root);auto* plot=new QCustomPlot(&root);plot->setMinimumHeight(280);qt::waveb::style(plot,"PRE / POST");
  auto* scroll=new QScrollArea(&root);scroll->setWidgetResizable(true);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);auto* content=new qt::CompactDock(&metrics,picker,read,plot,nullptr,{ref});scroll->setWidget(content);outer->addWidget(scroll);delete old;
  auto* legend=new qt::WrapLabel(content);static_cast<QVBoxLayout*>(content->layout())->insertWidget(2,legend);
  qt::overlay::Renderer overlay(plot,legend,catalog.value("LabelWavePre").toString(),catalog.value("LabelWavePost").toString());
  auto wave=std::make_shared<displayonly::SampleFrame>();wave->begin=10000000000ULL;wave->end=12000000000ULL;auto chunk=std::make_shared<displayonly::SampleChunk>();chunk->timestamp=wave->begin;chunk->rate=48000;chunk->samples.assign(480,.02f);wave->tracks[0].push_back(chunk);wave->tracks[1].push_back(chunk);plot->xAxis->setRange(0,2000);pita::Key waveKey{"mute-layout",1,1};
  displayonly::FrameCadence cadence;unsigned callbacks=0;auto* rate=new qt::WaveformRateButton(plot,[&](unsigned fps){++callbacks;cadence.setFps(fps);});
  root.show();settle();check(rate->fps()==60&&cadence.fps()==60,"default 60 fps");rate->click();check(rate->fps()==30&&cadence.fps()==30&&callbacks==1,"30 fps exactly once");rate->click();check(rate->fps()==60&&callbacks==2,"60 fps restored");
  unsigned operationCalls=0;metrics.connectActions(&root,[&](size_t){++operationCalls;});
  for(int width:{900,480,360,320}){context=file.toStdString()+" width="+std::to_string(width);root.resize(width,850);settle();const auto oldOperations=operationCalls;
   const auto absent=[&]{for(size_t i:{size_t(4),size_t(5)})for(auto* label:{metrics.metricLabel(i),metrics.metricValue(i)})check(label->isHidden()&&content->layout()->indexOf(label)==-1,"status/confidence absent, no layout row");check(content->layout()->itemAt(0)->widget()==picker&&content->layout()->itemAt(1)->widget()==plot,"picker directly followed by waveform");};
   absent();auto fresh=std::make_shared<display::Snapshot>();fresh->state=display::State::NoBaseline;fresh->confidence=display::Confidence::Stable;fresh->delay=392937000;fresh->current=-3000000;metrics.update({fresh,display::Hidden::None});settle();absent();
   context+=" format=["+tr(qt::Text::Milliseconds).toStdString()+"] D=["+metrics.value(0).toStdString()+"] offset=["+metrics.value(1).toStdString()+"]";
   std::cout<<"UI_NUMERIC "<<context<<std::endl;
   check(metrics.stateIs(qt::Text::NoBaseline)&&metrics.value(4)==tr(qt::Text::Stable),"internal state/confidence retained");check(metrics.value(0).contains("392.937")&&metrics.value(1).contains("-3.000"),"D and offset retain full precision/sign");check(metrics.writesDisabled(),"presentation grants no authority");
   check(metrics.metricValue(0)->styleSheet().isEmpty(),"fresh D has normal style");const auto columns=content->numericColumns();
   for(uint64_t age:{1700000000ULL,1900000000ULL,5000000000ULL,9999000000000ULL}){metrics.update({{},display::Hidden::Expired},{},qt::Metrics::HeldDelay{392937000,age},pita::Ns(-3000000));settle();absent();check(!metrics.value(0).contains(tr(qt::Text::Expired)),"no stale paragraph next to D");check(metrics.value(0).contains(" · ")&&!metrics.metricValue(0)->styleSheet().isEmpty(),"held D has short age and color");check(metrics.metricValue(0)->toolTip()==tr(qt::Text::Expired),"full stale explanation in tooltip");check(content->numericColumns()==columns,"age never forces numeric layout to one column");check(metrics.writesDisabled(),"held D cannot write");}
   check(picker->height()==picker->heightForWidth(picker->width()),"picker height equals wrapped text, no spare vertical stretch");
   const auto longName=picker->currentText();picker->setItemText(0,QStringLiteral("Mic"));settle();check(picker->height()==picker->heightForWidth(picker->width())&&picker->height()<=50,"short mic name uses compact row");picker->setItemText(0,longName);settle();
   check(metrics.operationButton(6)->height()==metrics.operationButton(7)->height()&&metrics.operationButton(6)->height()>=32,"Undo/Reset same comfortable height");
   for(size_t i=0;i<8;++i)check(metrics.operationButton(i)->height()==metrics.operationButton(6)->height(),"all eight action buttons share Undo-based height");check(rate->height()==metrics.operationButton(6)->height(),"fps button shares action height");
   for(size_t i:{size_t(6),size_t(7)}){auto* button=metrics.operationButton(i);check(button->height()>=button->heightForWidth(button->width()),"Undo/Reset wrapped font and padding fit");}
   check(dynamic_cast<qt::WrapButton*>(metrics.operationButton(5))->prominent()&&!dynamic_cast<qt::WrapButton*>(metrics.operationButton(4))->prominent(),"Auto retains green role; Audio Details uses gray host style");
   check(content->layout()->itemAt(content->layout()->count()-1)->spacerItem()!=nullptr,"spare dock space stays below controls");
   overlay.update(wave,waveKey,1000000000,0);settle();std::array<int,4> buttonY{};const std::array<size_t,4> actionIndices{5,6,7,4};for(size_t i=0;i<4;++i)buttonY[i]=metrics.operationButton(actionIndices[i])->mapTo(content,QPoint{}).y();const auto activeLegendText=legend->text();
   overlay.clear();settle();check(legend->isHidden()&&legend->sizePolicy().retainSizeWhenHidden()&&legend->text()==activeLegendText,"muted legend reserves its exact wrapped space without stale visible values");for(size_t i=0;i<4;++i)check(metrics.operationButton(actionIndices[i])->mapTo(content,QPoint{}).y()==buttonY[i],"mute does not move action buttons");
   overlay.update(wave,waveKey,2000000000,0);settle();check(!legend->isHidden(),"unmute restores legend");for(size_t i=0;i<4;++i)check(metrics.operationButton(actionIndices[i])->mapTo(content,QPoint{}).y()==buttonY[i],"unmute does not move action buttons");
   if(file=="ja-JP.ini"&&width==360){const auto pickerHeight=picker->height(),undoHeight=metrics.operationButton(6)->height();root.resize(width,1400);settle();check(picker->height()==pickerHeight&&metrics.operationButton(6)->height()==undoHeight,"growing window does not stretch picker or Undo");root.resize(width,220);settle();check(root.height()==220&&scroll->verticalScrollBar()->maximum()>0,"short dock uses vertical scrolling instead of a tall minimum");check(picker->height()==pickerHeight&&metrics.operationButton(6)->height()==undoHeight,"short dock preserves controls instead of compressing them");for(size_t i:{size_t(5),size_t(6),size_t(7),size_t(4)})check(metrics.operationButton(i)->height()>=metrics.operationButton(i)->heightForWidth(metrics.operationButton(i)->width()),"short dock keeps all button text readable");root.resize(width,850);settle();}
   check(content->width()<=scroll->viewport()->width(),"no horizontal overflow");for(size_t i=0;i<4;++i){auto* value=metrics.metricValue(i);check(value->isVisible()&&value->height()>=value->heightForWidth(value->width()),"complete numeric text with wrapping");}
   if(file=="ja-JP.ini"&&width>=360)check(content->numericColumns()==2,"Japanese original D/O then T/residual two columns");
   check(rate->isVisible()&&rate->geometry().right()<plot->width()&&rate->x()>=0,"small fps control stays at waveform edge");
   for(auto hidden:{display::Hidden::Expired,display::Hidden::ContextBlocked}){metrics.update({{},hidden});settle();absent();check(metrics.writesDisabled(),"hidden diagnostics never bypass protection");}
   metrics.update({fresh,display::Hidden::None});settle();check(metrics.metricValue(0)->styleSheet().isEmpty()&&metrics.metricValue(0)->toolTip().isEmpty(),"fresh D clears stale color/tooltip");
   rate->click();rate->click();check(operationCalls==oldOperations,"fps has no correction signal");
   if(file=="ja-JP.ini"&&width==360){const auto original=root.font();auto scaled=original;scaled.setPointSizeF(original.pointSizeF()>0?original.pointSizeF()*1.5:15);root.setFont(scaled);settle();check(metrics.operationButton(6)->height()==metrics.operationButton(7)->height(),"scaled Undo/Reset heights equal");for(size_t i=0;i<8;++i)check(metrics.operationButton(i)->height()==metrics.operationButton(6)->height()&&metrics.operationButton(i)->height()>=metrics.operationButton(i)->heightForWidth(metrics.operationButton(i)->width()),"scaled actions equal and uncut");check(rate->height()==metrics.operationButton(6)->height(),"scaled fps height matches");for(size_t i:{size_t(6),size_t(7)}){auto* button=metrics.operationButton(i);check(button->height()>=button->heightForWidth(button->width()),"150 percent font not clipped at 360 width");}root.setFont(original);settle();}
   if(file=="ja-JP.ini"){check(root.grab().save(output.filePath(QString("ja-%1-fresh.png").arg(width))),"save Japanese fresh geometry");metrics.update({{},display::Hidden::Expired},{},qt::Metrics::HeldDelay{392937000,1700000000},pita::Ns(-3000000));settle();check(root.grab().save(output.filePath(QString("ja-%1-held.png").arg(width))),"save Japanese held geometry");}
  }
 }
 // A committed offset change redraws the same immutable frame; an unknown
 // offset never silently falls back to an uncorrected POST projection.
 {QWidget probe;auto* grid=new QVBoxLayout(&probe);auto* p=new QCustomPlot(&probe);grid->addWidget(p);probe.resize(600,400);probe.show();settle();qt::waveb::style(p,"PRE / POST");p->xAxis->setRange(0,2000);qt::overlay::Renderer renderer(p,nullptr,"PRE","POST");
  auto samples=std::make_shared<displayonly::SampleFrame>();samples->begin=10000000000ULL;samples->end=12000000000ULL;
  auto pre=std::make_shared<displayonly::SampleChunk>();pre->timestamp=samples->begin+500000000ULL;pre->rate=48000;pre->samples.assign(480,.5f);auto post=std::make_shared<displayonly::SampleChunk>(*pre);post->timestamp+=393000000ULL;samples->tracks[0].push_back(pre);samples->tracks[1].push_back(post);
  pita::Key k{"offset-render",1,1};check(renderer.update(samples,k,1000000000,0),"raw zero-offset initial draw");const auto preX=renderer.curve(0)->data()->constBegin()->key;const auto rawPostX=renderer.curve(1)->data()->constBegin()->key;check(rawPostX-preX>380,"unadjusted waveform preserves measured lag");
  auto* preCurve=renderer.curve(0);auto* postCurve=renderer.curve(1);check(renderer.update(samples,k,1000000001,-393000000),"offset change invalidates same-frame cache");check(renderer.curve(0)==preCurve&&renderer.curve(1)==postCurve,"offset update preserves plot objects");check(renderer.curve(0)->data()->constBegin()->key==renderer.curve(1)->data()->constBegin()->key,"committed correction aligns large onset");check(!renderer.update(samples,k,1000000002,-393000000),"unchanged offset has no redraw");
  check(renderer.update(samples,k,1000000003,0)&&renderer.curve(1)->data()->constBegin()->key==rawPostX,"Undo/Reset restores zero-offset display");
  check(renderer.update(samples,k,1000000004,100000000)&&renderer.curve(1)->data()->constBegin()->key>rawPostX,"external positive offset moves POST later");
  check(renderer.update(samples,k,1000000005,std::nullopt)&&renderer.curve(0)->data()->isEmpty()&&renderer.curve(1)->data()->isEmpty(),"unknown offset clears projection");k.uuid="other-source";check(renderer.update(samples,k,1000000006,0)&&renderer.curve(1)->data()->constBegin()->key==rawPostX,"other source never inherits prior shift");check(pre->timestamp==10500000000ULL&&post->timestamp==10893000000ULL&&pre->samples==post->samples,"projection leaves PCM and timestamps unchanged");
 }
 context="retained offset edges";
 // Actual production Renderer: extra raw history never widens the 10 s axis.
 {QWidget probe;auto* grid=new QVBoxLayout(&probe);auto* p=new QCustomPlot(&probe);grid->addWidget(p);probe.resize(600,400);probe.show();settle();qt::waveb::style(p,"PRE / POST");p->xAxis->setRange(0,10000);p->replot();settle();qt::overlay::Renderer renderer(p,nullptr,"PRE","POST");
  displayonly::SampleHistory history;std::array<float,480> pcm{};pcm.fill(.25f);const uint64_t origin=10000000000ULL;
  for(unsigned packet=0;packet<1300;++packet)for(size_t tap=0;tap<2;++tap)history.add(tap,origin+uint64_t(packet)*10000000,pcm.data(),pcm.size(),48000);
  const auto samples=history.snapshot(origin+3000000000ULL,origin+13000000000ULL,1000000000ULL);check(samples->end-samples->begin==10000000000ULL,"extra history preserves ten-second frame");check(samples->tracks[0].front()->timestamp==origin+2000000000ULL,"one-second pre-frame history available");pita::Key k{"retained-offset",1,1};uint64_t now=1000000000;
  for(int64_t shift:{-950000000LL,1000000000LL,1500000000LL}){check(renderer.update(samples,k,now++,shift),"committed shift redraws same frame");const size_t width=std::clamp<size_t>(std::max(1,p->axisRect()->width()),1,8192);
   for(size_t tap=0;tap<2;++tap){const auto data=renderer.curve(tap)->data();check(!data->isEmpty(),"offset leaves drawable curve");const size_t first=tap&&shift==1500000000LL?width/20:0;const double firstX=(double(first)+.5)*10000/width,lastX=(double(width)-.5)*10000/width;auto last=data->constEnd();--last;check(std::abs(data->constBegin()->key-firstX)<1e-8,"negative/1000 offset has no blank; 1500 POST has exactly 500 ms boundary");check(std::abs(last->key-lastX)<1e-8,"rightmost pixel remains populated");check(data->size()==int(3*(width-first)),"every expected shared pixel column populated");}
   check(p->xAxis->range().lower==0&&p->xAxis->range().upper==10000,"offset never changes axis span");
  }
  auto unrepresentable=std::make_shared<displayonly::SampleFrame>(*samples);unrepresentable->begin=0;unrepresentable->end=10000000000ULL;check(renderer.update(unrepresentable,k,now++,-950000000),"unrepresentable negative common window clears prior frame");check(renderer.curve(0)->data()->isEmpty()&&renderer.curve(1)->data()->isEmpty(),"both taps blank together at uint64 underflow");check(!renderer.update(unrepresentable,k,now++,-950000000),"already blank underflow does not redraw");check(p->xAxis->range().size()==10000,"underflow never shrinks the viewport");
 }
 context="raw-sample zoom";
 // Production raw-sample WheelZoom overload: default 5 s, wheel maximum 10 s.
 QWidget host;auto* layout=new QVBoxLayout(&host);auto* plot=new QCustomPlot(&host);layout->addWidget(plot);host.resize(600,400);host.show();settle();qt::waveb::style(plot,"PRE / POST");displayonly::SampleFrame frame;frame.begin=10000000000;frame.end=20000000000;pita::Key key{"ui-combined",1,1};unsigned redraws=0;qt::waveb::WheelZoom* zoom{};
 auto draw=[&]{++redraws;check(zoom->update(frame,key),"raw viewport valid");plot->replot();};zoom=new qt::waveb::WheelZoom(plot,plot,&host,draw,5000);draw();check(plot->xAxis->range().size()==5000,"initial 5000 ms");const auto amplitude=plot->yAxis->range();
 auto labels=[&]{QVector<double> ticks;QVector<QString> labels;plot->xAxis->ticker()->generate(plot->xAxis->range(),QLocale::c(),QChar('g'),6,ticks,nullptr,&labels);return labels;};
 check(labels().front()==QStringLiteral("0")&&labels().back()==QStringLiteral("5000"),"initial visible labels remain 0 to 5000 ms");
 auto wheel=[&](int delta){auto point=QPointF(plot->axisRect()->rect().center());QWheelEvent event(point,plot->mapToGlobal(point.toPoint()),QPoint{},QPoint(0,delta),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QCoreApplication::sendEvent(plot,&event);return event.isAccepted();};
 for(unsigned i=0;i<50;++i)check(wheel(-120),"zoom-out consumed");check(plot->xAxis->range().size()==10000,"max 10000 ms");check(labels().front()==QStringLiteral("0")&&labels().back()==QStringLiteral("10000"),"full visible labels remain 0 to 10000 ms");const auto maxDraws=redraws;wheel(-120);check(redraws==maxDraws,"limit has no redundant redraw");check(plot->yAxis->range()==amplitude,"amplitude range unchanged");key.session=2;draw();check(plot->xAxis->range().size()==5000,"new session starts at 5000 ms");frame.end=frame.begin+10000000001;check(!zoom->update(frame,key)&&!zoom->viewport().ready(),"overlong raw frame rejected");
 std::cout<<"PASS "<<checks<<" combined UI Qt checks, sixteen locales and 320/360/480/900 widths; synthetic widgets only, no OBS or physical FPS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
