#include "correction_actions.hpp"
#include "correction_profile.hpp"
#include "display_adapter.hpp"
#include "measurement.hpp"
#include "capture_format.hpp"
#include "display_offset_columns.hpp"
#include "display_wave_gain.hpp"
#include "waveform_cadence.hpp"
#include "callback_gate.hpp"
#include <iostream>
#include <functional>
#include <thread>
#include <iomanip>
#include <algorithm>
#include <string>
#include <limits>
#include <cmath>

using pita::Ns; using pita::Result;
constexpr Ns MS=1000000, NOW=1000000000, FRESH=9000000000;
struct Checks {
 unsigned total=0, failed=0; std::string group;
 void test(bool ok,const std::string& text){++total;if(!ok){++failed;std::cout<<"FAIL\t"<<group<<'\t'<<text<<'\n';}}
} q;

// This backend does not call OBS or implement a hidden correction formula.
// It records requested absolute values; test expectations are independent literals.
struct FakeBackend final : pita::Backend {
 std::optional<Ns> value{0}; std::uint64_t revision=0;
 std::vector<Ns> writes; bool async=false,pending=false,reject=false;
 Ns queued=0; unsigned polls=0,cancels=0; std::uint64_t queuedRevision=0;
 std::function<void()> onWrite;
 std::optional<Ns> read() override {return value;}
 std::uint64_t externalRevision() const override {return revision;}
 void write(Ns n) override {writes.push_back(n);queued=n;queuedRevision=revision;if(async)pending=true;else if(!reject)value=n;if(onWrite)onWrite();}
 bool writePending() const override{return pending;}
 void pollWrite(Ns) override {++polls;if(pending){pending=false;if(revision==queuedRevision&&!reject)value=queued;}}
 void cancelWrite() override {++cancels;pending=false;}
 void external(Ns n){value=n;++revision;}
};
struct Fixture {
 FakeBackend b; pitatuner::DeliveryGate gate; pita::Key key{"synthetic-A",1,1};
 std::unique_ptr<pitatuner::ControlSession> c; std::uint64_t ids=0,baselines=0,seq=0;
 std::unique_ptr<pitatuner::CorrectionActions> actions;
 explicit Fixture(Ns initial=0){b.value=initial;c=std::make_unique<pitatuner::ControlSession>(b,key,pitatuner::qualification::select(true,true,initial),gate,true);actions=std::make_unique<pitatuner::CorrectionActions>(*c,ids,baselines);}
 pita::Estimate estimate(Ns d){return {key,++seq,d,FRESH,true,true,true,false};}
 pita::Estimate accept(Ns d){auto e=estimate(d);q.test(c->acceptEstimate(e),"fresh estimate accepted");return e;}
 Result click(size_t action,const std::string& text="",Ns now=NOW){return actions->operation(action,key,{},now,text);}
 Result autoAt(Ns d){accept(d);return click(5);}
 void expect(Ns n,const std::string& label){q.test(b.value&&*b.value==n,label+" actual="+(b.value?std::to_string(*b.value):"missing")+" expected="+std::to_string(n));}
};

