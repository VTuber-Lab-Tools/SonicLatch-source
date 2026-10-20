#pragma once
// Product-side owner/worker capture boundary; no device creation or offset setter.
#define NOMINMAX
#include <windows.h>
#pragma warning(push)
#pragma warning(disable:4201)
#include <obs-module.h>
#pragma warning(pop)
#include "measurement.hpp"
#include "zero_offset_bridge.hpp"
#include "capture_format.hpp"
#include "capture_packet_split.hpp"
#include "timeline_diagnostic.hpp"
#include "capture_timeline_policy.hpp"
#include "window_media_clock.hpp"
#include "paired_media_clock.hpp"
#include "live_capture_wave.hpp"
#include "sliding_capture_window.hpp"
#include "latest_analysis_lane.hpp"
#include "capture_recovery_policy.hpp"
#include "raw_capture_wave.hpp"
#include "callback_gate.hpp"
#ifdef PITATUNER_CAPTURE_LIFETIME_QA
#include "capture_lifetime_qa.hpp"
#endif
#ifdef PITATUNER_CAPTURE_TIMING_QA
#include "capture_timing_qa.hpp"
#endif
#include "correction_control.hpp"
#include "../integrations/waveform-model/waveform-model-draft/waveform_model.hpp"
#include <memory>
#include <mutex>
#include <thread>
#include <chrono>
#include <stdexcept>
#include <cstring>

