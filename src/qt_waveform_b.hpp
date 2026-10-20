#pragma once
#include "display_adapter.hpp"
#include "waveform_viewport.hpp"
#include "qcustomplot.h"
#include <cmath>
#include <algorithm>
namespace pitatuner::qt::waveb {
struct Segment { double x, minimum, maximum; };
// UI-side reduction only. Never normalize, mirror, interpolate or alter timestamps.
inline std::vector<Segment> geometry(const display::Series& s, waveform::Axis a, double width) {
 std::vector<Segment> out;
 if(a.end_ns<=a.begin_ns||!std::isfinite(width)||width<=0||s.bins.size()>waveform::max_bins)return out;
 const auto duration=a.end_ns-a.begin_ns;
 const size_t columns=std::max<size_t>(1,static_cast<size_t>(width/3));
 auto valid=[&](const display::Bin& b){return b.drawable&&b.minimum&&b.maximum&&std::isfinite(*b.minimum)&&std::isfinite(*b.maximum)&&*b.minimum<=*b.maximum&&b.begin_ns<b.end_ns&&b.begin_ns>=a.begin_ns&&b.end_ns<=a.end_ns;};
 auto mid=[&](const display::Bin& b){return (double(b.begin_ns-a.begin_ns)+double(b.end_ns-a.begin_ns))/2;};
 if(s.bins.size()<=columns){for(const auto& b:s.bins)if(valid(b))out.push_back({mid(b)/1e6,*b.minimum,*b.maximum});return out;}
 struct Bucket {bool present{},blocked{};float lo{},hi{};};std::vector<Bucket> buckets(columns);
 auto index=[&](double delta){return std::min(columns-1,static_cast<size_t>(std::max(0.0,delta)*columns/double(duration)));};
 auto block=[&](uint64_t begin,uint64_t end){begin=std::max(begin,a.begin_ns);end=std::min(end,a.end_ns);if(begin>=end)return;auto first=index(double(begin-a.begin_ns)),last=index(double(end-1-a.begin_ns));for(auto i=first;i<=last;++i)buckets[i].blocked=true;};
 uint64_t previous=a.begin_ns;
 for(const auto& b:s.bins){
  if(b.begin_ns>previous)block(previous,b.begin_ns);
  if(b.begin_ns<previous||!valid(b)){block(b.begin_ns,b.end_ns);previous=std::max(previous,b.end_ns);continue;}
  auto& v=buckets[index(mid(b))];if(!v.present){v.lo=*b.minimum;v.hi=*b.maximum;v.present=true;}else{v.lo=std::min(v.lo,*b.minimum);v.hi=std::max(v.hi,*b.maximum);}previous=b.end_ns;
 }
 for(size_t i=0;i<columns;++i){const auto& b=buckets[i];if(b.present&&!b.blocked)out.push_back({(double(i)+.5)*double(duration)/double(columns)/1e6,b.lo,b.hi});}
 return out;
}
// OBS dock surfaces use the host Base palette, including runtime theme changes.
inline void applyPalette(QCustomPlot* p){
 const auto palette=p->palette();const auto background=palette.brush(QPalette::Base);const auto text=palette.color(QPalette::WindowText);
 auto axisColor=text;axisColor.setAlpha(150);auto gridColor=text;gridColor.setAlpha(35);auto zeroColor=text;zeroColor.setAlpha(85);
 p->setBackground(background);p->axisRect()->setBackground(background);
 for(auto* axis:{p->xAxis,p->yAxis}){axis->setBasePen(QPen(axisColor));axis->setTickPen(QPen(axisColor));axis->setSubTickPen(QPen(axisColor));axis->setTickLabelColor(text);axis->setLabelColor(text);axis->grid()->setPen(QPen(gridColor));axis->grid()->setZeroLinePen(QPen(zeroColor));}
}
class PaletteObserver final:public QObject {
 QCustomPlot* plot_;
protected:
 bool eventFilter(QObject* watched,QEvent* event)override{
  if(watched==plot_&&(event->type()==QEvent::PaletteChange||event->type()==QEvent::ApplicationPaletteChange||event->type()==QEvent::StyleChange)){applyPalette(plot_);plot_->replot(QCustomPlot::rpQueuedReplot);}
  return QObject::eventFilter(watched,event);
 }
public:
 explicit PaletteObserver(QCustomPlot* plot):QObject(plot),plot_(plot){setObjectName(QStringLiteral("soniclatchPlotPaletteObserver"));plot->installEventFilter(this);}
};
inline void style(QCustomPlot* p,const QString& label){
 applyPalette(p);if(!p->findChild<QObject*>(QStringLiteral("soniclatchPlotPaletteObserver"),Qt::FindDirectChildrenOnly))new PaletteObserver(p);
 for(auto* axis:{p->xAxis,p->yAxis})axis->ticker()->setTickCount(5);
 p->yAxis->setRange(-.6,.6);p->yAxis->setLabel(label);p->xAxis->setLabel("ms");p->setAntialiasedElements(QCP::aeNone);
}
inline void axes(QCustomPlot* pre,QCustomPlot* post,waveform::Axis a){
 const double end=a.end_ns>a.begin_ns?double(a.end_ns-a.begin_ns)/1e6:1;
 for(auto* p:{pre,post})p->xAxis->setRange(0,end);
}
inline size_t add(QCustomPlot* p,const display::Series& series,waveform::Axis axis,bool preserveBins=false){
 const bool pre=series.kind==waveform::Kind::PreMeasured;
 const bool estimated=series.kind!=waveform::Kind::PreMeasured&&series.kind!=waveform::Kind::PostMeasured;
 const double fullMs=axis.end_ns>axis.begin_ns?double(axis.end_ns-axis.begin_ns)/1e6:0;
 const auto width=detailWidth(fullMs,p->xAxis->range().size(),p->axisRect()->width(),series.bins.size());
 const auto segments=geometry(series,axis,preserveBins?std::max(width,double(series.bins.size())*3):width);if(segments.empty())return 0;
 QVector<double> t,x,y;t.reserve(segments.size()*3);x.reserve(segments.size()*3);y.reserve(segments.size()*3);double order=0;const auto nan=std::numeric_limits<double>::quiet_NaN();
 for(const auto& s:segments){for(auto value:{s.minimum,s.maximum,nan}){t.push_back(order++);x.push_back(s.x);y.push_back(value);}}
 auto* curve=new QCPCurve(p->xAxis,p->yAxis);curve->setData(t,x,y,true);curve->setLineStyle(QCPCurve::lsLine);curve->setScatterStyle(QCPScatterStyle::ssNone);curve->setBrush(Qt::NoBrush);curve->setAntialiased(false);
 QPen pen(QColor(pre?"#63c8e9":"#efb170"));pen.setWidthF(1);if(estimated)pen.setStyle(Qt::DashLine);curve->setPen(pen);
 return segments.size();
}
// Polyline through retained actual sample values only. Missing/nonfinite or
// reversed media times break the stroke; no min/max, smoothing or normalization.
inline size_t addRaw(QCustomPlot* p,const display::Series& series,waveform::Axis axis){
 QVector<double> t,x,y;t.reserve(series.bins.size()*2);x.reserve(series.bins.size()*2);y.reserve(series.bins.size()*2);double order=0;uint64_t previous=0;bool have=false;size_t count=0;const auto nan=std::numeric_limits<double>::quiet_NaN();
 auto point=[&](double at,double value){t.push_back(order++);x.push_back(at);y.push_back(value);};
 for(const auto& b:series.bins){const bool valid=b.drawable&&b.minimum&&b.maximum&&*b.minimum==*b.maximum&&std::isfinite(*b.minimum)&&b.begin_ns>=axis.begin_ns&&b.end_ns<=axis.end_ns&&b.begin_ns<b.end_ns;
  if(!valid){if(have)point(nan,nan);have=false;continue;}
  if(have&&(b.begin_ns<=previous||b.begin_ns-previous>3000000ULL))point(nan,nan);
  point(double(b.begin_ns-axis.begin_ns)/1e6,*b.minimum);previous=b.begin_ns;have=true;++count;
 }
 if(!count)return 0;auto* curve=new QCPCurve(p->xAxis,p->yAxis);curve->setData(t,x,y,true);curve->setLineStyle(QCPCurve::lsLine);curve->setScatterStyle(QCPScatterStyle::ssNone);curve->setBrush(Qt::NoBrush);curve->setAntialiased(false);QPen pen(QColor(series.kind==waveform::Kind::PreMeasured?"#63c8e9":"#efb170"));pen.setWidthF(1);curve->setPen(pen);return count;
}
} // namespace