void U01(){
 struct Row{Ns d,s;};
 for(auto r:std::vector<Row>{{0,0},{0,20*MS},{20*MS,0},{20*MS,-20*MS},{50*MS,20*MS},{50*MS,-393*MS},{393*MS,0},{393*MS,-393*MS},{950*MS,0},{-20*MS,50*MS}}){
  Fixture f(r.s);const auto expected=-r.d;const auto result=f.autoAt(r.d);
  q.test(result==(r.s==expected?Result::Noop:Result::Applied),"absolute Auto result");f.expect(expected,"absolute Auto ignores initial S");
  const auto count=f.b.writes.size();for(int i=0;i<5;++i){q.test(f.autoAt(r.d)==Result::Noop,"same-value Auto Noop");f.expect(expected,"no cumulative subtraction");}
  q.test(f.b.writes.size()==count,"same-value Auto emits no new write");
 }
 Fixture f(20*MS);q.test(f.autoAt(20*MS)==Result::Applied,"D20 apply");f.expect(-20*MS,"D20");
 q.test(f.autoAt(50*MS)==Result::Applied,"changed D50 apply");f.expect(-50*MS,"new D replaces old target");
 q.test(f.click(3)==Result::Applied,"+5 after Auto");f.expect(-45*MS,"manual trim");
 q.test(f.autoAt(50*MS)==Result::Applied,"Auto after trim");f.expect(-50*MS,"Auto discards trim rather than accumulate");
 // Absolute mode must ignore even a deliberately seeded legacy baseline.
 Fixture legacy(20*MS);q.test(legacy.c->registerConfirmedBaseline({pita::Mode::MeasuredReference,20*MS,7*MS,0,1},true),"legacy baseline setup");
 q.test(legacy.autoAt(50*MS)==Result::Applied,"absolute ignores legacy baseline");legacy.expect(-50*MS,"no legacy C contribution");
}
void U02(){
 auto r=pitatuner::qualification::select(true,true,0);
 q.test(r.hostContract&&!r.verified,"host input contract distinct from output proof");
 for(Ns n:{-950*MS,-950*MS+1,-949*MS,Ns{0},20*MS,995*MS,999*MS,1000*MS-1,1000*MS}){
  q.test(r.permits(n),"legal target accepted "+std::to_string(n));Fixture f(123);const auto result=f.c->deliver({f.key,++f.ids,pitatuner::Operation::Manual,n});q.test(result==Result::Applied,"legal manual not old-band-rejected");f.expect(n,"exact 64-bit manual");
 }
 for(Ns n:{-950*MS-1,1000*MS+1,1001*MS,3000*MS,20000*MS,INT64_MIN,INT64_MAX}){Fixture f(20*MS);q.test(!r.permits(n),"outside range");q.test(f.c->deliver({f.key,++f.ids,pitatuner::Operation::Manual,n})==Result::Rejected,"invalid manual rejected");q.test(f.b.writes.empty(),"invalid target write=0");f.expect(20*MS,"preserve S");}
 const std::vector<std::pair<std::string,Ns>> exact{{"-950",-950*MS},{"20000",20000*MS},{"3000",3000*MS},{"20.125",20125000},{"0.000001",1},{"-0.000001",-1},{"9223372036854.775807",INT64_MAX},{"-9223372036854.775808",INT64_MIN}};
 for(const auto& [text,n]:exact)q.test(pita::parse_ms(text)==std::optional<Ns>(n),"exact parse "+text);
 for(const std::string text:{"","NaN","nan","Inf","-inf","1e100","1 ms"," 1","1.0000001","9223372036854.775808","-9223372036854.775809","999999999999999999999999999999"}){Fixture f(20*MS);q.test(!pita::parse_ms(text),"invalid parser "+text);q.test(f.click(4,text)==Result::Rejected,"invalid UI input rejected");q.test(f.b.writes.empty(),"invalid input emits no write");}
 q.test(!pita::add(INT64_MAX,1)&&!pita::add(INT64_MIN,-1),"add overflow guarded");
 q.test(!pita::sub(0,INT64_MIN)&&!pita::sub(INT64_MIN,1),"subtract overflow guarded");
 Fixture high(1000*MS);q.test(high.click(2)==Result::Rejected,"upper +1 rejected");high.expect(1000*MS,"upper unchanged");q.test(high.click(1)==Result::Applied,"upper -1 legal");high.expect(999*MS,"upper inward step");
 Fixture low(-950*MS);q.test(low.click(1)==Result::Rejected,"lower -1 rejected");low.expect(-950*MS,"lower unchanged");q.test(low.click(2)==Result::Applied,"lower +1 legal");low.expect(-949*MS,"lower inward step");
 Fixture boundary; q.test(boundary.autoAt(950*MS)==Result::Applied,"injected D950 Auto boundary");boundary.expect(-950*MS,"Auto lower edge");
 q.test(boundary.autoAt(951*MS)==Result::Rejected,"injected D951 target outside");boundary.expect(-950*MS,"out-of-range Auto unchanged");
 Fixture minD; q.test(minD.autoAt(INT64_MIN)==Result::Rejected,"negation overflow no Auto");q.test(minD.b.writes.empty(),"negation overflow no write");
 for(Ns current:{Ns{0},20*MS,-393*MS,3000*MS})q.test(pitatuner::qualification::select(true,true,current).permits(15*MS),"profile independent from current personal bands");
}
void U03(){
 Fixture f(20*MS);f.accept(50*MS);
 struct Row{size_t action;const char* text;Ns expected;Result result;};
 const std::vector<Row> rows{{5,"",-50*MS,Result::Applied},{2,"",-49*MS,Result::Applied},{1,"",-50*MS,Result::Applied},{3,"",-45*MS,Result::Applied},{0,"",-50*MS,Result::Applied},{4,"30",30*MS,Result::Applied},{6,"",-50*MS,Result::Applied},{7,"",0,Result::Applied},{6,"",-50*MS,Result::Applied},{7,"",0,Result::Applied},{7,"",0,Result::Noop},{5,"",-50*MS,Result::Applied}};
 for(const auto& r:rows){q.test(f.click(r.action,r.text)==r.result,"action sequence result "+std::to_string(r.action));f.expect(r.expected,"action sequence S");}
 Fixture same(20*MS);same.autoAt(50*MS);q.test(same.autoAt(50*MS)==Result::Noop,"repeat Auto no-op");q.test(same.click(6)==Result::Applied,"Undo after same Auto");same.expect(20*MS,"no-op preserved meaningful Undo");q.test(same.click(6)==Result::Rejected,"one-step Undo not accidental toggle");
 Fixture invalid(20*MS);invalid.autoAt(50*MS);q.test(invalid.click(4,"NaN")==Result::Rejected,"input failure");q.test(invalid.click(6)==Result::Applied,"Undo survives no-write invalid input");invalid.expect(20*MS,"Undo restores pre-Auto S");
 Fixture resetBase; q.test(resetBase.c->registerConfirmedBaseline({pita::Mode::ExplicitPreTarget,0,0,0,1},true),"zero baseline setup");q.test(resetBase.click(7)==Result::Applied,"same-zero Reset clears state");q.test(!resetBase.c->view().baseline,"Reset removes legacy reference");q.test(resetBase.b.writes.empty(),"same-zero Reset no offset write");q.test(resetBase.autoAt(50*MS)==Result::Applied,"Reset then Auto");resetBase.expect(-50*MS,"no Reset baseline residue");
}

