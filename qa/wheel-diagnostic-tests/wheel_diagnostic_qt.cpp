#include "qt_waveform_zoom.hpp"
#include <QApplication>
#include <QVBoxLayout>
#include <iostream>
#include <stdexcept>
#include <vector>
using Zoom=pitatuner::qt::waveb::WheelZoom;
struct Row {std::string stage,reason;bool ready;double lower,upper,frame;std::uint64_t session,epoch;bool accepted;};
int main(int argc,char** argv){QApplication app(argc,argv);unsigned checks=0;auto check=[&](bool ok,const char* what){++checks;if(!ok)throw std::runtime_error(what);};try{
 QWidget root;auto* layout=new QVBoxLayout(&root);auto* plot=new QCustomPlot(&root);layout->addWidget(plot);root.resize(600,400);root.show();for(int i=0;i<8;++i)app.processEvents();
 pitatuner::displayonly::SampleFrame frame;frame.begin=10000000000ULL;frame.end=20000000000ULL;pita::Key key{"diagnostic-test",1,2};std::vector<Row> rows;unsigned redraws=0;Zoom* zoom{};
 zoom=new Zoom(plot,plot,&root,[&]{++redraws;check(zoom->update(frame,key),"redraw update");plot->replot();},5000);
 auto sink=[&](const Zoom::Diagnostic& d){rows.push_back({d.stage,d.reason,d.ready,d.lowerMs,d.upperMs,d.frameDurationMs,d.session,d.epoch,d.wheel?d.wheel->isAccepted():false});};zoom->setDiagnostic(sink);
 auto send=[&](QObject* receiver,QPointF point,int delta,int pixel=0){QWheelEvent wheel(point,plot->mapToGlobal(point.toPoint()),QPoint(0,pixel),QPoint(0,delta),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);wheel.setAccepted(false);QCoreApplication::sendEvent(receiver,&wheel);return wheel.isAccepted();};
 auto has=[&](const char* stage,const char* reason){for(const auto& row:rows)if(row.stage==stage&&row.reason==reason)return true;return false;};
 const auto center=QPointF(plot->axisRect()->rect().center());
 send(plot,center,-480);check(!zoom->viewport().ready()&&redraws==0,"not ready preserves behavior");
 check(zoom->update(frame,key),"raw update");plot->replot();check(plot->xAxis->range().size()==5000,"initial five seconds");
 send(plot,QPointF(-50,-50),-480);check(plot->xAxis->range().size()==5000&&redraws==0,"outside axis rejected");
 send(plot,center,0,120);check(plot->xAxis->range().size()==5000&&redraws==0,"pixel-only wheel still rejected");
 check(send(plot,center,-480),"changed wheel consumed");check(plot->xAxis->range().size()==10000&&redraws==1,"minus 480 gives ten seconds exactly once");
 check(send(plot,center,-480)&&redraws==1,"at limit consumed without redraw");
 QWidget wrong;wrong.installEventFilter(zoom);send(&wrong,center,-480);check(redraws==1,"wrong receiver unchanged");
 zoom->clear("test_explicit_clear");check(!zoom->viewport().ready(),"clear leaves not ready");key.session=3;check(zoom->update(frame,key)&&plot->xAxis->range().size()==5000,"new session resets five seconds");
#if SONICLATCH_ENABLE_WHEEL_DIAGNOSTIC
 for(const auto* reason:{"not_ready","outside_axis","zero_angle","changed","at_limit","wrong_target"})check(has("return",reason),reason);
 check(has("clear_before","test_explicit_clear")&&has("clear_after","test_explicit_clear"),"clear callsite preserved");
 check(has("redraw_before","")&&has("redraw_after",""),"real redraw boundaries");bool before=false,after=false;
 for(const auto& row:rows){if(row.stage=="entry"&&row.upper-row.lower==5000&&row.frame==10000&&row.session==1&&row.epoch==2&&!row.accepted)before=true;if(row.stage=="return"&&row.reason=="changed"&&row.upper-row.lower==10000&&row.accepted)after=true;}
 check(before&&after,"real acceptance and view before after");
#else
 check(rows.empty(),"disabled build has no diagnostic callbacks");
#endif
 zoom->setDiagnostic([](const Zoom::Diagnostic&){throw std::runtime_error("observer only");});check(send(plot,center,-480)&&plot->xAxis->range().size()==10000,"observer exception cannot alter zoom");
 {auto* first=new QCustomPlot(&root);auto* second=new QCustomPlot(&root);auto* incomplete=new Zoom(first,second,&root,[]{},5000);rows.clear();incomplete->setDiagnostic(sink);check(incomplete->update(frame,key),"two plot setup");delete first;send(second,QPointF(10,10),-480);
#if SONICLATCH_ENABLE_WHEEL_DIAGNOSTIC
 check(has("return","missing_plot"),"missing paired plot actual branch");
#endif
 delete incomplete;delete second;}
 std::cout<<"PASS "<<checks<<" wheel diagnostic checks enabled="<<SONICLATCH_ENABLE_WHEEL_DIAGNOSTIC<<"; synthetic Qt delivery only, no native OBS wheel proof\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
