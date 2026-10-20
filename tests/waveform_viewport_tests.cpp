#include "waveform_viewport.hpp"
#include <climits>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace pitatuner::qt::waveb;
int main(){try{
 unsigned checks=0;auto check=[&](bool b,const char* m){++checks;if(!b)throw std::runtime_error(m);};
 auto close=[](double a,double b){return std::abs(a-b)<=1e-8*std::max({1.,std::abs(a),std::abs(b)});};
 Viewport v;check(!v.ready()&&!v.zoom(120,.5),"empty disabled");
 check(v.update(2560,3.2)&&close(v.lowerMs(),0)&&close(v.upperMs(),2560),"full 2.56 seconds initially");
 check(close(v.minimumSpanMs(),51.2),"16 source bins minimum");
 check(v.zoom(120,.25)&&close(v.upperMs()-v.lowerMs(),2048),"up narrows horizontal time");
 check(close(v.lowerMs()+.25*(v.upperMs()-v.lowerMs()),640),"cursor time anchored");
 check(v.zoom(-120,.25)&&close(v.lowerMs(),0)&&close(v.upperMs(),2560),"down restores range");
 check(!v.zoom(-120,.5),"full bound cannot expose unavailable time");
 for(int i=0;i<100;++i)v.zoom(120,.5);
 check(close(v.upperMs()-v.lowerMs(),51.2)&&close(v.magnification(),50),"50x bound");
 check(!v.zoom(120,.5),"minimum span stable");
 v.zoom(INT_MIN,0);check(close(v.lowerMs(),0)&&close(v.upperMs(),2560),"huge negative delta safe");
 v.zoom(INT_MAX,1);check(close(v.upperMs(),2560)&&close(v.lowerMs(),2508.8),"right anchor at bound");
 v.clear();v.update(2560,3.2);v.zoom(INT_MAX,0);check(close(v.lowerMs(),0)&&close(v.upperMs(),51.2),"left anchor at bound");
 v.clear();v.update(2560,3.2);v.zoom(60,.5);v.zoom(60,.5);check(close(v.upperMs()-v.lowerMs(),2048),"partial wheel supported");
 const auto lower=v.lowerMs(),upper=v.upperMs();v.update(2560,3.2);check(close(v.lowerMs(),lower)&&close(v.upperMs(),upper),"timer redraw preserves zoom");
 v.update(5120,6.4);check(close(v.lowerMs(),2*lower)&&close(v.upperMs(),2*upper),"rolling frame preserves relative window");
 v.zoom(INT_MAX,.5);v.update(5120,100);check(close(v.upperMs()-v.lowerMs(),1600),"coarser data widens minimum safely");
 v.update(5120,5120);check(close(v.lowerMs(),0)&&close(v.upperMs(),5120),"few bins cannot overzoom");
 for(double bad:{0.,-1.,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}){
  check(!v.update(bad,3.2)&&!v.ready(),"invalid duration disables");
  check(!v.update(2560,bad)&&!v.ready(),"invalid resolution disables");
 }
 check(!v.update(2560,3000),"bin exceeds frame rejected");
 v.update(2560,3.2);check(!v.zoom(120,std::numeric_limits<double>::quiet_NaN()),"invalid anchor ignored");
 check(!v.zoom(0,.5),"zero wheel ignored");
 check(detailWidth(2560,2560,240,800)==240,"full range reduction unchanged");
 check(detailWidth(2560,1280,240,800)==480,"zoom reveals more existing bins");
 check(detailWidth(2560,51.2,240,800)==2400,"detail capped to actual 800 bins");
 check(detailWidth(2560,2560,900,100)==300,"sparse data remains sparse");
 check(detailWidth(2560,0,240,800)==0&&detailWidth(2560,51.2,0,800)==0,"invalid detail bounds blank");
 unsigned seed=0x73215;
 for(int i=0;i<10000;++i){seed=seed*1664525u+1013904223u;const int delta=int(seed%20001)-10000;seed=seed*1664525u+1013904223u;const double anchor=double(seed%2001)/1000.-.5;v.zoom(delta,anchor);
  check(std::isfinite(v.lowerMs())&&std::isfinite(v.upperMs()),"finite fuzz");
  check(v.lowerMs()>=-1e-8&&v.upperMs()<=2560+1e-8,"inside captured frame fuzz");
  check(v.upperMs()-v.lowerMs()>=51.2-1e-8&&v.upperMs()>v.lowerMs(),"real resolution bound fuzz");
 }
 std::cout<<checks<<" checks PASS; pure viewport only, no Qt or OBS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
