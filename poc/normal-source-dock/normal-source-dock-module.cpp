#include <qa_build_gate.hpp>
#define NOMINMAX
#include <obs_source_capture.hpp>
#include <obs_offset_backend.hpp>
#include <correction_profile.hpp>
#include <readback_guard.hpp>
#include <correction_actions.hpp>
#include <first_auto_guard.hpp>
#include <QMessageBox>
#include <selected_source_store.hpp>
#include <last_successful_delay.hpp>
#include <qt_waveform_overlay.hpp>
#include <QDockWidget>
#include <QScreen>
#include <QScrollBar>
#include <QWindow>
#include <QMenu>
#include <QAction>
#include <display_consumer.hpp>
#include <qt_metrics.hpp>
#include <qt_advanced_audio.hpp>
#include <qt_compact_dock.hpp>
#include <obs_locale_dynamic.hpp>
#include <locale_runtime_keys.hpp>
#include "qcustomplot.h"
#include <qt_waveform_b.hpp>
#include <qt_waveform_zoom.hpp>
#if SONICLATCH_ENABLE_WHEEL_DIAGNOSTIC
#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QSet>
#include <chrono>
#endif
#include <QVBoxLayout>
#include <QPushButton>
#include <QComboBox>
#include <QSignalBlocker>
#include <QEvent>
#include <functional>
#include <QPointer>
#include <QTimer>
#include "waveform_cadence.hpp"
#include "qt_waveform_rate.hpp"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QJsonArray>
#include <QTextLayout>
#include <QStyleOptionButton>
#pragma warning(push)
#pragma warning(disable:4201)
#include <obs-frontend-api.h>
#pragma warning(pop)
#include <string_view>
#include <array>
#include <set>
OBS_DECLARE_MODULE()
namespace {lookup_t* localeLookup{};std::unique_ptr<pitatuner::locale::Api> localeApi;}
const char* obs_module_text(const char* key){const char* out=key;if(localeApi&&localeLookup)localeApi->get(localeLookup,key,&out);return out;}
bool obs_module_get_string(const char* key,const char** out){return localeApi&&localeLookup&&localeApi->get(localeLookup,key,out);}
void obs_module_free_locale(){if(localeApi&&localeLookup)localeApi->destroy(localeLookup);localeLookup=nullptr;}
void obs_module_set_locale(const char* language){try{if(!localeApi)localeApi=std::make_unique<pitatuner::locale::Api>();obs_module_free_locale();localeLookup=localeApi->load(obs_current_module(),"en-US",language);}catch(...){localeLookup=nullptr;}}

namespace {
namespace cap=pitatuner::capture;
using Logger=void(*)(int,const char*,...);Logger logger{};
// Fixed owner-side diagnostics only; never include source/user identifiers.
const char* selectionFailureCode(cap::SelectError error)noexcept{
 switch(error){
  case cap::SelectError::Catalog:return "CATALOG";
  case cap::SelectError::AudioConfiguration:return "AUDIO_CONFIGURATION";
  case cap::SelectError::Lookup:return "LOOKUP";
  case cap::SelectError::Identity:return "IDENTITY";
  case cap::SelectError::Exhausted:return "SESSION_EXHAUSTED";
  case cap::SelectError::FilterCreate:return "FILTER_CREATE";
  case cap::SelectError::Signals:return "SIGNALS";
  case cap::SelectError::Upstream:return "UPSTREAM";
  case cap::SelectError::None:return "UNCLASSIFIED";
 }
 return "UNCLASSIFIED";
}

#if SONICLATCH_ENABLE_EMBEDDED_QA
bool isolatedQa=false;
#else
constexpr bool isolatedQa=false;
#endif
std::unique_ptr<cap::Api> api;std::thread controller;std::atomic<bool> quitting{};
#define PAPI(n) decltype(&n) p_##n=nullptr
PAPI(obs_source_set_muted);PAPI(obs_source_update);PAPI(obs_source_set_enabled);PAPI(obs_source_filter_remove);PAPI(obs_source_filter_set_order);PAPI(obs_source_create);PAPI(obs_source_create_private);PAPI(obs_source_release);PAPI(obs_source_get_ref);PAPI(obs_source_filter_add);PAPI(obs_source_output_audio);PAPI(obs_source_get_uuid);PAPI(obs_source_remove);PAPI(obs_source_get_sync_offset);PAPI(obs_source_set_sync_offset);PAPI(obs_wait_for_destroy_queue);
PAPI(obs_data_create);PAPI(obs_data_set_int);PAPI(obs_data_get_int);PAPI(obs_data_release);
PAPI(obs_frontend_add_event_callback);PAPI(obs_frontend_remove_event_callback);
std::atomic<bool> nonSilentInvalid{};thread_local bool noisyPacket{};std::atomic<unsigned> noisyOracleSkips{};
std::atomic<unsigned> created{},destroyed[2],observerMismatch{};unsigned failed{},cases{};
std::atomic<unsigned> qa37Created{},qa37Destroyed[2],qa37VictimLookupCalls{},qa37VictimRefs{},qa37TwinLookupCalls{};std::string qa37VictimUuid,qa37TwinUuid;
decltype(&obs_get_source_by_uuid) qa37RealLookup{};
obs_source_t* qa37Lookup(const char* uuid){const bool victim=uuid&&qa37VictimUuid==uuid;const bool twin=uuid&&qa37TwinUuid==uuid;if(victim)++qa37VictimLookupCalls;if(twin)++qa37TwinLookupCalls;auto* value=qa37RealLookup(uuid);if(victim&&value)++qa37VictimRefs;return value;}
struct QA37Input {unsigned index;};
void* qa37Create(obs_data_t*,obs_source_t*){return new QA37Input{qa37Created.fetch_add(1)};}
void qa37Destroy(void* data){auto* value=static_cast<QA37Input*>(data);if(value->index<2)++qa37Destroyed[value->index];delete value;}
struct Input {unsigned index;};
struct Expected {const float* left{};const float* right{};uint32_t frames{};uint64_t timestamp{};int64_t delay{};};
thread_local Expected expected;
const char* inputName(void*){return "Known normal-source pipeline test input";}
void* inputCreate(obs_data_t*,obs_source_t*){return new Input{created.fetch_add(1)};}
void inputDestroy(void* data){auto* p=static_cast<Input*>(data);if(p->index<2)++destroyed[p->index];delete p;}
void* filterCreate(obs_data_t* settings,obs_source_t*){return new int64_t(p_obs_data_get_int(settings,"delayNs"));}
void filterUpdate(void* data,obs_data_t* settings){*static_cast<int64_t*>(data)=p_obs_data_get_int(settings,"delayNs");}
void filterDestroy(void* data){delete static_cast<int64_t*>(data);}
obs_audio_data* shift(void* data,obs_audio_data* a){a->timestamp+=static_cast<uint64_t>(*static_cast<int64_t*>(data));noisyPacket=nonSilentInvalid.load();if(noisyPacket){thread_local uint32_t rng=772231U;for(size_t ch=0;ch<2;ch++)for(uint32_t i=0;i<a->frames;i++){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;reinterpret_cast<float*>(a->data[ch])[i]=static_cast<float>(static_cast<int32_t>(rng))/4294967296.0F;}}return a;}
obs_audio_data* observe(void*,obs_audio_data* a){
 const bool valid=expected.left&&a->frames==expected.frames&&a->timestamp==expected.timestamp+static_cast<uint64_t>(expected.delay)&&a->data[0]&&a->data[1]&&
  (noisyPacket||(!std::memcmp(a->data[0],expected.left,a->frames*sizeof(float))&&!std::memcmp(a->data[1],expected.right,a->frames*sizeof(float))));
 if(noisyPacket)++noisyOracleSkips;
 if(!valid)++observerMismatch;return a;
}
obs_audio_data* passthrough(void*,obs_audio_data* a){return a;}
std::set<std::string> checks;
void check(const char* name,bool pass){const bool first=checks.insert(name).second;if(first)++cases;if(!pass)++failed;if(first||!pass)logger(pass?LOG_INFO:LOG_ERROR,"PT_PRODUCT_CASE name=%s pass=%s",name,pass?"true":"false");if(!pass)throw std::runtime_error(name);}

struct Source {
 obs_source_t* source{},*shiftFilter{},*observer{},*extra{};std::string uuid;std::jthread producer;unsigned identity;int64_t delay;uint32_t frames;std::atomic<bool> silent{};std::atomic<uint64_t> qaMediaJumpNs{};
 Source(const char* name,unsigned id,int64_t shiftNs,uint32_t n):identity(id),delay(shiftNs),frames(n){
  source=p_obs_source_create("pitatuner_known_normal_input",name,nullptr,nullptr);if(!source)throw std::runtime_error("known normal source creation");uuid=p_obs_source_get_uuid(source);
  auto* settings=p_obs_data_create();p_obs_data_set_int(settings,"delayNs",delay);
  shiftFilter=p_obs_source_create_private("pitatuner_known_shift", "known timestamp-only processing",settings);observer=p_obs_source_create_private("pitatuner_known_observer","known nonmutation observer",settings);p_obs_data_release(settings);
  if(!shiftFilter||!observer)throw std::runtime_error("known filter creation");p_obs_source_filter_add(source,shiftFilter);p_obs_source_filter_add(source,observer);
  p_obs_source_set_sync_offset(source,id?120000000:0); // Fixture initialization only; product capture has no setter.
 }
 uint32_t noiseState{};uint64_t samplesProduced{};
 void start(){auto* held=p_obs_source_get_ref(source);producer=std::jthread([this,held](std::stop_token stop){std::array<float,1024> left{},right{};if(!noiseState)noiseState=20261018+identity;auto& noise=noiseState;auto& samples=samplesProduced;
  obs_source_audio audio{};audio.data[0]=reinterpret_cast<uint8_t*>(left.data());audio.data[1]=reinterpret_cast<uint8_t*>(right.data());audio.frames=frames;audio.samples_per_sec=48000;audio.format=AUDIO_FORMAT_FLOAT_PLANAR;audio.speakers=SPEAKERS_STEREO;
  while(!stop.stop_requested()&&!quitting.load()){
   for(size_t i=0;i<frames;i++){noise^=noise<<13;noise^=noise>>17;noise^=noise<<5;left[i]=static_cast<float>(static_cast<int32_t>(noise))/4294967296.0f;if(silent.load(std::memory_order_relaxed))left[i]=0;right[i]=-left[i];}
   audio.timestamp=10000000000ULL+qaMediaJumpNs.load()+samples/48000*1000000000ULL+(samples%48000)*1000000000ULL/48000;expected={left.data(),right.data(),frames,audio.timestamp,delay};p_obs_source_output_audio(held,&audio);samples+=frames;std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }expected={};p_obs_source_release(held);
 });}
 void stopProducer(){if(producer.joinable()){producer.request_stop();producer.join();}}
 void addExtra(){extra=p_obs_source_create_private("pitatuner_known_passthrough","known added processing",nullptr);p_obs_source_filter_add(source,extra);}
 void releaseOwner(){if(source){p_obs_source_release(source);source=nullptr;}}
 ~Source(){stopProducer();releaseOwner();if(shiftFilter)p_obs_source_release(shiftFilter);if(observer)p_obs_source_release(observer);if(extra)p_obs_source_release(extra);}
};

namespace d=pitatuner::display;namespace q=pita::delivery;
QPointer<pitatuner::qt::waveb::WheelZoom> waveZoom;
QPointer<QWidget> root;QCustomPlot *upper{},*lower{};QPushButton* readButton{};QComboBox* picker{};QTimer* ticker{};std::unique_ptr<pitatuner::qt::Metrics> metrics;
decltype(&obs_frontend_add_dock_by_id) addDock{};decltype(&obs_frontend_remove_dock) removeDock{};
// This developer-only observer is separate from the isolated QA modes. It never
// consumes input, changes focus/acceptance, resends events or queries capture.
#if SONICLATCH_ENABLE_WHEEL_DIAGNOSTIC
class WheelDiagnostic final:public QObject,public QAbstractNativeEventFilter {
 using Zoom=pitatuner::qt::waveb::WheelZoom;
 QPointer<QWidget> root_;QPointer<QCustomPlot> plot_;QPointer<Zoom> zoom_;
 Zoom::Diagnostic latest_{"", "", nullptr,nullptr,false,0,0,0,false,0,0};
 bool spent_{},active_{},inRedraw_{},followDraw_{},qtSeen_{},nativeSeen_{};
 unsigned beforeReplots_{},afterReplots_{};
 std::chrono::steady_clock::time_point start_{};
 ulong qtTimestamp_{};QPoint angle_,pixel_;QPointF global_;Qt::ScrollPhase phase_{};
 DWORD nativeTime_{};HWND nativeWindow_{};UINT nativeMessage_{};WPARAM nativeWParam_{};LPARAM nativeLParam_{};
 QByteArray lastUpdate_;QSet<QByteArray> emittedClears_;
 static QString pointer(const void* p){return QString::number(reinterpret_cast<quintptr>(p),16);}
 static QString objectClass(const QObject* p){return p?QString::fromLatin1(p->metaObject()->className()):QString{};}
 static QJsonArray point(QPointF p){return {p.x(),p.y()};}
 static QJsonArray rectangle(QRect r){return {r.x(),r.y(),r.width(),r.height()};}
 qint64 elapsedUs()const{return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start_).count();}
 QJsonObject state()const{
  QJsonObject out{{"observer",pointer(this)},{"plot",pointer(plot_.data())},{"zoom",pointer(zoom_.data())},{"ready",latest_.ready},{"viewLowerMs",latest_.lowerMs},{"viewUpperMs",latest_.upperMs},{"viewSpanMs",latest_.upperMs-latest_.lowerMs},{"frameDurationMs",latest_.frameDurationMs},{"hasKey",latest_.hasKey},{"session",QString::number(latest_.session)},{"epoch",QString::number(latest_.epoch)}};
  if(plot_){const auto range=plot_->xAxis->range();out["axisLowerMs"]=range.lower;out["axisUpperMs"]=range.upper;out["axisSpanMs"]=range.size();out["axisRect"]=rectangle(plot_->axisRect()->rect());out["visible"]=plot_->isVisible();out["enabled"]=plot_->isEnabled();out["dpr"]=plot_->devicePixelRatioF();}
  return out;
 }
 void write(const char* stage,QJsonObject details={})const{
  auto row=state();for(auto i=details.constBegin();i!=details.constEnd();++i)row[i.key()]=i.value();row["stage"]=stage;row["wheelId"]=active_||spent_?1:0;row["elapsedUs"]=active_||spent_?double(elapsedUs()):0.;
  const auto json=QJsonDocument(row).toJson(QJsonDocument::Compact);logger(LOG_INFO,"SL_WHEEL_TRACE %s",json.constData());
 }
 bool withinWindow(){if(active_&&elapsedUs()>500000)finish("500ms_elapsed");return active_;}
 void start(const char* route){
  if(spent_||!root_)return;spent_=active_=true;start_=std::chrono::steady_clock::now();write("begin",{{"route",route}});
  const QPointer<WheelDiagnostic> self(this);
  QTimer::singleShot(500,Qt::PreciseTimer,root_.data(),[self]{if(self&&self->active_)self->finish("500ms_timer");});
 }
 void finish(const char* reason){if(!active_)return;write("end",{{"reason",reason},{"beforeReplots",int(beforeReplots_)},{"afterReplots",int(afterReplots_)}});active_=false;}
 bool correlate(const QWheelEvent* wheel){
  if(!active_&&!spent_)start("qt_without_native");if(!withinWindow())return false;
  if(!qtSeen_){qtSeen_=true;qtTimestamp_=wheel->timestamp();angle_=wheel->angleDelta();pixel_=wheel->pixelDelta();global_=wheel->globalPosition();phase_=wheel->phase();}
  else if(qtTimestamp_!=wheel->timestamp()||angle_!=wheel->angleDelta()||pixel_!=wheel->pixelDelta()||global_!=wheel->globalPosition()||phase_!=wheel->phase()){finish("interrupted_by_another_qt_wheel");return false;}
  return true;
 }
 QJsonObject wheelDetails(QObject* receiver,const QWheelEvent* wheel)const{
  QJsonObject out{{"receiver",pointer(receiver)},{"receiverClass",objectClass(receiver)},{"isTargetPlot",receiver==plot_.data()},{"event",pointer(wheel)},{"qtTimestamp",QString::number(wheel->timestamp())},{"local",point(wheel->position())},{"globalLogical",point(wheel->globalPosition())},{"angle",point(wheel->angleDelta())},{"pixel",point(wheel->pixelDelta())},{"phase",int(wheel->phase())},{"spontaneous",wheel->spontaneous()},{"accepted",wheel->isAccepted()},{"popupClass",objectClass(QApplication::activePopupWidget())},{"modalClass",objectClass(QApplication::activeModalWidget())},{"grabberClass",objectClass(QWidget::mouseGrabber())}};
  if(plot_){const auto mapped=plot_->mapFromGlobal(wheel->globalPosition().toPoint());out["mappedPlotLogical"]=point(mapped);out["mappedInsideAxis"]=plot_->axisRect()->rect().contains(mapped);}
  return out;
 }
 bool eventFilter(QObject* receiver,QEvent* event)override{
  if(event->type()==QEvent::Wheel){try{auto* wheel=static_cast<QWheelEvent*>(event);if(correlate(wheel))write("qt_receiver",wheelDetails(receiver,wheel));}catch(...){}}
  return false; // Observation must never affect routing or acceptance.
 }
 bool nativeEventFilter(const QByteArray& eventType,void* message,qintptr*)override{
  try{
   if((eventType!="windows_generic_MSG"&&eventType!="windows_dispatcher_MSG")||!message)return false;
   const auto* msg=static_cast<const MSG*>(message);if(msg->message!=WM_MOUSEWHEEL&&msg->message!=WM_MOUSEHWHEEL)return false;
   if(!active_&&!spent_)start("native");if(!withinWindow())return false;
   if(nativeSeen_&&(nativeTime_!=msg->time||nativeWindow_!=msg->hwnd||nativeMessage_!=msg->message||nativeWParam_!=msg->wParam||nativeLParam_!=msg->lParam)){finish("interrupted_by_another_native_wheel");return false;}
   nativeSeen_=true;nativeTime_=msg->time;nativeWindow_=msg->hwnd;nativeMessage_=msg->message;nativeWParam_=msg->wParam;nativeLParam_=msg->lParam;
   write("native",{{"eventType",QString::fromLatin1(eventType)},{"message",int(msg->message)},{"hwnd",pointer(msg->hwnd)},{"nativeTimeMs",QString::number(msg->time)},{"delta",int(static_cast<short>(HIWORD(msg->wParam)))},{"globalPhysical",QJsonArray{int(static_cast<short>(LOWORD(msg->lParam))),int(static_cast<short>(HIWORD(msg->lParam)))}}});
  }catch(...){}
  return false; // Do not touch the native result pointer.
 }
 void replot(bool before){try{if(!withinWindow())return;auto& count=before?beforeReplots_:afterReplots_;++count;if(count<=2)write(before?"before_replot":"after_replot",{{"ordinal",int(count)}});}catch(...) {}}
public:
 WheelDiagnostic(QWidget* root,QCustomPlot* plot,Zoom* zoom):QObject(root),root_(root),plot_(plot),zoom_(zoom){
  const QPointer<WheelDiagnostic> self(this);
  zoom->setDiagnostic([self](const Zoom::Diagnostic& event){if(self)self->observe(event);});
  qApp->installEventFilter(this);qApp->installNativeEventFilter(this);
  QObject::connect(plot,&QCustomPlot::beforeReplot,root,[self]{if(self)self->replot(true);});
  QObject::connect(plot,&QCustomPlot::afterReplot,root,[self]{if(self)self->replot(false);});
  write("installed",{{"qVersion",qVersion()},{"plotGeometry",rectangle(plot->geometry())},{"plotGlobalLogical",point(plot->mapToGlobal(QPoint{}))},{"windowGeometry",rectangle(plot->window()->geometry())},{"window",pointer(plot->window())},{"trigger","first_wheel_only"},{"windowMs",500},{"observerConsumes",false}});
 }
 ~WheelDiagnostic()override{try{if(qApp){qApp->removeEventFilter(this);qApp->removeNativeEventFilter(this);}if(zoom_)zoom_->setDiagnostic({});if(active_)finish("observer_removed");}catch(...) {}}
 void observe(const Zoom::Diagnostic& event){
  // Copy only scalar state from the already-running product path. Never retain
  // the event/receiver pointers or call a capture, offset or queue getter here.
  latest_=event;latest_.receiver=nullptr;latest_.wheel=nullptr;
  if(event.wheel&&!correlate(event.wheel))return;if(!withinWindow())return;
  if(!strcmp(event.stage,"redraw_before"))inRedraw_=true;
  if(!strcmp(event.stage,"redraw_after"))inRedraw_=false;
  auto details=event.wheel?wheelDetails(event.receiver,event.wheel):QJsonObject{};details["reason"]=event.reason;
  if(!strcmp(event.stage,"update")){const auto signature=QJsonDocument(state()).toJson(QJsonDocument::Compact);if(signature==lastUpdate_)return;lastUpdate_=signature;}
  if(!strcmp(event.stage,"clear_before")||!strcmp(event.stage,"clear_after")){const auto signature=QByteArray(event.stage)+":"+event.reason+":"+QJsonDocument(state()).toJson(QJsonDocument::Compact);if(emittedClears_.contains(signature))return;emittedClears_.insert(signature);}
  write(event.stage,details);
 }
 void drawComplete(){try{if(!withinWindow())return;if(inRedraw_)write("wheel_draw_complete");else if(!followDraw_){followDraw_=true;write("first_following_draw");}}catch(...) {}}
};
QPointer<WheelDiagnostic> wheelDiagnostic;
void installWheelDiagnostic(){if(!isolatedQa&&qgetenv("SONICLATCH_WHEEL_DIAGNOSTIC")=="one-wheel-v1"&&root&&upper&&waveZoom&&!wheelDiagnostic)wheelDiagnostic=new WheelDiagnostic(root,upper,waveZoom);}
void removeWheelDiagnostic(){if(wheelDiagnostic)delete wheelDiagnostic.data();}
void wheelDiagnosticDrawComplete(){if(wheelDiagnostic)wheelDiagnostic->drawComplete();}
#else
void installWheelDiagnostic(){}
void removeWheelDiagnostic(){}
void wheelDiagnosticDrawComplete(){}
#endif

