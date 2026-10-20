#pragma once
#include "correction_control.hpp"
#include "zero_offset_bridge.hpp"
#include <functional>
#include <algorithm>
#include <chrono>
#include <windows.h>
#include <obs.h>
#include <atomic>
#include <limits>
#include <stdexcept>

namespace pitatuner {
struct ObsOffsetApi {
#define OBS_OFFSET_API(n) decltype(&n) n=nullptr
 OBS_OFFSET_API(obs_source_get_ref);OBS_OFFSET_API(obs_source_release);
 OBS_OFFSET_API(obs_source_get_uuid);OBS_OFFSET_API(obs_source_removed);
 OBS_OFFSET_API(obs_source_get_signal_handler);OBS_OFFSET_API(obs_source_get_sync_offset);OBS_OFFSET_API(obs_source_set_sync_offset);
 OBS_OFFSET_API(signal_handler_connect);OBS_OFFSET_API(signal_handler_disconnect);
 OBS_OFFSET_API(calldata_get_data);
#undef OBS_OFFSET_API
 explicit ObsOffsetApi(HMODULE dll){
#define OBS_OFFSET_LOAD(n) n=reinterpret_cast<decltype(n)>(GetProcAddress(dll,#n));if(!n)throw std::runtime_error(#n)
  OBS_OFFSET_LOAD(obs_source_get_ref);OBS_OFFSET_LOAD(obs_source_release);OBS_OFFSET_LOAD(obs_source_get_uuid);OBS_OFFSET_LOAD(obs_source_removed);
  OBS_OFFSET_LOAD(obs_source_get_signal_handler);OBS_OFFSET_LOAD(obs_source_get_sync_offset);OBS_OFFSET_LOAD(obs_source_set_sync_offset);
  OBS_OFFSET_LOAD(signal_handler_connect);OBS_OFFSET_LOAD(signal_handler_disconnect);OBS_OFFSET_LOAD(calldata_get_data);
#undef OBS_OFFSET_LOAD
 }
};
// The OBS setter API has no origin ID or compare-and-swap. TLS can identify
// an expected synchronous notification candidate, never prove exclusive origin.
class ObsOffsetBackend final : public pita::Backend {
 const ObsOffsetApi& api_;
 obs_source_t* source_=nullptr;
 signal_handler_t* signals_=nullptr;
 const std::thread::id owner_=std::this_thread::get_id();
 pita::Key key_;
 std::atomic<std::uint64_t> revision_{0},notifications_{0};
 std::atomic<bool> invalidated_{false},dirty_{false},closed_{false};
 std::uint64_t ownCandidates_=0;
 pita::Ns intended_=0;std::uint64_t observedRevision_=0;std::optional<pita::Ns> observedValue_;
 static inline thread_local ObsOffsetBackend* activeWriter_=nullptr;
 bool onOwner()const{return std::this_thread::get_id()==owner_;}
 void external()noexcept{
  dirty_.store(true,std::memory_order_release);
  if(revision_.fetch_add(1,std::memory_order_acq_rel)==std::numeric_limits<std::uint64_t>::max())invalidated_.store(true,std::memory_order_release);
 }
 static void sync(void* context,calldata_t* data)noexcept{
  auto& self=*static_cast<ObsOffsetBackend*>(context);
  self.notifications_.fetch_add(1,std::memory_order_relaxed);
  obs_source_t* source=nullptr;pita::Ns offset=0;
  const bool parsed=self.api_.calldata_get_data(data,"source",&source,sizeof source)&&self.api_.calldata_get_data(data,"offset",&offset,sizeof offset);
  if(!self.closed_.load(std::memory_order_acquire)&&parsed&&source==self.source_&&activeWriter_==&self&&self.onOwner()&&offset==self.intended_&&self.ownCandidates_==0){++self.ownCandidates_;return;}
  self.external();
 }
 static void removed(void* context,calldata_t*)noexcept{
  auto& self=*static_cast<ObsOffsetBackend*>(context);self.invalidated_.store(true,std::memory_order_release);self.external();
 }
 bool available()const{return onOwner()&&source_&&!closed_.load(std::memory_order_acquire)&&!invalidated_.load(std::memory_order_acquire)&&!api_.obs_source_removed(source_);}
public:
 enum class WriteObservation { None, ExpectedSynchronousCandidate, Uncertain, Unavailable };
private:
 WriteObservation last_=WriteObservation::None;
public:
 ObsOffsetBackend(const ObsOffsetApi& api,obs_source_t* source,std::uint64_t session,std::uint64_t epoch):api_(api){
  source_=source?api_.obs_source_get_ref(source):nullptr;
  if(!source_)throw std::runtime_error("source reference unavailable");
  const auto* uuid=api_.obs_source_get_uuid(source_);signals_=api_.obs_source_get_signal_handler(source_);
  if(!uuid||!signals_){api_.obs_source_release(source_);source_=nullptr;throw std::runtime_error("source identity unavailable");}
  key_={uuid,session,epoch};
  api_.signal_handler_connect(signals_,"audio_sync",sync,this);
  api_.signal_handler_connect(signals_,"remove",removed,this);
  api_.signal_handler_connect(signals_,"destroy",removed,this);
 }
 ObsOffsetBackend(const ObsOffsetBackend&)=delete;
 ObsOffsetBackend& operator=(const ObsOffsetBackend&)=delete;
 ~ObsOffsetBackend()override{close();}
 pita::Key key()const{return key_;}
 std::optional<pita::Ns> read()override{
  const auto before=externalRevision();if(!available())return {};
  const auto actual=api_.obs_source_get_sync_offset(source_);if(!available()||before!=externalRevision())return {};observedRevision_=before;observedValue_=actual;return actual;
 }
 std::uint64_t externalRevision()const override{return revision_.load(std::memory_order_acquire);}
 bool consumeExternalChange(){if(!onOwner())return false;return dirty_.exchange(false,std::memory_order_acq_rel);}
 std::uint64_t notifications()const{return notifications_.load(std::memory_order_acquire);}
 WriteObservation lastWriteObservation()const{return last_;}
 void writeSingle(pita::Ns target){
  if(!onOwner()){external();return;}if(!available()||activeWriter_||!observedValue_||externalRevision()!=observedRevision_||api_.obs_source_get_sync_offset(source_)!=*observedValue_){last_=WriteObservation::Unavailable;external();return;}
  const auto before=externalRevision();ownCandidates_=0;intended_=target;
  activeWriter_=this;api_.obs_source_set_sync_offset(source_,target);activeWriter_=nullptr;
  const auto actual=read();
  if(!actual||*actual!=target||ownCandidates_!=1||externalRevision()!=before){last_=WriteObservation::Uncertain;external();}
  else last_=WriteObservation::ExpectedSynchronousCandidate;
 }
 struct ZeroBridgeHooks {
  std::function<bool()> valid;
  std::function<bool(pita::Ns)> permits;
  std::function<uint64_t()> arm;
  std::function<bool(uint64_t)> acknowledged;
  std::function<void()> cancel;
  std::function<void(const char*,pita::Ns,pita::Ns,uint64_t)> report;
 };
private:
 std::optional<ZeroBridgeHooks> bridge_;
 pita::Ns bridgeFreshUntil_=INT64_MAX;
 static pita::Ns bridgeNow(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
public:
 void zeroBridge(ZeroBridgeHooks hooks){bridge_=std::move(hooks);}
 void bridgeFreshUntil(pita::Ns until){bridgeFreshUntil_=until;}
 struct PendingBridge {pita::Ns target,intermediate,started,deadline;uint64_t revision,token;};
 std::optional<PendingBridge> pending_;
 bool writePending()const override{return pending_.has_value();}
 void cancelWrite()override{
  if(!pending_)return;
  const auto p=*pending_;pending_.reset();auto& h=*bridge_;h.cancel();
  const auto current=read();const bool own=available()&&externalRevision()==p.revision&&current&&*current==p.intermediate;
  if(own&&h.valid()){writeSingle(0);h.report(last_==WriteObservation::ExpectedSynchronousCandidate?"rollback":"rollback_failed",p.target,p.intermediate,bridgeNow()-p.started);}
  else h.report("cancel_external_or_invalid",p.target,p.intermediate,bridgeNow()-p.started);
  last_=WriteObservation::Uncertain;external();
 }
 void pollWrite(pita::Ns now)override{
  if(!pending_)return;
  const auto p=*pending_;auto& h=*bridge_;
  const auto current=read();const bool own=available()&&externalRevision()==p.revision&&current&&*current==p.intermediate;
  if(!own||!h.valid()||now>=p.deadline){cancelWrite();return;}
  if(!h.acknowledged(p.token))return;
  h.cancel();pending_.reset();h.report("ack",p.target,p.intermediate,p.token);
  writeSingle(p.target);h.report(last_==WriteObservation::ExpectedSynchronousCandidate?"committed":"final_uncertain",p.target,p.intermediate,bridgeNow()-p.started);
 }
 void write(pita::Ns target)override{
  if(pending_){last_=WriteObservation::Unavailable;return;}
  if(!bridge_||!observedValue_||*observedValue_!=0||target==0){writeSingle(target);return;}
  auto& h=*bridge_;const auto intermediate=soniclatch::bridge::intermediate(target,h.permits);
  const auto revision=externalRevision();const auto started=bridgeNow();
  if(!intermediate||!available()||!h.valid()||started>=bridgeFreshUntil_){last_=WriteObservation::Unavailable;external();return;}
  const auto deadline=std::min<pita::Ns>(started+250000000,bridgeFreshUntil_);
  writeSingle(*intermediate);if(last_!=WriteObservation::ExpectedSynchronousCandidate){h.report("first_uncertain",target,*intermediate,bridgeNow()-started);return;}
  const auto token=h.arm();pending_=PendingBridge{target,*intermediate,started,deadline,revision,token};
  h.report("first",target,*intermediate,token);if(!token)cancelWrite();
 }
 // Owner closes ControlSession first, then this backend, before OBS shutdown.
 // No callback invokes client code or close(). Disconnect's signal mutex is
 // the barrier for callbacks already in flight; the source reference spans it.
 void close(){
  if(onOwner())cancelWrite();
  if(closed_.exchange(true,std::memory_order_acq_rel))return;
  if(signals_){api_.signal_handler_disconnect(signals_,"audio_sync",sync,this);api_.signal_handler_disconnect(signals_,"remove",removed,this);api_.signal_handler_disconnect(signals_,"destroy",removed,this);signals_=nullptr;}
  if(source_){api_.obs_source_release(source_);source_=nullptr;}
 }
};
} // namespace pitatuner


