#include <qa-local-paths.hpp>
#define NOMINMAX
#include <windows.h>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QApplication>
#include <QDockWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QMessageBox>
#include <QTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <array>
#include <thread>
#include <atomic>
#include <chrono>
#include <memory>
#include <string_view>
#include <stdexcept>
#include <media-io/audio-io.h>
#include <algorithm>
#include <optional>
#include <vector>
#include <cmath>
OBS_DECLARE_MODULE()
namespace {
#define API(n) decltype(&n) f_##n{}
API(obs_register_source_s);API(obs_source_create);API(obs_source_create_private);API(obs_source_release);API(obs_source_remove);API(obs_source_get_uuid);API(obs_source_filter_add);API(obs_source_output_audio);API(obs_source_set_sync_offset);API(obs_source_get_sync_offset);API(obs_source_get_signal_handler);API(signal_handler_connect);API(signal_handler_disconnect);API(obs_source_get_ref);API(obs_frontend_add_event_callback);API(obs_frontend_remove_event_callback);API(obs_wait_for_destroy_queue);
API(obs_data_create);API(obs_data_get_int);API(obs_data_set_int);API(obs_data_release);API(obs_source_set_audio_mixers);API(obs_set_output_source);API(obs_get_audio);API(audio_output_connect);API(audio_output_disconnect);
using Logger=void(*)(int,const char*,...);Logger qaLogger{};
QPointer<QDockWidget> dock;QComboBox* picker{};QTimer* timer{};unsigned checks{},phase{};bool done{},failed{};std::chrono::steady_clock::time_point began,at;
const char* name(void*){return "SonicLatch independent product-flow fixture";}
void* create(obs_data_t*,obs_source_t*){return new int(1);}void destroy(void* p){delete static_cast<int*>(p);}
void* filterCreate(obs_data_t* settings,obs_source_t*){return new int64_t(f_obs_data_get_int(settings,"delayNs"));}
void filterDestroy(void* p){delete static_cast<int64_t*>(p);}
obs_audio_data* shift(void* p,obs_audio_data* a){a->timestamp+=static_cast<uint64_t>(*static_cast<int64_t*>(p));return a;}
using Clock=std::chrono::steady_clock;
std::jthread pairedProducer;
struct Mix {
 std::array<std::atomic<uint64_t>,128> marks{};std::atomic<unsigned> count{};std::atomic<bool> overflow{};bool above{};
 static void capture(void* p,size_t,audio_data* data){auto& self=*static_cast<Mix*>(p);auto* pcm=reinterpret_cast<const float*>(data->data[0]);if(!pcm)return;for(uint32_t i=0;i<data->frames;++i){const bool high=pcm[i]>.6f;if(high&&!self.above){auto n=self.count.load(std::memory_order_relaxed);if(n<self.marks.size()){self.marks[n].store(data->timestamp+uint64_t(i)*1000000000ULL/48000,std::memory_order_relaxed);self.count.store(n+1,std::memory_order_release);}else self.overflow.store(true);}self.above=high;}}
};
std::array<Mix,2> mixed;std::array<bool,2> attached{};
std::optional<double> beforeLag,afterLag,restoredLag;
std::optional<double> lagSince(Clock::time_point since){
 const auto floor=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(since.time_since_epoch()).count())+500000000ULL;
 if(mixed[0].overflow.load()||mixed[1].overflow.load())throw std::runtime_error("bounded marker storage overflow");
 std::vector<double> lags;
 const auto refs=mixed[0].count.load(std::memory_order_acquire),duts=mixed[1].count.load(std::memory_order_acquire);
 for(unsigned r=0;r<refs;++r){auto stamp=mixed[0].marks[r].load();if(stamp<floor)continue;int64_t nearest=INT64_MAX;for(unsigned d=0;d<duts;++d){auto value=mixed[1].marks[d].load();if(value<floor)continue;auto delta=static_cast<int64_t>(value)-static_cast<int64_t>(stamp);if(std::abs(delta)<std::abs(nearest))nearest=delta;}if(std::abs(nearest)<200000000LL)lags.push_back(double(nearest)/1e6);}
 if(lags.size()<3)return {};std::sort(lags.begin(),lags.end());return lags[lags.size()/2];
}
struct Fixture {
 obs_source_t* source{},*filter{};std::string uuid;std::jthread producer;std::atomic<unsigned> notifications{};unsigned seed;
 static void notify(void* p,calldata_t*){static_cast<Fixture*>(p)->notifications.fetch_add(1);}
 Fixture(const char* label,int64_t offset,unsigned index,int64_t delayNs):seed(20261019+index){
  source=f_obs_source_create("soniclatch_product_driver_input",label,nullptr,nullptr);if(!source)throw std::runtime_error("fixture create");uuid=f_obs_source_get_uuid(source);
  auto* settings=f_obs_data_create();f_obs_data_set_int(settings,"delayNs",delayNs);filter=f_obs_source_create_private("soniclatch_product_driver_shift","independent known timestamp shift",settings);f_obs_data_release(settings);if(!filter)throw std::runtime_error("fixture filter");f_obs_source_filter_add(source,filter);f_obs_source_set_sync_offset(source,offset);
  f_signal_handler_connect(f_obs_source_get_signal_handler(source),"audio_sync",notify,this);
 }
 void stop(){if(pairedProducer.joinable()){pairedProducer.request_stop();pairedProducer.join();}}
 ~Fixture(){stop();if(source){f_signal_handler_disconnect(f_obs_source_get_signal_handler(source),"audio_sync",notify,this);f_obs_source_remove(source);f_obs_source_release(source);}if(filter)f_obs_source_release(filter);}
};
std::unique_ptr<Fixture> a,b;
void startPaired(){
 for(unsigned i=0;i<2;++i){audio_convert_info info{};info.format=AUDIO_FORMAT_FLOAT_PLANAR;info.samples_per_sec=48000;info.speakers=SPEAKERS_STEREO;attached[i]=f_audio_output_connect(f_obs_get_audio(),i,&info,Mix::capture,&mixed[i]);if(!attached[i])throw std::runtime_error("mixed output observer connect");}
 f_obs_source_set_audio_mixers(b->source,1);f_obs_source_set_audio_mixers(a->source,2);f_obs_set_output_source(0,b->source);f_obs_set_output_source(1,a->source);
 pairedProducer=std::jthread([](std::stop_token stop){std::array<float,1024> pcm{};uint32_t rng=20261019;uint64_t samples=0;const auto origin=Clock::now();const auto base=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(origin.time_since_epoch()).count());obs_source_audio data{};data.data[0]=reinterpret_cast<uint8_t*>(pcm.data());data.data[1]=reinterpret_cast<uint8_t*>(pcm.data());data.frames=1024;data.samples_per_sec=48000;data.format=AUDIO_FORMAT_FLOAT_PLANAR;data.speakers=SPEAKERS_STEREO;
  while(!stop.stop_requested()){std::this_thread::sleep_until(origin+std::chrono::nanoseconds(samples*1000000000ULL/48000));for(unsigned i=0;i<1024;++i){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;pcm[i]=((samples+i)%48000<16)?0.8f:float(static_cast<int32_t>(rng))/4294967296.0f*.06f;}data.timestamp=base+samples*1000000000ULL/48000;f_obs_source_output_audio(b->source,&data);f_obs_source_output_audio(a->source,&data);samples+=1024;}
 });
}
void stopPaired(){if(pairedProducer.joinable()){pairedProducer.request_stop();pairedProducer.join();}f_obs_set_output_source(0,nullptr);f_obs_set_output_source(1,nullptr);for(unsigned i=0;i<2;++i)if(attached[i]){f_audio_output_disconnect(f_obs_get_audio(),i,Mix::capture,&mixed[i]);attached[i]=false;}}

