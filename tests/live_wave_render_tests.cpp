#include "live_capture_wave.hpp"
#include "qt_waveform_b.hpp"
#include <QApplication>
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv){QApplication app(argc,argv);unsigned checks=0;auto check=[&](bool b){++checks;if(!b)throw std::runtime_error("live Qt render check="+std::to_string(checks));};try{
 pitatuner::capture::livewave::Window window;std::array<float,480> pre{},post{};pre.fill(.002f);post.fill(.02f);pre[79]=-.003f;post[79]=-.03f;
 window.add(0,10000000000ULL,pre.data(),480,48000);window.add(1,10000000000ULL,post.data(),480,48000);auto frame=window.publish();
 QCustomPlot upper,lower;upper.resize(800,160);lower.resize(800,160);pitatuner::qt::waveb::style(&upper,"PRE");pitatuner::qt::waveb::style(&lower,"POST");pitatuner::qt::waveb::axes(&upper,&lower,frame.axis);
 for(size_t t=0;t<2;t++){pitatuner::display::Series series{};series.kind=frame.series[t].kind;for(auto& bin:frame.series[t].bins)series.bins.push_back({bin.begin_ns,bin.end_ns,bin.minimum,bin.maximum,bin.drawable()});auto* plot=t?&lower:&upper;check(pitatuner::qt::waveb::add(plot,series,frame.axis,true)==10);check(plot->plottableCount()==1);const auto geometry=pitatuner::qt::waveb::geometry(series,frame.axis,series.bins.size()*3);check(geometry.size()==10);check(std::abs(geometry[1].minimum-(t?-.03:-.003))<1e-7);check(std::abs(geometry[1].maximum-(t?.02:.002))<1e-7);check(plot->yAxis->range().lower==-.6&&plot->yAxis->range().upper==.6);}
 check(upper.xAxis->range().lower==lower.xAxis->range().lower&&upper.xAxis->range().upper==lower.xAxis->range().upper);
 std::cout<<"PASS "<<checks<<" Qt measured PRE/POST assignment/raw-amplitude/shared-axis/cell-preserving render checks; synthetic samples, no native OBS UI\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