void saveL04Layout(QWidget* widget,const QString& dir,const QString& tag){
 if(SL_QA_ENV("PITATUNER_L04_QA")!="known-screen-v1"||(tag!="normal-source-dock"&&tag!="50b-invalid-cleared"))return;
 QJsonArray rows;const auto capture=[&](QWidget* child,const QString& text,bool wrap,const QRect& area,const char* kind){
  QTextLayout shaped(text,child->font());QTextOption option;option.setWrapMode(wrap?QTextOption::WordWrap:QTextOption::NoWrap);shaped.setTextOption(option);shaped.beginLayout();qreal height=0,width=0;unsigned lineCount=0;
  while(true){auto line=shaped.createLine();if(!line.isValid())break;line.setLineWidth(wrap?area.width():100000);line.setPosition(QPointF(0,height));height+=line.height();width=std::max(width,line.naturalTextWidth());++lineCount;}shaped.endLayout();unsigned missing=0;for(const auto& run:shaped.glyphRuns())for(auto glyph:run.glyphIndexes())if(!glyph)++missing;
  QJsonObject row;row["kind"]=kind;row["text"]=text;row["wordWrap"]=wrap;row["areaWidth"]=area.width();row["areaHeight"]=area.height();row["textWidth"]=width;row["textHeight"]=height;row["lines"]=static_cast<int>(lineCount);row["missingGlyphs"]=static_cast<int>(missing);row["replacementCharacters"]=text.count(QChar::ReplacementCharacter);row["widthRisk"]=width>area.width()+1;row["heightRisk"]=height>area.height()+1;row["font"]=child->font().family();rows.append(row);
 };
 for(auto* label:widget->findChildren<QLabel*>())capture(label,label->text(),label->wordWrap(),label->contentsRect(),"QLabel");
 for(auto* button:widget->findChildren<QPushButton*>()){QStyleOptionButton option;option.initFrom(button);capture(button,button->text(),false,button->style()->subElementRect(QStyle::SE_PushButtonContents,&option,button),"QPushButton");}
 QJsonObject result;result["locale"]=QString::fromUtf8(SL_QA_ENV("PITATUNER_LOCALE_EXPECT"));result["state"]=tag;result["rootWidth"]=widget->width();result["rootHeight"]=widget->height();result["controls"]=rows;QFile file(dir+"/l04-"+tag+".json");if(!file.open(QIODevice::WriteOnly)||file.write(QJsonDocument(result).toJson())<0)throw std::runtime_error("L04 QA output failed");
}
struct Owner {
 bool qaFourActive=SL_QA_ENV("SONICLATCH_FOUR_QA")=="isolated-ui-v1";int qaFourPhase{};unsigned qaFourFrames{};uint64_t qaFourAutoReads{};QByteArray qaFourPreviousGeometry;unsigned qaFourStableGeometryTicks{};uint64_t qaFourFrameAt{};std::thread qaFourWorker;d::Packet qaFourOld;uint64_t qaFourRevision{},qaFourNotifications{};DWORD qaFourWriterTid{};
 void qaFourClose(){auto get=reinterpret_cast<decltype(&obs_frontend_get_main_window)>(GetProcAddress(GetModuleHandleW(L"obs-frontend-api.dll"),"obs_frontend_get_main_window"));if(get){auto* main=static_cast<QWidget*>(get());QTimer::singleShot(0,main,[main]{main->close();});}}
 void qaFourImage(const QString& tag){save(tag);auto* scroll=root->findChild<QScrollArea*>();check("four_scroll_found",scroll);check("four_full_widget_image",scroll->widget()->grab().save(QCoreApplication::applicationDirPath()+"/../../config/pitatuner-integration/"+tag+"-full.png"));}
 void qaFourExternal(pita::Ns value){b->stopProducer();qaFourOld=lastPacket;check("four_baseline_seed_without_write",confirmCorrection(pita::Mode::MeasuredReference,true));check("four_baseline_present_before_external",control->view().baseline.has_value());qaFourRevision=backend->externalRevision();qaFourNotifications=backend->notifications();auto* held=p_obs_source_get_ref(b->source);qaFourWorker=std::thread([this,held,value]{qaFourWriterTid=GetCurrentThreadId();p_obs_source_set_sync_offset(held,value);p_obs_source_release(held);});}
 bool qaFourCase(bool valid){if(!qaFourActive)return false;auto* content=dynamic_cast<pitatuner::qt::CompactDock*>(root->findChild<QScrollArea*>()->widget());
  if(qaFourPhase==0&&valid){if(auto* dock=qobject_cast<QDockWidget*>(root->parentWidget())){dock->setFloating(true);dock->show();}root->setFixedWidth(360);root->show();check("four_actual_dock_visible",root->isVisible());check("four_initial_auto_read_a",control->view().offset==0&&!invalidated&&queue.state()==q::State::Ready);check("four_initial_no_write_authority",metrics->writesDisabled()&&!control->view().baseline&&!control->view().canUndo);qaFourImage("four-initial-a");choose(*b);check("four_switch_auto_read_b",control->view().offset==120000000&&metrics->value(1).startsWith(pitatuner::qt::nsValue(120000000)));qaFourPhase=1;return true;}
  if(qaFourPhase==1&&valid){if(now()-qaFourFrameAt<100000000ULL)return true;qaFourFrameAt=now();const auto dir=QCoreApplication::applicationDirPath()+"/../../config/pitatuner-integration";check("four_frame_image",content->grab().save(dir+QString("/four-frame-%1.png").arg(qaFourFrames,3,10,QChar('0'))));logger(LOG_INFO,"SL_FOUR_FRAME index=%u steadyNs=%llu sequence=%llu",qaFourFrames,now(),sequence);++qaFourFrames;if(qaFourFrames<20)return true;
   check("four_360_layout",root->width()==360&&content->width()<=root->findChild<QScrollArea*>()->viewport()->width());qaFourImage("four-live-b");content->settingsButton()->click();logger(LOG_INFO,"SL_FOUR_SETTINGS rootVisible=%s panelHidden=%s readHidden=%s editorHidden=%s",root->isVisible()?"true":"false",content->settingsPanel()->isHidden()?"true":"false",readButton->isHidden()?"true":"false",metrics->manualEditor()->isHidden()?"true":"false");check("four_settings_open",!content->settingsPanel()->isHidden()&&!readButton->isHidden()&&!metrics->manualEditor()->isHidden());qaFourPhase=7;qaFourFrameAt=now();return true;}

  if(qaFourPhase==7){
   QJsonArray rows;std::vector<QRect> rectangles;auto capture=[&](QWidget* widget,const QString& name){if(!widget->isVisible())return;const auto at=widget->mapTo(content,QPoint());QRect rect(at,widget->size());rectangles.push_back(rect);rows.append(QJsonObject{{"name",name},{"x",rect.x()},{"y",rect.y()},{"width",rect.width()},{"height",rect.height()}});};
   capture(picker,"source");capture(upper,"pre");capture(lower,"post");for(size_t i=0;i<6;++i){capture(metrics->metricLabel(i),QString("label-%1").arg(i));capture(metrics->metricValue(i),QString("value-%1").arg(i));}capture(metrics->manualEditor(),"manual-input");for(auto* button:content->findChildren<QPushButton*>())capture(button,button->text());
   const auto signature=QJsonDocument(rows).toJson(QJsonDocument::Compact);qaFourStableGeometryTicks=signature==qaFourPreviousGeometry?qaFourStableGeometryTicks+1:0;qaFourPreviousGeometry=signature;
   if(now()-qaFourFrameAt<250000000ULL||qaFourStableGeometryTicks<3)return true;
   bool overlap=false,bounds=true;for(size_t i=0;i<rectangles.size();++i){const auto& a=rectangles[i];bounds=bounds&&a.width()>0&&a.height()>0&&a.x()>=0&&a.right()<content->width();for(size_t j=i+1;j<rectangles.size();++j)overlap=overlap||a.intersects(rectangles[j]);}
   QJsonObject result{{"settledTicks",int(qaFourStableGeometryTicks)},{"elapsedNs",QString::number(now()-qaFourFrameAt)},{"overlap",overlap},{"horizontalBounds",bounds},{"rows",rows}};QFile proof(QCoreApplication::applicationDirPath()+"/../../config/pitatuner-integration/four-settings-geometry.json");check("four_settings_geometry_saved",proof.open(QIODevice::WriteOnly)&&proof.write(QJsonDocument(result).toJson())>0);qaFourImage("four-settings-settled-b");logger(LOG_INFO,"SL_FOUR_SETTINGS_SETTLED ticks=%u elapsedNs=%llu overlap=%s",qaFourStableGeometryTicks,now()-qaFourFrameAt,overlap?"true":"false");check("four_settled_settings_no_overlap",!overlap&&bounds);check("four_settled_settings_visible",content->settingsPanel()->isVisible()&&readButton->isVisible()&&metrics->manualEditor()->isVisible());
content->settingsButton()->click();const auto notifications=backend->notifications();for(size_t i=0;i<8;++i)check("four_unverified_operations_rejected",correctionClick(i)==pita::Result::Rejected);
   check("four_no_write_from_operations",backend->notifications()==notifications&&p_obs_source_get_sync_offset(b->source)==120000000&&p_obs_source_get_sync_offset(a->source)==0&&!control->view().canUndo);qaFourExternal(123000000);qaFourPhase=2;return true;}
  if(qaFourPhase==2||qaFourPhase==4){const auto state=control->view();logger(LOG_INFO,"SL_FOUR_EXTERNAL phase=%d beforeRevision=%llu afterRevision=%llu notifications=%llu offsetNs=%lld",qaFourPhase,qaFourRevision,backend->externalRevision(),backend->notifications(),state.offset.value_or(-1));check("four_external_callback_off_ui",qaFourWriterTid!=GetCurrentThreadId());check("four_external_revision_delivered",backend->externalRevision()>qaFourRevision&&backend->notifications()>qaFourNotifications);check("four_external_auto_read_no_old_authority",!invalidated&&state.offset==123000000&&!state.baseline&&!state.proposal&&!state.canUndo&&metrics->writesDisabled());check("four_old_packet_rejected",!consumer.receive(qaFourOld,context(),static_cast<pita::Ns>(now())));qaFourImage(qaFourPhase==2?"four-external-b":"four-same-value-b");if(qaFourPhase==2){b->start();qaFourPhase=3;}else{const auto n=backend->notifications();const auto reads=qaFourAutoReads;root->hide();root->show();logger(LOG_INFO,"SL_FOUR_REDISPLAY beforeAutoReads=%llu afterAutoReads=%llu visible=%s",reads,qaFourAutoReads,root->isVisible()?"true":"false");check("four_redisplay_read_invoked",qaFourAutoReads>reads&&root->isVisible());check("four_redisplay_read_only",control->view().offset==123000000&&backend->notifications()==n&&metrics->writesDisabled());qaFourImage("four-redisplay-b");choose(*a);check("four_switch_back_auto_zero",control->view().offset==0&&metrics->value(1).startsWith(pitatuner::qt::nsValue(0)));qaFourPhase=5;}return true;}
  if(qaFourPhase==3&&valid){qaFourExternal(123000000);qaFourPhase=4;return true;}
  if(qaFourPhase==5&&valid){qaFourImage("four-switched-a");a->stopProducer();p_obs_source_remove(a->source);qaFourPhase=6;return true;}
  if(qaFourPhase==6){check("four_removed_source_protected",invalidated&&control->view().protectedState&&!control->view().baseline&&!control->view().canUndo&&metrics->writesDisabled()&&metrics->value(1)==pitatuner::qt::nsValue({})&&upper->plottableCount()==0&&lower->plottableCount()==0);qaFourImage("four-removed-a");select("");check("four_deleted_binding_released",!control&&!backend&&binding.key().uuid.empty());finish();logger(LOG_INFO,"SL_FOUR_DONE frames=%u obsVersion=32.2.2 actualUndoSeeded=false writeGate=unverified",qaFourFrames);qaFourClose();return true;}
  if(now()-phaseAt>30000000000ULL)throw std::runtime_error("four QA stage timeout");return true;
 }
 bool qa37Active=SL_QA_ENV("PITATUNER_37A_QA")=="deleted-before-select-v1",qa37Started{};unsigned qa37Ticks{};obs_source_t* qa37Victim{},*qa37Twin{};std::string qa37Name="QA37 same-name source";int qa37VictimRow=-1,qa37TwinRow=-1;
 void qa37Cleanup(){if(qa37Victim){p_obs_source_remove(qa37Victim);p_obs_source_release(qa37Victim);qa37Victim=nullptr;}if(qa37Twin){p_obs_source_remove(qa37Twin);p_obs_source_release(qa37Twin);qa37Twin=nullptr;}}
 void qa37Setup(){if(!qa37Active)return;qa37Victim=p_obs_source_create("pitatuner_qa37_selector_input",qa37Name.c_str(),nullptr,nullptr);check("37a_victim_created",qa37Victim!=nullptr);qa37VictimUuid=p_obs_source_get_uuid(qa37Victim);check("37a_victim_name",qa37Name==api->obs_source_get_name(qa37Victim));}
 void qa37Enumerated(){if(!qa37Active)return;qa37VictimRow=picker->findData(QString::fromStdString(qa37VictimUuid));check("37a_real_catalog_enumerated_uuid",qa37VictimRow>=0&&picker->itemData(qa37VictimRow).toString()==QString::fromStdString(qa37VictimUuid)&&picker->itemText(qa37VictimRow)==QString::fromStdString(qa37Name));logger(LOG_INFO,"PT_PRODUCT_37A_ENUM uuid=%s row=%d name=%s created=%u selectorDataIsUuid=true",qa37VictimUuid.c_str(),qa37VictimRow,qa37Name.c_str(),qa37Created.load());}
 bool qa37Cleared(){return !control&&!backend&&binding.key().uuid.empty()&&!binding.selectedBorrowedForOwner()&&invalidated&&queue.state()!=q::State::Ready&&!readButton->isEnabled()&&metrics->unknown()&&metrics->writesDisabled()&&metrics->value(0)==pitatuner::qt::nsValue({})&&metrics->value(1)==pitatuner::qt::nsValue({})&&metrics->value(2)==pitatuner::qt::nsValue({})&&metrics->value(3)==pitatuner::qt::nsValue({})&&upper->plottableCount()==0&&lower->plottableCount()==0;}
 bool qa37Case(bool newValid){if(!qa37Active)return false;
  if(!qa37Started&&phase==1&&newValid){qa37Started=true;check("37a_old_display_fresh_before_delete",lastPresentation.snapshot&&upper->plottableCount()>0&&lower->plottableCount()>0);save(QStringLiteral("37a-before-delete"));const auto oldSequence=sequence;const auto oldKey=binding.key();
   p_obs_source_remove(qa37Victim);check("37a_remove_marked",api->obs_source_removed(qa37Victim));p_obs_source_release(qa37Victim);qa37Victim=nullptr;p_obs_wait_for_destroy_queue();check("37a_victim_destroy_completed",qa37Destroyed[0].load()==1);auto* gone=qa37RealLookup(qa37VictimUuid.c_str());check("37a_destroyed_uuid_lookup_null",gone==nullptr);if(gone)p_obs_source_release(gone);
   logger(LOG_INFO,"PT_PRODUCT_37A_DESTROY uuid=%s destroyed=%u lookupNull=true steadyNs=%llu",qa37VictimUuid.c_str(),qa37Destroyed[0].load(),now());
   qa37Twin=p_obs_source_create("pitatuner_qa37_selector_input",qa37Name.c_str(),nullptr,nullptr);check("37a_twin_created",qa37Twin!=nullptr);qa37TwinUuid=p_obs_source_get_uuid(qa37Twin);check("37a_same_name_different_uuid",qa37Name==api->obs_source_get_name(qa37Twin)&&qa37TwinUuid!=qa37VictimUuid);
   const auto entries=cap::catalog(*api);bool victimFound=false,twinFound=false;for(const auto& entry:entries){if(entry.uuid==qa37VictimUuid)victimFound=true;if(entry.uuid==qa37TwinUuid){twinFound=entry.name==qa37Name;picker->addItem(QString::fromStdString(entry.name),QString::fromStdString(entry.uuid));}}
   qa37TwinRow=picker->findData(QString::fromStdString(qa37TwinUuid));check("37a_fresh_catalog_only_twin",!victimFound&&twinFound&&qa37TwinRow>=0&&picker->itemText(qa37TwinRow)==picker->itemText(qa37VictimRow));check("37a_old_display_still_fresh_before_selection",control&&binding.key()==oldKey&&sequence==oldSequence&&lastPacket.snapshot->freshUntil&&static_cast<uint64_t>(*lastPacket.snapshot->freshUntil)>now()&&upper->plottableCount()>0&&lower->plottableCount()>0);
   logger(LOG_INFO,"PT_PRODUCT_37A_SELECT_REQUEST staleUuid=%s staleRow=%d twinUuid=%s twinRow=%d identicalNames=true guiTid=%lu steadyNs=%llu",qa37VictimUuid.c_str(),qa37VictimRow,qa37TwinUuid.c_str(),qa37TwinRow,GetCurrentThreadId(),now());
   picker->setCurrentIndex(qa37VictimRow);check("37a_deleted_selection_fail_closed",qa37Cleared()&&binding.selectError()==cap::SelectError::Catalog);check("37a_no_stale_reference_or_twin_fallback",qa37VictimLookupCalls.load()==0&&qa37VictimRefs.load()==0&&qa37TwinLookupCalls.load()==0&&qa37Destroyed[0].load()==1);save(QStringLiteral("37a-deleted-selection-cleared"));phase=37;return true;
  }
  if(qa37Started&&phase==37){check("37a_failure_stays_clear_ordinary_ticks",qa37Cleared()&&qa37VictimLookupCalls.load()==0&&qa37VictimRefs.load()==0&&qa37TwinLookupCalls.load()==0);++qa37Ticks;logger(LOG_INFO,"PT_PRODUCT_37A_FAILED_SELECTION tick=%llu observedTicks=%u staleUuid=%s error=%d victimLookupCalls=%u victimRefs=%u twinLookupCalls=%u sourceBound=%s controlExists=%s queueReady=%s readEnabled=%s graphsUpper=%d graphsLower=%d",tickNumber,qa37Ticks,qa37VictimUuid.c_str(),static_cast<int>(binding.selectError()),qa37VictimLookupCalls.load(),qa37VictimRefs.load(),qa37TwinLookupCalls.load(),binding.selectedBorrowedForOwner()?"true":"false",control?"true":"false",queue.state()==q::State::Ready?"true":"false",readButton->isEnabled()?"true":"false",upper->plottableCount(),lower->plottableCount());
   if(qa37Ticks==3){save(QStringLiteral("37a-next-ticks-cleared"));picker->setCurrentIndex(qa37TwinRow);check("37a_twin_only_by_explicit_uuid_row",binding.key().uuid==qa37TwinUuid&&binding.selectedBorrowedForOwner()==qa37Twin&&qa37TwinLookupCalls.load()==1&&qa37VictimLookupCalls.load()==0&&readButton->isEnabled());readButton->click();save(QStringLiteral("37a-explicit-twin-selected"));logger(LOG_INFO,"PT_PRODUCT_37A_TWIN selectedUuid=%s staleUuid=%s twinLookupCalls=%u victimLookupCalls=%u sameName=true",binding.key().uuid.c_str(),qa37VictimUuid.c_str(),qa37TwinLookupCalls.load(),qa37VictimLookupCalls.load());finish();}return true;
  }return false;
 }
 QString qa51Mode=QString::fromUtf8(SL_QA_ENV("PITATUNER_51B_QA"));bool qa51Started{},qa51Injected{};uint64_t qa51ArmTick{},qa51DeliveredTick{};unsigned qa51ClearTicks{};d::Packet qa51Old;uint64_t qa51BeforeNotifications{},qa51BeforeRevision{};
 std::thread qa51Worker;signal_handler_t* qa51Signals{};std::atomic<uint64_t> qa51GuiTick{},qa51CompletedTick{},qa51CompletedNs{};std::atomic<DWORD> qa51WorkerTid{},qa51CallbackTid{};std::atomic<unsigned> qa51CallbackCount{};std::atomic<bool> qa51Complete{},qa51WorkerError{};
 static void qa51Signal(void* data,calldata_t*)noexcept{auto& o=*static_cast<Owner*>(data);o.qa51CallbackTid.store(GetCurrentThreadId());o.qa51CallbackCount.fetch_add(1);}
 void qa51Disconnect(){if(qa51Worker.joinable())qa51Worker.join();if(qa51Signals){offsetApi.signal_handler_disconnect(qa51Signals,"audio_sync",qa51Signal,this);qa51Signals=nullptr;}}
 ~Owner(){if(qaFourWorker.joinable())qaFourWorker.join();qa51Disconnect();qa37Cleanup();if(control)control->close();control.reset();backend.reset();binding.stop();}
 void qa51PostException()noexcept{if(!qa51Injected)return;try{const bool clear=!control&&!backend&&binding.key().uuid.empty()&&queue.state()!=q::State::Ready&&upper->plottableCount()==0&&lower->plottableCount()==0&&metrics->value(0)==pitatuner::qt::nsValue({})&&metrics->value(1)==pitatuner::qt::nsValue({})&&metrics->value(2)==pitatuner::qt::nsValue({})&&metrics->value(3)==pitatuner::qt::nsValue({})&&metrics->unknown()&&metrics->writesDisabled()&&!readButton->isEnabled()&&ticker&&!ticker->isActive()&&done;
  logger(LOG_INFO,"PT_PRODUCT_51B_EXCEPTION_RESULT mode=%s pass=%s guiTid=%lu tick=%llu armedTick=%llu controlGone=%s queueReady=%s graphsUpper=%d graphsLower=%d readDisabled=%s timerStopped=%s liveCaptureFilters=%llu",qa51Mode.toUtf8().constData(),clear?"true":"false",GetCurrentThreadId(),tickNumber,qa51ArmTick,!control?"true":"false",queue.state()==q::State::Ready?"true":"false",upper->plottableCount(),lower->plottableCount(),!readButton->isEnabled()?"true":"false",!ticker->isActive()?"true":"false",cap::Binding::liveFilters());if(SL_QA_ENV("PITATUNER_50E_QA")=="terminal-status-v1"){const bool protectedReason=metrics->stateIs(pitatuner::qt::Text::Protected),waitingReason=metrics->stateIs(pitatuner::qt::Text::Waiting);logger(LOG_INFO,"PT_PRODUCT_50E_STATE mode=%s pass=%s protected=%s waiting=%s closedProtection=%s guiTid=%lu tick=%llu",qa51Mode.toUtf8().constData(),clear&&protectedReason&&!waitingReason?"true":"false",protectedReason?"true":"false",waitingReason?"true":"false",clear?"true":"false",GetCurrentThreadId(),tickNumber);save(QStringLiteral("50e-")+qa51Mode+QStringLiteral("-cleared"));}
  save(QStringLiteral("51b-")+qa51Mode+QStringLiteral("-cleared"));
 }catch(...){logger(LOG_ERROR,"PT_PRODUCT_51B_EXCEPTION_RESULT pass=false evidence_failure=true");}}
 bool qa51Case(bool newValid){
  if(qa51Mode.isEmpty())return false;
  if(!qa51Started&&phase==1&&newValid){qa51Started=true;qa51ArmTick=tickNumber;qa51Old=lastPacket;check("51b_initial_fresh_values_waves",lastPresentation.snapshot&&upper->plottableCount()>0&&lower->plottableCount()>0&&metrics->value(0)!=pitatuner::qt::nsValue({}));save(QStringLiteral("51b-")+qa51Mode+QStringLiteral("-before"));logger(LOG_INFO,"PT_PRODUCT_51B_ARM mode=%s guiTid=%lu tick=%llu sequence=%llu graphsUpper=%d graphsLower=%d",qa51Mode.toUtf8().constData(),GetCurrentThreadId(),tickNumber,sequence,upper->plottableCount(),lower->plottableCount());
   if(qa51Mode=="notify"){b->stopProducer();qa51BeforeNotifications=backend->notifications();qa51BeforeRevision=backend->externalRevision();qa51Signals=offsetApi.obs_source_get_signal_handler(b->source);offsetApi.signal_handler_connect(qa51Signals,"audio_sync",qa51Signal,this);auto* held=offsetApi.obs_source_get_ref(b->source);if(!held)throw std::runtime_error("51b held source");qa51Worker=std::thread([this,held]{qa51WorkerTid=GetCurrentThreadId();try{p_obs_source_set_sync_offset(held,120000000);}catch(...){qa51WorkerError=true;}p_obs_source_release(held);qa51CompletedTick=qa51GuiTick.load();qa51CompletedNs=now();qa51Complete.store(true,std::memory_order_release);});}phase=31;return true;
  }
  if(qa51Started&&phase==31){
   if(qa51Mode!="notify")return true;
   if(!qa51Complete.load(std::memory_order_acquire)||renderedAt<qa51CompletedNs.load())return true;
   check("51b_cross_thread_backend_notification",!qa51WorkerError.load()&&qa51WorkerTid.load()!=GetCurrentThreadId()&&qa51CallbackTid.load()==qa51WorkerTid.load()&&qa51CallbackCount.load()==1&&backend->notifications()>qa51BeforeNotifications&&backend->externalRevision()>qa51BeforeRevision);
   const auto state=control->view();const bool clear=!invalidated&&!lastPresentation.snapshot&&upper->plottableCount()==0&&lower->plottableCount()==0&&metrics->value(0)==pitatuner::qt::nsValue({})&&metrics->value(1).startsWith(pitatuner::qt::nsValue(state.offset))&&metrics->writesDisabled()&&!state.protectedState&&!state.baseline&&!state.proposal&&!state.canUndo;
   check("51b_next_ordinary_tick_clears",clear&&qa51DeliveredTick>0&&qa51DeliveredTick<=qa51CompletedTick.load()+1);++qa51ClearTicks;
   logger(LOG_INFO,"PT_PRODUCT_51B_NOTIFY mode=notify clear=%s guiTid=%lu workerTid=%lu callbackTid=%lu completedTick=%llu deliveredTick=%llu observedTick=%llu completionNs=%llu renderedAtNs=%llu notifications=%llu revision=%llu snapshot=%s graphsUpper=%d graphsLower=%d proposal=%s undo=%s",clear?"true":"false",GetCurrentThreadId(),qa51WorkerTid.load(),qa51CallbackTid.load(),qa51CompletedTick.load(),qa51DeliveredTick,tickNumber,qa51CompletedNs.load(),renderedAt,backend->notifications(),backend->externalRevision(),lastPresentation.snapshot?"true":"false",upper->plottableCount(),lower->plottableCount(),state.proposal?"true":"false",state.canUndo?"true":"false");
   check("51b_old_packet_not_revived",!consumer.receive(qa51Old,context(),static_cast<pita::Ns>(now()))&&!control->propose(static_cast<pita::Ns>(now())));if(qa51ClearTicks==1||qa51ClearTicks==3)save(QStringLiteral("51b-notify-tick%1-cleared").arg(qa51ClearTicks));if(qa51ClearTicks>=3){qa51Disconnect();finish();}return true;
  }return false;
 }

