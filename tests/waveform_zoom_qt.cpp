#include "qt_waveform_zoom.hpp"
#include <QApplication>
#include <QDir>
#include <QVBoxLayout>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
#include <stdexcept>
using namespace pitatuner;
struct CounterBackend final:pita::Backend {
 pita::Ns offset=120000000;unsigned reads{},writes{};
 std::optional<pita::Ns> read()override{++reads;return offset;}
 std::uint64_t externalRevision()const override{return 0;}
 void write(pita::Ns value)override{++writes;offset=value;}
};
int main(int argc,char** argv){QApplication app(argc,argv);try{
 const QString directory=argc>1?argv[1]:"waveform-zoom-render";QDir().mkpath(directory);
 unsigned checks=0,draws=0;auto check=[&](bool b,const char* m){++checks;if(!b)throw std::runtime_error(m);};
 auto close=[](double a,double b){return std::abs(a-b)<1e-7;};
 QWidget root;QVBoxLayout layout(&root);auto* pre=new QCustomPlot(&root);auto* post=new QCustomPlot(&root);layout.addWidget(pre);layout.addWidget(post);
 qt::waveb::style(pre,"PRE (synthetic zoom QA)");qt::waveb::style(post,"POST (synthetic zoom QA)");
 auto* margins=new QCPMarginGroup(pre);for(auto* plot:{pre,post})plot->axisRect()->setMarginGroup(QCP::msLeft|QCP::msRight,margins);
 root.resize(360,460);root.show();app.processEvents();
 CounterBackend backend;DeliveryGate gate;const pita::Key key{"zoom-test",1,1};
 ControlSession control(backend,key,pita::Range{-500000000,500000000,1,false},gate);
 const auto readsBefore=backend.reads,writesBefore=backend.writes;
 display::Snapshot snapshot;snapshot.key=key;snapshot.axis={9000000000000000000ULL,9000000002560000000ULL,800};
 for(auto kind:{waveform::Kind::PreMeasured,waveform::Kind::PostMeasured}){
  display::Series series{kind,0,0,0,{}};
  for(int i=0;i<800;++i){const auto begin=snapshot.axis.begin_ns+uint64_t(i)*3200000;const float amplitude=float(.1+.3*std::pow(std::sin(double(i)/31),6));series.bins.push_back({begin,begin+3200000,-amplitude,amplitude,i<390||i>400});}
  snapshot.waveforms.push_back(std::move(series));
 }
 bool fresh=true;qt::waveb::WheelZoom* zoom{};
 auto draw=[&]{++draws;for(auto* plot:{pre,post})plot->clearPlottables();
  if(fresh){zoom->update(snapshot);for(const auto& series:snapshot.waveforms)qt::waveb::add(series.kind==waveform::Kind::PreMeasured?pre:post,series,snapshot.axis);}else zoom->clear();
  pre->replot();post->replot();
 };
 zoom=new qt::waveb::WheelZoom(pre,post,&root,draw);draw();
 auto wheel=[&](QCustomPlot* plot,int y,QPointF position,int x=0){QWheelEvent event(position,plot->mapToGlobal(position.toPoint()),QPoint{},QPoint(x,y),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QCoreApplication::sendEvent(plot,&event);return event.isAccepted();};
 auto center=[](QCustomPlot* plot){return QPointF(plot->axisRect()->rect().center());};
 const auto yPre=pre->yAxis->range(),yPost=post->yAxis->range();
 check(close(pre->xAxis->range().size(),2560)&&pre->xAxis->range()==post->xAxis->range(),"full common range");
 check(root.grab().save(directory+"/full.png"),"full image");
 check(wheel(pre,120,center(pre)),"upper wheel consumed");
 check(close(pre->xAxis->range().size(),2048)&&pre->xAxis->range()==post->xAxis->range(),"up zooms both");
 auto range=pre->xAxis->range();draw();check(pre->xAxis->range()==range,"normal redraw preserves range");
 wheel(post,-120,center(post));check(close(pre->xAxis->range().size(),2560)&&pre->xAxis->range()==post->xAxis->range(),"lower wheel zooms both out");
 for(int i=0;i<100;++i)wheel(i%2?pre:post,120,center(i%2?pre:post));
 check(close(pre->xAxis->range().size(),51.2)&&pre->xAxis->range()==post->xAxis->range(),"minimum 16-bin span");
 check(root.grab().save(directory+"/maximum.png"),"maximum image");
 range=pre->xAxis->range();const auto atLimit=draws;check(wheel(pre,120,center(pre))&&draws==atLimit&&pre->xAxis->range()==range,"limit consumed without extra draw");
 wheel(pre,-120,QPointF(0,0));check(pre->xAxis->range()==range,"plot margin not zoomed");
 wheel(pre,0,center(pre),120);check(pre->xAxis->range()==range,"horizontal wheel not zoomed");
 check(pre->yAxis->range()==yPre&&post->yAxis->range()==yPost,"amplitude axes unchanged");
 check(backend.writes==writesBefore&&backend.reads==readsBefore&&backend.offset==120000000&&!control.view().canUndo,"wheel has no backend access or offset/undo change");
 snapshot.axis.begin_ns+=2560000000ULL;snapshot.axis.end_ns+=2560000000ULL;for(auto& series:snapshot.waveforms)for(auto& bin:series.bins){bin.begin_ns+=2560000000ULL;bin.end_ns+=2560000000ULL;}draw();
 check(pre->xAxis->range()==range,"rolling frame keeps normalized view");
 snapshot.key.session=2;draw();check(close(pre->xAxis->range().size(),2560),"new source session resets zoom");
 wheel(pre,120,center(pre));fresh=false;wheel(pre,120,center(pre));check(!zoom->viewport().ready()&&pre->plottableCount()==0&&post->plottableCount()==0,"freshness callback clears rather than revives expired data");
 fresh=true;draw();check(close(pre->xAxis->range().size(),2560),"fresh after clear begins full range");
 for(auto& series:snapshot.waveforms)for(auto& bin:series.bins)bin.drawable=false;draw();check(!zoom->viewport().ready()&&pre->plottableCount()==0&&post->plottableCount()==0,"unavailable data disables zoom");
 check(backend.writes==writesBefore&&backend.offset==120000000,"all event cases leave offset unchanged");
 const QJsonObject result{{"pass",true},{"checks",int(checks)},{"draws",int(draws)},{"dpr",root.devicePixelRatioF()},{"backendReadsDuringWheel",int(backend.reads-readsBefore)},{"backendWritesDuringWheel",int(backend.writes-writesBefore)},{"scope","Synthetic Qt events and fixture backend only; not OBS runtime or physical mouse acceptance"}};
 QFile file(directory+"/result.json");check(file.open(QIODevice::WriteOnly),"result file");file.write(QJsonDocument(result).toJson());std::cout<<checks<<" Qt checks PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
