#!/usr/bin/env python3
"""Compile exact lifecycle helpers/dispatch/observer against owner/Qt doubles.
This checks local ordering and state contracts, not OBS or real Qt delivery.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
module = (root / "poc/normal-source-dock/normal-source-dock-module.cpp").read_text()
start = module.index("void detachCaptureSelection(")
end = module.index(" if(e==OBS_FRONTEND_EVENT_FINISHED_LOADING)", start)
handler = module[start:end] + "}\n"
start = module.index("class DockVisibilityObserver final:")
observer = module[start:module.index("void checkLocales(){", start)]
# Integration assertions: initial sync follows owner/store/timer initialization;
# a hidden startup has no unconditional restore/start; menu fallback is installed.
finished = module[module.index(" if(e==OBS_FRONTEND_EVENT_FINISHED_LOADING)"):]
assert finished.index("ticker=new QTimer(root)") < finished.index("owner->selectedSourceStore=std::make_unique") < finished.index("captureVisibilityChanged(dock->isVisible())")
assert "ticker->start(isolatedQa?33:16)" not in finished
assert "pitatuner::selection::restore" not in finished
assert "&QDockWidget::visibilityChanged,root,[](bool visible)noexcept{captureVisibilityChanged(visible);}" in finished
assert "dock->installEventFilter(new DockVisibilityObserver(root,[](bool visible)noexcept{captureVisibilityChanged(visible);}))" in finished
assert "!normalDockVisible.value_or(false)||collectionTransition||shutdownStarted" in finished
assert "OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN){shutdownStarted=true;" in finished
assert "shutdownStarted=true" not in module[module.index("class MainCloseObserver"):module.index("class DockVisibilityObserver")]
prelude = r'''
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
constexpr int LOG_INFO=1;
void logger(int,const char*,...){}
struct QString {std::string value;std::string toStdString()const{return value;}};
struct Picker {int index=-1;bool blocked=false;void setCurrentIndex(int i){index=i;}};
Picker pickerStorage;Picker* picker=&pickerStorage;
struct QSignalBlocker {Picker* p;bool previous;QSignalBlocker(Picker* v):p(v),previous(v->blocked){p->blocked=true;}~QSignalBlocker(){p->blocked=previous;}};
struct Widget{} rootStorage;Widget* root=&rootStorage;
struct Api{} apiStorage;Api* api=&apiStorage;
bool isolatedQa=false,throwSelect=false,throwCleanup=false,throwRefresh=false,selectionFails=false;
std::optional<bool> normalDockVisible;bool collectionTransition{},shutdownStarted{};
std::vector<std::string> calls;
struct Timer {bool active=false;unsigned starts=0,stops=0;void start(int n){if(n!=16)throw std::runtime_error("normal timer interval");active=true;++starts;calls.push_back("start");}void stop(){active=false;++stops;calls.push_back("stop");}};
Timer tickerStorage;Timer* ticker=&tickerStorage;
namespace pitatuner::selection {
enum class Restore {NoSavedSelection,InvalidStorage,MissingSource,SelectionFailed,Restored};
struct Store {Restore result=Restore::Restored;unsigned writes=0;};
const char* code(Restore){return "test";}
Restore restore(Picker& p,const Store& s,const std::function<bool(const QString&)>& select){
 calls.push_back("restore");if(s.result!=Restore::Restored)return s.result;
 QSignalBlocker block(&p);p.setCurrentIndex(2);if(!select(QString{"saved-uuid"})){p.setCurrentIndex(-1);return Restore::SelectionFailed;}return Restore::Restored;
}
}
namespace cap {struct Binding {
 bool selected=false;std::string uuid;
 void* selectedBorrowedForOwner(){return selected?this:nullptr;}
 struct Key {std::string uuid;};Key key(){return {uuid};}
 static unsigned long long liveFilters(){return 0;}
 static size_t removeSavedBoundaries(Api&){calls.push_back("cleanup");if(throwCleanup)throw std::runtime_error("cleanup");return 1;}
};}
struct Consumer{unsigned count=0;void discardSnapshot(){++count;calls.push_back("discard");}};
struct Owner {
 cap::Binding binding;Consumer consumer;int lastPacket=8,lastPresentation=9;std::optional<int> correctionEstimate=7;
 std::unique_ptr<pitatuner::selection::Store> selectedSourceStore=std::make_unique<pitatuner::selection::Store>();
 bool control=false,invalidated=true,done=false;unsigned selections=0,attaches=0,refreshes=0,failures=0;
 void select(const std::string& s){
  if(!picker||!picker->blocked)throw std::runtime_error("selection signals not blocked");
  calls.push_back(s.empty()?"detach":"attach");++selections;
  if(s.empty()){if(picker->index!=-1&&picker->index!=2)throw std::runtime_error("detach order");binding.selected=false;binding.uuid.clear();control=false;invalidated=true;}
  else{if(picker->index!=2||s!="saved-uuid")throw std::runtime_error("UUID-only restore");++attaches;binding.selected=!selectionFails;binding.uuid=s;control=!selectionFails;invalidated=selectionFails;}
  if(throwSelect)throw std::runtime_error("select");
 }
 void refreshCatalog(){calls.push_back("refresh");++refreshes;if(throwRefresh)throw std::runtime_error("refresh");}
 void uiFailure(const char*){++failures;binding.selected=false;done=true;if(ticker)ticker->stop();}
};
std::unique_ptr<Owner> owner;
enum obs_frontend_event {OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING,OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP,OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED,OTHER};
struct QEvent {enum Type{Show,Hide,Close,Other};Type kind;Type type()const{return kind;}};
struct QObject {bool blocked=false;explicit QObject(QObject* =nullptr){}virtual ~QObject()=default;bool signalsBlocked()const{return blocked;}virtual bool eventFilter(QObject*,QEvent*){return false;}bool send(QObject* watched,QEvent::Type t){QEvent e{t};return eventFilter(watched,&e);}};
struct QPoint {int left=0,top=0;int x()const{return left;}int y()const{return top;}};
struct QRect {int left=0,top=0,r=100,b=100;int right()const{return r;}int bottom()const{return b;}QPoint topLeft()const{return {left,top};}};
struct QScreen {QRect rect;QRect availableVirtualGeometry()const{return rect;}};
struct QDockWidget:QObject {bool floating=false;QRect rect;QScreen monitor;bool isWindow()const{return floating;}QScreen* screen(){return &monitor;}QRect geometry()const{return rect;}};
template<class T>T qobject_cast(QObject* value){return dynamic_cast<T>(value);}
unsigned checks=0;
void check(bool b,const char* message){++checks;if(!b)throw std::runtime_error(message);}
void reset(){owner=std::make_unique<Owner>();pickerStorage=Picker{};picker=&pickerStorage;root=&rootStorage;tickerStorage=Timer{};ticker=&tickerStorage;isolatedQa=throwSelect=throwCleanup=throwRefresh=selectionFails=collectionTransition=shutdownStarted=false;normalDockVisible.reset();calls.clear();}
'''
main = r'''
int main(){try {
 reset();captureVisibilityChanged(false);
 check(!ticker->active&&!owner->binding.selected&&owner->attaches==0,"hidden startup never attaches or starts timer");
 check(picker->index==-1&&!picker->blocked,"hidden picker cleared and blocker restored");
 check(owner->consumer.count==1&&!owner->lastPacket&&!owner->lastPresentation&&!owner->correctionEstimate,"hidden drops stale presentation");
 check(calls==std::vector<std::string>{"stop","detach","discard"},"timer stopped before release/discard");
 auto callsBefore=calls;captureVisibilityChanged(false);check(calls==callsBefore,"duplicate hidden signal/event no repeated detach");
 captureVisibilityChanged(true);check(owner->attaches==1&&owner->binding.selected&&ticker->active&&ticker->starts==1,"show restores exact saved UUID and starts timer");
 check(picker->index==2&&!picker->blocked&&owner->selectedSourceStore->writes==0,"restore never saves selection");
 callsBefore=calls;captureVisibilityChanged(true);check(calls==callsBefore,"duplicate shown signal/event no repeated attach");
 captureVisibilityChanged(false);check(!ticker->active&&!owner->binding.selected,"hide synchronously releases capture");
 captureVisibilityChanged(true);check(owner->attaches==2&&ticker->active,"reshow restores once");
 event(OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING,nullptr);check(collectionTransition&&!ticker->active&&!owner->binding.selected,"CHANGING stops and detaches synchronously");
 event(OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP,nullptr);check(!owner->binding.selected&&!ticker->active,"CLEANUP safely repeats detach");
 const auto attaches=owner->attaches;captureVisibilityChanged(false);captureVisibilityChanged(true);check(owner->attaches==attaches&&!ticker->active,"show during transition cannot reattach");
 calls.clear();event(OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED,nullptr);
 check(!collectionTransition&&owner->binding.selected&&ticker->active&&owner->attaches==attaches+1,"visible CHANGED restores saved UUID");
 check(calls.size()>2&&calls[0]=="cleanup"&&calls[1]=="refresh","saved boundaries removed before refresh/restore");
 reset();normalDockVisible=true;event(OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP,nullptr);event(OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED,nullptr);check(owner->attaches==1&&ticker->active,"duplicate activation CLEANUP without CHANGING restores");
 reset();captureVisibilityChanged(false);event(OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING,nullptr);event(OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED,nullptr);check(!ticker->active&&owner->attaches==0&&!owner->binding.selected,"hidden CHANGED stays detached");captureVisibilityChanged(true);check(owner->attaches==1,"show after hidden CHANGED restores");
 for(auto result:{pitatuner::selection::Restore::MissingSource,pitatuner::selection::Restore::NoSavedSelection,pitatuner::selection::Restore::InvalidStorage}){reset();owner->selectedSourceStore->result=result;captureVisibilityChanged(false);captureVisibilityChanged(true);check(!owner->binding.selected&&picker->index==-1&&owner->attaches==0&&owner->selectedSourceStore->writes==0,"missing/invalid UUID never falls back or overwrites");}
 reset();selectionFails=true;captureVisibilityChanged(true);check(!owner->binding.selected&&picker->index==-1&&owner->attaches==1,"failed restore remains unselected");
 reset();owner->done=true;captureVisibilityChanged(false);captureVisibilityChanged(true);event(OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED,nullptr);check(owner->done&&!ticker->active&&owner->attaches==0,"done remains terminal: no check recovery");
 reset();shutdownStarted=true;captureVisibilityChanged(true);check(owner->attaches==0&&!ticker->active,"shutdown visibility cannot reattach");
 reset();isolatedQa=true;event(OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING,nullptr);event(OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP,nullptr);event(OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED,nullptr);captureVisibilityChanged(true);check(calls.empty()&&!collectionTransition,"embedded QA lifecycle unchanged");
 reset();root=nullptr;event(OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING,nullptr);event(OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED,nullptr);check(calls.empty()&&!collectionTransition,"before root initialization safe");
 reset();owner.reset();event(OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP,nullptr);event(OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED,nullptr);captureVisibilityChanged(false);check(calls.empty(),"after owner destruction safe");
 reset();picker=nullptr;event(OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING,nullptr);captureVisibilityChanged(false);check(calls.empty(),"missing picker safe");
 reset();ticker=nullptr;captureVisibilityChanged(true);check(calls.empty()&&owner->attaches==0,"no capture before timer initialization");
 reset();picker->blocked=true;captureVisibilityChanged(false);check(picker->blocked,"preexisting signal blocker preserved");
 reset();throwSelect=true;captureVisibilityChanged(false);check(owner->failures==1&&owner->done&&!ticker->active&&!picker->blocked,"detach exception protected and blocker released");
 reset();throwCleanup=true;event(OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED,nullptr);check(owner->failures==1&&owner->refreshes==0,"cleanup exception protected");
 reset();throwRefresh=true;captureVisibilityChanged(true);check(owner->failures==1&&!ticker->active&&owner->attaches==0,"show refresh exception protected");
 reset();event(OTHER,nullptr);check(calls.empty(),"unrelated event unchanged");
 // No signal is delivered here: exact event-filter fallback alone drives state.
 reset();DockVisibilityObserver observer(nullptr,[](bool visible){captureVisibilityChanged(visible);});QDockWidget dock;dock.blocked=true;
 check(!observer.send(&dock,QEvent::Show)&&owner->attaches==1&&ticker->active,"signal-free Show observed without consuming event");
 captureVisibilityChanged(true);check(owner->attaches==1,"later visibility signal deduplicated");
 check(!observer.send(&dock,QEvent::Hide)&&!owner->binding.selected&&!ticker->active,"signal-free Hide stops without consuming event");
 callsBefore=calls;check(!observer.send(&dock,QEvent::Hide)&&calls==callsBefore,"duplicate fallback Hide deduplicated");
 check(!observer.send(&dock,QEvent::Other)&&!observer.send(&dock,QEvent::Close)&&calls==callsBefore,"unrelated/Close events left untouched");
 check(!observer.send(&dock,QEvent::Show)&&owner->attaches==2,"signal-free re-show restores");
 reset();dock.blocked=false;check(!observer.send(&dock,QEvent::Show)&&owner->attaches==0,"unblocked Show defers to Qt signal");captureVisibilityChanged(false);check(owner->attaches==0&&!ticker->active,"Qt inactive-tab false signal never transiently attaches");
 reset();dock.blocked=true;dock.rect.r=-1;check(!observer.send(&dock,QEvent::Show)&&owner->attaches==0&&!ticker->active,"blocked inactive-tab geometry stays detached");
 reset();dock.floating=true;dock.monitor.rect.left=-1920;dock.monitor.rect.top=-1080;dock.rect.r=-500;dock.rect.b=-200;check(!observer.send(&dock,QEvent::Show)&&owner->attaches==1,"blocked floating Show honors negative screen origin");
 std::printf("PASS %u lifecycle contract checks + 8 source wiring assertions (exact helpers/observer; doubles, not OBS/Qt runtime)\n",checks);
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
'''
with tempfile.TemporaryDirectory(prefix="soniclatch-lifecycle-") as directory:
    d = Path(directory)
    (d/"test.cpp").write_text(prelude+observer+handler+main)
    flags = ["-fsanitize=undefined", "-fno-sanitize-recover=all"] if os.environ.get("SONICLATCH_TEST_UBSAN") == "1" else []
    subprocess.run(["g++","-std=c++20","-O2","-Wall","-Wextra","-pedantic",*flags,str(d/"test.cpp"),"-o",str(d/"test")],check=True)
    subprocess.run([str(d/"test")],check=True)
