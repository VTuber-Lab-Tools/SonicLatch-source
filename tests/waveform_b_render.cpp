#include "qt_waveform_b.hpp"
#include <QApplication>
#include <QVBoxLayout>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <iostream>
#include <stdexcept>
using namespace pitatuner;
void require(bool b,const char* msg){if(!b)throw std::runtime_error(msg);}
int main(int argc,char** argv){QApplication app(argc,argv);try{
 const QString dir=argc>1?argv[1]:"wave-b-render";QDir().mkpath(dir);
 unsigned checks=0;auto check=[&](bool b,const char* m){require(b,m);++checks;};
 const uint64_t origin=9000000000000000000ULL;waveform::Axis axis{origin,origin+1000000000ULL,800};
 display::Series s{waveform::Kind::PreMeasured,0,0,0,{}};
 for(int i=0;i<100;i++)s.bins.push_back({origin+i*10000000ULL,origin+(i+1)*10000000ULL,.2F,.4F,true});
 auto g=qt::waveb::geometry(s,axis,900);check(g.size()==100&&g[0].minimum==.2F&&g[0].maximum==.4F&&g[0].x==5,"positive signed sparse actual centers");
 for(auto& b:s.bins){b.minimum=-.4F;b.maximum=-.2F;}g=qt::waveb::geometry(s,axis,900);check(g[0].maximum==-.2F,"negative not mirrored");
 s.bins[2].minimum=-.55F;s.bins[2].maximum=.59F;s.bins[3].minimum=-.51F;s.bins[3].maximum=.57F;
 g=qt::waveb::geometry(s,axis,30);check(g.size()==10&&g[0].minimum==-.55F&&g[0].maximum==.59F,"aggregated multiple extrema retained");
 s.bins[4].drawable=false;g=qt::waveb::geometry(s,axis,30);check(g.size()==9&&g[0].x==150,"gap bucket blank");
 s.bins[4].drawable=true;s.bins[4].minimum=std::numeric_limits<float>::quiet_NaN();check(qt::waveb::geometry(s,axis,30).size()==9,"nan blank");
 s.bins[4].minimum=.1F;s.bins[4].maximum=std::numeric_limits<float>::infinity();check(qt::waveb::geometry(s,axis,30).size()==9,"inf blank");
 s.bins[4].maximum=.2F;s.bins[4].minimum.reset();check(qt::waveb::geometry(s,axis,30).size()==9,"optional blank");
 s.bins[4].minimum=.5F;check(qt::waveb::geometry(s,axis,30).size()==9,"reversed extrema blank");
 s.bins[4].minimum=-.4F;s.bins[4].maximum=-.2F;s.bins.erase(s.bins.begin()+4);check(qt::waveb::geometry(s,axis,30).size()==9,"implicit time gap blank");
 s.bins.clear();check(qt::waveb::geometry(s,axis,30).empty(),"empty blank");
 for(int shift:{-50,0,50}){s.bins={{origin+100000000ULL+shift*1000000LL,origin+110000000ULL+shift*1000000LL,-.1F,.3F,true}};g=qt::waveb::geometry(s,axis,900);check(g.size()==1&&g[0].x==105+shift,"signed time shift and huge origin");}
 s.bins={{origin-1,origin+10,-.1F,.3F,true}};check(qt::waveb::geometry(s,axis,900).empty(),"outside timeline blank");
 QWidget root;QVBoxLayout layout(&root);auto* pre=new QCustomPlot(&root);auto* post=new QCustomPlot(&root);layout.addWidget(pre);layout.addWidget(post);qt::waveb::style(pre,"PRE (synthetic renderer QA)");qt::waveb::style(post,"POST (synthetic renderer QA)");auto* margins=new QCPMarginGroup(pre);for(auto* p:{pre,post})p->axisRect()->setMarginGroup(QCP::msLeft|QCP::msRight,margins);root.resize(900,440);root.show();app.processEvents();qt::waveb::axes(pre,post,axis);pre->replot();post->replot();
 display::Series sparse{waveform::Kind::PreMeasured,0,0,0,{{origin+100000000,origin+110000000,.2F,.4F,true},{origin+300000000,origin+310000000,.2F,.4F,true},{origin+500000000,origin+510000000,-.4F,-.2F,true}}};qt::waveb::add(pre,sparse,axis);pre->replot();
 auto im=pre->toPixmap().toImage();const QColor blue("#63c8e9");auto colored=[&](int x,int y){return im.pixelColor(x,y)==blue;};
 const int bx=qRound(pre->xAxis->coordToPixel(105)),by=qRound(pre->yAxis->coordToPixel(.3));bool found=false;for(int x=bx-1;x<=bx+1;x++)for(int y=by-1;y<=by+1;y++)found|=colored(x,y);check(found,"real vertical segment rendered");
 bool bridge=false;for(int x=qRound(pre->xAxis->coordToPixel(150));x<qRound(pre->xAxis->coordToPixel(250));++x)for(double y:{.2,.3,.4})bridge|=colored(x,qRound(pre->yAxis->coordToPixel(y)));check(!bridge,"NaN segments have no connecting bridge");
 im.save(dir+"/nan-separation.png");pre->clearPlottables();post->clearPlottables();check(pre->plottableCount()==0&&post->plottableCount()==0,"clear removes curves");
 display::Series measured{waveform::Kind::PreMeasured,0,0,0,{}};for(int i=0;i<800;i++){const double t=double(i)/800;float hi=.08F+.44F*std::pow(std::sin(t*13),6),lo=-.03F-.3F*std::pow(std::sin(t*19),4);bool ok=!(i>=320&&i<360);measured.bins.push_back({origin+i*1250000ULL,origin+(i+1)*1250000ULL,lo,hi,ok});}
 auto after=measured;after.kind=waveform::Kind::PostMeasured;for(auto& b:after.bins){b.begin_ns+=50000000;b.end_ns+=50000000;}
 QJsonArray images;for(int width:{900,480,360}){root.resize(width,440);app.processEvents();for(auto* p:{pre,post}){p->clearPlottables();p->replot();}qt::waveb::axes(pre,post,axis);qt::waveb::add(pre,measured,axis);qt::waveb::add(post,after,axis);pre->replot();post->replot();app.processEvents();check(pre->axisRect()->left()==post->axisRect()->left()&&pre->axisRect()->right()==post->axisRect()->right(),"paired pixel margins");check(pre->yAxis->range()==post->yAxis->range(),"shared amplitude");QString name=QString("/wave-b-%1.png").arg(width);check(root.grab().save(dir+name),"image save");images.append(name);}
 display::Series max=measured;max.bins.clear();for(size_t i=0;i<waveform::max_bins;++i){const auto begin=origin+i*1000000000ULL/waveform::max_bins,end=origin+(i+1)*1000000000ULL/waveform::max_bins;max.bins.push_back({begin,end,-.5F,.5F,true});}
 std::vector<double> elapsed;for(int i=0;i<100;i++){QElapsedTimer timer;timer.start();for(auto* p:{pre,post}){p->clearPlottables();qt::waveb::add(p,max,axis);qt::waveb::add(p,max,axis);p->replot();}elapsed.push_back(timer.nsecsElapsed()/1e6);}std::sort(elapsed.begin(),elapsed.end());
 for(auto* p:{pre,post}){p->clearPlottables();p->replot();}check(pre->plottableCount()==0&&post->plottableCount()==0,"clear after repeated max bins");check(root.grab().save(dir+"/cleared.png"),"cleared image save");
 QJsonObject out{{"pass",true},{"checks",int(checks)},{"dpr",root.devicePixelRatioF()},{"images",images},{"maxBins",int(waveform::max_bins)},{"seriesPerFrame",4},{"iterations",100},{"renderMsMedian",elapsed[50]},{"renderMsP99",elapsed[98]},{"renderMsMax",elapsed.back()},{"scope","synthetic renderer only; no capture, estimator, cursor or OBS33 certification"}};QFile file(dir+"/result.json");require(file.open(QIODevice::WriteOnly),"result open");file.write(QJsonDocument(out).toJson());std::cout<<QJsonDocument(out).toJson().toStdString();return 0;
 }catch(const std::exception& e){std::cerr<<e.what();return 1;}}