pitatuner::display::ImmutableSnapshot displayFor(Fixture& f,pita::Estimate e,Ns now=NOW){
 pitatuner::display::Input in(pita::Mode::Unchosen);in.selection=f.key;in.latestSequence=e.seq;in.observedAt=NOW;in.now=now;in.control=f.c->view();in.estimate=e;in.deliveryState=pita::delivery::State::Ready;return pitatuner::display::build(in);
}
void U04(){
 Fixture f(20*MS);auto e=f.accept(50*MS);auto d=displayFor(f,e);
 q.test(d->filterResidualPre==std::optional<Ns>(70*MS),"D+S even without proposal/baseline");
 e=f.accept(951*MS);q.test(!f.c->propose(NOW),"D951 Auto unavailable");d=displayFor(f,e);q.test(d->filterResidualPre==std::optional<Ns>(971*MS),"residual survives target outside legal range");
 for(int failure=0;failure<6;++failure){Fixture x(20*MS);auto invalid=x.estimate(50*MS);if(failure==0)invalid.valid=false;if(failure==1)invalid.confident=false;if(failure==2)invalid.active=false;if(failure==3)invalid.muted=true;if(failure==4)invalid.freshUntil=NOW;if(failure==5)invalid.key.uuid="other";
  const bool accepted=x.c->acceptEstimate(invalid);q.test(accepted==(failure!=5),"estimate identity accepted/rejected");q.test(x.click(5)==Result::Rejected,"unavailable D cannot Auto");q.test(x.b.writes.empty(),"invalid D no automatic write");q.test(!displayFor(x,invalid)->filterResidualPre,"invalid D not displayed as zero residual");q.test(x.click(4,"30")==Result::Applied,"manual does not depend on D validity");x.expect(30*MS,"manual valid without estimate");q.test(x.click(7)==Result::Applied,"Reset does not depend on D validity");}
 Fixture stale;auto old=stale.accept(20*MS);auto p=stale.c->propose(NOW);q.test(p.has_value(),"fresh proposal");stale.accept(50*MS);q.test(stale.c->apply(stale.key,++stale.ids,*p,NOW)==Result::Rejected,"old estimate proposal revoked");q.test(!stale.c->acceptEstimate(old),"old sequence rejected");
 Fixture over(20*MS);auto huge=over.accept(INT64_MAX);q.test(!displayFor(over,huge)->filterResidualPre,"residual overflow blank");
}
void U05(){
 Fixture f;f.autoAt(50*MS);auto e=f.accept(50*MS);auto p=f.c->propose(NOW);f.b.external(20*MS);const auto before=f.b.writes.size();q.test(p&&f.c->apply(f.key,++f.ids,*p,NOW)==Result::Protected,"external change before commit protects");f.expect(20*MS,"external value remains");q.test(f.b.writes.size()==before,"stale apply no write");q.test(f.click(6)==Result::Rejected,"old Undo invalidated");f.c->externalChange();q.test(f.c->acknowledgeRead(),"fresh read recovery");q.test(f.autoAt(50*MS)==Result::Applied,"new explicit Auto allowed after recovery");f.expect(-50*MS,"new Auto target");
 Fixture same; same.autoAt(20*MS);same.b.external(-20*MS);q.test(same.click(3)==Result::Protected,"same-value external revision rejects old authority");same.expect(-20*MS,"same-value external preserved");
 Fixture during;during.b.onWrite=[&]{during.b.external(50*MS);};q.test(during.autoAt(20*MS)==Result::Protected,"external change during synchronous completion");during.expect(50*MS,"no rollback overwrites external");q.test(during.b.writes.size()==1,"only initial attempted write");
 Fixture gone;gone.b.value.reset();q.test(gone.click(3)==Result::Protected,"removed backend unavailable");q.test(gone.b.writes.empty(),"removed source write=0");
 Fixture wrong;auto key=wrong.key;key.uuid="synthetic-B";q.test(wrong.c->deliver({key,++wrong.ids,pitatuner::Operation::Manual,20*MS})==Result::Rejected,"wrong UUID rejected");key=wrong.key;++key.session;q.test(wrong.c->deliver({key,++wrong.ids,pitatuner::Operation::Manual,20*MS})==Result::Rejected,"same-name new session rejected");q.test(wrong.b.writes.empty(),"identity mismatch write=0");
}
void U06(){
 Fixture idle;idle.accept(50*MS);for(int i=0;i<20;++i){idle.c->propose(NOW+i);idle.c->pollPending(NOW+i);}q.test(idle.b.writes.empty(),"measurement/proposals without user action write=0");
 Fixture replay;const pitatuner::Intent intent{replay.key,1,pitatuner::Operation::Step,MS};q.test(replay.c->deliver(intent)==Result::Applied,"initial delivery");q.test(replay.c->deliver(intent)==Result::Rejected,"same intent replay rejected");q.test(replay.b.writes.size()==1,"replayed event not reexecuted");
 Fixture async;async.b.async=true;auto e=async.accept(20*MS);q.test(async.click(5)==Result::Pending,"pending write");q.test(async.click(3)==Result::Busy,"rapid second action busy");q.test(async.b.writes.size()==1,"pending write not duplicated");auto result=async.c->pollPending(NOW+1);q.test(result&&*result==Result::Applied,"one pending completion");async.expect(-20*MS,"pending committed target");for(int i=0;i<20;++i)async.c->pollPending(NOW+i);q.test(async.b.writes.size()==1,"polling cannot continuously write");
 Fixture expired;expired.b.async=true;expired.accept(20*MS);expired.click(5);auto out=expired.c->pollPending(FRESH);q.test(out&&*out==Result::Protected,"pending Auto expires");q.test(expired.b.cancels==1&&!expired.b.pending,"pending canceled once");expired.expect(0,"expired operation not committed");q.test(!expired.c->view().canUndo,"failed operation never undo success");
 Fixture mismatch(20*MS);mismatch.b.reject=true;q.test(mismatch.autoAt(50*MS)==Result::Protected,"failed readback not success");mismatch.expect(20*MS,"failure preserved old S");q.test(!mismatch.c->view().canUndo,"failed commit not success history");
 Fixture cross;Result threadResult=Result::Applied;std::thread t([&]{threadResult=cross.c->deliver({cross.key,1,pitatuner::Operation::Manual,20*MS});});t.join();q.test(threadResult==Result::Rejected&&cross.b.writes.empty(),"wrong owner thread no write");
}

