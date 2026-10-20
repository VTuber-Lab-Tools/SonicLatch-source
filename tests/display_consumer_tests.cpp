#include "display_consumer.hpp"
#include <cassert>
#include <iostream>
using namespace pitatuner;
namespace d=pitatuner::display;
namespace q=pita::delivery;
struct Backend final:pita::Backend {
 std::optional<pita::Ns> value=100;std::uint64_t revision{};unsigned writes{};
 std::optional<pita::Ns> read() override{return value;}
 std::uint64_t externalRevision() const override{return revision;}
 void write(pita::Ns n)override{value=n;++writes;}
};
struct Owner {
 Backend backend;DeliveryGate gate;pita::Key key{"source-A",1,1};
 ControlSession control{backend,key,{-1000,1000,1,true},gate};
 std::uint64_t observedNotification=1;q::Queue<2> queue;q::Key token{11,22,1,1};q::Ticket ticket;
 pita::Estimate estimate{key,1,70,1000,true,true,true,false};bool estimateAvailable=true;
 Owner(std::string uuid="source-A",std::uint64_t session=1):key{std::move(uuid),session,1},control(backend,key,{-1000,1000,1,true},gate),token{11+session,22,session,1}{assert(queue.select(token));ticket=*queue.request();assert(queue.recover({ticket,1,100}));assert(queue.acknowledge(ticket));
  assert(control.registerConfirmedBaseline({pita::Mode::MeasuredReference,100,20,0,1},true));assert(control.acceptEstimate(estimate));assert(control.propose(10));}
 d::Context context(){auto v=control.view();return {key,ticket,queue.state(),queue.delivered_sequence(),observedNotification>queue.delivered_sequence()?observedNotification:queue.delivered_sequence(),control.acceptedEstimateSequence(),queue.size()==0,v.closed,v.protectedState};}
 d::Packet packet(pita::Ns observation=10,pita::Ns now=20){
  d::Input in(pita::Mode::MeasuredReference);in.selection=key;in.latestSequence=estimate.seq;in.observedAt=observation;in.now=now;
  in.control=control.view();if(estimateAvailable)in.estimate=estimate;in.deliveryState=queue.state();
  waveform::Track t{{key.uuid,key.session,key.epoch},1000,0,1000000,{{0,1000000,0.5F,true},{1,2000000,-0.5F,true}}};
  in.wave=d::WaveInput{key,estimate.seq,1,100,100,50,waveform::build({{1000000,3000000,2},t,t,100,100,50})};
  return {d::build(in),ticket,queue.delivered_sequence()};
 }
 void notify(std::uint64_t sequence){observedNotification=sequence;++backend.revision;auto r=queue.enqueue({token,q::Kind::Notification,sequence});assert(r==q::Result::Accepted||r==q::Result::Overflow||r==q::Result::Exhausted);}
 void drain(){while(auto e=queue.pop()){assert(e->kind==q::Kind::Notification);control.externalChange();estimateAvailable=false;}}
};
int main(){unsigned n=0;
 {Owner o;d::Consumer c;auto p=o.packet();assert(c.receive(p,o.context(),20));auto v=c.present(o.context(),21);assert(v.snapshot&&v.snapshot->target==50&&v.snapshot->waveforms.size()==4);assert(o.backend.writes==0);++n;}
 {Owner o;d::Consumer c;auto p=o.packet();assert(c.receive(p,o.context(),20));assert(c.present(o.context(),999).snapshot);auto v=c.present(o.context(),1000);assert(!v.snapshot&&v.hidden==d::Hidden::Expired);assert(!c.present(o.context(),1001).snapshot);++n;}
 {Owner o;d::Consumer c;auto old=o.packet();o.estimate.seq=2;assert(o.control.acceptEstimate(o.estimate));assert(o.control.propose(21));auto newer=o.packet(21,22);assert(c.receive(newer,o.context(),22));assert(!c.receive(old,o.context(),23));assert(c.present(o.context(),24).snapshot->sequence==2);++n;}
 {Owner o;d::Consumer c;auto p=o.packet();assert(c.receive(p,o.context(),20));assert(!c.receive(p,o.context(),21));assert(c.present(o.context(),22).snapshot==p.snapshot);++n;}
 {Owner o;d::Consumer c;assert(c.receive(o.packet(),o.context(),20));o.notify(2);assert(o.queue.state()==q::State::Ready);assert(!o.context().notificationsDrained);assert(!c.present(o.context(),21).snapshot);assert(!c.receive(o.packet(21,22),o.context(),22));o.drain();assert(o.control.view().protectedState);assert(!c.present(o.context(),23).snapshot);assert(o.backend.writes==0);++n;}
 {Owner o;d::Consumer c;auto old=o.packet();assert(c.receive(old,o.context(),20));o.notify(2);o.drain();auto oldContext=o.context();oldContext.notificationSequence=1;assert(!c.receive(old,oldContext,21));assert(!c.present(o.context(),22).snapshot);++n;}
 {Owner o;d::Consumer c;assert(c.receive(o.packet(),o.context(),20));o.notify(2);o.notify(3);o.notify(4);assert(o.queue.state()==q::State::NeedsSnapshot);assert(!c.present(o.context(),21).snapshot);o.control.externalChange();o.estimateAvailable=false;auto t=*o.queue.request();assert(o.control.acknowledgeRead());assert(o.queue.recover({t,5,100}));assert(o.queue.acknowledge(t));o.ticket=t;assert(c.receive(o.packet(21,22),o.context(),22));assert(c.present(o.context(),23).snapshot->state==d::State::NoEstimate);assert(!c.present(o.context(),23).snapshot->recommendationEnabled);assert(!o.control.view().baseline);++n;}
 {Owner o;d::Consumer c;auto old=o.packet();assert(c.receive(old,o.context(),20));o.control.close();Owner next("source-B",2);assert(c.receive(next.packet(),next.context(),21));assert(!c.receive(old,o.context(),22));assert(c.present(next.context(),23).snapshot->key.uuid=="source-B");++n;}
 {Owner o;d::Consumer c;auto old=o.packet();assert(c.receive(old,o.context(),20));auto ctx=o.context();ctx.key.epoch=2;ctx.ticket.key.epoch=2;ctx.ticket.generation=2;assert(!c.present(ctx,21).snapshot);assert(!c.receive(old,ctx,22));++n;}
 {Owner o;d::Consumer c;assert(c.receive(o.packet(),o.context(),20));o.control.close();assert(!c.present(o.context(),21).snapshot);auto ctx=o.context();ctx.controlClosed=false;assert(!c.receive(o.packet(21,22),ctx,22));o.queue.shutdown();assert(!c.present(o.context(),23).snapshot);++n;}
 {Owner o;d::Consumer c;auto p=o.packet();assert(c.receive(p,o.context(),20));auto v=c.present(o.context(),19);assert(!v.snapshot&&v.hidden==d::Hidden::ClockFault);assert(!c.receive(o.packet(22,23),o.context(),23));++n;}
 {Owner o;d::Consumer c;auto p=o.packet();assert(!c.receive(p,o.context(),19));assert(c.receive(p,o.context(),20));auto bad=p;bad.notificationSequence=2;assert(!c.receive(bad,o.context(),21));++n;}
 {Owner o;d::Consumer c;auto p=o.packet();assert(c.receive(p,o.context(),20));auto ctx=o.context();++ctx.ticket.generation;assert(!c.present(ctx,21).snapshot);assert(!c.receive(p,ctx,22));assert(!c.present(o.context(),23).snapshot);++n;}
 {Owner o;d::Consumer c;auto p=o.packet();assert(c.receive(p,o.context(),20));auto newer=o.packet(11,21);assert(c.receive(newer,o.context(),21));assert(!c.receive(p,o.context(),22));assert(c.present(o.context(),23).snapshot==newer.snapshot);++n;}
 {Owner o;d::Consumer c;auto p=o.packet();auto ctx=o.context();ctx.ticket.key.session=9;assert(!c.receive(p,ctx,20));ctx=o.context();ctx.notificationSequence=UINT64_MAX;assert(!c.receive(p,ctx,21));++n;}
 {Owner o;d::Consumer c;assert(c.receive(o.packet(),o.context(),20));o.notify(UINT64_MAX);assert(o.queue.state()==q::State::NeedsNewKey);assert(!c.present(o.context(),21).snapshot);++n;}
 {Owner o;d::Consumer c;auto oldContext=o.context();assert(c.receive(o.packet(),oldContext,20));o.notify(2);assert(!c.present(o.context(),21).snapshot);assert(!c.receive(o.packet(21,22),oldContext,22));assert(!c.present(oldContext,23).snapshot);o.drain();assert(!c.present(o.context(),24).snapshot);++n;}
 {Owner o;d::Consumer c;auto old=o.packet();assert(c.receive(old,o.context(),20));o.estimate.seq=2;o.estimate.valid=false;o.estimate.active=false;o.estimate.confident=false;assert(o.control.acceptEstimate(o.estimate));auto invalid=o.packet(21,22);assert(c.receive(invalid,o.context(),22));auto view=c.present(o.context(),23);assert(view.snapshot&&view.snapshot->state==d::State::Silent&&!view.snapshot->delay&&view.snapshot->waveforms.size()==2);assert(!c.receive(old,o.context(),24));assert(!o.control.propose(24)&&o.backend.writes==0);++n;}
 {Owner o;d::Consumer c;auto old=o.packet();assert(c.receive(old,o.context(),20));c.discardSnapshot();assert(!c.present(o.context(),21).snapshot);assert(!c.receive(old,o.context(),22));o.estimate.seq=2;assert(o.control.acceptEstimate(o.estimate));assert(c.receive(o.packet(23,24),o.context(),24));assert(c.present(o.context(),25).snapshot->sequence==2);++n;}
 std::cout<<"PASS "<<n<<" serialized display-consumer groups; actual Control/queue/projection; no OBS/Qt/RT concurrency; no writes\n";
}