QString text(const char* key){auto dll=GetModuleHandleW(L"SonicLatch.dll");using Get=bool(*)(const char*,const char**);auto get=reinterpret_cast<Get>(GetProcAddress(dll,"obs_module_get_string"));const char* value=nullptr;if(!get||!get(key,&value)||!value)throw std::runtime_error("product locale key");return QString::fromUtf8(value);}
QPushButton* button(const char* key){const auto value=text(key);for(auto* p:dock->findChildren<QPushButton*>())if(p->text()==value)return p;throw std::runtime_error(key);}
QString metric(const char* key){for(auto* label:dock->findChildren<QLabel*>())if(label->text()==text(key)){for(auto* value:label->parentWidget()->findChildren<QLabel*>(QString{},Qt::FindDirectChildrenOnly))if(value!=label)return value->text();}return {};}
QString current(){return metric("LabelCurrentOffset");}
void check(const char* what,bool pass){++checks;qaLogger(pass?LOG_INFO:LOG_ERROR,"SL_PRODUCT_FLOW_CASE name=%s pass=%s phase=%u",what,pass?"true":"false",phase);if(!pass)throw std::runtime_error(what);}
void finish(bool success){if(done)return;done=true;failed=!success;timer->stop();
 if(picker)picker->setCurrentIndex(-1);stopPaired();a.reset();b.reset();f_obs_wait_for_destroy_queue();
 const auto dir=QCoreApplication::applicationDirPath()+"/../../config/pitatuner-integration";
 QFile file(dir+"/product-flow-result.json");if(file.open(QIODevice::WriteOnly))file.write(QJsonDocument(QJsonObject{{"pass",success},{"checks",int(checks)},{"normalProductEntry",true},{"productQaMode",false},{"fixtureSourcesOnly",true},{"actualMixedOutputMeasured",beforeLag.has_value()&&afterLag.has_value()&&restoredLag.has_value()},{"beforeLagMs",beforeLag.value_or(-999)},{"afterLagMs",afterLag.value_or(-999)},{"restoredLagMs",restoredLag.value_or(-999)},{"referenceMarkers",int(mixed[0].count.load())},{"dutMarkers",int(mixed[1].count.load())},{"PCMStored",false},{"fixturePolicyOnly",true}}).toJson());
 qaLogger(success?LOG_INFO:LOG_ERROR,"SL_PRODUCT_FLOW_DONE pass=%s checks=%u",success?"true":"false",checks);
 auto* main=dock?dock->parentWidget():nullptr;if(main)QTimer::singleShot(100,main,[main]{main->close();});
}
void tick(){if(done)return;try{
 const auto now=std::chrono::steady_clock::now();if(now-began>std::chrono::seconds(35))throw std::runtime_error("product flow timeout");
 if(phase==0){for(auto* widget:QApplication::allWidgets())if(auto* p=qobject_cast<QDockWidget*>(widget);p&&p->objectName()=="pitatuner_normal_source_integration")dock=p;
  if(!dock)return;picker=dock->findChild<QComboBox*>();check("normal_dock_title",dock->windowTitle()==text("DockTitle"));check("normal_initial_unselected",picker&&picker->currentIndex()==-1);
  a=std::make_unique<Fixture>("Independent artificial DUT",100000000,1,70000000);b=std::make_unique<Fixture>("Independent artificial reference",100000000,2,0);startPaired();
  dock->hide();dock->setFloating(true);dock->resize(380,880);dock->show();dock->raise();
  QTimer::singleShot(100,dock,[]{try{const int row=picker->findData(QString::fromStdString(a->uuid));check("catalog_real_input_uuid",row>=0);picker->setCurrentIndex(row);phase=1;at=std::chrono::steady_clock::now();}catch(const std::exception& e){qaLogger(LOG_ERROR,"SL_PRODUCT_FLOW_ERROR reason=%s",e.what());finish(false);}});phase=99;return;
 }
 if(phase==99)return;
 if(phase==1&&button("BtnPlus1")->isEnabled()&&metric("LabelDelay").startsWith("70")){
  check("selection_readback_100",f_obs_source_get_sync_offset(a->source)==100000000&&current().startsWith("100"));check("no_implicit_automatic_baseline",!button("BtnRecommend")->isEnabled());
  button("BtnSettings")->click();phase=99;auto* responder=new QTimer(dock);responder->setInterval(50);QObject::connect(responder,&QTimer::timeout,responder,[responder]{for(auto* widget:QApplication::allWidgets())if(auto* box=qobject_cast<QMessageBox*>(widget);box&&box->windowTitle()==text("LabelReferenceMode")&&box->text()==text("ModeExplicitPreTargetDescription")){qaLogger(LOG_INFO,"SL_PRODUCT_FLOW_DIALOG matching_confirmation=true");responder->stop();if(auto* yes=box->button(QMessageBox::Yes))yes->click();responder->deleteLater();break;}});responder->start();
  button("BtnSetExplicitPreTargetReference")->click();phase=2;return;
 }
 if(phase==2&&button("BtnRecommend")->isEnabled()){
  beforeLag=lagSince(began);if(!beforeLag)return;
  qaLogger(LOG_INFO,"SL_PRODUCT_FLOW_OUTPUT stage=before lagMs=%.6f",*beforeLag);check("before_output_lag_70ms",std::abs(*beforeLag-70.)<=10.);
  button("BtnRecommend")->click();check("auto_ui_getter_30",f_obs_source_get_sync_offset(a->source)==30000000);check("success_current_readback_30",current().startsWith("30"));check("other_source_unchanged_auto",f_obs_source_get_sync_offset(b->source)==100000000);check("undo_enabled_after_apply",button("BtnUndo")->isEnabled());at=now;phase=3;return;
 }
 if(phase==3){afterLag=lagSince(at);if(!afterLag)return;qaLogger(LOG_INFO,"SL_PRODUCT_FLOW_OUTPUT stage=after lagMs=%.6f",*afterLag);check("actual_output_improves_to_zero",std::abs(*afterLag)<=10.&&std::abs(*beforeLag)-std::abs(*afterLag)>=50.);button("BtnUndo")->click();check("undo_ui_getter_100",f_obs_source_get_sync_offset(a->source)==100000000&&current().startsWith("100"));at=now;phase=4;return;}
 if(phase==4){restoredLag=lagSince(at);if(!restoredLag)return;qaLogger(LOG_INFO,"SL_PRODUCT_FLOW_OUTPUT stage=undo lagMs=%.6f",*restoredLag);check("actual_output_undo_restores_70ms",std::abs(*restoredLag-70.)<=10.&&std::abs(*restoredLag-*beforeLag)<=10.);check("other_source_unchanged_after_undo",f_obs_source_get_sync_offset(b->source)==100000000);check("single_apply_and_undo_notifications",a->notifications.load()==2&&b->notifications.load()==0);const auto dir=QCoreApplication::applicationDirPath()+"/../../config/pitatuner-integration";check("product_dock_image",dock->grab().save(dir+"/product-flow-dock.png"));finish(true);}
 }catch(const std::exception& e){qaLogger(LOG_ERROR,"SL_PRODUCT_FLOW_ERROR reason=%s",e.what());finish(false);}catch(...){finish(false);}}