std::vector<float> signal(std::size_t n,std::uint32_t seed){std::vector<float> v(n);for(auto& x:v){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;x=(float(seed%20001)-10000.f)/20000.f;}return v;}
void U07(){
 for(unsigned rate:{44100U,48000U}){
  q.test(pitatuner::capture::format::supportedRate(rate),"supported rate");q.test(pitatuner::capture::format::windowFrames(rate)==2*rate,"2-second estimator window at actual rate");
  // Independent PCM delay, not timestamp-only displacement; L/R carry unrelated signals.
  for(int delay:{0,20,50,393}){
   const auto n=std::size_t(rate)*2,lag=std::size_t(std::llround(double(delay)*rate/1000.));const double trueMs=1000.*double(lag)/rate;
   for(unsigned channel:{0U,1U}){auto pre=signal(n,20261019+channel*7919);auto post=std::vector<float>(n);for(std::size_t i=lag;i<n;++i)post[i]=pre[i-lag]*.25f;const auto originalPre=pre,originalPost=post;
    const auto estimate=latency::estimate({pre,NOW,rate,1,false},{post,NOW,rate,1,false});q.test(estimate.valid&&std::abs(estimate.delayMs-trueMs)<=1.,"PCM estimator rate="+std::to_string(rate)+" D="+std::to_string(delay)+" channel="+std::to_string(channel));q.test(pre==originalPre&&post==originalPost,"estimator does not mutate raw samples");
    Fixture f(20*MS);const Ns expectedNs=Ns(std::llround(double(lag)*1000000000./rate));q.test(f.autoAt(expectedNs)==Result::Applied,"sample-derived general D accepted");f.expect(-expectedNs,"sample-derived target independent oracle");
   }
  }
  auto pre=signal(rate,11),unrelated=signal(rate,991),silence=std::vector<float>(rate);q.test(!latency::estimate({pre,NOW,rate,1,false},{unrelated,NOW,rate,1,false}).valid,"uncorrelated signal cannot claim D");q.test(!latency::estimate({silence,NOW,rate,1,false},{silence,NOW,rate,1,false}).valid,"silence cannot claim D=0");
  pitatuner::displayonly::SampleHistory history;std::vector<float> chunk(1024,.25f);
  for(std::size_t frame=0;frame<std::size_t(rate)*11;frame+=1024){const auto count=std::min<std::size_t>(1024,std::size_t(rate)*11-frame);const auto time=std::uint64_t(1000000000)+std::uint64_t(frame)*1000000000ULL/rate;history.add(0,time,chunk.data(),count,rate);history.add(1,time,chunk.data(),count,rate);}
  auto frame=history.snapshot(2000000000ULL,12000000000ULL);q.test(!frame->tracks[0].empty()&&!frame->tracks[1].empty()&&frame->droppedChunks==0,"10-second history retained without capacity drops");q.test(frame->tracks[0].front()->end()>2000000000ULL,"pruned history starts at intersecting boundary");q.test(frame->tracks[0].back()->end()<=12000000000ULL,"history timestamps remain raw");
  auto sourceBefore=chunk;pitatuner::displayonly::Gain gain;for(int i=1;i<=60;++i)gain.update(.25,i*16666667ULL);q.test(gain.value()>1&&chunk==sourceBefore,"display gain leaves source PCM untouched");
 }
 // A visible impulse lands at t+S exactly once. PRE remains at its raw location.
 pitatuner::displayonly::SampleHistory h;std::vector<float> pulse(480,0);pulse[0]=1;h.add(0,2000000000,pulse.data(),pulse.size(),48000);h.add(1,2050000000,pulse.data(),pulse.size(),48000);auto frame=h.snapshot(1900000000,2200000000);
 auto peak=[](const auto& cols){for(std::size_t i=0;i<cols.size();++i)if(cols[i].valid&&cols[i].hi>.5f)return i;return cols.size();};
 const auto pre=pitatuner::displayonly::offsetSampleColumns(*frame,0,1900000000,2200000000,300,0);
 const auto raw=pitatuner::displayonly::offsetSampleColumns(*frame,1,1900000000,2200000000,300,0);
 const auto aligned=pitatuner::displayonly::offsetSampleColumns(*frame,1,1900000000,2200000000,300,-50*MS);
 const auto trim=pitatuner::displayonly::offsetSampleColumns(*frame,1,1900000000,2200000000,300,-45*MS);
 q.test(peak(pre)==100&&peak(raw)==150&&peak(aligned)==100&&peak(trim)==105,"POST coordinates D+S once; PRE fixed");
 for(unsigned fps:{30U,60U}){pitatuner::displayonly::FrameCadence cadence;cadence.setFps(fps);unsigned draws=0;for(std::uint64_t time=1000000;time<1000000+5000000000ULL;time+=1000000)if(cadence.due(time))++draws;q.test(std::abs(int(draws)-int(fps*5))<=1,"drawing cadence "+std::to_string(fps));}
 q.test(!pitatuner::displayonly::shiftedTime(0,-1)&&!pitatuner::displayonly::shiftedTime(UINT64_MAX,1),"display shift uint64 edges guarded");
}
void U08(){
 Fixture f;f.b.async=true;f.accept(20*MS);q.test(f.click(5)==Result::Pending,"pending before close");f.c->close();q.test(f.b.cancels==1&&!f.b.pending,"close cancels pending");const auto n=f.b.writes.size();q.test(f.click(3)==Result::Rejected,"queued action after close rejected");q.test(!f.c->pollPending(NOW),"poll after close cannot commit");q.test(f.b.writes.size()==n,"close no late write");f.expect(0,"closed operation not committed");
 CallbackGate gate;q.test(gate.enter(),"live callback lease");gate.close();q.test(gate.isClosed()&&!gate.quiet(),"closing waits for active callback");q.test(!gate.enter(),"new callback refused after close");gate.leave();q.test(gate.quiet(),"accepted lease drains before context deletion");
 latency::SpscRing<int,4> pre,post;std::atomic<bool> done=false;int value=0,total=0;const bool drained=latency::drainAndCheckDone(pre,post,done,value,[&](int x,bool){total+=x;},[&]{pre.push(3);post.push(5);done.store(true);});q.test(drained&&total==8,"final publication before done drained");
 std::cout<<"NOT_RUN\tU08-runtime\tActual OBS callback removal/context release and global-mic shutdown require Windows H cases; fake gate tests are not their substitute.\n";
}
int main(){
 const std::vector<std::pair<std::string,std::function<void()>>> groups{{"U01",U01},{"U02",U02},{"U03",U03},{"U04",U04},{"U05",U05},{"U06",U06},{"U07",U07},{"U08",U08}};
 for(const auto& [name,run]:groups){q.group=name;const auto before=q.failed,total=q.total;try{run();}catch(const std::exception& e){q.test(false,std::string("exception: ")+e.what());}std::cout<<(q.failed==before?"PASS":"FAIL")<<'\t'<<name<<"\tchecks="<<q.total-total<<" failed="<<q.failed-before<<'\n';}
 std::cout<<"NOT_RUN\tU07-runtime\tQt visual appearance, mute UI geometry, real PRE/POST taps and OBS mix output are Windows E cases.\n";
 std::cout<<"SUMMARY\tchecks="<<q.total<<" failed="<<q.failed<<"\tScope=pure Linux model/math/estimator/display-data/gates; no OBS runtime.\n";return q.failed?1:0;
}
