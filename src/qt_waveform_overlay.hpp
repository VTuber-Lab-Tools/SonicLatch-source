#pragma once
#include "qt_waveform_b.hpp"
#include "display_wave_gain.hpp"
#include "display_sample_history.hpp"
#include "display_offset_columns.hpp"
#include <QLabel>
#include <QPointer>
namespace pitatuner::qt::overlay {
struct Column{bool valid{};float lo{},hi{};};
// Every drawable source envelope enters a display column; a missing sub-bin
// never deletes actual extrema in the same column. No horizontal joining.
inline std::vector<Column> columns(const waveform::Series& series,waveform::Axis axis,size_t width){
 width=std::clamp<size_t>(width,1,waveform::max_bins);std::vector<Column> out(width);if(axis.end_ns<=axis.begin_ns)return out;
 for(const auto& b:series.bins){if(!b.drawable()||!b.minimum||!b.maximum||!std::isfinite(*b.minimum)||!std::isfinite(*b.maximum)||*b.minimum>*b.maximum||b.begin_ns<axis.begin_ns||b.end_ns>axis.end_ns||b.begin_ns>=b.end_ns)continue;
  const long double middle=(static_cast<long double>(b.begin_ns-axis.begin_ns)+static_cast<long double>(b.end_ns-axis.begin_ns))/2;
  const auto at=std::min(width-1,static_cast<size_t>(middle*width/(axis.end_ns-axis.begin_ns)));auto& c=out[at];if(!c.valid){c={true,*b.minimum,*b.maximum};}else{c.lo=std::min(c.lo,*b.minimum);c.hi=std::max(c.hi,*b.maximum);}
 }
 return out;
}
class BlendCurve final:public QCPCurve {
protected:
 void draw(QCPPainter* painter)override{painter->save();painter->setCompositionMode(QPainter::CompositionMode_Plus);QCPCurve::draw(painter);painter->restore();}
public:
 BlendCurve(QCPAxis* x,QCPAxis* y):QCPCurve(x,y){}
};
class LegendPaletteObserver final:public QObject {
 QLabel* legend_;
protected:
 bool eventFilter(QObject* watched,QEvent* event)override{
  if(watched==legend_&&(event->type()==QEvent::PaletteChange||event->type()==QEvent::ApplicationPaletteChange||event->type()==QEvent::StyleChange)){
   const bool light=legend_->palette().color(QPalette::Window).lightnessF()>.5;auto text=legend_->text();text.replace(light?QStringLiteral("#00dc50"):QStringLiteral("#006e28"),light?QStringLiteral("#006e28"):QStringLiteral("#00dc50"));text.replace(light?QStringLiteral("#39eeee"):QStringLiteral("#1c7777"),light?QStringLiteral("#1c7777"):QStringLiteral("#39eeee"));if(text!=legend_->text())legend_->setText(text);
  }return QObject::eventFilter(watched,event);
 }
public:
 explicit LegendPaletteObserver(QLabel* legend):QObject(legend),legend_(legend){setObjectName(QStringLiteral("soniclatchLegendPaletteObserver"));legend->installEventFilter(this);}
};
class Renderer {
 QPointer<QCustomPlot> plot_;QPointer<QLabel> legend_;std::array<QPointer<BlendCurve>,2> curves_;
 displayonly::Gain gains_[2];std::shared_ptr<const void> shown_;uint64_t shownBegin_{},shownEnd_{};std::optional<pita::Key> key_;QString names_[2];size_t width_{};
 uint64_t updates_{};std::optional<pita::Ns> shownPostShift_;
 void updateLegend(){if(!legend_)return;const bool light=legend_->palette().color(QPalette::Window).lightnessF()>.5;const auto text=QStringLiteral("<span style='color:%5'>%1 ×%2</span> · <span style='color:%6'>%3 ×%4</span> · <span style='color:%7'>%1∩%3</span>").arg(names_[0].toHtmlEscaped(),QString::number(gains_[0].value(),'f',2),names_[1].toHtmlEscaped(),QString::number(gains_[1].value(),'f',2),light?QStringLiteral("#006e28"):QStringLiteral("#00dc50"),QStringLiteral("#0050ff"),light?QStringLiteral("#1c7777"):QStringLiteral("#39eeee"));if(legend_->text()!=text)legend_->setText(text);legend_->show();}
public:
 Renderer(QCustomPlot* plot,QLabel* legend,QString pre,QString post):plot_(plot),legend_(legend),names_{std::move(pre),std::move(post)}{
  // Add PRE and POST into their own transparent QCP buffer, then composite
  // that buffer over the host background. Direct Plus over white is invisible.
  if(!plot->layer("soniclatchWave"))plot->addLayer("soniclatchWave",plot->layer("main"),QCustomPlot::limAbove);
  plot->layer("soniclatchWave")->setMode(QCPLayer::lmBuffered);
  const QColor colors[2]{QColor(0,255,80,180),QColor(0,80,255,180)};
  for(size_t i=0;i<2;i++){auto* curve=new BlendCurve(plot->xAxis,plot->yAxis);curves_[i]=curve;curve->setLayer("soniclatchWave");curve->setAntialiased(false);curve->setLineStyle(QCPCurve::lsLine);curve->setScatterStyle(QCPScatterStyle::ssNone);curve->setBrush(Qt::NoBrush);QPen pen(colors[i]);pen.setWidthF(1);curve->setPen(pen);}
  plot->yAxis->setRange(-1,1);plot->yAxis->setLabel(names_[0]+QStringLiteral(" / ")+names_[1]);if(legend_){legend_->setWordWrap(true);legend_->setTextFormat(Qt::RichText);auto policy=legend_->sizePolicy();policy.setRetainSizeWhenHidden(true);legend_->setSizePolicy(policy);if(!legend_->findChild<QObject*>(QStringLiteral("soniclatchLegendPaletteObserver"),Qt::FindDirectChildrenOnly))new LegendPaletteObserver(legend_);updateLegend();legend_->hide();}
 }
 bool clear(){const bool had=bool(shown_)||(curves_[0]&&!curves_[0]->data()->isEmpty())||(curves_[1]&&!curves_[1]->data()->isEmpty());shown_.reset();key_.reset();shownPostShift_.reset();width_=0;for(auto& gain:gains_)gain.reset();for(auto& curve:curves_)if(curve)curve->data()->clear();if(legend_)legend_->hide();return had;}
 bool update(std::shared_ptr<const waveform::Frame> frame,const pita::Key& key,uint64_t now){
  if(!plot_||!frame||frame->series.size()!=2||frame->axis.end_ns<=frame->axis.begin_ns)return false;
  if(!key_||!(*key_==key)){clear();key_=key;}
  const auto fullMs=double(frame->axis.end_ns-frame->axis.begin_ns)/1e6;const auto visibleMs=plot_->xAxis->range().size();const auto fullColumns=double(std::max(1,plot_->axisRect()->width()))*(visibleMs>0?std::max(1.,fullMs/visibleMs):1.);const auto width=static_cast<size_t>(std::clamp(fullColumns,1.,double(waveform::max_bins)));
  const bool newData=frame!=shown_;if(!newData&&width==width_)return false;
  std::array<std::vector<Column>,2> data;
  for(size_t i=0;i<2;i++){
   const auto kind=i?waveform::Kind::PostMeasured:waveform::Kind::PreMeasured;
   const auto found=std::find_if(frame->series.begin(),frame->series.end(),[&](auto& s){return s.kind==kind;});if(found==frame->series.end())return false;data[i]=columns(*found,frame->axis,width);
   if(newData){double peak=0;for(const auto& c:data[i])if(c.valid)peak=std::max(peak,std::max(std::abs(double(c.lo)),std::abs(double(c.hi))));gains_[i].update(peak,now);}
  }
  const auto span=double(frame->axis.end_ns-frame->axis.begin_ns)/1e6;
  for(size_t i=0;i<2;i++){
   if(!curves_[i])return false;QVector<double> t,x,y;t.reserve(width*3);x.reserve(width*3);y.reserve(width*3);double order=0;const auto nan=std::numeric_limits<double>::quiet_NaN();
   for(size_t c=0;c<width;c++)if(data[i][c].valid){const auto at=(double(c)+.5)*span/width;for(double v:{double(data[i][c].lo)*gains_[i].value(),double(data[i][c].hi)*gains_[i].value(),nan}){t.push_back(order++);x.push_back(at);y.push_back(v);}}
   curves_[i]->setData(t,x,y,true);
  }
  shown_=std::move(frame);width_=width;++updates_;updateLegend();return true;
 }
 // Exact production path: visible-time pixel columns are built from retained
 // immutable samples. One-ms envelopes remain only for viewport metadata/QA.
 bool update(std::shared_ptr<const displayonly::SampleFrame> frame,const pita::Key& key,uint64_t now,std::optional<pita::Ns> committedOffset=pita::Ns(0)){
  if(!committedOffset)return clear();
  if(!plot_||!frame||frame->end<=frame->begin)return false;
  if(!key_||!(*key_==key)){clear();key_=key;}
  const double fullMs=double(frame->end-frame->begin)/1e6;
  const double lowMs=std::clamp(plot_->xAxis->range().lower,0.,fullMs);
  const double highMs=std::clamp(plot_->xAxis->range().upper,0.,fullMs);
  if(highMs<=lowMs)return false;
  const auto window=displayonly::displayWindow(frame->begin+static_cast<uint64_t>(std::llround(lowMs*1e6)),frame->begin+static_cast<uint64_t>(std::llround(highMs*1e6)),*committedOffset);
  if(!window)return clear();
  const uint64_t begin=window->begin,end=window->end;
  const size_t width=std::clamp<size_t>(std::max(1,plot_->axisRect()->width()),1,8192);
  const bool newData=frame!=shown_;
  if(!newData&&width==width_&&begin==shownBegin_&&end==shownEnd_&&shownPostShift_==committedOffset)return false;
  for(size_t tap=0;tap<2;++tap){
   if(newData)gains_[tap].update(displayonly::samplePeak(*frame,tap),now);
   const auto columns=displayonly::offsetSampleColumns(*frame,tap,begin,end,width,tap?*committedOffset:0);
   QVector<double> order,x,y;order.reserve(width*3);x.reserve(width*3);y.reserve(width*3);
   const double nan=std::numeric_limits<double>::quiet_NaN();double sequence=0;
   for(size_t column=0;column<width;++column)if(columns[column].valid){
    const double at=lowMs+(double(column)+.5)*(highMs-lowMs)/width;
    for(double value:{double(columns[column].lo)*gains_[tap].value(),double(columns[column].hi)*gains_[tap].value(),nan}){order.push_back(sequence++);x.push_back(at);y.push_back(value);}
   }
   if(!curves_[tap])return false;curves_[tap]->setData(order,x,y,true);
  }
  shown_=std::move(frame);shownBegin_=begin;shownEnd_=end;shownPostShift_=committedOffset;width_=width;++updates_;updateLegend();return true;
 }
 BlendCurve* curve(size_t index)const{return curves_.at(index);}
 double gain(size_t index)const{return gains_[index].value();}
 uint64_t updates()const{return updates_;}
};
}
