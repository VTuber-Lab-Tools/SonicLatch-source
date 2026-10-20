#include "qt_metrics.hpp"
#include "qt_waveform_b.hpp"
#include "last_successful_delay.hpp"
#include "correction_actions.hpp"
#include <QApplication>
#include <iostream>
#include <stdexcept>
struct Fake:pita::Backend{pita::Ns value=100000000;unsigned writes{};std::optional<pita::Ns> read()override{return value;}uint64_t externalRevision()const override{return 0;}void write(pita::Ns n)override{++writes;value=n;}};
int main(int argc,char** argv){QApplication app(argc,argv);unsigned checks=0;auto check=[&](bool b){++checks;if(!b)throw std::runtime_error("overlap Qt check="+std::to_string(checks));};try{
 QWidget root;QGridLayout layout(&root);auto text=[](pitatuner::qt::Text t){return t==pitatuner::qt::Text::Milliseconds?QStringLiteral("%1 ms"):t==pitatuner::qt::Text::Expired?QStringLiteral("stale"):QStringLiteral("state");};pitatuner::qt::Metrics metrics(&root,&layout,text);
 pitatuner::LastSuccessfulDelay memory;pita::Key key{"same-source",1,1};memory.observe(key,392958333,1000000000,true);memory.observe(key,0,1250000000,false);auto old=memory.held(key,1500000000);check(old.has_value());metrics.showHeldDelay(old->delay,old->ageNs);check(metrics.value(0)==QStringLiteral("392.958 ms · 0 s"));check(metrics.writesDisabled());
 Fake backend;pitatuner::DeliveryGate gate;pitatuner::ControlSession control(backend,key,{-500000000,500000000,1,true},gate);uint64_t ids=0,bases=0;pitatuner::CorrectionActions actions(control,ids,bases);pita::Estimate success{key,1,20000000,2000000000,true,true,true,false};check(control.acceptEstimate(success));check(actions.confirm(pita::Mode::ExplicitPreTarget,success,1000000001,true));auto proposal=control.propose(1000000001);check(proposal.has_value());auto invalid=success;invalid.seq=2;invalid.valid=invalid.confident=false;check(control.acceptEstimate(invalid));check(actions.operation(5,key,proposal,1500000000,"")==pita::Result::Rejected&&backend.writes==0);check(metrics.value(0).startsWith("392.958"));
 // Fresh valid display clears the stale annotation; held display is not storage
 // for the proposal or backend, and has no change/value signal connection.
 auto snapshot=std::make_shared<pitatuner::display::Snapshot>();snapshot->delay=390000000;snapshot->confidence=pitatuner::display::Confidence::Stable;metrics.update({snapshot,pitatuner::display::Hidden::None});check(metrics.value(0)=="390.000 ms"&&metrics.metricValue(0)->toolTip().isEmpty());check(backend.writes==0);
 QCustomPlot plot;pitatuner::qt::waveb::style(&plot,"PRE");pitatuner::display::Series series{};series.kind=pitatuner::waveform::Kind::PreMeasured;series.bins={{1000000,1020834,.002f,.002f,true},{1666666,1687500,-.003f,-.003f,true},{2000000,2020834,.5f,.8f,true},{3000000,3020834,.004f,.004f,true}};check(pitatuner::qt::waveb::addRaw(&plot,series,{0,4000000,800})==3);check(plot.plottableCount()==1);auto* curve=dynamic_cast<QCPCurve*>(plot.plottable(0));check(curve!=nullptr);unsigned finite=0;for(auto it=curve->data()->constBegin();it!=curve->data()->constEnd();++it)if(std::isfinite(it->value)){check(it->value==double(.002f)||it->value==double(-.003f)||it->value==double(.004f));++finite;}check(finite==3&&plot.yAxis->range().lower==-.6&&plot.yAxis->range().upper==.6);
 std::cout<<"PASS "<<checks<<" Qt held-age/current-recovery/invalid-auto-no-write/raw-sample plot checks; fake backend, no OBS screen\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
