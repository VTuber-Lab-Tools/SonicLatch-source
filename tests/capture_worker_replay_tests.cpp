#define SONICLATCH_CAPTURE_REPLAY_TEST 1
#include "obs_source_capture.hpp"
#include "display_adapter.hpp"
#include "real_packet_fixture.hpp"
#include <iostream>
#include <stdexcept>
using namespace pitatuner;
int main(){unsigned checks=0,oldStops=0,newWindows=0,validWindows=0,rejectedWindows=0,packets=0,groupIndex=0;auto check=[&](bool b){++checks;if(!b)throw std::runtime_error("capture worker replay check="+std::to_string(checks));};try{
 for(const auto& group:actualPackets()){
  std::vector<capture::Binding::ReplayPacket> input;size_t frames=0;bool oldStop=false;uint64_t previous=0;uint32_t priorFrames=0;
  for(const auto& p:group){const auto expected=group.front().media+capture::format::frameNs(frames,48000);if(!capture::timeline::continuous(p.media,expected,previous,priorFrames,48000,true))oldStop=true;input.push_back({p.media,p.frames});frames+=p.frames;previous=p.media;priorFrames=p.frames;++packets;}
  oldStops+=oldStop;
  for(uint64_t delay:{0ULL,20000000ULL,50000000ULL}){auto result=capture::Binding::replay(input,delay,frames);check(result.faults==0&&result.drops==0&&result.windows.size()==1);const auto& m=result.windows.front();check(m.timelineOk&&m.valid&&m.wave&&m.wave->series.size()==3&&m.commonClockMatched);check(std::abs(m.channels[0].delayMs-double(delay)/1000000)<0.001);++validWindows;++newWindows;}
  ++groupIndex;
 }
 check(packets==186&&oldStops>0&&newWindows==18&&validWindows==18&&rejectedWindows==0);
 // Exact published failure values, including a per-step failure with small anchor delta.
 check(!capture::timeline::continuous(263183229772700ULL,263183227754100ULL,263183217829600ULL,480,48000,true));
 check(!capture::timeline::continuous(263183797397000ULL,263183797771800ULL,263183789428100ULL,480,48000,true));
 std::vector<capture::Binding::ReplayPacket> base;for(uint64_t i=0;i<31;i++)base.push_back({1000000000+i*10000000,480});
 auto silent=capture::Binding::replay(base,20000000,31*480,true);check(silent.windows.size()==1);const auto& m=silent.windows[0];check(!m.valid&&m.wave&&m.preRms[0]>0.1&&m.postRms[0]==0&&m.postPeak[0]==0);
 display::Input in(pita::Mode::Unchosen);in.selection=m.key;in.latestSequence=m.sequence;in.observedAt=0;in.now=1;in.control.key=m.key;in.control.offset=0;in.deliveryState=pita::delivery::State::Ready;in.estimate=pita::Estimate{m.key,m.sequence,0,1000,false,false,false,false};in.wave=display::WaveInput{m.key,m.sequence,0,0,0,{},*m.wave};auto shown=display::build(in);check(shown->waveforms.size()==2&&!shown->delay&&!shown->target&&!shown->recommendationEnabled);for(const auto& series:shown->waveforms)check(series.kind==waveform::Kind::PreMeasured||series.kind==waveform::Kind::PostMeasured);
 auto gap=base;for(size_t i=15;i<gap.size();i++)gap[i].timestamp+=100000000;auto second=base;for(auto& p:second)p.timestamp+=410000000;gap.insert(gap.end(),second.begin(),second.end());auto recovery=capture::Binding::replay(gap,20000000,31*480);check(recovery.faults==0&&recovery.windows.size()==2);check(!recovery.windows[0].valid&&!recovery.windows[0].wave&&recovery.windows[0].clockDiscontinuity);check(recovery.windows[1].valid&&recovery.windows[1].wave);
 auto seq=capture::Binding::replay(base,0,31*480,false,true);check(seq.windows.size()==1&&!seq.windows[0].valid&&!seq.windows[0].wave);
 // Live measured waveform is available before any 2.56 s estimate completes.
 auto partial=capture::Binding::replay(base,20000000,48000*64/25);check(partial.windows.empty()&&partial.liveWave&&partial.liveWavePublications>=6);check(partial.liveWave->series.size()==2);check(partial.liveWave->series[0].kind==waveform::Kind::PreMeasured&&partial.liveWave->series[1].kind==waveform::Kind::PostMeasured);
 // Real callback arrival pause changes generation; retained old PCM cannot mix
 // into the restarted window. This is a unit replay, not hardware disconnect QA.
 auto paused=base;auto afterPause=base;for(auto& p:afterPause)p.timestamp+=1000000000;afterPause.front().pauseMs=270;paused.insert(paused.end(),afterPause.begin(),afterPause.end());auto restarted=capture::Binding::replay(paused,20000000,31*480);check(restarted.windows.size()==2&&restarted.windows[0].valid&&restarted.windows[1].valid);check(restarted.windows[0].captureGeneration!=restarted.windows[1].captureGeneration);check(restarted.windows[1].preMediaNs>=2000000000);
 // Owner sampled time T precedes both normal callbacks. Invoke the SAME
 // production activity observer with T, then continue real PRE/POST callbacks.
 // The old !active rule repeatedly changed generation and lost this window.
 auto racing=base;for(auto& p:racing)p.historicalActivityQuery=true;auto noFalseReset=capture::Binding::replay(racing,20000000,31*480);check(noFalseReset.activityRaceQueries==31&&noFalseReset.falseIdleMarks==0);check(noFalseReset.windows.size()==1&&noFalseReset.windows.front().valid&&noFalseReset.windows.front().captureGeneration==1);check(!noFalseReset.faults&&!noFalseReset.drops);
 // Production default (no test short-window override): 3 s of synthetic PCM,
 // delivered at accelerated 2 ms/block. Five 2 s windows, 250 ms frame hops.
 std::vector<capture::Binding::ReplayPacket> overlapping;for(uint64_t i=0;i<300;i++)overlapping.push_back({1000000000+i*10000000,480,2});auto stream=capture::Binding::replay(overlapping,20000000,0);check(!stream.faults&&!stream.drops&&stream.analysisSubmitted==5);check(!stream.windows.empty()&&stream.windows.back().sequence==5&&stream.windows.back().preMediaNs==2000000000);for(const auto& window:stream.windows)check(window.valid&&window.commonClockMatched&&std::abs(window.channels[0].delayMs-20)<.001);check(stream.analysisCompleted+stream.analysisReplaced==stream.analysisSubmitted);check(stream.liveWavePublications>=299&&stream.liveWavePublications<=301);check(stream.liveWave&&stream.liveWave->series.size()==2&&std::any_of(stream.liveWave->series[0].bins.begin(),stream.liveWave->series[0].bins.end(),[](const auto& bin){return bin.minimum&&bin.maximum&&*bin.minimum<*bin.maximum;}));
 std::cout<<"PIPELINE default_window_ms=2000 hop_ms=250 submitted="<<stream.analysisSubmitted<<" completed="<<stream.analysisCompleted<<" replaced="<<stream.analysisReplaced<<" compute_max_ms="<<double(stream.analysisComputeMaxNs)/1e6<<" queue_max_ms="<<double(stream.analysisQueueMaxNs)/1e6<<" ingest_dwell_max_ms="<<double(stream.ingestMaxDwellNs)/1e6<<"; 3 s media delivered at accelerated cadence, not hardware QA\n";
 std::cout<<"PASS "<<checks<<" exact copy/ring/worker/display checks; recorded_packets="<<packets<<" groups=6 old_stopped_groups="<<oldStops<<" new_windows="<<newWindows<<" valid_windows="<<validWindows<<" safely_rejected_windows="<<rejectedWindows<<"; synthetic PCM; no OBS runtime\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