void event(obs_frontend_event e,void*){if(e==OBS_FRONTEND_EVENT_FINISHED_LOADING){began=std::chrono::steady_clock::now();timer=new QTimer(QCoreApplication::instance());QObject::connect(timer,&QTimer::timeout,[]{tick();});timer->start(50);QTimer::singleShot(35000,QCoreApplication::instance(),[]{if(done)return;for(auto* widget:QApplication::allWidgets())if(auto* box=qobject_cast<QMessageBox*>(widget);box&&box->windowTitle()==text("LabelReferenceMode")){if(auto* no=box->button(QMessageBox::No))no->click();}finish(false);});}if(e==OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN){if(timer)timer->stop();stopPaired();a.reset();b.reset();}}
}
bool obs_module_load(){try{
 wchar_t mode[64]{},path[MAX_PATH]{};GetEnvironmentVariableW(L"SONICLATCH_PRODUCT_DRIVER",mode,64);GetModuleFileNameW(nullptr,path,MAX_PATH);for(auto& c:path)if(c==L'\\')c=L'/';
 if(wcscmp(mode,L"isolated-product-v1")||GetEnvironmentVariableW(L"PITATUNER_ISOLATED_PRODUCT_PATH",nullptr,0)||std::wstring_view(path)!=SONICLATCH_QA_OBS32_PATH)return false;
 auto dll=GetModuleHandleW(L"obs.dll"),front=GetModuleHandleW(L"obs-frontend-api.dll");if(!dll||!front)return false;
#define LOAD(n) f_##n=reinterpret_cast<decltype(f_##n)>(GetProcAddress(dll,#n));if(!f_##n)return false
 LOAD(obs_data_create);LOAD(obs_data_get_int);LOAD(obs_data_set_int);LOAD(obs_data_release);LOAD(obs_source_set_audio_mixers);LOAD(obs_set_output_source);LOAD(obs_get_audio);LOAD(audio_output_connect);LOAD(audio_output_disconnect); LOAD(obs_register_source_s);LOAD(obs_source_create);LOAD(obs_source_create_private);LOAD(obs_source_release);LOAD(obs_source_remove);LOAD(obs_source_get_uuid);LOAD(obs_source_filter_add);LOAD(obs_source_output_audio);LOAD(obs_source_set_sync_offset);LOAD(obs_source_get_sync_offset);LOAD(obs_source_get_signal_handler);LOAD(signal_handler_connect);LOAD(signal_handler_disconnect);LOAD(obs_source_get_ref);LOAD(obs_wait_for_destroy_queue);
 f_obs_frontend_add_event_callback=reinterpret_cast<decltype(f_obs_frontend_add_event_callback)>(GetProcAddress(front,"obs_frontend_add_event_callback"));f_obs_frontend_remove_event_callback=reinterpret_cast<decltype(f_obs_frontend_remove_event_callback)>(GetProcAddress(front,"obs_frontend_remove_event_callback"));qaLogger=reinterpret_cast<Logger>(GetProcAddress(dll,"blog"));if(!qaLogger||!f_obs_frontend_add_event_callback||!f_obs_frontend_remove_event_callback)return false;
 obs_source_info input{};input.id="soniclatch_product_driver_input";input.type=OBS_SOURCE_TYPE_INPUT;input.output_flags=OBS_SOURCE_AUDIO;input.get_name=name;input.create=create;input.destroy=destroy;f_obs_register_source_s(&input,sizeof input);
 auto filter=input;filter.id="soniclatch_product_driver_shift";filter.type=OBS_SOURCE_TYPE_FILTER;filter.create=filterCreate;filter.destroy=filterDestroy;filter.filter_audio=shift;f_obs_register_source_s(&filter,sizeof filter);return true;
 }catch(...){return false;}}
void obs_module_post_load(){if(f_obs_frontend_add_event_callback)f_obs_frontend_add_event_callback(event,nullptr);}
void obs_module_unload(){if(timer)delete timer;timer=nullptr;stopPaired();a.reset();b.reset();if(f_obs_frontend_remove_event_callback)f_obs_frontend_remove_event_callback(event,nullptr);}
