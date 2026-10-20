#include <vector>
#include <string>
#include <cstring>
#include <iostream>
#include <cassert>
#include <atomic>
#include <thread>
#include "obs_offset_backend.hpp"
#include "readback_guard.hpp"
#include "correction_profile.hpp"
struct signal_handler_t {struct Item{std::string name;void(*cb)(void*,calldata_t*);void* ctx;};std::vector<Item> items;};
struct obs_source_t {std::atomic<int64_t> offset{};signal_handler_t signals;int mode=0;};
struct calldata_t {obs_source_t* source;int64_t offset;};
obs_source_t* obs_source_get_ref(obs_source_t* s){return s;}void obs_source_release(obs_source_t*){}
const char* obs_source_get_uuid(obs_source_t*){return "owned-test";}bool obs_source_removed(obs_source_t*){return false;}
signal_handler_t* obs_source_get_signal_handler(obs_source_t*s){return &s->signals;}
int64_t obs_source_get_sync_offset(obs_source_t*s){return s->offset.load();}
void obs_source_set_sync_offset(obs_source_t*s,int64_t n){calldata_t data{s,n};for(auto& i:s->signals.items)if(i.name=="audio_sync")i.cb(i.ctx,&data);if(n==-393000000&&s->mode==4)return;s->offset.store(data.offset);if(n==-393000000&&s->mode==5){for(auto& i:s->signals.items)if(i.name=="audio_sync")i.cb(i.ctx,&data);}}
void signal_handler_connect(signal_handler_t*s,const char*n,void(*cb)(void*,calldata_t*),void*c){s->items.push_back({n,cb,c});}
void signal_handler_disconnect(signal_handler_t*,const char*,void(*)(void*,calldata_t*),void*){}
bool calldata_get_data(calldata_t*d,const char*n,void*p,size_t z){if(!std::strcmp(n,"source")&&z==sizeof d->source){std::memcpy(p,&d->source,z);return true;}if(!std::strcmp(n,"offset")&&z==sizeof d->offset){std::memcpy(p,&d->offset,z);return true;}return false;}
void* GetProcAddress(HMODULE,const char*n){
#define LOOKUP(x) if(!std::strcmp(n,#x))return reinterpret_cast<void*>(&x)
LOOKUP(obs_source_get_ref);LOOKUP(obs_source_release);LOOKUP(obs_source_get_uuid);LOOKUP(obs_source_removed);LOOKUP(obs_source_get_signal_handler);LOOKUP(obs_source_get_sync_offset);LOOKUP(obs_source_set_sync_offset);LOOKUP(signal_handler_connect);LOOKUP(signal_handler_disconnect);LOOKUP(calldata_get_data);
return nullptr;}
std::atomic<unsigned> checks{0};void check(bool b){++checks;if(!b){std::cerr<<"FAIL "<<checks<<'\n';std::abort();}}
int main(){
 using namespace soniclatch::bridge;
 const auto range=[](int64_t n){return n>=-400000000&&n<=0;};
 check(intermediate(-393000000,range)==-393000001);check(intermediate(-400000000,range)==-399999999);check(!intermediate(0,range));check(!intermediate(1,range));
 check(intermediate(INT64_MIN,[](auto){return true;})==INT64_MIN+1);check(intermediate(INT64_MAX,[](auto){return true;})==INT64_MAX-1);check(!intermediate(-1,[](auto n){return n==-1||n==0;}));
 PacketAck q;auto pre=q.pre();auto token=q.arm();q.post(pre,true,true);check(!q.complete(token));q.post(token,false,true);check(!q.complete(token));q.post(token,true,false);check(!q.complete(token));q.post(token,true,true);check(q.complete(token));q.cancel();check(!q.complete(token));auto second=q.arm();q.post(token,true,true);check(!q.complete(second));
 for(int mode=0;mode<6;++mode){
  obs_source_t source;source.mode=mode; pitatuner::ObsOffsetApi api(nullptr);pitatuner::ObsOffsetBackend backend(api,&source,1,1);pitatuner::DeliveryGate delivery;
  PacketAck ack;std::atomic<bool> armed{false};std::vector<std::string> events;
  backend.zeroBridge({[]{return true;},range,[&]{auto n=ack.arm();armed=true;return n;},[&](auto n){return ack.complete(n);},[&]{ack.cancel();},[&](const char*s,auto,auto,auto){events.push_back(s);}});
  pitatuner::ControlSession control(backend,{"owned-test",1,1},{-400000000,0,1,true},delivery);
  std::jthread audio([&](std::stop_token stop){while(!stop.stop_requested()&&!armed.load())std::this_thread::yield();if(stop.stop_requested())return;auto n=ack.pre();
   if(mode==0||mode>=4){check(source.offset==-393000001);ack.post(n,true,true);} // source-guarded packet completion stub, not OBS
   if(mode==2)obs_source_set_sync_offset(&source,-390000000);
   if(mode==3)obs_source_set_sync_offset(&source,-393000001); // same-value foreign write still wins
  });
  const auto started=std::chrono::steady_clock::now();auto result=control.deliver({control.view().key,1,pitatuner::Operation::Manual,-393000000});
  check(result==pita::Result::Pending&&control.view().pending&&control.view().offset==0&&!control.view().canUndo);check(backend.notifications()==1||(mode==2||mode==3));
  check(pitatuner::readback::follow(backend,control,control.view().key)==0); // intermediate never acknowledged by display
  check(control.deliver({control.view().key,2,pitatuner::Operation::Manual,-392000000})==pita::Result::Busy);
  audio.join();const auto now=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
  auto completed=control.pollPending(now+(mode==1?251000000:0));check(completed.has_value()&&*completed!=pita::Result::Pending);result=*completed;
  check(!control.view().pending);(void)started;
  if(mode==0){check(result==pita::Result::Applied&&source.offset==-393000000);check(backend.notifications()==2&&control.view().canUndo);auto undo=control.deliver({control.view().key,3,pitatuner::Operation::Undo,0});check(undo==pita::Result::Applied&&source.offset==0&&!control.view().canUndo);check(backend.notifications()==3);}
  if(mode==1){check(result==pita::Result::Protected&&source.offset==0);check(backend.notifications()==2&&!control.view().canUndo);check(events.back()=="rollback");}
  if(mode>=4){check(result==pita::Result::Protected&&source.offset==(mode==4?-393000001:-393000000));check(!control.view().canUndo);check(events.back()=="final_uncertain");check(backend.notifications()==(mode==4?2:3));}
  if(mode==2||mode==3){check(result==pita::Result::Protected&&source.offset==(mode==2?-390000000:-393000001));check(backend.notifications()==2&&!control.view().canUndo);check(events.back()=="cancel_external_or_invalid");}
 }

 // Product write bounds replace the old fixture-specific positive/negative bands.
 // Host API admission is distinct from output-verified evidence and current S.
 auto productRange=pitatuner::qualification::select(true,true,0);check(productRange.hostContract&&!productRange.verified);check(productRange.min==-950000000&&productRange.max==1000000000);
 for(const pita::Ns target:{-950000000LL,-400000000LL,-392958333LL,-1000000LL,0LL,20000000LL,995000000LL,1000000000LL})check(productRange.permits(target));
 for(const pita::Ns target:{-950000001LL,1000000001LL,1001000000LL,20000000000LL})check(!productRange.permits(target));
 for(const auto unavailable:{pitatuner::qualification::select(false,true,0),pitatuner::qualification::select(true,false,0)})check(!unavailable.hostContract&&!unavailable.verified&&!unavailable.permits(0));
 for(const pita::Ns current:{0LL,1000000LL,100000000LL,3000000000LL}){const auto selected=pitatuner::qualification::select(true,true,current);check(selected.hostContract&&!selected.verified&&selected.permits(-950000000)&&selected.permits(0)&&selected.permits(1000000000)&&!selected.permits(1000000001));}
 check(!pitatuner::qualification::targetAllowed(productRange,0,-393000000,false));check(pitatuner::qualification::targetAllowed(productRange,0,-393000000,true));check(pitatuner::qualification::targetAllowed(productRange,0,0,false));
 check(pitatuner::qualification::targetAllowed(productRange,-393000000,0,false));check(pitatuner::qualification::targetAllowed(productRange,-393000000,-392000000,false));
 check(pitatuner::qualification::targetAllowed(productRange,3000000000,0,false));check(!pitatuner::qualification::targetAllowed(productRange,995000000,1001000000,true));
 std::cout<<"PASS "<<checks<<" async bridge integer/token/Pending/Undo/timeout/foreign-write/profile contracts; fake OBS API, no real output certification\n";
}