 QString qa58Mode=QString::fromUtf8(SL_QA_ENV("PITATUNER_58A_QA"));bool qa58Started{};unsigned qa58Ticks{};d::Packet qa58Old;
 bool qa58Empty(){const auto offset=control&&!invalidated&&!control->view().protectedState?control->view().offset:std::optional<pita::Ns>{};return upper->plottableCount()==0&&lower->plottableCount()==0&&metrics->unknown()&&metrics->writesDisabled()&&metrics->value(0)==pitatuner::qt::nsValue({})&&(offset?metrics->value(1).startsWith(pitatuner::qt::nsValue(offset)):metrics->value(1)==pitatuner::qt::nsValue({}))&&metrics->value(2)==pitatuner::qt::nsValue({})&&metrics->value(3)==pitatuner::qt::nsValue({});}
 bool qa58Case(bool newValid){if(qa58Mode.isEmpty())return false;
  if(!qa58Started&&phase==1&&newValid){qa58Started=true;qa58Old=lastPacket;check("58a_before_fresh_b",lastPresentation.snapshot&&lastPresentation.snapshot->current==120000000&&lastPresentation.snapshot->delay==20000000&&upper->plottableCount()>0&&lower->plottableCount()>0);save(QStringLiteral("58a-")+qa58Mode+QStringLiteral("-before"));const auto oldTicket=ticket;
   if(qa58Mode=="read-failure"){b->stopProducer();p_obs_source_remove(b->source);check("58a_real_source_removed",api->obs_source_removed(b->source));readButton->click();const bool clear=qa58Empty()&&!control&&!backend&&binding.key().uuid.empty()&&invalidated&&queue.state()!=q::State::Ready&&!readButton->isEnabled()&&!ticker->isActive()&&done&&metrics->stateIs(pitatuner::qt::Text::Protected);check("58a_read_failure_closed",clear);readButton->click();check("58a_disabled_read_cannot_reopen",qa58Empty()&&!control&&!backend&&done);save(QStringLiteral("58a-read-failure-cleared"));logger(LOG_INFO,"PT_PRODUCT_58A_NEGATIVE mode=read-failure pass=%s actualReadButton=true removedSource=true protected=true readDisabled=true timerStopped=true oldValuesWavesAbsent=true guiTid=%lu tick=%llu",clear?"true":"false",GetCurrentThreadId(),tickNumber);return true;}
   check("58a_known_mode",qa58Mode=="source-switch");a->stopProducer();choose(*a);check("58a_switch_immediate_empty",qa58Empty());readButton->click();check("58a_new_source_read_empty",qa58Empty()&&queue.state()==q::State::Ready&&binding.key().uuid==a->uuid&&!control->view().protectedState);check("58a_old_source_packet_rejected",!consumer.receive(qa58Old,context(),static_cast<pita::Ns>(now())));logger(LOG_INFO,"PT_PRODUCT_58A_SWITCH oldGeneration=%llu newGeneration=%llu oldSession=%llu newSession=%llu synchronousRead=true oldMeasurementRejected=true",oldTicket.generation,ticket.generation,qa58Old.snapshot->key.session,binding.key().session);save(QStringLiteral("58a-source-switch-cleared"));phase=58;return true;
  }
  if(qa58Started&&phase==58){check("58a_old_packet_stays_rejected",!consumer.receive(qa58Old,context(),static_cast<pita::Ns>(now())));if(qa58Ticks<3){check("58a_ordinary_ticks_remain_empty",!lastPresentation.snapshot&&qa58Empty());++qa58Ticks;logger(LOG_INFO,"PT_PRODUCT_58A_EMPTY tick=%llu observedTicks=%u oldValuesWavesAbsent=true",tickNumber,qa58Ticks);if(qa58Ticks==3){save(QStringLiteral("58a-source-switch-next-ticks-cleared"));a->start();}return true;}
   if(newValid){check("58a_new_a_fresh_not_old_b",lastPresentation.snapshot&&lastPresentation.snapshot->key==binding.key()&&lastPresentation.snapshot->current==0&&lastPresentation.snapshot->delay==0&&upper->plottableCount()>0&&lower->plottableCount()>0&&metrics->writesDisabled()&&!control->view().proposal&&!control->view().canUndo);save(QStringLiteral("58a-source-switch-new-a"));logger(LOG_INFO,"PT_PRODUCT_58A_SWITCH_RESULT pass=true currentNs=0 delayNs=0 oldMeasurementRejected=true automaticRead=true correctionUiDisabled=true");finish();}return true;
  }return false;
 }


