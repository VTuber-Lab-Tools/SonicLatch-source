#pragma once
#include "qt_waveform_b.hpp"
#include "waveform_viewport.hpp"
#include "display_sample_history.hpp"
#include <QPointer>
#include <QWheelEvent>
#include <functional>
#ifndef SONICLATCH_ENABLE_WHEEL_DIAGNOSTIC
#define SONICLATCH_ENABLE_WHEEL_DIAGNOSTIC 0
#endif

namespace pitatuner::qt::waveb {
// Label-only offset: samples and axis geometry keep their original media-time
// coordinates; the visible left edge reads 0 ms as in the original display.
class VisibleTimeTicker final:public QCPAxisTicker {
protected:
 QString getTickLabel(double tick,const QLocale& locale,QChar format,int precision)override{
  const double relative=tick-tickOrigin();
  return QCPAxisTicker::getTickLabel(std::abs(relative)<1e-9?0.:relative,locale,format,precision);
 }
};
// One UI-thread controller owns both time axes; neither amplitude axis changes.
// The callback must revalidate presentation freshness before drawing, just as a
// normal timer paint does. Do not retain a Snapshot here or connect write actions.
class WheelZoom final:public QObject {
public:
 // Optional, read-only developer observation. No source UUID or PCM is exposed.
 struct Diagnostic {
  const char* stage;const char* reason;QObject* receiver;const QWheelEvent* wheel;
  bool ready;double lowerMs,upperMs,frameDurationMs;bool hasKey;std::uint64_t session,epoch;
 };
 using DiagnosticSink=std::function<void(const Diagnostic&)>;
private:
#if SONICLATCH_ENABLE_WHEEL_DIAGNOSTIC
 DiagnosticSink diagnostic_;double diagnosticFrameDurationMs_{};
 void diagnostic(const char* stage,const char* reason="",QObject* receiver=nullptr,const QWheelEvent* wheel=nullptr) const noexcept {
  if(!diagnostic_)return;
  try{diagnostic_({stage,reason,receiver,wheel,view_.ready(),view_.lowerMs(),view_.upperMs(),diagnosticFrameDurationMs_,bool(key_),key_?key_->session:0,key_?key_->epoch:0});}catch(...){/* Observation cannot change product handling. */}
 }
 void diagnosticDuration(double value){diagnosticFrameDurationMs_=value;}
#else
 void diagnostic(const char*,const char* = "",QObject* = nullptr,const QWheelEvent* = nullptr)const noexcept{}
 void diagnosticDuration(double){}
#endif
 QPointer<QCustomPlot> pre_,post_;
 std::function<void()> redraw_;
 Viewport view_;QSharedPointer<VisibleTimeTicker> visibleTicker_;
 std::optional<pita::Key> key_;
protected:
 bool eventFilter(QObject* watched,QEvent* event) override {
  if(event->type()!=QEvent::Wheel)return QObject::eventFilter(watched,event);
  auto* wheel=static_cast<QWheelEvent*>(event);
  auto* plot=watched==pre_.data()?pre_.data():watched==post_.data()?post_.data():nullptr;
  diagnostic("entry","",watched,wheel);
  if(!plot){diagnostic("return","wrong_target",watched,wheel);return QObject::eventFilter(watched,event);}
  if(!pre_||!post_){diagnostic("return","missing_plot",watched,wheel);return QObject::eventFilter(watched,event);}
  if(!view_.ready()){diagnostic("return","not_ready",watched,wheel);return QObject::eventFilter(watched,event);}
  const auto rect=plot->axisRect()->rect();
  if(!rect.contains(wheel->position().toPoint())){diagnostic("return","outside_axis",watched,wheel);return QObject::eventFilter(watched,event);}
  if(rect.width()<=0){diagnostic("return","empty_axis",watched,wheel);return QObject::eventFilter(watched,event);}
  if(!wheel->angleDelta().y()){diagnostic("return","zero_angle",watched,wheel);return QObject::eventFilter(watched,event);}
  const auto anchor=(wheel->position().x()-rect.left())/rect.width();
  const bool changed=view_.zoom(wheel->angleDelta().y(),anchor);
  diagnostic("zoom",changed?"changed":"at_limit",watched,wheel);
  // Consume at the limit too: do not unexpectedly scroll the surrounding dock.
  wheel->accept();
  if(changed&&redraw_){diagnostic("redraw_before","",watched,wheel);redraw_();diagnostic("redraw_after","",watched,wheel);}
  diagnostic("return",changed?"changed":"at_limit",watched,wheel);
  return true;
 }
public:
 WheelZoom(QCustomPlot* pre,QCustomPlot* post,QObject* parent,std::function<void()> redraw,double initialSpanMs=0)
  :QObject(parent),pre_(pre),post_(post),redraw_(std::move(redraw)),view_(initialSpanMs) {
  if(initialSpanMs>0){visibleTicker_=QSharedPointer<VisibleTimeTicker>::create();visibleTicker_->setTickCount(5);}
  for(auto* plot:{pre,post}){
   if(visibleTicker_)plot->xAxis->setTicker(visibleTicker_);
   // An event filter handles wheel zoom, avoiding QCP's independent x/y zoom.
   plot->setInteraction(QCP::iRangeZoom,false);plot->installEventFilter(this);
  }
 }
 void setDiagnostic(DiagnosticSink sink){
#if SONICLATCH_ENABLE_WHEEL_DIAGNOSTIC
  diagnostic_=std::move(sink);diagnostic("attached");
#else
  (void)sink;
#endif
 }
 void clear(const char* reason="unspecified"){diagnostic("clear_before",reason);view_.clear();key_.reset();diagnosticDuration(0);if(visibleTicker_)visibleTicker_->setTickOrigin(0);diagnostic("clear_after",reason);}
 bool update(const display::Snapshot& snap) {
  if(!key_||!(*key_==snap.key)){clear("snapshot_key_changed");key_=snap.key;}
  if(snap.axis.end_ns<=snap.axis.begin_ns){clear("snapshot_empty_axis");return false;}
  const auto duration=snap.axis.end_ns-snap.axis.begin_ns;
  if(duration>waveform::max_window_ns){clear("snapshot_overlong");return false;}
  std::uint64_t widest=0;
  for(const auto& series:snap.waveforms){
   if(series.bins.size()>waveform::max_bins){clear("snapshot_too_many_bins");return false;}
   for(const auto& bin:series.bins)if(bin.drawable&&bin.minimum&&bin.maximum&&std::isfinite(*bin.minimum)&&std::isfinite(*bin.maximum)&&*bin.minimum<=*bin.maximum&&bin.begin_ns>=snap.axis.begin_ns&&bin.end_ns<=snap.axis.end_ns&&bin.begin_ns<bin.end_ns)
    widest=std::max(widest,bin.end_ns-bin.begin_ns);
  }
  if(!view_.update(double(duration)/1e6,double(widest)/1e6)){clear("snapshot_viewport_rejected");return false;}
  diagnosticDuration(double(duration)/1e6);
  if(visibleTicker_)visibleTicker_->setTickOrigin(view_.lowerMs());
  for(auto* plot:{pre_.data(),post_.data()})if(plot)plot->xAxis->setRange(view_.lowerMs(),view_.upperMs());
  diagnostic("update");
  return true;
 }
 // Production raw samples already carry immutable time bounds. Do not copy
 // and re-scan the 1 ms envelope on every UI tick merely to recover these bounds.
 bool update(const displayonly::SampleFrame& frame,const pita::Key& key){
  if(!key_||!(*key_==key)){clear("raw_key_changed");key_=key;}
  if(frame.end<=frame.begin||frame.end-frame.begin>10000000000ULL){clear("raw_invalid_duration");return false;}
  if(!view_.update(double(frame.end-frame.begin)/1e6,1.)){clear("raw_viewport_rejected");return false;}
  diagnosticDuration(double(frame.end-frame.begin)/1e6);
  if(visibleTicker_)visibleTicker_->setTickOrigin(view_.lowerMs());
  for(auto* plot:{pre_.data(),post_.data()})if(plot)plot->xAxis->setRange(view_.lowerMs(),view_.upperMs());
  diagnostic("update");
  return true;
 }
 const Viewport& viewport() const{return view_;}
};
} // namespace pitatuner::qt::waveb