namespace pitatuner::capture {
struct Api {
#define CAP_API(n) decltype(&::n) n=nullptr
 CAP_API(obs_source_enum_filters);CAP_API(obs_enum_sources);CAP_API(obs_get_source_by_uuid);CAP_API(obs_source_get_ref);CAP_API(obs_source_release);
 CAP_API(obs_source_get_uuid);CAP_API(obs_source_get_name);CAP_API(obs_source_get_id);CAP_API(obs_source_get_output_flags);CAP_API(obs_source_get_type);CAP_API(obs_source_removed);CAP_API(obs_source_muted);CAP_API(obs_source_enabled);
 CAP_API(obs_register_source_s);CAP_API(obs_source_create_private);CAP_API(obs_source_filter_add);CAP_API(obs_source_filter_remove);CAP_API(obs_source_filter_set_order);CAP_API(obs_source_filter_get_index);CAP_API(obs_source_filter_count);
 CAP_API(obs_source_add_audio_capture_callback);CAP_API(obs_source_remove_audio_capture_callback);CAP_API(obs_source_get_signal_handler);CAP_API(signal_handler_connect);CAP_API(signal_handler_disconnect);CAP_API(obs_get_audio_info);
#undef CAP_API
 explicit Api(HMODULE dll){
#define CAP_LOAD(n) n=reinterpret_cast<decltype(n)>(GetProcAddress(dll,#n));if(!n)throw std::runtime_error(#n)
  CAP_LOAD(obs_source_enum_filters);CAP_LOAD(obs_enum_sources);CAP_LOAD(obs_get_source_by_uuid);CAP_LOAD(obs_source_get_ref);CAP_LOAD(obs_source_release);CAP_LOAD(obs_source_get_uuid);CAP_LOAD(obs_source_get_name);CAP_LOAD(obs_source_get_id);CAP_LOAD(obs_source_get_output_flags);CAP_LOAD(obs_source_get_type);CAP_LOAD(obs_source_removed);CAP_LOAD(obs_source_muted);CAP_LOAD(obs_source_enabled);
  CAP_LOAD(obs_register_source_s);CAP_LOAD(obs_source_create_private);CAP_LOAD(obs_source_filter_add);CAP_LOAD(obs_source_filter_remove);CAP_LOAD(obs_source_filter_set_order);CAP_LOAD(obs_source_filter_get_index);CAP_LOAD(obs_source_filter_count);CAP_LOAD(obs_source_add_audio_capture_callback);CAP_LOAD(obs_source_remove_audio_capture_callback);CAP_LOAD(obs_source_get_signal_handler);CAP_LOAD(signal_handler_connect);CAP_LOAD(signal_handler_disconnect);CAP_LOAD(obs_get_audio_info);
#undef CAP_LOAD
 }
};
// The one allocator for a control lifetime, retained across dock/source objects.
// This is source-session identity, never an estimate/Intent/notification counter.
class SessionIds {
 std::atomic<uint64_t> next_{};
public:
 std::optional<uint64_t> claim(){auto value=next_.load();while(value!=UINT64_MAX){if(next_.compare_exchange_weak(value,value+1))return value+1;}return{};}
};
struct Source {std::string uuid,name,type;};
inline std::vector<Source> catalog(const Api& api){
 struct Context {const Api& api;std::vector<Source> result;};Context c{api,{}};
 api.obs_enum_sources([](void* data,obs_source_t* source){auto& x=*static_cast<Context*>(data);if((x.api.obs_source_get_output_flags(source)&OBS_SOURCE_AUDIO)&&!x.api.obs_source_removed(source)){
  const auto* uuid=x.api.obs_source_get_uuid(source);const auto* name=x.api.obs_source_get_name(source);const auto* type=x.api.obs_source_get_id(source);
  if(uuid&&*uuid)x.result.push_back({uuid,name?name:"",type?type:""});}return true;},&c);
 return c.result;
}
enum class Fault:uint32_t {None=0,Configuration=1,Removed=2,Shape=4,Overflow=8,AudioConfiguration=16,Timeline=32,Muted=64,Analysis=128};
static_assert(static_cast<uint32_t>(Fault::Configuration)==recovery::configuration && static_cast<uint32_t>(Fault::Muted)==recovery::muted);
enum class SelectError {None,Catalog,AudioConfiguration,Lookup,Identity,Exhausted,FilterCreate,Signals,Upstream};
struct Counters {std::atomic<uint64_t> preEntries{},postEntries{},drops{};};
struct Measurement {
 pita::Key key;uint64_t captureGeneration{},sequence{},preFrames{},postFrames{},preMediaNs{},postMediaNs{},preArrivalNs{},postArrivalNs{},capturedThroughNs{},submittedAtNs{},analysisStartedAtNs{},publishedAtNs{},expiresAtNs{};
 uint64_t preClockSpreadNs{},postClockSpreadNs{},matchedCallPairs{},pairedFrames{},pairDeltaSpreadNs{};bool clockConfident{},clockDiscontinuity{},commonClockMatched{};
 double preRms[2]{},postRms[2]{},prePeak[2]{},postPeak[2]{};
 std::shared_ptr<const waveform::Frame> wave;latency::Estimate channels[2];bool valid{},muted{},timelineOk{};
};
inline constexpr uint64_t freshnessNs=750000000ULL; // three 250 ms analysis hops, data-derived
inline bool fresh(const Measurement& m,uint64_t now){return m.capturedThroughNs&&m.capturedThroughNs<=m.publishedAtNs&&m.publishedAtNs<=now&&m.expiresAtNs>=m.capturedThroughNs&&now<m.expiresAtNs;}
struct DisplayWave {std::shared_ptr<const waveform::Frame> envelope;std::shared_ptr<const displayonly::SampleFrame> samples;};
class Binding {
 // Immutable format per binding; product 2-second window with 250 ms hops.
 static bool supportedRate(uint32_t rate){return format::supportedRate(rate);}
 struct Session {
#ifdef PITATUNER_CAPTURE_LIFETIME_QA
  ~Session(){lifetimeqa::point(lifetimeqa::Point::SessionDestroyed,this);}
#endif
  soniclatch::bridge::PacketAck offsetAck;std::atomic<bool> pairedPacketSeen{false};
  pita::Key key;uint32_t sampleRate{};std::array<char,64> uuid{},type{};
#ifdef SONICLATCH_CAPTURE_REPLAY_TEST
  size_t testWindowFrames{};std::vector<Measurement> testHistory;
#endif
  latency::SpscRing<latency::AudioBlock,187> pre,post;CallbackGate gate;
  std::atomic<bool> accepting{},done{},filterAlive{};std::atomic<uint32_t> faults{};
  uint64_t preSequence{},postSequence{},invocationCounter{};std::atomic<uint64_t> lastPreArrival{},lastPostArrival{},lastAnyArrival{},captureGeneration{1};std::atomic<bool> idleObserved{};RuntimeStats runtime;std::thread worker;
  std::shared_ptr<Counters> counters=std::make_shared<Counters>();
  diagnostic::Trace<> trace[2];diagnostic::FirstFailure timelineFailure;std::atomic<uint64_t> timelineFaultAt{},configurationAt{};bool traceLogged{};
  std::mutex latestMutex;std::optional<Measurement> latest;std::shared_ptr<const waveform::Frame> liveWave;std::shared_ptr<const DisplayWave> displayWave;uint64_t liveWavePublications{},liveWaveGeneration{},liveWaveCapturedThroughNs{};
 };
 struct FilterContext {std::shared_ptr<Session> session;};
 static inline thread_local std::shared_ptr<Session> pending_;
 struct Invocation{Session* session;uint64_t id,sessionId,offsetToken;};static inline thread_local Invocation invocation_{};
 static inline std::atomic<uint64_t> liveFilters_{};
 static constexpr const char* filterId="pitatuner_capture_boundary_dev";
 static constexpr const char* filterDisplayName="\xE9\x9F\xB3\xE5\xA3\xB0\xE9\x81\x85\xE5\xBB\xB6\xE6\xB8\xAC\xE5\xAE\x9A\xEF\xBC\x88\x53\x6F\x6E\x69\x63\x4C\x61\x74\x63\x68\xEF\xBC\x89"; // UTF-8 display name; stable internal ID above.
 const Api& api_;SessionIds& ids_;const std::thread::id owner_=std::this_thread::get_id();
 obs_source_t* source_{};obs_source_t* filter_{};signal_handler_t* signals_{};std::shared_ptr<Session> session_;
 struct Watched {obs_source_t* source;signal_handler_t* signals;};std::vector<Watched> watched_;
 bool automaticRestartUsed_{};uint32_t terminalFault_{};
 SelectError selectionError_{SelectError::None};int checkedIndex_=-1;size_t checkedCount_{};
 static uint64_t clockNs(){return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
 void owner()const{if(std::this_thread::get_id()!=owner_)throw std::logic_error("capture owner thread required");}
 static void fault(Session& s,Fault f){s.faults.fetch_or(static_cast<uint32_t>(f),std::memory_order_acq_rel);s.accepting.store(false,std::memory_order_release);}
 static void configuration(void* data,calldata_t*)noexcept{auto& s=*static_cast<Session*>(data);atomicMax(s.configurationAt,clockNs());fault(s,Fault::Configuration);}
 static void muteChanged(void* data,calldata_t*)noexcept{auto& s=*static_cast<Session*>(data);atomicMax(s.configurationAt,clockNs());fault(s,Fault::Muted);}
 static void removed(void* data,calldata_t*)noexcept{fault(*static_cast<Session*>(data),Fault::Removed);}
 static bool observeStreamActivity(Session& s,uint64_t now){const auto pre=s.lastPreArrival.load(std::memory_order_acquire),post=s.lastPostArrival.load(std::memory_order_acquire);const bool active=pairedclock::active(pre,post,now);if(pairedclock::idleElapsed(pre,post,now))s.idleObserved.store(true,std::memory_order_release);return active;}
 static void copy(Session& s,const uint8_t*const* planes,uint32_t frames,uint64_t timestamp,bool before,bool muted=false,uint64_t invocationId=0){
  #ifdef PITATUNER_CAPTURE_TIMING_QA
  timingqa::Scope qa(before,true);timingqa::pending.timestamp=timestamp;
  #endif
  (before?s.counters->preEntries:s.counters->postEntries).fetch_add(1,std::memory_order_relaxed);
  CallbackGate::Lease lease(s.gate);
#ifdef PITATUNER_CAPTURE_LIFETIME_QA
  if(lease.accepted)lifetimeqa::point(before?lifetimeqa::Point::PreLease:lifetimeqa::Point::PostLease,&s);
#endif
  if(!lease.accepted)return;
  const auto traceNow=clockNs();const auto faultAt=s.timelineFaultAt.load(std::memory_order_acquire);
  if(!faultAt||diagnostic::observing(faultAt,traceNow))s.trace[before?0:1].append({timestamp,traceNow,frames,muted});
  if(!s.accepting.load(std::memory_order_acquire)||s.faults.load(std::memory_order_acquire))return;
  if(!packet::valid(planes,frames,timestamp,s.sampleRate)){fault(s,Fault::Shape);return;}
  const auto arrival=clockNs();const auto anyPrevious=s.lastAnyArrival.exchange(arrival,std::memory_order_acq_rel);
  if(s.idleObserved.exchange(false,std::memory_order_acq_rel)||(anyPrevious&&arrival>=anyPrevious&&arrival-anyPrevious>=pairedclock::captureIdleNs))s.captureGeneration.fetch_add(1,std::memory_order_acq_rel);
  const auto captureGeneration=s.captureGeneration.load(std::memory_order_acquire);(before?s.lastPreArrival:s.lastPostArrival).store(arrival,std::memory_order_release);
  auto fill=[&](latency::AudioBlock& b){b.sourceUuid=s.uuid;b.sourceType=s.type;b.sessionId=s.key.session;b.configEpoch=s.key.epoch;b.tap=before?latency::Tap::Pre:latency::Tap::Post;b.channels=2;b.sampleRate=s.sampleRate;b.arrivalMonotonicNs=arrival;b.muted=muted;b.invocationId=invocationId;b.captureGeneration=captureGeneration;};
  const auto result=packet::enqueue(before?s.pre:s.post,planes,frames,timestamp,s.sampleRate,before?s.preSequence:s.postSequence,fill);
  if(result==packet::Result::Overflow){s.counters->drops.fetch_add(1);fault(s,Fault::Overflow);}
  else if(result==packet::Result::Shape)fault(s,Fault::Shape);
 }
 static obs_audio_data* pre(void* data,obs_audio_data* audio){
#ifdef PITATUNER_CAPTURE_TIMING_QA
 timingqa::Scope qa(true,false);
#endif
 auto* context=static_cast<FilterContext*>(data);if(!context||!context->session)return audio;auto& s=*context->session;invocation_={&s,++s.invocationCounter,s.key.session,s.offsetAck.pre()};copy(s,audio->data,audio->frames,audio->timestamp,true,false,invocation_.id);return audio;}
 static void post(void* data,obs_source_t*,const audio_data* audio,bool muted){
#ifdef PITATUNER_CAPTURE_TIMING_QA
 timingqa::Scope qa(false,false);
#endif
 auto& s=*static_cast<Session*>(data);const auto call=invocation_.session==&s&&invocation_.sessionId==s.key.session?invocation_.id:0;const auto offsetToken=invocation_.offsetToken;invocation_={};copy(s,audio->data,audio->frames,audio->timestamp,false,muted,call);if(call&&s.accepting.load(std::memory_order_acquire)&&!s.faults.load(std::memory_order_acquire)&&!muted)s.pairedPacketSeen.store(true,std::memory_order_release);s.offsetAck.post(offsetToken,call!=0,s.accepting.load(std::memory_order_acquire)&&!s.faults.load(std::memory_order_acquire)&&!muted);}
 static const char* name(void*){return filterDisplayName;}
 static void* create(obs_data_t*,obs_source_t*){auto* c=new FilterContext{pending_};if(c->session){c->session->filterAlive.store(true);++liveFilters_;}return c;}
 static void destroy(void* data){auto* c=static_cast<FilterContext*>(data);if(!c)return;if(c->session){c->session->filterAlive.store(false);fault(*c->session,Fault::Removed);--liveFilters_;}delete c;}
 static void analyze(const std::shared_ptr<Session>& s)noexcept{try{
#ifdef PITATUNER_CAPTURE_LIFETIME_QA
  lifetimeqa::WorkerScope qaWorker(s.get());
#endif
  const uint32_t rate=s->sampleRate;
#ifdef SONICLATCH_CAPTURE_REPLAY_TEST
  const size_t windowFrames=s->testWindowFrames?s->testWindowFrames:sliding::frames(rate);
  const size_t hopFrames=s->testWindowFrames?s->testWindowFrames:sliding::hop(rate);
#else
  const size_t windowFrames=sliding::frames(rate),hopFrames=sliding::hop(rate);
#endif
  struct Job{std::array<std::vector<float>,2> a,b;Measurement measurement;bool drawable{};};
  LatestAnalysisLane<Job> lane(s->runtime,[s,rate](Job& job){
    auto& m=job.measurement;m.analysisStartedAtNs=clockNs();if(m.captureGeneration!=s->captureGeneration.load(std::memory_order_acquire)||s->faults.load())return;
    auto& a=job.a;auto& b=job.b;const uint64_t firstMedia[2]{m.preMediaNs,m.postMediaNs};const bool muted=m.muted;
    for(size_t side=0;side<2;side++)for(size_t ch=0;ch<2;ch++){const auto& values=side?b[ch]:a[ch];double energy=0,peak=0;bool finite=true;for(float value:values){if(!std::isfinite(value)){finite=false;continue;}energy+=double(value)*value;peak=std::max(peak,std::abs(double(value)));}const auto rms=finite?std::sqrt(energy/values.size()):std::numeric_limits<double>::quiet_NaN();(side?m.postRms:m.preRms)[ch]=rms;(side?m.postPeak:m.prePeak)[ch]=finite?peak:std::numeric_limits<double>::quiet_NaN();}
    for(size_t ch=0;ch<2;ch++)m.channels[ch]=latency::estimate({a[ch],static_cast<int64_t>(firstMedia[0]),rate,s->key.epoch,!m.timelineOk},{b[ch],static_cast<int64_t>(firstMedia[1]),rate,s->key.epoch,!m.timelineOk});
    m.valid=m.timelineOk&&!muted&&m.channels[0].valid&&m.channels[1].valid&&std::abs(m.channels[0].delayMs-m.channels[1].delayMs)<=10;
    if(job.drawable){
     const auto origin=std::min(firstMedia[0],firstMedia[1]);
     waveform::Track tracks[2];bool mapped=true;
     for(size_t t=0;t<2;t++){
      auto& track=tracks[t];track.key={s->key.uuid,s->key.session,s->key.epoch};track.rate=rate;track.channel=0;track.origin_ns=origin;
      const auto delta=firstMedia[t]-origin;const auto start=format::nearestFrame(delta,rate);
      const auto mappedDelta=format::frameNs(start,rate);
      if(!timeline::sampleProjection(delta,mappedDelta,rate)){mapped=false;break;}
      const auto& values=t?b[0]:a[0];track.points.reserve(values.size());
      for(size_t i=0;i<values.size();i++){const auto index=start+i;track.points.push_back({index,origin+format::frameNs(index,rate),values[i],true});}
     }
     if(mapped){const auto end=std::max(tracks[0].points.back().timestamp_ns,tracks[1].points.back().timestamp_ns)+format::sampleCeilNs(rate);
      auto frame=waveform::build({{origin,end,800},std::move(tracks[0]),std::move(tracks[1]),0,0,{}});
      if(frame)m.wave=std::make_shared<const waveform::Frame>(std::move(frame));}
    }
    m.publishedAtNs=clockNs();
    if(m.captureGeneration!=s->captureGeneration.load(std::memory_order_acquire)||s->faults.load())return;
#ifdef SONICLATCH_CAPTURE_REPLAY_TEST
    s->testHistory.push_back(m);
#endif
    {std::lock_guard guard(s->latestMutex);s->latest=std::move(m);}
  },[s]{fault(*s,Fault::Analysis);});
  sliding::Tap taps[2]{{windowFrames,rate},{windowFrames,rate}};auto live=std::make_unique<livewave::Window>(120,5000);
  latency::AudioBlock blocks[2];uint32_t consumed[2]{},remaining[2]{};uint64_t nextSequence[2]{},generation=s->captureGeneration.load(),estimateSequence=0,lastWaveAt=0;
  bool gap[2]{};
  auto reset=[&](uint64_t next){generation=next;for(size_t t=0;t<2;t++){taps[t].clear();live->clear(t);remaining[t]=0;gap[t]=false;}std::lock_guard guard(s->latestMutex);s->latest.reset();s->liveWave.reset();s->displayWave.reset();};
  auto consume=[&](size_t t){
   if(taps[t].full())return false;
   const auto current=s->captureGeneration.load(std::memory_order_acquire);if(current!=generation)reset(current);
   auto& block=blocks[t];if(!remaining[t]){
    if(!(t==0?s->pre.pop(block):s->post.pop(block)))return false;
    gap[t]=block.sequence!=nextSequence[t];nextSequence[t]=block.sequence+1;
    if(block.captureGeneration!=generation)return true;
    consumed[t]=0;remaining[t]=block.frames;
   }
   const auto count=std::min<size_t>(remaining[t],windowFrames-taps[t].size());
   const auto timestamp=packet::advance(block.mediaTimestampNs,block.invocationFrameOffset,consumed[t],rate);
   if(gap[t]||block.muted)live->clear(t);if(!block.muted)live->add(t,timestamp,block.pcm[0].data()+consumed[t],count,rate);
   taps[t].append(block,consumed[t],count,gap[t]);gap[t]=false;
   const auto now=clockNs();if(now>=block.arrivalMonotonicNs)atomicMax(s->runtime.ingestMaxDwellNs,now-block.arrivalMonotonicNs);
   consumed[t]+=static_cast<uint32_t>(count);remaining[t]-=static_cast<uint32_t>(count);return true;
  };
  for(;;){bool progress=false;if(consume(0))progress=true;if(consume(1))progress=true;
   if(live->ready(rate)){
    auto frame=std::make_shared<const waveform::Frame>(live->publish(rate));auto display=std::make_shared<const DisplayWave>(DisplayWave{frame,live->sampleSnapshot(frame->axis)});const auto at=clockNs();if(lastWaveAt)atomicMax(s->runtime.waveIntervalMaxNs,at-lastWaveAt);lastWaveAt=at;s->runtime.wavePublished.fetch_add(1);
    std::lock_guard guard(s->latestMutex);s->liveWave=std::move(frame);s->displayWave=std::move(display);s->liveWaveGeneration=generation;s->liveWaveCapturedThroughNs=std::min(taps[0].lastArrival(),taps[1].lastArrival());++s->liveWavePublications;
   }
   if(s->faults.load(std::memory_order_acquire))return;
   if(taps[0].full()&&taps[1].full()){
    windowclock::Window clocks[2];pairedclock::Window pairs;taps[0].observe(0,clocks[0],pairs);taps[1].observe(1,clocks[1],pairs);
    const auto preClock=clocks[0].finish(),postClock=clocks[1].finish();uint64_t firstMedia[2]{preClock.origin,postClock.origin};const auto pair=pairs.finish(rate);
    const bool pairedOriginSafe=pair.matched&&(pair.windowDeltaNs>=0?uint64_t(pair.windowDeltaNs)<=UINT64_MAX-firstMedia[0]:uint64_t(-pair.windowDeltaNs)<=firstMedia[0]);
    if(pairedOriginSafe)firstMedia[1]=pair.windowDeltaNs>=0?firstMedia[0]+uint64_t(pair.windowDeltaNs):firstMedia[0]-uint64_t(-pair.windowDeltaNs);
    Job job;auto& m=job.measurement;m.captureGeneration=generation;m.preClockSpreadNs=preClock.spread;m.postClockSpreadNs=postClock.spread;m.commonClockMatched=pairedOriginSafe;m.matchedCallPairs=pair.pairs;m.pairedFrames=pair.matchedFrames;m.pairDeltaSpreadNs=pair.deltaSpreadNs;
    m.clockConfident=pairedOriginSafe||(!pair.discontinuity&&preClock.confident&&postClock.confident);m.clockDiscontinuity=pair.discontinuity||(!pairedOriginSafe&&(preClock.discontinuity||postClock.discontinuity));m.key=s->key;m.sequence=++estimateSequence;m.preFrames=taps[0].total;m.postFrames=taps[1].total;m.preMediaNs=firstMedia[0];m.postMediaNs=firstMedia[1];m.preArrivalNs=taps[0].firstArrival();m.postArrivalNs=taps[1].firstArrival();m.capturedThroughNs=std::min(taps[0].lastArrival(),taps[1].lastArrival());m.expiresAtNs=m.capturedThroughNs<=UINT64_MAX-freshnessNs?m.capturedThroughNs+freshnessNs:0;m.muted=taps[0].muted()||taps[1].muted();m.timelineOk=m.clockConfident&&!taps[0].gap()&&!taps[1].gap()&&s->counters->drops.load()==0;
    job.drawable=!m.muted&&!m.clockDiscontinuity&&!taps[0].gap()&&!taps[1].gap()&&s->counters->drops.load()==0;job.a=taps[0].pcm;job.b=taps[1].pcm;m.submittedAtNs=clockNs();lane.submit(std::move(job));taps[0].discard(hopFrames);taps[1].discard(hopFrames);
   }
   if(!progress&&s->done.load(std::memory_order_acquire))break;
   if(!progress)std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
 }catch(...){fault(*s,Fault::Analysis);}}

public:
#ifdef SONICLATCH_CAPTURE_REPLAY_TEST
 struct ConfigurationSignalProbe{uint32_t mask{};uint64_t at{};bool accepting{};};
 static ConfigurationSignalProbe probeConfigurationSignal(){auto s=std::make_unique<Session>();s->accepting=true;configuration(s.get(),nullptr);return{s->faults.load(),s->configurationAt.load(),s->accepting.load()};}
 struct ReplayPacket{uint64_t timestamp;uint32_t frames;uint32_t pauseMs{};bool historicalActivityQuery{};uint32_t postFrames{};uint64_t postTimestamp{};bool nonfinite{};};
 struct ReplayResult{std::vector<Measurement> windows;uint32_t faults;uint64_t drops;std::shared_ptr<const waveform::Frame> liveWave;std::shared_ptr<const DisplayWave> displayWave;uint64_t liveWavePublications{},liveWaveGeneration{},liveWaveCapturedThroughNs{},analysisSubmitted{},analysisReplaced{},analysisCompleted{},analysisComputeMaxNs{},analysisQueueMaxNs{},ingestMaxDwellNs{},activityRaceQueries{},falseIdleMarks{};};
 // Test-only access to the exact production copy -> ring -> analyze path.
 // The shorter window is explicit; normal production uses the 2-second/250-ms policy.
 static ReplayResult replay(const std::vector<ReplayPacket>& packets,uint64_t delayNs,size_t windowFrames,bool silentPost=false,bool sequenceGap=false,uint32_t sampleRate=48000){
  auto s=std::make_shared<Session>();s->sampleRate=sampleRate;s->key={"synthetic-replay",1,1};s->testWindowFrames=windowFrames;s->accepting=true;s->worker=std::thread(analyze,s);uint64_t cursor[2]{},raceQueries=0,falseIdleMarks=0;
  std::vector<float> samples,postSamples,zero;
  for(size_t p=0;p<packets.size();p++){
   const auto& packet=packets[p];if(packet.pauseMs)std::this_thread::sleep_for(std::chrono::milliseconds(packet.pauseMs));
   const auto postFrames=packet.postFrames?packet.postFrames:packet.frames;samples.resize(packet.frames);postSamples.resize(postFrames);zero.assign(postFrames,0.f);
   for(size_t side=0;side<2;side++){auto& values=side?postSamples:samples;for(size_t i=0;i<values.size();i++){uint32_t v=uint32_t(cursor[side]+i)+12345;v=(v^(v>>16))*0x7feb352dU;v=(v^(v>>15))*0x846ca68bU;v^=v>>16;values[i]=float(int(v%20001)-10000)/10000.f;}}
   if(packet.nonfinite&&!samples.empty())samples.back()=std::numeric_limits<float>::quiet_NaN();
   const uint8_t* prePlanes[2]{reinterpret_cast<const uint8_t*>(samples.data()),reinterpret_cast<const uint8_t*>(samples.data())};const uint8_t* postPlanes[2]{reinterpret_cast<const uint8_t*>(silentPost?zero.data():postSamples.data()),reinterpret_cast<const uint8_t*>(silentPost?zero.data():postSamples.data())};
   if(sequenceGap&&p==packets.size()/2)++s->preSequence;FilterContext context{s};obs_audio_data before{};before.frames=packet.frames;before.timestamp=packet.timestamp;for(size_t ch=0;ch<2;ch++)before.data[ch]=const_cast<uint8_t*>(prePlanes[ch]);pre(&context,&before);
   audio_data after{};after.frames=postFrames;after.timestamp=packet.postTimestamp?packet.postTimestamp:packet.timestamp+delayNs;for(size_t ch=0;ch<2;ch++)after.data[ch]=const_cast<uint8_t*>(postPlanes[ch]);post(s.get(),nullptr,&after,false);
   if(packet.historicalActivityQuery){const auto sampledBeforeArrival=std::min(s->lastPreArrival.load(),s->lastPostArrival.load())-1;observeStreamActivity(*s,sampledBeforeArrival);++raceQueries;if(s->idleObserved.load())++falseIdleMarks;}
   cursor[0]+=packet.frames;cursor[1]+=postFrames;
  }
  s->done=true;s->worker.join();return{std::move(s->testHistory),s->faults.load(),s->counters->drops.load(),s->liveWave,s->displayWave,s->liveWavePublications,s->liveWaveGeneration,s->liveWaveCapturedThroughNs,s->runtime.submitted.load(),s->runtime.replaced.load(),s->runtime.completed.load(),s->runtime.computeMaxNs.load(),s->runtime.queueMaxNs.load(),s->runtime.ingestMaxDwellNs.load(),raceQueries,falseIdleMarks};
 }
#endif
 // Owner startup or collection activation, while unselected. Persisted boundaries are inert
 // on load. Remove our exact internal type; leave processing filters and offset.
 static size_t removeSavedBoundaries(const Api& api){
  size_t removed=0;
  for(const auto& entry:catalog(api)){
   auto* source=api.obs_get_source_by_uuid(entry.uuid.c_str());if(!source)continue;
   struct Saved {const Api* api;std::vector<obs_source_t*> filters;};Saved saved{&api,{}};
   api.obs_source_enum_filters(source,[](obs_source_t*,obs_source_t* filter,void* data){auto& saved=*static_cast<Saved*>(data);const auto* id=saved.api->obs_source_get_id(filter);if(id&&!std::strcmp(id,filterId)){if(auto* held=saved.api->obs_source_get_ref(filter))saved.filters.push_back(held);}},&saved);
   for(auto* filter:saved.filters){api.obs_source_filter_remove(source,filter);api.obs_source_release(filter);++removed;}
   api.obs_source_release(source);
  }
  return removed;
 }
 static uint64_t liveFilters(){return liveFilters_.load();}
 static bool configurationRecoveryReady(uint32_t reason,bool alive,bool enabled,bool muted,uint64_t lastEvent,uint64_t now){return recovery::ready(reason,alive,enabled,muted,lastEvent,now);}
 enum class Recovery {None,Restarted,Stopped};
 bool automaticRestartUsed()const{owner();return automaticRestartUsed_;}
 // Owner-only; existing detach/quiesce/join/select, never an offset write.
 Recovery pollRecovery(){owner();if(!session_)return Recovery::None;const auto reason=faults();const auto timelineMask=static_cast<uint32_t>(Fault::Timeline);if(recovery::refreshable(reason)){
   if(!configurationRecoveryReady(reason,!api_.obs_source_removed(source_),api_.obs_source_enabled(source_),api_.obs_source_muted(source_),session_->configurationAt.load(std::memory_order_acquire),clockNs()))return Recovery::None;
   const auto uuid=session_->key.uuid;stop();if(select(uuid))return Recovery::Restarted;terminalFault_=reason;return Recovery::Stopped;
  }
  if(!(reason&timelineMask))return Recovery::None;
  const auto faultAt=session_->timelineFaultAt.load(std::memory_order_acquire);if(!diagnostic::recoveryReady(faultAt,clockNs(),session_->traceLogged))return Recovery::None;
  const auto uuid=session_->key.uuid;const bool permitted=!automaticRestartUsed_&&reason==timelineMask;stop();terminalFault_=reason;
  if(!permitted)return Recovery::Stopped;
  const bool selected=select(uuid);automaticRestartUsed_=true;
  if(!selected){terminalFault_=reason;return Recovery::Stopped;}return Recovery::Restarted;
 }

 bool hasPairedPacket()const{owner();return session_&&session_->pairedPacketSeen.load(std::memory_order_acquire)&&!faults();}
 uint64_t armOffsetBridge(){owner();return session_?session_->offsetAck.arm():0;}
 bool offsetBridgeAcknowledged(uint64_t token)const{owner();return session_&&session_->offsetAck.complete(token)&&!faults();}
 void cancelOffsetBridge(){owner();if(session_)session_->offsetAck.cancel();}
 SelectError selectError()const{owner();return selectionError_;}
 int checkedIndex()const{owner();return checkedIndex_;}
 size_t checkedCount()const{owner();return checkedCount_;}
 static void registerFilter(const Api& api){obs_source_info i{};i.id=filterId;i.type=OBS_SOURCE_TYPE_FILTER;i.output_flags=OBS_SOURCE_AUDIO;i.get_name=name;i.create=create;i.destroy=destroy;i.filter_audio=pre;api.obs_register_source_s(&i,sizeof(i));}
 Binding(const Api& api,SessionIds& ids):api_(api),ids_(ids){}
 Binding(const Binding&)=delete;Binding& operator=(const Binding&)=delete;
 ~Binding(){stop();}
 void stop(){owner();terminalFault_=0;if(!session_){if(filter_)api_.obs_source_release(filter_);if(source_)api_.obs_source_release(source_);filter_=source_=nullptr;return;}auto keep=session_;keep->accepting.store(false);keep->gate.close();
#ifdef PITATUNER_CAPTURE_LIFETIME_QA
  lifetimeqa::point(lifetimeqa::Point::GateClosed,keep.get());
#endif
  for(const auto& watched:watched_){for(const auto* event:{"update","enable","remove","destroy"})api_.signal_handler_disconnect(watched.signals,event,configuration,keep.get());api_.obs_source_release(watched.source);}watched_.clear();
  if(signals_){for(const auto* event:{"update","enable","volume","filter_add","filter_remove","reorder_filters"})api_.signal_handler_disconnect(signals_,event,configuration,keep.get());api_.signal_handler_disconnect(signals_,"mute",muteChanged,keep.get());for(const auto* event:{"remove","destroy"})api_.signal_handler_disconnect(signals_,event,removed,keep.get());signals_=nullptr;}
  if(source_){api_.obs_source_remove_audio_capture_callback(source_,post,keep.get());if(filter_)api_.obs_source_filter_remove(source_,filter_);}
  while(!keep->gate.quiet())std::this_thread::yield();keep->done.store(true,std::memory_order_release);
#ifdef PITATUNER_CAPTURE_LIFETIME_QA
  lifetimeqa::point(lifetimeqa::Point::DoneSet,keep.get());
#endif
  if(keep->worker.joinable())keep->worker.join();
#ifdef PITATUNER_CAPTURE_LIFETIME_QA
  lifetimeqa::point(lifetimeqa::Point::WorkerJoined,keep.get());
#endif
  if(filter_){api_.obs_source_release(filter_);filter_=nullptr;}if(source_){api_.obs_source_release(source_);source_=nullptr;}session_.reset();
 }
 bool select(const std::string& uuid){owner();stop();automaticRestartUsed_=false;selectionError_=SelectError::None;
  const auto entries=catalog(api_);if(std::none_of(entries.begin(),entries.end(),[&](const auto& e){return e.uuid==uuid;})){selectionError_=SelectError::Catalog;return false;}
  obs_audio_info info{};if(!api_.obs_get_audio_info(&info)||!supportedRate(info.samples_per_sec)||info.speakers!=SPEAKERS_STEREO){selectionError_=SelectError::AudioConfiguration;return false;}
  source_=api_.obs_get_source_by_uuid(uuid.c_str());if(!source_){selectionError_=SelectError::Lookup;return false;}
  const auto* actual=api_.obs_source_get_uuid(source_);if(!actual||uuid!=actual||api_.obs_source_removed(source_)||!api_.obs_source_enabled(source_)){api_.obs_source_release(source_);source_=nullptr;selectionError_=SelectError::Identity;return false;}
  const auto id=ids_.claim();if(!id){api_.obs_source_release(source_);source_=nullptr;selectionError_=SelectError::Exhausted;return false;}
  session_=std::make_shared<Session>();session_->sampleRate=info.samples_per_sec;session_->key={uuid,*id,1};
  if(uuid.size()>=session_->uuid.size()){stop();return false;}std::memcpy(session_->uuid.data(),uuid.data(),uuid.size());
  const auto* type=api_.obs_source_get_id(source_);if(type){const auto n=std::min(std::strlen(type),session_->type.size()-1);std::memcpy(session_->type.data(),type,n);}
  pending_=session_;filter_=api_.obs_source_create_private(filterId,filterDisplayName,nullptr);pending_.reset();if(!filter_){stop();selectionError_=SelectError::FilterCreate;return false;}
  // OBS visual TOP is the last array entry; filter_audio executes in reverse.
  // Move only our measurement filter; other filters retain relative order.
  api_.obs_source_filter_add(source_,filter_);api_.obs_source_filter_set_order(source_,filter_,OBS_ORDER_MOVE_TOP);
  signals_=api_.obs_source_get_signal_handler(source_);if(!signals_){stop();selectionError_=SelectError::Signals;return false;}
  for(const auto* event:{"update","enable","volume","filter_add","filter_remove","reorder_filters"})api_.signal_handler_connect(signals_,event,configuration,session_.get());api_.signal_handler_connect(signals_,"mute",muteChanged,session_.get());for(const auto* event:{"remove","destroy"})api_.signal_handler_connect(signals_,event,removed,session_.get());
  const auto count=api_.obs_source_filter_count(source_);checkedCount_=count;checkedIndex_=api_.obs_source_filter_get_index(source_,filter_);if(!count||checkedIndex_!=static_cast<int>(count-1)){stop();selectionError_=SelectError::Upstream;return false;}
  api_.obs_source_enum_filters(source_,[](obs_source_t*,obs_source_t* filter,void* data){auto& self=*static_cast<Binding*>(data);auto* held=self.api_.obs_source_get_ref(filter);auto* signals=self.api_.obs_source_get_signal_handler(filter);if(!held||!signals){if(held)self.api_.obs_source_release(held);fault(*self.session_,Fault::Configuration);return;}self.watched_.push_back({held,signals});for(const auto* event:{"update","enable","remove","destroy"})self.api_.signal_handler_connect(signals,event,configuration,self.session_.get());},this);
  api_.obs_source_add_audio_capture_callback(source_,post,session_.get());session_->worker=std::thread(analyze,session_);session_->accepting.store(!session_->faults.load());return true;
 }
 pita::Key key()const{owner();return session_?session_->key:pita::Key{};}
 obs_source_t* selectedBorrowedForOwner()const{owner();return source_;}
 uint32_t faults()const{owner();if(!session_)return terminalFault_;if(api_.obs_source_removed(source_))fault(*session_,Fault::Removed);if(!api_.obs_source_enabled(source_))fault(*session_,Fault::Configuration);obs_audio_info i{};if(!api_.obs_get_audio_info(&i)||i.samples_per_sec!=session_->sampleRate||i.speakers!=SPEAKERS_STEREO)fault(*session_,Fault::AudioConfiguration);return session_->faults.load();}
 // Owner logs bounded metadata before recovery destroys the failed session.
 std::optional<diagnostic::Failure> timelineFailure()const{owner();return session_?session_->timelineFailure.read():std::nullopt;}
 bool takeTimelineTrace(std::array<std::vector<diagnostic::Packet>,2>& result){owner();if(!session_||session_->traceLogged)return false;const auto at=session_->timelineFaultAt.load(std::memory_order_acquire);if(!at||diagnostic::observing(at,clockNs()))return false;session_->traceLogged=true;
  for(size_t side=0;side<2;side++){const auto n=session_->trace[side].size();result[side].reserve(n);for(size_t i=0;i<n;i++)result[side].push_back(session_->trace[side].at(i));}return true;}
 std::shared_ptr<const Counters> counters()const{owner();return session_?session_->counters:nullptr;}
 bool streamActiveAt(uint64_t now)const{owner();return session_&&observeStreamActivity(*session_,now);}
 bool streamActive()const{return streamActiveAt(clockNs());}
 struct RuntimeView{uint64_t submitted{},replaced{},completed{},computeTotalNs{},computeMaxNs{},queueMaxNs{},ingestMaxDwellNs{},wavePublished{},waveIntervalMaxNs{},drops{};};
 RuntimeView runtime()const{owner();if(!session_)return{};auto& r=session_->runtime;return{r.submitted.load(),r.replaced.load(),r.completed.load(),r.computeTotalNs.load(),r.computeMaxNs.load(),r.queueMaxNs.load(),r.ingestMaxDwellNs.load(),r.wavePublished.load(),r.waveIntervalMaxNs.load(),session_->counters->drops.load()};}

 // UI reads sample now AFTER taking the arrival/frame snapshot. This avoids
 // falsely clearing a fresh graph when a callback overtakes renderedAt.
 std::shared_ptr<const DisplayWave> waveForDisplay()const{owner();if(!session_||faults()||api_.obs_source_muted(source_))return{};auto& s=*session_;const auto pre=s.lastPreArrival.load(std::memory_order_acquire),post=s.lastPostArrival.load(std::memory_order_acquire);std::lock_guard guard(s.latestMutex);const auto generation=s.captureGeneration.load(std::memory_order_acquire),now=clockNs();if(pairedclock::idleElapsed(pre,post,now))s.idleObserved.store(true,std::memory_order_release);if(!pairedclock::active(pre,post,now)||s.liveWaveGeneration!=generation||!s.liveWaveCapturedThroughNs||now<s.liveWaveCapturedThroughNs||now-s.liveWaveCapturedThroughNs>=pairedclock::captureIdleNs||generation!=s.captureGeneration.load(std::memory_order_acquire))return{};return s.displayWave;}
 std::optional<Measurement> measurementForDisplay()const{owner();if(!session_||faults())return{};auto& s=*session_;const auto pre=s.lastPreArrival.load(std::memory_order_acquire),post=s.lastPostArrival.load(std::memory_order_acquire);std::lock_guard guard(s.latestMutex);auto value=s.latest;const auto generation=s.captureGeneration.load(std::memory_order_acquire),now=clockNs();if(pairedclock::idleElapsed(pre,post,now))s.idleObserved.store(true,std::memory_order_release);if(!pairedclock::active(pre,post,now)||!value||value->captureGeneration!=generation||!fresh(*value,now)||generation!=s.captureGeneration.load(std::memory_order_acquire))return{};if(api_.obs_source_muted(source_)){value->muted=true;value->valid=false;}return value;}
 std::shared_ptr<const waveform::Frame> latestWaveAt(uint64_t now)const{owner();if(!session_||faults()||!streamActiveAt(now)||api_.obs_source_muted(source_))return{};std::lock_guard guard(session_->latestMutex);if(session_->liveWaveGeneration!=session_->captureGeneration.load(std::memory_order_acquire)||!session_->liveWaveCapturedThroughNs||now<session_->liveWaveCapturedThroughNs||now-session_->liveWaveCapturedThroughNs>=pairedclock::captureIdleNs)return{};return session_->liveWave;}
 std::optional<Measurement> latest()const{return latestAt(clockNs());}
 std::optional<Measurement> latestAt(uint64_t now)const{owner();if(!session_||faults()||!streamActiveAt(now))return{};std::lock_guard guard(session_->latestMutex);auto value=session_->latest;if(value&&(value->captureGeneration!=session_->captureGeneration.load(std::memory_order_acquire)||!fresh(*value,now)))return{};if(value&&api_.obs_source_muted(source_)){value->muted=true;value->valid=false;}return value;}
};
} // namespace pitatuner::capture