 QString qa44Mode=QString::fromUtf8(SL_QA_ENV("PITATUNER_44C_QA"));bool qa44Started{},qa44Stopped{};unsigned qa44Restarts{},qa44EmptyTicks{},qa44StopTicks{};d::Packet qa44Old;uint64_t qa44Session{};
 void qa44Recovery(cap::Binding::Recovery action){if(qa44Mode.isEmpty()||!qa44Started)return;check("44c_transition_numeric_wave_empty",qa58Empty());
  if(action==cap::Binding::Recovery::Restarted){++qa44Restarts;check("44c_new_session_old_packet_rejected",binding.key().session!=qa44Session&&binding.automaticRestartUsed()&&!consumer.receive(qa44Old,context(),static_cast<pita::Ns>(now())));save(QStringLiteral("44c-")+qa44Mode+QStringLiteral("-restart-cleared"));logger(LOG_INFO,"PT_PRODUCT_44C_RESTART oldSession=%llu newSession=%llu restarts=%u currentOffsetNs=%lld valuesAbsent=true oldPacketRejected=true",qa44Session,binding.key().session,qa44Restarts,p_obs_source_get_sync_offset(b->source));}
  else{qa44Stopped=true;check("44c_second_gap_protected",qa44Restarts==1&&!control&&!backend&&binding.key().uuid.empty()&&!readButton->isEnabled()&&metrics->stateIs(pitatuner::qt::Text::Protected));save(QStringLiteral("44c-twice-stop-cleared"));}
 }
 void qa44StoppedTick(){if(!qa44Stopped)return;check("44c_stop_stays_empty",qa58Empty()&&metrics->stateIs(pitatuner::qt::Text::Protected)&&!readButton->isEnabled()&&!control&&!backend&&binding.pollRecovery()==cap::Binding::Recovery::None);++qa44StopTicks;logger(LOG_INFO,"PT_PRODUCT_44C_STOP tick=%llu observed=%u restarts=%u currentOffsetNs=%lld valuesAbsent=true protected=true",tickNumber,qa44StopTicks,qa44Restarts,p_obs_source_get_sync_offset(b->source));if(qa44StopTicks==3){save(QStringLiteral("44c-twice-next-ticks-cleared"));finish();}}
 bool qa44Case(bool newValid){if(qa44Mode.isEmpty())return false;
  if(!qa44Started&&phase==1&&newValid){qa44Started=true;qa44Old=lastPacket;qa44Session=binding.key().session;check("44c_arm_existing_fresh_b",lastPresentation.snapshot&&lastPresentation.snapshot->current==120000000&&lastPresentation.snapshot->delay==20000000&&upper->plottableCount()>0&&lower->plottableCount()>0);save(QStringLiteral("44c-")+qa44Mode+QStringLiteral("-before"));b->qaMediaJumpNs=5000000;phase=44;return true;}
  if(qa44Started&&phase==44){if(!lastPresentation.snapshot){++qa44EmptyTicks;check("44c_wait_continuous_audio_no_values",qa58Empty());return true;}if(newValid&&qa44Restarts==1){check("44c_recovered_new_session_only",lastPresentation.snapshot->key==binding.key()&&binding.key().session!=qa44Session&&lastPresentation.snapshot->current==120000000&&lastPresentation.snapshot->delay==20000000&&qa44EmptyTicks>0&&metrics->writesDisabled());save(QStringLiteral("44c-")+qa44Mode+QStringLiteral("-recovered"));logger(LOG_INFO,"PT_PRODUCT_44C_RECOVERED session=%llu restarts=%u emptyTicks=%u currentOffsetNs=%lld delayNs=20000000",binding.key().session,qa44Restarts,qa44EmptyTicks,p_obs_source_get_sync_offset(b->source));if(qa44Mode=="twice"){b->qaMediaJumpNs=10000000;phase=45;}else finish();}return true;}
  if(qa44Started&&phase==45)return true;return false;
 }

 bool qaBatchActive=isolatedQa&&SL_QA_ENV("SONICLATCH_RECOVERY_BATCH_QA")=="owned-source-v1";
 unsigned qaBatchStep{};bool qaBatchWaiting{},qaBatchFaultSeen{};uint64_t qaBatchDeadline{};pita::Key qaBatchOldKey;d::Packet qaBatchOldPacket;pita::Ns qaBatchOffset{};
 bool qaBatchCase(bool valid){
  if(!qaBatchActive)return false;if(!qaBatchDeadline)qaBatchDeadline=now()+10000000000ULL;if(now()>qaBatchDeadline)throw std::runtime_error("batch recovery timeout");
  if(qaBatchWaiting&&binding.key()==qaBatchOldKey){if(binding.faults()){qaBatchFaultSeen=true;check("batch_change_revokes_current_measurement",!binding.latest());}return true;}
  if(!valid)return true;
  static constexpr const char* events[]{"filter_update","mute_and_update","filter_enable","filter_add","filter_remove","source_volume"};
  if(qaBatchWaiting){
   check("batch_refresh_same_uuid_new_session",qaBatchFaultSeen&&binding.key().uuid==qaBatchOldKey.uuid&&binding.key().session!=qaBatchOldKey.session);
   check("batch_refresh_old_packet_rejected",!consumer.receive(qaBatchOldPacket,context(),static_cast<pita::Ns>(now())));
   check("batch_refresh_no_offset_write",p_obs_source_get_sync_offset(a->source)==qaBatchOffset&&!control->view().canUndo&&!control->view().baseline&&metrics->writesDisabled());
   logger(LOG_INFO,"SL_BATCH_RECOVERY event=%s old_session=%llu new_session=%llu fresh=true offset_unchanged=true stale_packet_rejected=true",events[qaBatchStep],qaBatchOldKey.session,binding.key().session);
   qaBatchWaiting=false;if(++qaBatchStep==std::size(events)){logger(LOG_INFO,"SL_BATCH_RECOVERY_DONE pass=true events=6 manual_reselect=false");finish();qaFourClose();return true;}
  }
  qaBatchOldKey=binding.key();qaBatchOldPacket=lastPacket;qaBatchOffset=p_obs_source_get_sync_offset(a->source);qaBatchWaiting=true;qaBatchFaultSeen=false;qaBatchDeadline=now()+10000000000ULL;
  auto updateOwned=[&]{auto* settings=p_obs_data_create();p_obs_data_set_int(settings,"delayNs",0);p_obs_source_update(a->shiftFilter,settings);p_obs_data_release(settings);};
  if(qaBatchStep==0)updateOwned();
  else if(qaBatchStep==1){p_obs_source_set_muted(a->source,true);updateOwned();p_obs_source_set_muted(a->source,false);}
  else if(qaBatchStep==2){p_obs_source_set_enabled(a->shiftFilter,false);p_obs_source_set_enabled(a->shiftFilter,true);}
  else if(qaBatchStep==3)a->addExtra();
  else if(qaBatchStep==4)p_obs_source_filter_remove(a->source,a->extra);
  else if(qaBatchStep==5){auto setVolume=reinterpret_cast<decltype(&obs_source_set_volume)>(GetProcAddress(GetModuleHandleW(L"obs.dll"),"obs_source_set_volume"));check("batch_owned_volume_api",setVolume!=nullptr);setVolume(a->source,.5f);}
  return true;
 }

 cap::SessionIds ids;cap::Binding binding{*api,ids};pitatuner::DeliveryGate delivery;
 pitatuner::ObsOffsetApi offsetApi{GetModuleHandleW(L"obs.dll")};
 std::uint64_t actionIds{},baselineIds{};std::optional<pita::Estimate> correctionEstimate;bool correctionQaDone{};pita::Result correctionResult=pita::Result::Rejected;
 std::unique_ptr<pitatuner::ObsOffsetBackend> backend;std::unique_ptr<pitatuner::ControlSession> control;
 q::Queue<16> queue;q::Ticket ticket;d::Consumer consumer;d::Packet lastPacket;d::Presentation lastPresentation;uint64_t renderedAt{},tickNumber{},staleDeadline{};unsigned staleTicks{},staleCycle{};
 std::unique_ptr<Source> a,b;uint64_t sequence{};int phase{};uint64_t phaseAt{};bool done{},invalidated{};const char* pendingAutoCheck{};unsigned paints{};uint64_t silentStartSequence{},silentObservedSequence{},invalidStartSequence{},invalidObservedSequence{};
 static uint64_t now(){return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
 d::Context context(){
  if(control&&backend){
   const bool changed=backend->consumeExternalChange();
   if(!invalidated&&(binding.faults()||changed)){
    control->externalChange();queue.invalidate();invalidated=true;
    if(qa51Started&&qa51Mode=="notify"){qa51DeliveredTick=tickNumber;logger(LOG_INFO,"PT_PRODUCT_51B_DELIVERY guiTid=%lu tick=%llu revision=%llu notifications=%llu",GetCurrentThreadId(),tickNumber,backend->externalRevision(),backend->notifications());}
   }
   if(changed&&!binding.faults()){control->externalChange();read(true);}
  }
  auto v=control->view();return {binding.key(),ticket,queue.state(),queue.delivered_sequence(),queue.delivered_sequence(),control->acceptedEstimateSequence(),queue.size()==0,v.closed,v.protectedState};
 }
 void clearDisplay(const char* reason="clear_display"){waveformCadence.reset();if(waveZoom)waveZoom->clear(reason);if(metrics){metrics->authority(false,false,false);metrics->update({});}if(upper){if(overlayRenderer){if(overlayRenderer->clear())upper->replot(QCustomPlot::rpQueuedReplot);}else{upper->clearPlottables();lower->clearPlottables();upper->replot(isolatedQa?QCustomPlot::rpImmediateRefresh:QCustomPlot::rpQueuedReplot);}if(isolatedQa)lower->replot(QCustomPlot::rpImmediateRefresh);}}
 std::unique_ptr<pitatuner::selection::Store> selectedSourceStore;
 void refreshCatalog(){
  if(isolatedQa||!picker)return;
  const auto selected=picker->currentData().toString();QSignalBlocker block(picker);
  picker->clear();for(const auto& source:cap::catalog(*api))picker->addItem(QString::fromStdString(source.name),QString::fromStdString(source.uuid));
  picker->setCurrentIndex(selected.isEmpty()?-1:picker->findData(selected));
 }
 void select(const std::string& uuid,bool remember=false){uiDraws=uiDrawTotalNs=uiDrawMaxNs=uiIntervalMaxNs=lastDrawAt=runtimeLogAt=plotReplots=plotTotalNs=plotMaxNs=plotBeginNs=displayDroppedChunks=0;lastSuccessful.clear();clearDisplay("source_selection");readButton->setEnabled(false);if(control)control->close();control.reset();backend.reset();const bool selected=binding.select(uuid);selectionRejected=!uuid.empty()&&!selected;if(!selected){if(!uuid.empty()){obs_audio_info format{};const bool formatRead=api->obs_get_audio_info(&format);logger(LOG_WARNING,"SL_SELECT_FAILED code=%s formatRead=%s rate=%u speakers=%u",selectionFailureCode(binding.selectError()),formatRead?"true":"false",format.samples_per_sec,static_cast<unsigned>(format.speakers));if(metrics)metrics->showUnbound(true);}queue.invalidate();invalidated=true;sequence=0;return;}check("gui_select_uuid",selected);attachSelected();read(true);
  if(remember&&!isolatedQa&&selectedSourceStore){const bool saved=selectedSourceStore->save(QString::fromStdString(uuid));logger(saved?LOG_INFO:LOG_WARNING,"SL_SELECTION_SAVED ok=%s",saved?"true":"false");}
 }
 // SonicLatch writes use -950..1000 ms, independently of wider host bounds.
 // Input bounds alone do not validate effective correction. Normal product
 // uses the qualified API contract and the product write limits below.
 // Actual writes still require identity, readback/revision and packet guards;
 // output evidence remains representative.
 pita::Range selectedRange{pitatuner::qualification::minimumOffsetNs,pitatuner::qualification::maximumOffsetNs,2,false};
 pita::Range offsetRange(){
#ifdef SONICLATCH_ISOLATED_POSITIVE_FIXTURE
  // Test-only binary, admitted by exact path/version/env below. Only the
  // independent artificial source type may use this positive-transition policy.
  auto* selected=binding.selectedBorrowedForOwner();
  using GetId=const char*(*)(obs_source_t*);auto getId=reinterpret_cast<GetId>(GetProcAddress(GetModuleHandleW(L"obs.dll"),"obs_source_get_id"));
  if(selected&&getId&&!strcmp(getId(selected),"soniclatch_product_driver_input"))return {30000000,100000000,3001,true};
#endif
  return isolatedQa?pita::Range{-500000000,500000000,1,false}:selectedRange;
 }
 bool automaticReady{};
 std::optional<std::array<uint64_t,29>> lastAuthorityDiagnostic;
 void logAuthority(const char* reason){
  if(!metrics)return;
  const auto v=control?std::optional{control->view()}:std::optional<pitatuner::View>{};const auto e=correctionEstimate;
  const auto t=now();const auto measured=binding.latest();const auto revision=backend?backend->externalRevision():0;
  const bool fresh=e&&static_cast<pita::Ns>(t)<e->freshUntil;
  const bool sequenceMatch=e&&measured&&control&&measured->sequence==e->seq&&e->seq==control->acceptedEstimateSequence();
  const bool bindingIdentity=v&&v->key==binding.key(),backendIdentity=v&&backend&&v->key==backend->key();
  const auto baseline=v?v->baseline:std::optional<pita::Baseline>{};const auto proposal=v?v->proposal:std::optional<pita::Proposal>{};
  const bool manual=metrics->manualEditor()->isEnabled(),automatic=metrics->operationButton(5)->isEnabled(),undo=metrics->operationButton(6)->isEnabled();
  const std::array<uint64_t,29> state{bool(v),invalidated,binding.faults(),binding.streamActive(),binding.hasPairedPacket(),uint64_t(queue.state()),v&&v->closed,v&&v->protectedState,v&&v->pending,v&&bool(v->offset),uint64_t(v?v->offset.value_or(0):0),bool(baseline),uint64_t(baseline?baseline->s0:0),uint64_t(baseline?baseline->d0:0),uint64_t(baseline?baseline->m:0),bool(proposal),bindingIdentity,backendIdentity,revision,e&&e->valid,fresh,sequenceMatch,manual,automatic,undo,baseline?baseline->id:0,v?v->key.session:0,v?v->key.epoch:0,automaticReady};
  if(lastAuthorityDiagnostic&&*lastAuthorityDiagnostic==state)return;lastAuthorityDiagnostic=state;
  // Do not use backend->read(): it updates the backend's write-observation cache.
  auto* selected=binding.selectedBorrowedForOwner();const auto getter=selected?p_obs_source_get_sync_offset(selected):0;
  const auto first=proposal?std::optional<pita::Ns>{proposal->target}:std::optional<pita::Ns>{};
  logger(LOG_INFO,"SL_AUTHORITY reason=%s autoEnabled=%s autoWriteReady=%s manualEnabled=%s undoEnabled=%s hasControl=%s invalidated=%s faults=%u active=%s paired=%s queue=%u closed=%s protected=%s pending=%s offsetKnown=%s currentNs=%lld getterKnown=%s getterNs=%lld baseline=%s baseS0=%lld baseD0=%lld baseM=%lld proposal=%s targetNs=%lld firstTarget=%s firstTargetNs=%lld valid=%s fresh=%s freshRemainingMs=%.3f capturedAgeMs=%.3f estimateSeq=%llu latestSeq=%llu acceptedSeq=%llu sequenceMatch=%s bindingIdentity=%s backendIdentity=%s externalRevision=%llu policy=%llu",
   reason,automatic?"true":"false",automaticReady?"true":"false",manual?"true":"false",undo?"true":"false",v?"true":"false",invalidated?"true":"false",binding.faults(),binding.streamActive()?"true":"false",binding.hasPairedPacket()?"true":"false",unsigned(queue.state()),v&&v->closed?"true":"false",v&&v->protectedState?"true":"false",v&&v->pending?"true":"false",v&&v->offset?"true":"false",v?v->offset.value_or(0):0,selected?"true":"false",getter,baseline?"true":"false",baseline?baseline->s0:0,baseline?baseline->d0:0,baseline?baseline->m:0,proposal?"true":"false",proposal?proposal->target:0,first?"true":"false",first.value_or(0),e&&e->valid?"true":"false",fresh?"true":"false",e?double(e->freshUntil-static_cast<pita::Ns>(t))/1e6:0.,measured?double(static_cast<pita::Ns>(t)-static_cast<pita::Ns>(measured->capturedThroughNs))/1e6:-1.,e?e->seq:0,measured?measured->sequence:0,control?control->acceptedEstimateSequence():0,sequenceMatch?"true":"false",bindingIdentity?"true":"false",backendIdentity?"true":"false",revision,offsetRange().policy);
 }
 void authority(){
  automaticReady=false;
  if(!metrics)return;
  if(automaticConfirmationOpen||isolatedQa||!control||!backend||invalidated||binding.faults()){metrics->authority(false,false,false);metrics->operationButton(5)->setToolTip(QString::fromUtf8(obs_module_text("StatusProtected")));logAuthority("StatusProtected");return;}
  const auto currentMeasurement=binding.latest();const auto v=control->view();const bool ready=!v.closed&&!v.protectedState&&!v.pending&&binding.hasPairedPacket()&&v.offset&&v.key==binding.key()&&v.key==backend->key()&&queue.state()==q::State::Ready;
  const bool manual=ready&&(offsetRange().verified||offsetRange().hostContract);
  const bool apply=manual&&binding.streamActive()&&currentMeasurement&&currentMeasurement->valid&&correctionEstimate&&currentMeasurement->sequence==correctionEstimate->seq&&correctionEstimate->key==v.key&&correctionEstimate->seq==control->acceptedEstimateSequence()&&correctionEstimate->valid&&correctionEstimate->confident&&correctionEstimate->active&&!correctionEstimate->muted&&static_cast<pita::Ns>(now())<correctionEstimate->freshUntil&&v.proposal.has_value();
  automaticReady=apply;
  // Availability is structural. Every click still requires the strict fresh gate above.
  // Invalid/ambiguous estimator windows must not flash the button every 250 ms.
  metrics->authority(manual&&(*v.offset!=0||binding.streamActive()),manual&&binding.streamActive(),manual&&v.canUndo&&(*v.offset!=0||binding.streamActive()));
  // Explain existing authority without adding another status row or relaxing it.
  const char* reason=apply?"BtnRecommend":v.pending?"StatusWritePending":!manual?"StatusProtected":
   !correctionEstimate||static_cast<pita::Ns>(now())>=correctionEstimate->freshUntil?"StatusStale":
   !currentMeasurement||!currentMeasurement->valid||!correctionEstimate->valid?"StatusInvalidEstimate":
   !correctionEstimate->confident?"StatusLowConfidence":"StatusNoProposal";
  metrics->operationButton(5)->setToolTip(QString::fromUtf8(obs_module_text(reason)));
  if(readButton)readButton->setEnabled(!v.pending);
  constexpr pita::Ns steps[]{-5000000,-1000000,1000000,5000000};
  for(size_t i=0;i<4;++i){const auto target=v.offset?pita::add(*v.offset,steps[i]):std::optional<pita::Ns>{};metrics->operationButton(i)->setEnabled(manual&&target&&pitatuner::qualification::targetAllowed(offsetRange(),*v.offset,*target,binding.streamActive()));}
  metrics->operationButton(7)->setEnabled(manual&&offsetRange().permits(0));
  logAuthority(reason);
 }
 void attachSelected(){
  selectedRange={pitatuner::qualification::minimumOffsetNs,pitatuner::qualification::maximumOffsetNs,2,false};
  if(!isolatedQa){using Version=const char*(*)();auto version=reinterpret_cast<Version>(GetProcAddress(GetModuleHandleW(L"obs.dll"),"obs_get_version_string"));obs_audio_info format{};const bool formatOk=api->obs_get_audio_info(&format);auto* source=binding.selectedBorrowedForOwner();using SourceId=const char*(*)(obs_source_t*);auto sourceId=reinterpret_cast<SourceId>(GetProcAddress(GetModuleHandleW(L"obs.dll"),"obs_source_get_id"));logger(LOG_INFO,"SL_CORRECTION_SOURCE sourceType=%s guard=same_pre_post_packet",source&&sourceId?sourceId(source):"unknown");if(source)selectedRange=pitatuner::qualification::select(offsetApi.obs_source_get_sync_offset&&offsetApi.obs_source_set_sync_offset&&(api->obs_source_get_output_flags(source)&OBS_SOURCE_AUDIO)&&!api->obs_source_removed(source),formatOk&&cap::format::supportedRate(format.samples_per_sec)&&format.speakers==SPEAKERS_STEREO,p_obs_source_get_sync_offset(source));logger(LOG_INFO,"SL_CORRECTION_POLICY policy=%llu outputVerified=%s hostContract=%s minNs=%lld maxNs=%lld hostVersion=%s rate=%u speakers=%u evidenceScope=representative_32_2_2_48k_not_all_offsets",selectedRange.policy,selectedRange.verified?"true":"false",selectedRange.hostContract?"true":"false",selectedRange.min,selectedRange.max,version?version():"unknown",formatOk?format.samples_per_sec:0,formatOk?unsigned(format.speakers):0);}
  logger(LOG_INFO,"SL_CAPTURE_BOUNDARY pre_array_index=%d filter_count=%llu upstream_checked=true raw_media_time=true ui_fps_target=60 wave_publish_target_fps=120 display_mode=column_minmax",binding.checkedIndex(),static_cast<unsigned long long>(binding.checkedCount()));readButton->setEnabled(true);if(metrics){const auto range=offsetRange();metrics->manualEditor()->setRange(double(range.min)/1000000.0,double(range.max)/1000000.0);}auto k=binding.key();backend=std::make_unique<pitatuner::ObsOffsetBackend>(offsetApi,binding.selectedBorrowedForOwner(),k.session,k.epoch);control=std::make_unique<pitatuner::ControlSession>(*backend,k,offsetRange(),delivery,!isolatedQa);if(!isolatedQa)backend->zeroBridge({
   [this,k]{auto* selected=binding.selectedBorrowedForOwner();return selected&&binding.key()==k&&!binding.faults()&&!api->obs_source_muted(selected)&&api->obs_source_enabled(selected)&&binding.hasPairedPacket()&&binding.streamActive();},
   [this](pita::Ns n){return offsetRange().permits(n);},
   [this]{return binding.armOffsetBridge();},
   [this](uint64_t token){return binding.offsetBridgeAcknowledged(token);},
   [this]{binding.cancelOffsetBridge();},
   [this,observedBackend=backend.get()](const char* stage,pita::Ns target,pita::Ns intermediate,uint64_t value){logger(LOG_INFO,"SL_ZERO_BRIDGE stage=%s targetNs=%lld intermediateNs=%lld tokenOrElapsedNs=%llu async=true",stage,target,intermediate,value);if(strcmp(stage,"first")&&strcmp(stage,"ack")){const auto actual=observedBackend->read();logger(LOG_INFO,"SL_ZERO_BRIDGE_FINAL readbackAvailable=%s remainingNs=%lld",actual?"true":"false",actual.value_or(0));}}
  });
  // Source session is unique for this owner lifetime; no UUID hash collisions.
  check("queue_select",queue.select({0,k.session,k.session,k.epoch}));ticket=*queue.request();sequence=0;invalidated=false;if(metrics)metrics->update({});if(upper){if(overlayRenderer){if(overlayRenderer->clear())upper->replot(QCustomPlot::rpQueuedReplot);}else{upper->clearPlottables();lower->clearPlottables();upper->replot(isolatedQa?QCustomPlot::rpImmediateRefresh:QCustomPlot::rpQueuedReplot);}if(isolatedQa)lower->replot();}
 }
 void uiFailure(const char* reason)noexcept{try{clearDisplay("ui_failure");readButton->setEnabled(false);if(control)control->close();control.reset();backend.reset();binding.stop();queue.invalidate();invalidated=true;if(ticker)ticker->stop();done=true;if(metrics)metrics->update({{},d::Hidden::ContextBlocked});logger(LOG_ERROR,"PT_PRODUCT_FAILED ui_handler=%s",reason);}catch(...){logger(LOG_ERROR,"PT_PRODUCT_FAILED ui_cleanup");}}
 void choose(Source& source){const auto i=picker->findData(QString::fromStdString(source.uuid));check("catalog_picker_uuid",i>=0);picker->setCurrentIndex(-1);picker->setCurrentIndex(i);check("picker_binding_uuid",binding.key().uuid==source.uuid);}
 void read(bool automatic=false){
  if(automatic&&qaFourActive)++qaFourAutoReads;
  if(!backend||!control){clearDisplay("read_no_control");readButton->setEnabled(false);return;}
  if(control->view().pending){authority();return;}
  if(automatic&&backend->consumeExternalChange()){control->externalChange();queue.invalidate();invalidated=true;}
  const auto offset=automatic?pitatuner::readback::follow(*backend,*control,binding.key()):pitatuner::readback::acknowledge(*backend,*control,binding.key());
  logger(LOG_INFO,"SL_READBACK ok=%s automatic=%s",offset?"true":"false",automatic?"true":"false");if(!offset){control->externalChange();queue.invalidate();invalidated=true;clearDisplay("readback_failed");return;}
  if(automatic&&!invalidated&&queue.state()==q::State::Ready){if(metrics){metrics->readOffset(offset);metrics->seedManualOffset(*offset);}return;}
  const auto delivered=queue.delivered_sequence();if(delivered>=UINT64_MAX-1){uiFailure("read_sequence_exhausted");return;}
  if(queue.state()!=q::State::NeedsSnapshot)queue.invalidate();const auto requested=queue.request();
  if(!requested){control->externalChange();invalidated=true;clearDisplay("snapshot_request_failed");return;}ticket=*requested;clearDisplay("snapshot_requested");
  const bool recovered=queue.recover({ticket,delivered+1,*offset})&&queue.acknowledge(ticket);invalidated=!recovered;if(recovered&&metrics){metrics->readOffset(offset);metrics->seedManualOffset(*offset);}
 }
 bool confirmCorrection(pita::Mode mode,bool confirmed){
  if(!control||!correctionEstimate||!consumer.present(context(),static_cast<pita::Ns>(now())).snapshot)return false;
  pitatuner::CorrectionActions actions(*control,actionIds,baselineIds);
  const bool ok=actions.confirm(mode,*correctionEstimate,static_cast<pita::Ns>(now()),confirmed);clearDisplay("reference_confirmation");return ok;
 }
 pita::Result correctionClick(size_t index){
  if(!control||!backend)return pita::Result::Rejected;
  context();
  const auto v=control->view();
  if(invalidated||binding.faults()||v.closed||v.protectedState||v.pending||!v.offset||!(v.key==binding.key())||!(v.key==backend->key()))return pita::Result::Rejected;
  // Only automatic correction depends on a fresh measurement. Undo is governed
  // by the model's successful transaction and authoritative source/revision.
  if(index==5){
   if(automaticConfirmationOpen||!binding.streamActive()||!v.proposal||!offsetRange().permits(v.proposal->target))return pita::Result::Rejected;
   const auto shown=lastPresentation.snapshot;
   const auto current=consumer.present(context(),static_cast<pita::Ns>(now()));
   const auto measured=binding.latest();if(!measured||!measured->valid||!shown||measured->sequence!=shown->sequence)return pita::Result::Rejected;
   if(!shown||!current.snapshot||shown->key!=current.snapshot->key||shown->sequence!=current.snapshot->sequence)return pita::Result::Rejected;
  }
  if(index==4){const auto entered=pita::parse_ms(metrics->enteredMilliseconds());
   if(!entered||!offsetRange().permits(*entered)){const auto message=QString::fromUtf8(obs_module_text(entered?"StatusRangeRejected":"ErrorInvalidNumber"));auto* editor=metrics->manualEditor();editor->setToolTip(message);QHelpEvent help(QEvent::ToolTip,QPoint(0,editor->height()),editor->mapToGlobal(QPoint(0,editor->height())));QCoreApplication::sendEvent(editor,&help);logger(LOG_INFO,"SL_MANUAL_REJECT reason=%s write=false",entered?"range":"number");return pita::Result::Rejected;}
   metrics->manualEditor()->setToolTip(QString{});
  }
  pitatuner::CorrectionActions actions(*control,actionIds,baselineIds);
  backend->bridgeFreshUntil(index==5&&correctionEstimate?correctionEstimate->freshUntil:INT64_MAX);
  const auto result=actions.operation(index,v.key,{},static_cast<pita::Ns>(now()),metrics->enteredMilliseconds());
  if(result==pita::Result::Pending){if(metrics)metrics->readOffset(control->view().offset);authority();return result;}
  if(result==pita::Result::Applied||result==pita::Result::Noop){
   consumer.discardSnapshot();lastPacket={};lastPresentation={};clearDisplay("correction_applied_or_noop");
   // A successful write must not acknowledge/reset baseline or Undo.
   const auto current=pitatuner::readback::follow(*backend,*control,binding.key());
   if(current&&metrics){metrics->readOffset(current);metrics->seedManualOffset(*current);}
  }else clearDisplay("correction_other_result");
  authority();return result;
 }
 bool correctionQa(bool valid){
  if(SL_QA_ENV("PITATUNER_CORRECTION_QA")!="unverified-v1")return false;
  if(!valid||correctionQaDone)return true;
  correctionQaDone=true;const auto before=p_obs_source_get_sync_offset(a->source);
  check("correction_no_implicit_baseline",!control->view().baseline);
  check("correction_B_requires_confirmation",!confirmCorrection(pita::Mode::ExplicitPreTarget,false)&&!control->view().baseline);
  check("correction_B_explicit_confirmation",confirmCorrection(pita::Mode::ExplicitPreTarget,true)&&control->view().baseline->d0==0);
  // Rebuild a current display after the confirmation cleared old presentation.
  draw();
  constexpr const char* rejected[]{"correction_minus5_rejected","correction_minus1_rejected","correction_plus1_rejected","correction_plus5_rejected","correction_manual_rejected","correction_recommendation_rejected","correction_undo_rejected"};
  for(size_t i=0;i<7;++i){correctionResult=pita::Result::Busy;const bool delivered=QMetaObject::invokeMethod(metrics->operationButton(i),"clicked",Qt::DirectConnection);check(rejected[i],delivered&&correctionResult==pita::Result::Rejected);}
  check("correction_unverified_no_write",p_obs_source_get_sync_offset(a->source)==before&&!control->view().canUndo&&metrics->writesDisabled());
  check("correction_A_explicit_confirmation",confirmCorrection(pita::Mode::MeasuredReference,true)&&control->view().baseline->d0==correctionEstimate->delay);
  p_obs_source_set_sync_offset(a->source,before);context();
  check("correction_external_revokes_reference_undo",control->view().protectedState&&!control->view().baseline&&!control->view().proposal&&!control->view().canUndo);
  save(QStringLiteral("52-unverified-connected"));finish();return true;
 }
 bool observe(){if(invalidated||!control)return false;auto m=isolatedQa?binding.latest():binding.measurementForDisplay();if(!m||m->sequence<=sequence)return false;const auto time=now();auto delay=latency::msToNs(m->channels[0].delayMs);if(m->valid)check("mapped_wave",delay&&m->wave&&m->wave->series.size()==3);const bool silent=!strcmp(m->channels[0].reason,"silence")||!strcmp(m->channels[1].reason,"silence");
  if(delay)lastSuccessful.observe(m->key,*delay,m->capturedThroughNs,m->valid);
  if(!m->valid)if(auto held=lastSuccessful.held(m->key,time))logger(LOG_INFO,"SL_LAST_SUCCESS current_valid=false held_ms=%.6f age_ms=%.3f held_value_write_authority=false",double(held->delay)/1e6,double(held->ageNs)/1e6);
  pita::Estimate e{};e.key=m->key;e.seq=m->sequence;e.delay=delay.value_or(0);e.freshUntil=static_cast<pita::Ns>(m->expiresAtNs);e.valid=m->valid;e.confident=m->valid;e.active=!silent;e.muted=m->muted;check("owner_accept",control->acceptEstimate(e));sequence=m->sequence;correctionEstimate=e;control->propose(static_cast<pita::Ns>(time));
  logger(LOG_INFO,"SL_PAIR_WINDOW seq=%llu matched=%s pairs=%llu frames=%llu delta_spread_ns=%llu",m->sequence,m->commonClockMatched?"true":"false",m->matchedCallPairs,m->pairedFrames,m->pairDeltaSpreadNs);
  logger(LOG_INFO,"SL_DELAY seq=%llu delay_ms=%.6f valid=%s captured_ns=%llu submitted_ns=%llu analysis_started_ns=%llu published_ns=%llu observed_ns=%llu",m->sequence,m->channels[0].delayMs,m->valid?"true":"false",m->capturedThroughNs,m->submittedAtNs,m->analysisStartedAtNs,m->publishedAtNs,time);
  logger(LOG_INFO,"SL_PCM_LEVEL seq=%llu pre_rms0=%.8g pre_rms1=%.8g post_rms0=%.8g post_rms1=%.8g pre_peak0=%.8g post_peak0=%.8g pcm_saved=false",m->sequence,m->preRms[0],m->preRms[1],m->postRms[0],m->postRms[1],m->prePeak[0],m->postPeak[0]);
  logger(LOG_INFO,"SL_CLOCK_WINDOW seq=%llu pre_spread_ns=%llu post_spread_ns=%llu confident=%s discontinuity=%s frame_clock=window_median",m->sequence,m->preClockSpreadNs,m->postClockSpreadNs,m->clockConfident?"true":"false",m->clockDiscontinuity?"true":"false");
  logger(LOG_INFO,"SL_ESTIMATE seq=%llu valid=%s pre=%llu post=%llu reason0=%s reason1=%s wave=%s",m->sequence,m->valid?"true":"false",m->preFrames,m->postFrames,m->channels[0].reason,m->channels[1].reason,m->wave?"true":"false");
  d::Input input(control->view().baseline?control->view().baseline->mode:pita::Mode::Unchosen);input.selection=m->key;input.latestSequence=sequence;input.observedAt=static_cast<pita::Ns>(m->capturedThroughNs);input.now=static_cast<pita::Ns>(time);input.control=control->view();input.estimate=e;input.deliveryState=queue.state();if(m->wave)input.wave=d::WaveInput{m->key,sequence,0,0,*input.control.offset,{},*m->wave};
  lastPacket={d::build(input),ticket,queue.delivered_sequence()};if(!consumer.receive(lastPacket,context(),static_cast<pita::Ns>(now()))){logger(LOG_WARNING,"SL_CONSUMER_REJECT seq=%llu",m->sequence);clearDisplay("consumer_rejected");return false;}return m->valid;
 }
 std::unique_ptr<pitatuner::qt::overlay::Renderer> overlayRenderer;QPointer<QLabel> overlayLegend;uint64_t displayDroppedChunks{};
 pitatuner::LastSuccessfulDelay lastSuccessful;
 pitatuner::displayonly::FrameCadence waveformCadence;
 uint64_t uiDraws{},uiDrawTotalNs{},uiDrawMaxNs{},uiIntervalMaxNs{},lastDrawAt{},runtimeLogAt{},plotReplots{},plotTotalNs{},plotMaxNs{},plotBeginNs{};
 std::optional<pitatuner::qt::Metrics::HeldDelay> heldDelayForDisplay(){
  if(isolatedQa||!control||!binding.selectedBorrowedForOwner()||api->obs_source_removed(binding.selectedBorrowedForOwner()))return{};
  if(auto value=lastSuccessful.diagnosticForSource(binding.key().uuid,now()))return pitatuner::qt::Metrics::HeldDelay{value->delay,value->ageNs};return{};
 }
 void logRuntime(){const auto time=now();if(runtimeLogAt&&time-runtimeLogAt<1000000000ULL)return;runtimeLogAt=time;const auto r=binding.runtime();logger(LOG_INFO,"SL_RUNTIME window_ms=2000 hop_ms=250 ui_tick_ms=16 ui_fps_target=%u wave_publish_target_fps=120 display_mode=column_minmax wave_published=%llu wave_interval_max_ms=%.3f analysis_submitted=%llu analysis_completed=%llu analysis_replaced=%llu compute_avg_ms=%.3f compute_max_ms=%.3f queue_max_ms=%.3f ingest_dwell_max_ms=%.3f pcm_drops=%llu ui_draw_calls=%llu ui_avg_ms=%.3f ui_max_ms=%.3f ui_interval_max_ms=%.3f plot_replots=%llu plot_avg_ms=%.3f plot_max_ms=%.3f display_dropped_chunks=%llu screen_fps_unverified=true",waveformCadence.fps(),r.wavePublished,double(r.waveIntervalMaxNs)/1e6,r.submitted,r.completed,r.replaced,r.completed?double(r.computeTotalNs)/r.completed/1e6:0.,double(r.computeMaxNs)/1e6,double(r.queueMaxNs)/1e6,double(r.ingestMaxDwellNs)/1e6,r.drops,uiDraws,uiDraws?double(uiDrawTotalNs)/uiDraws/1e6:0.,double(uiDrawMaxNs)/1e6,double(uiIntervalMaxNs)/1e6,plotReplots,plotReplots?double(plotTotalNs)/plotReplots/1e6:0.,double(plotMaxNs)/1e6,displayDroppedChunks);}
 void draw(){const auto begin=now();drawContent();const auto end=now();++uiDraws;uiDrawTotalNs+=end-begin;uiDrawMaxNs=std::max(uiDrawMaxNs,end-begin);if(lastDrawAt)uiIntervalMaxNs=std::max(uiIntervalMaxNs,begin-lastDrawAt);lastDrawAt=begin;wheelDiagnosticDrawComplete();}
 void drawContent(){if(!control){clearDisplay("draw_no_control");if(metrics)metrics->showUnbound(selectionRejected);return;}renderedAt=now();if(isolatedQa&&!binding.streamActiveAt(renderedAt)){consumer.discardSnapshot();clearDisplay("isolated_stream_inactive");metrics->update({{},d::Hidden::ContextBlocked});return;}if(!isolatedQa&&!binding.measurementForDisplay())consumer.discardSnapshot();auto view=consumer.present(context(),static_cast<pita::Ns>(renderedAt));lastPresentation=view;if(isolatedQa){upper->clearPlottables();lower->clearPlottables();}auto* selected=binding.selectedBorrowedForOwner();const auto reason=!view.snapshot&&selected&&api->obs_source_muted(selected)?std::optional{pitatuner::qt::Text::Muted}:std::optional<pitatuner::qt::Text>{};const auto offsetFallback=!invalidated&&!control->view().protectedState?control->view().offset:std::optional<pita::Ns>{};metrics->update(view,reason,heldDelayForDisplay(),offsetFallback);if(isolatedQa)check("correction_controls_unconnected_disabled",metrics->writesDisabled());if(isolatedQa&&!view.snapshot)check("hidden_numeric_values_cleared",metrics->value(0)==pitatuner::qt::nsValue({}));
  if(view.snapshot){const auto& snap=*view.snapshot;if(isolatedQa)check("no_implicit_baseline",!snap.recommendationEnabled&&!snap.target);if(snap.confidence!=d::Confidence::Stable)check("unavailable_result_measured_only_no_delay",!snap.delay&&std::all_of(snap.waveforms.begin(),snap.waveforms.end(),[](const auto& series){return series.kind==pitatuner::waveform::Kind::PreMeasured||series.kind==pitatuner::waveform::Kind::PostMeasured;}));if(isolatedQa)check("numeric_missing_not_zero",metrics->value(2)==pitatuner::qt::nsValue({})&&metrics->value(3)==pitatuner::qt::nsValue({}));if(isolatedQa||snap.delay)check("numeric_delay_current_from_consumer",metrics->value(0).startsWith(pitatuner::qt::nsValue(snap.delay))&&metrics->value(1).startsWith(pitatuner::qt::nsValue(snap.current)));const auto origin=snap.axis.begin_ns;
   if(isolatedQa){
   if(!waveZoom||!waveZoom->update(snap))pitatuner::qt::waveb::axes(upper,lower,snap.axis);
   for(const auto& series:snap.waveforms){auto* plot=series.kind==pitatuner::waveform::Kind::PreMeasured?upper:lower;pitatuner::qt::waveb::add(plot,series,snap.axis);}
   }
   ++paints;
  }else if(isolatedQa&&waveZoom)waveZoom->clear("isolated_no_snapshot");
  bool waveChanged=false;
  // Display-only normalization/one-column minmax; no gain returns to capture.
  if(!isolatedQa&&overlayRenderer){
   if(!invalidated&&!control->view().protectedState){if(auto frame=binding.waveForDisplay()){if(waveformCadence.due(renderedAt)){
    displayDroppedChunks=frame->samples->droppedChunks;
    const auto& key=binding.key();
    if(waveZoom&&waveZoom->update(*frame->samples,key))waveChanged=overlayRenderer->update(frame->samples,key,renderedAt,control->view().offset);
    else waveChanged=overlayRenderer->clear();
   }}else{waveChanged=overlayRenderer->clear();if(waveZoom)waveZoom->clear("wave_unavailable");}}
   else{waveChanged=overlayRenderer->clear();if(waveZoom)waveZoom->clear(invalidated?"display_invalidated":"display_protected");}
  }
  authority();if(isolatedQa){upper->replot();lower->replot();}else if(waveChanged)upper->replot(QCustomPlot::rpQueuedReplot);
 }
 bool automaticConfirmationOpen{};
 bool selectionRejected{};uint32_t loggedCaptureFaults{};
 bool compactSaved{};
 void saveCompact(const QString& dir,const QString& tag){
  if(compactSaved||phase!=3||tag!=QStringLiteral("normal-source-dock")||SL_QA_ENV("PITATUNER_51D_QA")!="compact-layout-v1")return;compactSaved=true;
  auto* scroll=root->findChild<QScrollArea*>();check("51d_real_scroll_found",scroll);auto* content=dynamic_cast<pitatuner::qt::CompactDock*>(scroll->widget());check("51d_real_compact_content",content);
  check("51d_no_horizontal_overflow",content->width()<=scroll->viewport()->width()&&!scroll->horizontalScrollBar()->isVisible());
  const int requested=SL_QA_ENV("PITATUNER_COMPACT_WIDTH").toInt();check("51d_real_requested_width",root->width()==requested);check("51d_height850_retained",root->height()>=850);check("51d_gate_stays_disabled",metrics->writesDisabled());
  QJsonArray buttons;for(size_t i=0;i<7;i++){auto* b=metrics->operationButton(i);const auto at=b->mapTo(content,QPoint());logger(LOG_INFO,"PT_PRODUCT_51D_DIM index=%llu x=%d width=%d content=%d height=%d required=%d",static_cast<unsigned long long>(i),at.x(),b->width(),content->width(),b->height(),b->heightForWidth(b->width()));check("51d_operation_fits_wrap",at.x()>=0&&at.x()+b->width()<=content->width()&&b->height()>=b->heightForWidth(b->width()));buttons.append(QJsonObject{{"index",int(i)},{"text",b->text()},{"x",at.x()},{"y",at.y()},{"width",b->width()},{"height",b->height()},{"enabled",b->isEnabled()}});}
  const int position=scroll->verticalScrollBar()->value();scroll->verticalScrollBar()->setValue(0);check("51d_top_image_saved",root->grab().save(dir+"/51d-compact-top.png"));scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());check("51d_bottom_image_saved",root->grab().save(dir+"/51d-compact-bottom.png"));scroll->verticalScrollBar()->setValue(position);
  QJsonObject result{{"rootWidth",root->width()},{"rootHeight",root->height()},{"viewportWidth",scroll->viewport()->width()},{"contentWidth",content->width()},{"contentHeight",content->height()},{"numericColumns",content->numericColumns()},{"adjustmentColumns",content->adjustmentColumns()},{"operations",buttons}};QFile f(dir+"/51d-layout.json");check("51d_layout_evidence_saved",f.open(QIODevice::WriteOnly)&&f.write(QJsonDocument(result).toJson())>0);
 }
 bool brandSaved{};
 void saveBrand(const QString& dir){
  if(brandSaved||SL_QA_ENV("PITATUNER_02A_QA")!="known-title-v1")return;brandSaved=true;
  auto* dock=qobject_cast<QDockWidget*>(root->parentWidget());check("02a_real_dock_found",dock);
  auto* action=dock->toggleViewAction();check("02a_real_title_soniclatch",dock->windowTitle()==QStringLiteral("SonicLatch"));check("02a_real_menu_text_soniclatch",action&&action->text()==QStringLiteral("SonicLatch"));
  QMenu* found=nullptr;for(auto* menu:dock->parentWidget()->findChildren<QMenu*>())if(menu->actions().contains(action)){found=menu;break;}
  check("02a_actual_docks_menu_action_found",found);check("02a_title_widget_image_saved",dock->grab().save(dir+"/02a-real-dock.png"));
  found->popup(QPoint(40,40));QCoreApplication::processEvents();check("02a_actual_menu_visible",found->isVisible());check("02a_actual_menu_image_saved",found->grab().save(dir+"/02a-real-menu.png"));found->hide();
  const bool wasVisible=dock->isVisible();dock->show();dock->raise();QCoreApplication::processEvents();
  check("02a_own_dock_exposed",dock->windowHandle()&&dock->windowHandle()->isExposed());
  const auto frame=dock->frameGeometry();check("02a_native_title_frame_saved",dock->screen()->grabWindow(0,frame.x(),frame.y(),frame.width(),frame.height()).save(dir+"/02a-native-title.png"));if(!wasVisible)dock->hide();
  QJsonObject identity{{"title",dock->windowTitle()},{"menuAction",action->text()},{"menuObjectName",found->objectName()},{"internalDockId",dock->objectName()},{"locale",QString::fromUtf8(SL_QA_ENV("PITATUNER_LOCALE_EXPECT"))},{"floating",dock->isFloating()}};
  QFile f(dir+"/02a-identity.json");check("02a_identity_saved",f.open(QIODevice::WriteOnly)&&f.write(QJsonDocument(identity).toJson())>0);
 }
 void save(const QString& tag=QStringLiteral("normal-source-dock")){const auto dir=QCoreApplication::applicationDirPath()+"/../../config/pitatuner-integration";QDir().mkpath(dir);check("gui_render_saved",root&&root->grab().save(dir+"/"+tag+".png"));saveL04Layout(root,dir,tag);saveCompact(dir,tag);if(tag==QStringLiteral("normal-source-dock"))saveBrand(dir);}
 void finish(){if(done)return;done=true;ticker->stop();if(control)control->close();queue.shutdown();control.reset();backend.reset();binding.stop();qa37Cleanup();a.reset();b.reset();p_obs_wait_for_destroy_queue();if(qa37Active){check("37a_qa_sources_destroyed",qa37Created.load()==2&&qa37Destroyed[0].load()==1&&qa37Destroyed[1].load()==1);logger(LOG_INFO,"PT_PRODUCT_37A_CLEANUP created=%u victimDestroyed=%u twinDestroyed=%u liveCaptureFilters=%llu",qa37Created.load(),qa37Destroyed[0].load(),qa37Destroyed[1].load(),cap::Binding::liveFilters());}check("gui_contexts_destroyed",cap::Binding::liveFilters()==0&&destroyed[0]==1&&destroyed[1]==1);logger(LOG_INFO,"PT_PRODUCT_DONE pass=true cases=%u failed=%u paints=%u liveCaptureFilters=%llu",cases,failed,paints,cap::Binding::liveFilters());}
 void tick()noexcept{if(done)return;try{++tickNumber;if(!qa51Mode.isEmpty())qa51GuiTick.store(tickNumber);
  check("gui_owner_tick",QThread::currentThread()==QCoreApplication::instance()->thread());if(qaFourActive&&qaFourWorker.joinable())qaFourWorker.join();
  std::array<std::vector<cap::diagnostic::Packet>,2> trace;
  if(binding.takeTimelineTrace(trace)){
   if(auto f=binding.timelineFailure())logger(LOG_WARNING,"SL_TIMELINE_FAILURE side=%s rate=%u expected=%llu actual=%llu delta_abs_ns=%llu direction=%s frames=%u consumed=%u total_frames=%llu anchor=%llu seq=%llu arrival=%llu previous_media=%llu previous_arrival=%llu previous_frames=%u threshold_ns=2000000 observation_ms=500",f->before?"pre":"post",f->rate,f->expected,f->actual,f->difference,f->actual>=f->expected?"late":"early",f->frames,f->consumed,f->totalFrames,f->first,f->sequence,f->arrival,f->previousMedia,f->previousArrival,f->previousFrames);
   for(size_t side=0;side<2;side++)for(size_t i=0;i<trace[side].size();i++){const auto& p=trace[side][i];logger(LOG_INFO,"SL_TIMELINE_PACKET side=%s index=%llu media=%llu arrival=%llu frames=%u muted=%s",side?"post":"pre",static_cast<unsigned long long>(i),p.timestamp,p.arrival,p.frames,p.muted?"true":"false");}
   logger(LOG_INFO,"SL_TIMELINE_TRACE pre=%llu post=%llu capacity_per_side=128 pcm_saved=false upstream_discontinuity_flags=unavailable",static_cast<unsigned long long>(trace[0].size()),static_cast<unsigned long long>(trace[1].size()));
  }
  const auto faultMask=binding.faults();if(faultMask!=loggedCaptureFaults){loggedCaptureFaults=faultMask;if(faultMask){auto counts=binding.counters();logger(LOG_WARNING,"SL_CAPTURE_FAULT mask=%u pre=%llu post=%llu drops=%llu",faultMask,counts?counts->preEntries.load():0,counts?counts->postEntries.load():0,counts?counts->drops.load():0);}}
  if(qaBatchWaiting&&faultMask)qaBatchFaultSeen=true;
  const auto recovery=binding.pollRecovery();if(recovery!=cap::Binding::Recovery::None){logger(LOG_INFO,"SL_CAPTURE_RECOVERY outcome=%s reason=%u new_session=%llu",recovery==cap::Binding::Recovery::Restarted?"restarted":"stopped",faultMask,binding.key().session);if(!isolatedQa){consumer.discardSnapshot();lastPacket={};lastPresentation={};correctionEstimate.reset();/* Keep lastSuccessful only as same-UUID diagnostic, never authority. */}clearDisplay("capture_recovery");if(control)control->close();control.reset();backend.reset();queue.invalidate();invalidated=true;sequence=0;
   if(recovery==cap::Binding::Recovery::Stopped){readButton->setEnabled(false);if(metrics)metrics->update({{},d::Hidden::ContextBlocked});qa44Recovery(recovery);return;}
   attachSelected();read();if(!isolatedQa)draw();qa44Recovery(recovery);return;
  }
  if(!control&&(binding.faults()&static_cast<uint32_t>(cap::Fault::Timeline))){if(metrics)metrics->update({{},d::Hidden::ContextBlocked});qa44StoppedTick();return;}

#if SONICLATCH_ENABLE_EMBEDDED_QA
  if(qa51Started&&!qa51Injected&&qa51Mode!="notify"&&tickNumber>qa51ArmTick){qa51Injected=true;logger(LOG_INFO,"PT_PRODUCT_51B_THROW mode=%s guiTid=%lu tick=%llu graphsUpper=%d graphsLower=%d",qa51Mode.toUtf8().constData(),GetCurrentThreadId(),tickNumber,upper->plottableCount(),lower->plottableCount());if(qa51Mode=="timer-std")throw std::runtime_error("QA_51B_UNEXPECTED_STD");throw 51;}
#endif
  if(control&&control->view().pending){context();if(control&&control->view().pending){observe();const auto latest=binding.latest();const bool automaticEnvironment=latest&&latest->key==control->view().key&&latest->valid&&!latest->muted&&binding.streamActive();const auto result=control->pollPending(static_cast<pita::Ns>(now()),automaticEnvironment);if(result&&*result!=pita::Result::Pending){consumer.discardSnapshot();lastPacket={};lastPresentation={};clearDisplay("correction_completed");if(*result==pita::Result::Applied){const auto current=pitatuner::readback::follow(*backend,*control,binding.key());if(current&&metrics){metrics->readOffset(current);metrics->seedManualOffset(*current);}}logger(LOG_INFO,"SL_CORRECTION_COMPLETE applied=%s pending=false",*result==pita::Result::Applied?"true":"false");authority();}}}
  // Every timer turn observes owner context even when no new valid estimate.
  if(isolatedQa)draw();const bool newValid=observe();draw();if(!isolatedQa){logRuntime();return;}
#if SONICLATCH_ENABLE_EMBEDDED_QA
if(qaBatchCase(newValid))return;if(qaFourCase(newValid))return;if(correctionQa(newValid))return;if(qa44Case(newValid))return;if(qa58Case(newValid))return;if(qa51Case(newValid))return;if(qa37Case(newValid))return;
  if(SL_QA_ENV("PITATUNER_43_QA")=="shutdown-active-v1"){if(phase==1&&newValid){check("43_active_owner_fresh",control&&backend&&lastPresentation.snapshot&&a->producer.joinable()&&b->producer.joinable());save(QStringLiteral("43-active-before-shutdown"));phase=43;logger(LOG_INFO,"PT_PRODUCT_43_READY sourcesCreated=%u sourcesDestroyed=%u producersJoinable=2 liveCaptureFilters=%llu",created.load(),destroyed[0].load()+destroyed[1].load(),cap::Binding::liveFilters());return;}if(phase==43)return;}
  if(pendingAutoCheck){const bool autoOffset=!strcmp(pendingAutoCheck,"same_value_external_next_tick_hides");check(pendingAutoCheck,(autoOffset?!invalidated:invalidated)&&(!binding.latest()||phase==3)&&!consumer.present(context(),static_cast<pita::Ns>(now())).snapshot&&upper->plottableCount()==0&&lower->plottableCount()==0&&metrics->value(0)==pitatuner::qt::nsValue({}));save(QString::fromLatin1(pendingAutoCheck)+QStringLiteral("-cleared"));if(phase==12)check("50c_mute_reason_distinct",metrics->stateIs(pitatuner::qt::Text::Muted)&&metrics->unknown());pendingAutoCheck=nullptr;
   if(phase==9){picker->setCurrentIndex(-1);check("combo_empty_selection_clears",!control&&binding.key().uuid.empty()&&upper->plottableCount()==0&&lower->plottableCount()==0);picker->setCurrentIndex(picker->findData(QString::fromStdString(b->uuid)));check("disabled_source_select_rejected",!control&&binding.key().uuid.empty()&&!readButton->isEnabled());check("gui_nonmutation",observerMismatch==0&&p_obs_source_get_sync_offset(b->source)==120000000);finish();return;}
   if(phase==12)p_obs_source_set_muted(b->source,false);if(phase==6)p_obs_source_set_enabled(b->shiftFilter,true);if(phase==7)p_obs_source_filter_set_order(b->source,b->shiftFilter,OBS_ORDER_MOVE_TOP);choose(*b);readButton->click();if(!b->producer.joinable())b->start();++phase;phaseAt=now();return;
  }
  if(phase==0&&newValid){draw();save();auto old=lastPacket;choose(*b);check("switch_immediate_graph0",upper->plottableCount()==0&&lower->plottableCount()==0&&metrics->value(0)==pitatuner::qt::nsValue({})&&metrics->unknown());save(QStringLiteral("switch-cleared"));check("switch_hides_old",!consumer.present(context(),static_cast<pita::Ns>(now())).snapshot&&!consumer.receive(old,context(),static_cast<pita::Ns>(now())));const auto badIndex=picker->count();picker->addItem(QStringLiteral("test-invalid-uuid"),QStringLiteral("no-such-known-source"));picker->setCurrentIndex(badIndex);check("combo_failed_selection_clears",binding.key().uuid.empty()&&!control&&!readButton->isEnabled()&&upper->plottableCount()==0&&lower->plottableCount()==0&&metrics->value(0)==pitatuner::qt::nsValue({}));save(QStringLiteral("selection-failed"));choose(*b);readButton->click();phase=1;phaseAt=now();}
  else if(phase==1&&newValid){check("normal_b_20ms_display",lastPacket.snapshot->delay&&*lastPacket.snapshot->delay==20000000&&lastPacket.snapshot->current==120000000);draw();save();invalidStartSequence=sequence;nonSilentInvalid.store(true);phase=14;phaseAt=now();}
  else if(phase==14){auto m=binding.latest();if(m&&m->sequence>invalidStartSequence&&!m->valid&&!strcmp(m->channels[0].reason,"ambiguous_or_deformed")){if(!invalidObservedSequence){invalidObservedSequence=m->sequence;return;}
   logger(LOG_INFO,"PT_PRODUCT_50B observedSequence=%llu acceptedSequence=%llu state=%d invalidState=%d noisyOracleSkips=%u",invalidObservedSequence,lastPacket.snapshot->sequence,static_cast<int>(lastPacket.snapshot->state),static_cast<int>(d::State::InvalidEstimate),noisyOracleSkips.load());
   check("50b_non_silent_invalid_not_silent",lastPacket.snapshot->sequence>=invalidObservedSequence&&lastPacket.snapshot->state==d::State::InvalidEstimate&&lastPacket.snapshot->confidence==d::Confidence::Invalid&&!lastPacket.snapshot->delay&&upper->plottableCount()==0&&lower->plottableCount()==0);save(QStringLiteral("50b-invalid-cleared"));nonSilentInvalid.store(false);phase=15;phaseAt=now();}}
  else if(phase==15&&newValid){draw();silentStartSequence=sequence;b->silent.store(true);phase=10;phaseAt=now();}
  else if(phase==10){auto m=binding.latest();if(m&&m->sequence>silentStartSequence&&!m->valid&&!strcmp(m->channels[0].reason,"silence")){
   if(!silentObservedSequence){silentObservedSequence=m->sequence;return;}
   logger(LOG_INFO,"PT_PRODUCT_SILENCE observedSequence=%llu acceptedSequence=%llu state=%d upperGraphs=%d lowerGraphs=%d",silentObservedSequence,lastPacket.snapshot->sequence,static_cast<int>(lastPacket.snapshot->state),upper->plottableCount(),lower->plottableCount());
   check("silent_new_invalid_drops_previous_display",upper->plottableCount()==0&&lower->plottableCount()==0&&metrics->value(0)==pitatuner::qt::nsValue({})&&lastPacket.snapshot->state==d::State::Silent&&lastPacket.snapshot->sequence>=silentObservedSequence&&metrics->stateIs(pitatuner::qt::Text::Silent)&&metrics->unknown());save(QStringLiteral("silence-cleared"));b->silent.store(false);phase=11;phaseAt=now();}}
  else if(phase==11&&newValid){draw();b->stopProducer();p_obs_source_set_muted(b->source,true);pendingAutoCheck="source_mute_next_tick_hides";phase=12;}
  else if(phase==13&&newValid){b->stopProducer();phase=2;staleCycle=0;staleTicks=0;staleDeadline=static_cast<uint64_t>(*lastPacket.snapshot->freshUntil);phaseAt=now();}
  else if(phase==2){
   if(lastPresentation.snapshot&&lastPresentation.snapshot->freshUntil)staleDeadline=static_cast<uint64_t>(*lastPresentation.snapshot->freshUntil);
   if(renderedAt+90000000ULL>=staleDeadline){logger(LOG_INFO,"PT_PRODUCT_50D cycle=%u tick=%llu renderedAtNs=%llu deadlineNs=%llu hidden=%d snapshot=%s delayCleared=%s currentCleared=%s graphsUpper=%d graphsLower=%d staleReason=%s",staleCycle,tickNumber,renderedAt,staleDeadline,static_cast<int>(lastPresentation.hidden),lastPresentation.snapshot?"true":"false",metrics->value(0)==pitatuner::qt::nsValue({})?"true":"false",metrics->value(1)==pitatuner::qt::nsValue({})?"true":"false",upper->plottableCount(),lower->plottableCount(),metrics->stateIs(pitatuner::qt::Text::Expired)?"true":"false");}
   if(renderedAt>=staleDeadline){++staleTicks;if(staleTicks==1||staleTicks==3)save(QStringLiteral("50d-cycle%1-stale%2").arg(staleCycle).arg(staleTicks));
    check("50d_stale_reason_multiple_normal_ticks",lastPresentation.hidden==d::Hidden::Expired&&!lastPresentation.snapshot&&metrics->stateIs(pitatuner::qt::Text::Expired)&&metrics->unknown()&&metrics->value(0)==pitatuner::qt::nsValue({})&&metrics->value(1).startsWith(pitatuner::qt::nsValue(control->view().offset))&&upper->plottableCount()==0&&lower->plottableCount()==0&&metrics->writesDisabled());
    if(staleTicks>=3){
     if(staleCycle==0){b->start();phase=16;}
     else if(staleCycle==1){choose(*a);readButton->click();phase=17;}
     else if(staleCycle==2){readButton->click();phase=19;}
     else if(staleCycle==3){p_obs_source_set_muted(b->source,true);phase=21;}
     else{auto* settings=p_obs_data_create();p_obs_data_set_int(settings,"delayNs",20000000);p_obs_source_update(b->shiftFilter,settings);p_obs_data_release(settings);phase=23;}
     phaseAt=now();
    }
   }
  }
  else if(phase==16&&newValid){check("50d_fresh_packet_clears_stale",lastPresentation.snapshot&&lastPresentation.hidden==d::Hidden::None&&!metrics->stateIs(pitatuner::qt::Text::Expired)&&upper->plottableCount()>0&&lower->plottableCount()>0);save(QStringLiteral("50d-fresh-recovered"));b->stopProducer();staleCycle=1;staleTicks=0;staleDeadline=static_cast<uint64_t>(*lastPacket.snapshot->freshUntil);phase=2;phaseAt=now();}
  else if(phase==17){check("50d_source_switch_clears_stale",lastPresentation.hidden==d::Hidden::NoSnapshot&&!lastPresentation.snapshot&&!metrics->stateIs(pitatuner::qt::Text::Expired)&&upper->plottableCount()==0&&lower->plottableCount()==0);save(QStringLiteral("50d-switch-reason-cleared"));choose(*b);readButton->click();b->start();phase=18;phaseAt=now();}
  else if(phase==18&&newValid){b->stopProducer();staleCycle=2;staleTicks=0;staleDeadline=static_cast<uint64_t>(*lastPacket.snapshot->freshUntil);phase=2;phaseAt=now();}
  else if(phase==19){check("50d_reread_clears_stale",lastPresentation.hidden==d::Hidden::NoSnapshot&&!lastPresentation.snapshot&&!metrics->stateIs(pitatuner::qt::Text::Expired)&&upper->plottableCount()==0&&lower->plottableCount()==0);save(QStringLiteral("50d-reread-reason-cleared"));b->start();phase=20;phaseAt=now();}
  else if(phase==20&&newValid){check("50d_fresh_after_reread_no_old_values",lastPresentation.hidden==d::Hidden::None&&lastPresentation.snapshot&&lastPresentation.snapshot->sequence==sequence&&!lastPresentation.snapshot->target&&metrics->writesDisabled());b->stopProducer();staleCycle=3;staleTicks=0;staleDeadline=static_cast<uint64_t>(*lastPacket.snapshot->freshUntil);phase=2;phaseAt=now();}
  else if(phase==21){check("50d_mute_protection_overrides_stale",lastPresentation.hidden==d::Hidden::ContextBlocked&&!lastPresentation.snapshot&&metrics->stateIs(pitatuner::qt::Text::Muted)&&metrics->unknown()&&upper->plottableCount()==0&&lower->plottableCount()==0);save(QStringLiteral("50d-mute-priority"));p_obs_source_set_muted(b->source,false);choose(*b);readButton->click();b->start();phase=22;phaseAt=now();}
  else if(phase==22&&newValid){b->stopProducer();staleCycle=4;staleTicks=0;staleDeadline=static_cast<uint64_t>(*lastPacket.snapshot->freshUntil);phase=2;phaseAt=now();}
  else if(phase==23){check("50d_config_queue_protection_overrides_stale",lastPresentation.hidden==d::Hidden::ContextBlocked&&!lastPresentation.snapshot&&metrics->stateIs(pitatuner::qt::Text::Protected)&&metrics->unknown()&&upper->plottableCount()==0&&lower->plottableCount()==0);save(QStringLiteral("50d-config-priority"));choose(*b);readButton->click();b->start();phase=3;phaseAt=now();}
  else if(phase==3&&newValid){draw();save();p_obs_source_set_sync_offset(b->source,120000000);pendingAutoCheck="same_value_external_next_tick_hides";}
  else if(phase==4&&newValid){draw();save();b->addExtra();pendingAutoCheck="filter_add_next_tick_hides";}
  else if(phase>=5&&phase<=8&&newValid){
   draw();b->stopProducer();if(phase==5){auto* settings=p_obs_data_create();p_obs_data_set_int(settings,"delayNs",20000000);p_obs_source_update(b->shiftFilter,settings);p_obs_data_release(settings);}
   if(phase==12)p_obs_source_set_muted(b->source,false);if(phase==6)p_obs_source_set_enabled(b->shiftFilter,false);
   if(phase==7)p_obs_source_filter_set_order(b->source,b->shiftFilter,OBS_ORDER_MOVE_BOTTOM);
   if(phase==8)p_obs_source_filter_remove(b->source,b->extra);
   const char* names[]={"filter_update_next_tick_hides","filter_enable_next_tick_hides","filter_reorder_next_tick_hides","filter_remove_next_tick_hides"};pendingAutoCheck=names[phase-5];
  }
  else if(phase==9&&newValid){draw();b->stopProducer();p_obs_source_set_enabled(b->source,false);pendingAutoCheck="source_disable_next_tick_hides";}
  if(!done&&now()-phaseAt>10000000000ULL)throw std::runtime_error("GUI stage timeout");

#endif
 }catch(const std::exception& e){++failed;uiFailure(e.what());qa51PostException();if(qaFourActive)qaFourClose();}catch(...){++failed;uiFailure("timer_exception");qa51PostException();if(qaFourActive)qaFourClose();}}
};
std::unique_ptr<Owner> owner;
std::optional<bool> normalDockVisible;bool collectionTransition{},shutdownStarted{};
QString selectedSourcePath(){
 auto dll=GetModuleHandleW(L"obs.dll");using Path=decltype(&obs_module_get_config_path);using Free=void(*)(void*);
 auto get=reinterpret_cast<Path>(GetProcAddress(dll,"obs_module_get_config_path"));auto release=reinterpret_cast<Free>(GetProcAddress(dll,"bfree"));if(!get||!release)return{};
 auto* path=get(obs_current_module(),"selected-source.json");if(!path)return{};const auto value=QString::fromUtf8(path);release(path);return value;
}

class MainCloseObserver final:public QObject {
 std::function<void()> beforeClose_;
 bool eventFilter(QObject*,QEvent* event)override{if(event->type()==QEvent::Close&&beforeClose_)beforeClose_();return false;}
public:
 MainCloseObserver(QObject* parent,std::function<void()> callback):QObject(parent),beforeClose_(std::move(callback)){}
};
// OBS blocks QDockWidget signals in its Docks menu. Only then use Show/Hide
// as a fallback; otherwise Qt's signal is authoritative (including tab changes).
class DockVisibilityObserver final:public QObject {
 std::function<void(bool)> changed_;
 bool eventFilter(QObject* watched,QEvent* event)override{
  if(changed_&&watched->signalsBlocked()&&(event->type()==QEvent::Show||event->type()==QEvent::Hide)){
   auto* dock=qobject_cast<QDockWidget*>(watched);
   if(dock){
    // Match QDockWidget's Show visibility test, including inactive tab geometry
    // and floating windows on a screen with a negative virtual origin.
    const auto origin=dock->isWindow()&&dock->screen()?dock->screen()->availableVirtualGeometry().topLeft():QPoint{};
    const bool visible=event->type()==QEvent::Show&&dock->geometry().right()>=origin.x()&&dock->geometry().bottom()>=origin.y();
    changed_(visible);
   }
  }
  return false;
 }
public:
 DockVisibilityObserver(QObject* parent,std::function<void(bool)> callback):QObject(parent),changed_(std::move(callback)){}
};
void checkLocales(){
 QFile oracleFile(QCoreApplication::applicationDirPath()+"/../../config/pitatuner-integration/expected-locales.json");check("locale_oracle_loaded",oracleFile.open(QIODevice::ReadOnly));const auto oracle=QJsonDocument::fromJson(oracleFile.readAll()).object();check("locale_oracle_16",oracle.size()==16);

 check("obs_locale_startup_loaded",localeApi&&localeLookup);
 auto* english=localeApi->load(obs_current_module(),"en-US","en-US");check("english_fallback_loaded",english!=nullptr);
 for(const auto* language:approvedLocales){auto* lookup=localeApi->load(obs_current_module(),"en-US",language);unsigned count=0;bool valid=lookup!=nullptr;
  for(const auto* key:localeKeys){const char* value=nullptr;if(!lookup||!localeApi->get(lookup,key,&value)||!value||!*value||!strcmp(key,value)||QString::fromUtf8(value).contains(QChar::ReplacementCharacter))valid=false;else if(QString::fromUtf8(value)!=oracle.value(QString::fromLatin1(language)).toObject().value(QString::fromLatin1(key)).toString())valid=false;else ++count;}
  logger(LOG_INFO,"PT_PRODUCT_LOCALE language=%s keys=%u pass=%s",language,count,valid?"true":"false");if(lookup)localeApi->destroy(lookup);check("all_locale_keys_resolve",valid&&count==std::size(localeKeys));
 }
 if(SL_QA_ENV("PITATUNER_LOCALE_QA")=="known-partial-v1"){
  auto* partial=localeApi->load(obs_current_module(),"en-US","zz-PITA-QA");bool inherited=partial!=nullptr,overridden=false;unsigned count=0;
  for(const auto* key:localeKeys){const char *a=nullptr,*b=nullptr;const bool found=english&&partial&&localeApi->get(english,key,&a)&&localeApi->get(partial,key,&b)&&a&&b;if(!strcmp(key,"LabelSource")){overridden=found&&QString::fromUtf8(b)==oracle.value("ja-JP").toObject().value("LabelSource").toString();}else{if(!found||strcmp(a,b))inherited=false;else ++count;}}
  check("partial_locale_inherits_english",inherited&&count+1==std::size(localeKeys));check("partial_locale_override_preserved",overridden);if(partial)localeApi->destroy(partial);
  const auto language=QString::fromUtf8(SL_QA_ENV("PITATUNER_LOCALE_EXPECT"));const auto expected=oracle.value(language).toObject();bool startup=expected.size()==std::size(localeKeys);unsigned resolved=0;
  for(const auto* key:localeKeys){if(QString::fromUtf8(obs_module_text(key))!=expected.value(QString::fromLatin1(key)).toString())startup=false;else ++resolved;}
  check("configured_startup_locale_exact",startup&&resolved==std::size(localeKeys));logger(LOG_INFO,"PT_PRODUCT_LOCALE_QA startup=%s startupKeys=%u partialEnglishInherited=%u override=%s refresh=%s",language.toUtf8().constData(),resolved,count,overridden?"true":"false",obs_module_text("BtnRefreshOffset"));
 }
 auto* fallback=localeApi->load(obs_current_module(),"en-US","zz-MISSING-test");bool same=fallback!=nullptr;
 for(const auto* key:localeKeys){const char *a=nullptr,*b=nullptr;if(!localeApi->get(english,key,&a)||!fallback||!localeApi->get(fallback,key,&b)||strcmp(a,b))same=false;}
 if(fallback)localeApi->destroy(fallback);localeApi->destroy(english);check("missing_locale_english_fallback",same);
 const char* missing="__pitatuner_missing_key_test__";check("missing_key_safe_identity",!strcmp(obs_module_text(missing),missing));
}
// Frontend collection changes are synchronous. A 16 ms UI timer cannot be
// relied on to release source refs while OBS drains posted events in ClearSceneData.
// The same release is required when the dock is hidden; stopping its timer alone
// would leave the capture worker and source refs alive.
void detachCaptureSelection(){
 if(ticker)ticker->stop();
 QSignalBlocker block(picker);picker->setCurrentIndex(-1);owner->select({});
 owner->consumer.discardSnapshot();owner->lastPacket={};owner->lastPresentation={};owner->correctionEstimate.reset();
}
// Exact saved UUID only. A missing UUID remains unselected, and restoration never
// saves configuration or issues an offset action. Protected failures stay stopped.
bool resumeVisibleCapture(const char* stage){
 if(isolatedQa||!owner||!root||!picker||!ticker||!owner->selectedSourceStore||owner->done||
    !normalDockVisible.value_or(false)||collectionTransition||shutdownStarted)return false;
 if(!owner->binding.selectedBorrowedForOwner()){
  owner->refreshCatalog();
  const auto restored=pitatuner::selection::restore(*picker,*owner->selectedSourceStore,[](const QString& uuid){owner->select(uuid.toStdString());const bool ready=owner->control&&!owner->invalidated&&owner->binding.key().uuid==uuid.toStdString();if(!ready)owner->select({});return ready;});
  logger(LOG_INFO,"SL_SELECTION_RESTORE stage=%s status=%s",stage,pitatuner::selection::code(restored));
 }
 ticker->start(16);return owner->binding.selectedBorrowedForOwner()!=nullptr;
}
void captureVisibilityChanged(bool visible)noexcept{
 if(normalDockVisible&&*normalDockVisible==visible)return;
 normalDockVisible=visible;
 try{
  if(isolatedQa||!owner||!root||!picker||!ticker||shutdownStarted)return;
  if(!visible){detachCaptureSelection();return;}
  resumeVisibleCapture("dock_shown");
 }catch(const std::exception& ex){if(owner)owner->uiFailure(ex.what());}catch(...){if(owner)owner->uiFailure("visibility_exception");}
}
// CLEANUP also covers host paths (including duplicate activation) without CHANGING.
void detachCollectionSelection(const char* stage)noexcept{
 try{
  if(isolatedQa)return;
  collectionTransition=true;
  if(!owner||!root||!picker)return;
  const bool selected=owner->binding.selectedBorrowedForOwner()!=nullptr;
  detachCaptureSelection();
  logger(LOG_INFO,"SL_COLLECTION stage=%s previouslySelected=%s captureDetached=%s liveCaptureFilters=%llu",stage,selected?"true":"false",owner->binding.selectedBorrowedForOwner()?"false":"true",cap::Binding::liveFilters());
 }catch(const std::exception& ex){if(owner)owner->uiFailure(ex.what());}catch(...){if(owner)owner->uiFailure("collection_detach_exception");}
}
void event(obs_frontend_event e,void*){
 if(e==OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING){detachCollectionSelection("changing");return;}
 if(e==OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP){detachCollectionSelection("cleanup");return;}
 if(e==OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED){
  try{if(!isolatedQa){
   if(owner&&root){
    // Copies saved before CHANGING may contain an inert persisted boundary.
    // Remove only our internal type before restoring the saved UUID if visible.
    const auto cleaned=cap::Binding::removeSavedBoundaries(*api);owner->refreshCatalog();collectionTransition=false;
    const bool restored=resumeVisibleCapture("collection_changed");
    logger(LOG_INFO,"SL_COLLECTION stage=changed savedBoundariesCleaned=%llu selectionRestored=%s",static_cast<unsigned long long>(cleaned),restored?"true":"false");
   }else collectionTransition=false;
  }}catch(const std::exception& ex){if(owner)owner->uiFailure(ex.what());}catch(...){if(owner)owner->uiFailure("collection_changed_exception");}
  return;
 }
 if(e==OBS_FRONTEND_EVENT_FINISHED_LOADING){try{if(isolatedQa)checkLocales();else{const auto cleaned=cap::Binding::removeSavedBoundaries(*api);logger(LOG_INFO,"SL_SAVED_BOUNDARIES_CLEANED count=%llu",static_cast<unsigned long long>(cleaned));}
  root=new QWidget;root->setBackgroundRole(QPalette::Base);root->setAutoFillBackground(true);auto* layout=new QVBoxLayout(root);root->setMinimumWidth(360);root->resize(360,850);auto* grid=new QGridLayout;std::vector<QPushButton*> referenceButtons;
  metrics=std::make_unique<pitatuner::qt::Metrics>(root,grid,[](pitatuner::qt::Text role){return QString::fromUtf8(obs_module_text(pitatuner::locale::key(role)));});picker=new pitatuner::qt::WrapComboBox(root,[]()noexcept{try{if(owner&&!owner->done)owner->refreshCatalog();}catch(const std::exception& e){if(owner)owner->uiFailure(e.what());}catch(...){if(owner)owner->uiFailure("catalog_refresh_exception");}});picker->setAccessibleName(QString::fromUtf8(obs_module_text("LabelSource")));readButton=new pitatuner::qt::WrapButton(QString::fromUtf8(obs_module_text("BtnRefreshOffset")),root);
  for(auto** slot:{&upper,&lower}){*slot=new QCustomPlot(root);(*slot)->setMinimumHeight(200);(*slot)->yAxis->setRange(-0.6,0.6);}
  pitatuner::qt::waveb::style(upper,QString::fromUtf8(obs_module_text("LabelWavePre")));
  pitatuner::qt::waveb::style(lower,QString::fromUtf8(obs_module_text("LabelWavePost")));
  waveZoom=new pitatuner::qt::waveb::WheelZoom(upper,isolatedQa?lower:upper,root,[]()noexcept{try{if(owner&&!owner->done){owner->waveformCadence.reset();owner->draw();}}catch(const std::exception& e){if(owner)owner->uiFailure(e.what());}catch(...){if(owner)owner->uiFailure("wheel_zoom_exception");}},isolatedQa?0:5000);
  if(isolatedQa){auto* waveMargins=new QCPMarginGroup(upper);upper->axisRect()->setMarginGroup(QCP::msLeft|QCP::msRight,waveMargins);lower->axisRect()->setMarginGroup(QCP::msLeft|QCP::msRight,waveMargins);}
  check("dock_added",addDock("pitatuner_normal_source_integration",obs_module_text("DockTitle"),root));owner=std::make_unique<Owner>();if(!isolatedQa){owner->overlayLegend=new pitatuner::qt::WrapLabel(root);owner->overlayRenderer=std::make_unique<pitatuner::qt::overlay::Renderer>(upper,owner->overlayLegend,QString::fromUtf8(obs_module_text("LabelWavePre")),QString::fromUtf8(obs_module_text("LabelWavePost")));upper->setMinimumHeight(280);lower->hide();new pitatuner::qt::WaveformRateButton(upper,[](unsigned fps)noexcept{try{if(owner&&!owner->done){owner->waveformCadence.setFps(fps);owner->draw();}}catch(const std::exception& e){if(owner)owner->uiFailure(e.what());}catch(...){if(owner)owner->uiFailure("waveform_rate_exception");}});QObject::connect(upper,&QCustomPlot::beforeReplot,root,[]{if(owner)owner->plotBeginNs=Owner::now();});QObject::connect(upper,&QCustomPlot::afterReplot,root,[]{if(owner&&owner->plotBeginNs){const auto elapsed=Owner::now()-owner->plotBeginNs;++owner->plotReplots;owner->plotTotalNs+=elapsed;owner->plotMaxNs=std::max(owner->plotMaxNs,elapsed);}});}if(isolatedQa){owner->a=std::make_unique<Source>("known normal source A",0,0,1024);owner->b=std::make_unique<Source>("known normal source B",1,20000000,1000);owner->a->start();owner->b->start();owner->qa37Setup();}for(const auto& source:cap::catalog(*api))picker->addItem(QString::fromStdString(source.name),QString::fromStdString(source.uuid));picker->setCurrentIndex(-1);if(isolatedQa)owner->qa37Enumerated();
  if(!isolatedQa)if(auto* dock=qobject_cast<QDockWidget*>(root->parentWidget()))if(auto* main=dock->parentWidget()){
   auto* closer=new MainCloseObserver(root,[]()noexcept{try{if(owner){QSignalBlocker block(picker);picker->setCurrentIndex(-1);owner->select({});logger(LOG_INFO,"SL_MAIN_CLOSE_CAPTURE_DETACHED live=%llu",cap::Binding::liveFilters());}}catch(...){if(owner)owner->uiFailure("main_close_cleanup");}});main->installEventFilter(closer);
  }
  QObject::connect(picker,qOverload<int>(&QComboBox::currentIndexChanged),root,[](int i)noexcept{try{if(!isolatedQa&&(!normalDockVisible.value_or(false)||collectionTransition||shutdownStarted))return;owner->select(i>=0?picker->itemData(i).toString().toStdString():std::string{},true);}catch(const std::exception& e){owner->uiFailure(e.what());}catch(...){owner->uiFailure("selection_exception");}});if(isolatedQa)owner->choose(*owner->a);
  metrics->connectActions(root,[](size_t index)noexcept{try{
   if(!owner||owner->done)return;
   if(index==5){
    if(!owner->control||!owner->backend)return;
    owner->context();owner->observe();owner->draw();owner->authority();
    if(!owner->automaticReady){
     logger(LOG_INFO,"SL_AUTO_REJECT freshGate=false write=false deferred=false");return;
    }
   }
   if(owner)owner->correctionResult=owner->correctionClick(index);
  }catch(const std::exception& e){if(owner)owner->uiFailure(e.what());}catch(...){if(owner)owner->uiFailure("correction_exception");}});
  if(isolatedQa)for(const auto mode:{pita::Mode::ExplicitPreTarget,pita::Mode::MeasuredReference}){
   const auto key=mode==pita::Mode::ExplicitPreTarget?"BtnSetExplicitPreTargetReference":"BtnSetMeasuredReference";
   auto* button=new pitatuner::qt::WrapButton(QString::fromUtf8(obs_module_text(key)),root);referenceButtons.push_back(button);
   QObject::connect(button,&QPushButton::clicked,root,[mode]()noexcept{try{
    const auto description=mode==pita::Mode::ExplicitPreTarget?"ModeExplicitPreTargetDescription":"ModeMeasuredReferenceDescription";
    const bool confirmed=QMessageBox::question(root,QString::fromUtf8(obs_module_text("LabelReferenceMode")),QString::fromUtf8(obs_module_text(description)),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)==QMessageBox::Yes;
    owner->confirmCorrection(mode,confirmed);
   }catch(const std::exception& e){owner->uiFailure(e.what());}catch(...){owner->uiFailure("reference_exception");}});
  }
  auto* scroll=new QScrollArea(root);scroll->setWidgetResizable(true);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);scroll->setMinimumWidth(0);
  auto* content=new pitatuner::qt::CompactDock(metrics.get(),picker,readButton,upper,isolatedQa?lower:nullptr,referenceButtons,nullptr,[]()noexcept{try{if(owner&&!owner->done){owner->refreshCatalog();owner->read(true);}}catch(const std::exception& e){if(owner)owner->uiFailure(e.what());}catch(...){if(owner)owner->uiFailure("show_read_exception");}});if(!isolatedQa&&owner->overlayLegend){auto* displayLayout=static_cast<QVBoxLayout*>(content->layout());displayLayout->insertWidget(displayLayout->indexOf(upper)+1,owner->overlayLegend);}scroll->setWidget(content);scroll->setBackgroundRole(QPalette::Base);scroll->viewport()->setBackgroundRole(QPalette::Base);content->setBackgroundRole(QPalette::Base);layout->setContentsMargins(0,0,0,0);layout->addWidget(scroll);delete grid;
  using GetMainWindow=void*(*)();auto getMainWindow=reinterpret_cast<GetMainWindow>(GetProcAddress(GetModuleHandleW(L"obs-frontend-api.dll"),"obs_frontend_get_main_window"));
  const bool audioDetailsBound=pitatuner::qt::bindAdvancedAudioButton(metrics->operationButton(4),getMainWindow?static_cast<QWidget*>(getMainWindow()):nullptr,root,[]()noexcept{
   try{if(!owner||owner->done)return false;
    // Opening host settings is not a correction action. Discard an unfinished
    // numeric draft before the new dialog causes a focus-out editingFinished.
    const auto v=owner->control?std::optional{owner->control->view()}:std::optional<pitatuner::View>{};
    if(v&&v->offset)metrics->seedManualOffset(*v->offset);else metrics->discardManualDraft();
    logger(LOG_INFO,"SL_AUDIO_DETAILS requested=true write=false");return true;
   }catch(...){return false;}
  });logger(LOG_INFO,"SL_AUDIO_DETAILS bound=%s route=host_qaction",audioDetailsBound?"true":"false");
#if SONICLATCH_ENABLE_EMBEDDED_QA
  const auto qaWidth=SL_QA_ENV("PITATUNER_COMPACT_WIDTH").toInt();if(qaWidth==360||qaWidth==480||qaWidth==900){root->resize(qaWidth,850);if(auto* dock=qobject_cast<QDockWidget*>(root->parentWidget()))dock->resize(qaWidth,850);}
#endif
  QObject::connect(readButton,&QPushButton::clicked,root,[]()noexcept{try{owner->read();}catch(const std::exception& e){owner->uiFailure(e.what());}catch(...){owner->uiFailure("read_exception");}});if(isolatedQa&&!owner->qaFourActive){readButton->click();readButton->click();}if(isolatedQa)check("repeat_read_no_stale_view",!owner->done&&!owner->control->view().protectedState&&upper->plottableCount()==0&&lower->plottableCount()==0&&metrics->value(0)==pitatuner::qt::nsValue({}));ticker=new QTimer(root);QObject::connect(ticker,&QTimer::timeout,root,[]{owner->tick();});owner->phaseAt=Owner::now();ticker->setTimerType(Qt::PreciseTimer);if(isolatedQa)ticker->start(33);
  if(!isolatedQa){owner->selectedSourceStore=std::make_unique<pitatuner::selection::Store>(selectedSourcePath());
   if(auto* dock=qobject_cast<QDockWidget*>(root->parentWidget())){
    QObject::connect(dock,&QDockWidget::visibilityChanged,root,[](bool visible)noexcept{captureVisibilityChanged(visible);});
    dock->installEventFilter(new DockVisibilityObserver(root,[](bool visible)noexcept{captureVisibilityChanged(visible);}));
    // Store/timer/owner are ready before the first visibility decision. A hidden
    // startup must not restore capture, even if child showEvent ran earlier.
    captureVisibilityChanged(dock->isVisible());
   }
  }
  installWheelDiagnostic();
 }catch(const std::exception& ex){logger(LOG_ERROR,"PT_PRODUCT_FAILED reason=%s",ex.what());}}
 if(e==OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN){shutdownStarted=true;removeWheelDiagnostic();if(ticker)ticker->stop();if(SL_QA_ENV("PITATUNER_43_QA")=="shutdown-active-v1")logger(LOG_INFO,"PT_PRODUCT_43_SHUTDOWN_BEGIN ownerAlive=%s producerA=%s producerB=%s",owner?"true":"false",owner&&owner->a&&owner->a->producer.joinable()?"true":"false",owner&&owner->b&&owner->b->producer.joinable()?"true":"false");owner.reset();metrics.reset();removeDock("pitatuner_normal_source_integration");if(root)delete root.data();p_obs_frontend_remove_event_callback(event,nullptr);logger(LOG_INFO,"PT_PRODUCT_SHUTDOWN rootAlive=%s liveCaptureFilters=%llu",root?"true":"false",cap::Binding::liveFilters());}
}
}
bool obs_module_load(){try{
#if SONICLATCH_ENABLE_EMBEDDED_QA
 wchar_t mode[64]{},path[MAX_PATH]{};GetEnvironmentVariableW(L"PITATUNER_ISOLATED_PRODUCT_PATH",mode,64);GetModuleFileNameW(nullptr,path,MAX_PATH);
 isolatedQa=mode[0]!=0;
 // QA containment only: exact previously verified path/version pairs.
 // Normal product entry keeps its separate capability-based load path.
 for(auto& c:path)if(c==L'\\')c=L'/';
 const std::wstring_view executable(path);
 const bool isolated32=executable==SONICLATCH_QA_OBS32_PATH;
 const bool isolated33=executable==SONICLATCH_QA_OBS33_PATH;
 if(isolatedQa&&(wcscmp(mode,L"known-normal-dock-v1")||(!isolated32&&!isolated33)))return false;
#ifndef SONICLATCH_ISOLATED_POSITIVE_FIXTURE
 if(!isolatedQa)return false;
#endif
#endif
 auto dll=GetModuleHandleW(L"obs.dll"),front=GetModuleHandleW(L"obs-frontend-api.dll");if(!dll||!front)return false;auto version=reinterpret_cast<decltype(&obs_get_version_string)>(GetProcAddress(dll,"obs_get_version_string"));if(!version)return false;
#if SONICLATCH_ENABLE_EMBEDDED_QA
 if(isolatedQa&&((isolated32&&strcmp(version(),"32.2.2"))||(isolated33&&strcmp(version(),"33.0.0-rc2"))))return false;
#ifdef SONICLATCH_ISOLATED_POSITIVE_FIXTURE
 if(isolatedQa||!isolated32||strcmp(version(),"32.2.2")||SL_QA_ENV("SONICLATCH_POSITIVE_CORRECTION_QA")!="100-to-30-v1"||SL_QA_ENV("SONICLATCH_PRODUCT_DRIVER")!="isolated-product-v1")return false;
#endif
#endif
#define LOAD(n) p_##n=reinterpret_cast<decltype(p_##n)>(GetProcAddress(dll,#n));if(!p_##n)return false
 LOAD(obs_source_set_muted);LOAD(obs_source_update);LOAD(obs_source_set_enabled);LOAD(obs_source_filter_remove);LOAD(obs_source_filter_set_order);LOAD(obs_source_create);LOAD(obs_source_create_private);LOAD(obs_source_release);LOAD(obs_source_get_ref);LOAD(obs_source_filter_add);LOAD(obs_source_output_audio);LOAD(obs_source_get_uuid);LOAD(obs_source_remove);LOAD(obs_source_get_sync_offset);LOAD(obs_source_set_sync_offset);LOAD(obs_wait_for_destroy_queue);LOAD(obs_data_create);LOAD(obs_data_set_int);LOAD(obs_data_get_int);LOAD(obs_data_release);
 p_obs_frontend_add_event_callback=reinterpret_cast<decltype(p_obs_frontend_add_event_callback)>(GetProcAddress(front,"obs_frontend_add_event_callback"));p_obs_frontend_remove_event_callback=reinterpret_cast<decltype(p_obs_frontend_remove_event_callback)>(GetProcAddress(front,"obs_frontend_remove_event_callback"));logger=reinterpret_cast<Logger>(GetProcAddress(dll,"blog"));if(!logger||!p_obs_frontend_add_event_callback||!p_obs_frontend_remove_event_callback)return false;
 addDock=reinterpret_cast<decltype(addDock)>(GetProcAddress(front,"obs_frontend_add_dock_by_id"));removeDock=reinterpret_cast<decltype(removeDock)>(GetProcAddress(front,"obs_frontend_remove_dock"));if(!addDock||!removeDock)return false;
 api=std::make_unique<cap::Api>(dll);cap::Binding::registerFilter(*api);
#if SONICLATCH_ENABLE_EMBEDDED_QA
if(isolatedQa&&SL_QA_ENV("PITATUNER_37A_QA")=="deleted-before-select-v1"){qa37RealLookup=api->obs_get_source_by_uuid;api->obs_get_source_by_uuid=qa37Lookup;obs_source_info qa{};qa.id="pitatuner_qa37_selector_input";qa.type=OBS_SOURCE_TYPE_INPUT;qa.output_flags=OBS_SOURCE_AUDIO;qa.get_name=inputName;qa.create=qa37Create;qa.destroy=qa37Destroy;api->obs_register_source_s(&qa,sizeof(qa));}
 if(isolatedQa){obs_source_info input{};input.id="pitatuner_known_normal_input";input.type=OBS_SOURCE_TYPE_INPUT;input.output_flags=OBS_SOURCE_AUDIO;input.get_name=inputName;input.create=inputCreate;input.destroy=inputDestroy;api->obs_register_source_s(&input,sizeof(input));
 for(unsigned i=0;i<3;i++){obs_source_info f{};f.id=i==0?"pitatuner_known_shift":i==1?"pitatuner_known_observer":"pitatuner_known_passthrough";f.type=OBS_SOURCE_TYPE_FILTER;f.output_flags=OBS_SOURCE_AUDIO;f.get_name=inputName;f.create=filterCreate;f.update=filterUpdate;f.destroy=filterDestroy;f.filter_audio=i==0?shift:i==1?observe:passthrough;api->obs_register_source_s(&f,sizeof(f));}
 }
#endif
 return true;
}catch(const std::exception& e){if(logger)logger(LOG_ERROR,"SL_LOAD_FAILED reason=%s",e.what());return false;}catch(...){return false;}
}
void obs_module_post_load(){if(p_obs_frontend_add_event_callback)p_obs_frontend_add_event_callback(event,nullptr);}
void obs_module_unload(){quitting.store(true);if(controller.joinable())controller.join();if(logger)logger(LOG_INFO,"PT_PRODUCT_UNLOAD liveCaptureFilters=%llu",cap::Binding::liveFilters());if(SL_QA_ENV("PITATUNER_43_QA")=="shutdown-active-v1")logger(LOG_INFO,"PT_PRODUCT_43_UNLOAD sourcesCreated=%u sourceADestroyed=%u sourceBDestroyed=%u liveCaptureFilters=%llu",created.load(),destroyed[0].load(),destroyed[1].load(),cap::Binding::liveFilters());api.reset();}






