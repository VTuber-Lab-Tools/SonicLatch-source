#include "readback_guard.hpp"
#include <iostream>
#include <stdexcept>
using namespace pitatuner;using namespace pita;
namespace {unsigned checks{};void c(bool value){++checks;if(!value)throw std::runtime_error("readback/parser guard regression");}
struct BackendFake:Backend{Ns value=120000000;uint64_t revision{};unsigned reads{},writes{};bool fail{},notifyDuringRead{};
 std::optional<Ns> read()override{++reads;if(notifyDuringRead){notifyDuringRead=false;++revision;}return fail?std::optional<Ns>{}:value;}
 uint64_t externalRevision()const override{return revision;}void write(Ns n)override{++writes;value=n;}
};}
int main(){
 for(const auto& pair:std::initializer_list<std::pair<const char*,Ns>>{{"0",0},{"-0.000000",0},{"+1.000001",1000001},{"-0.000001",-1},{"0.000001",1},{"1",1000000},{"-5",-5000000},{"120.000000",120000000},{"9223372036854.775807",INT64_MAX},{"-9223372036854.775808",INT64_MIN}})c(parse_ms(pair.first)==pair.second);
 for(const char* text:{"","+","-",".1","1.","1.0000001","1e3","NaN","Inf","1 ms"," 1","1 ","1,5","1_000","\xEF\xBC\x91","\xE2\x88\x92" "1","9223372036854.775808","-9223372036854.775809","9223372036855","-9223372036855"})c(!parse_ms(text));
 DeliveryGate ids;Key key{"A",1,1};BackendFake backend;ControlSession control(backend,key,{},ids);
 const auto before=backend.reads;auto read=readback::acknowledge(backend,control,key);c(read==120000000&&backend.reads==before+1&&backend.writes==0);c(!control.view().baseline&&!control.view().proposal&&!control.view().canUndo);
 // Same-value external notification is an invalidated observation, not a new
 // permission merely because the read value happens to match the old value.
 backend.notifyDuringRead=true;c(!readback::acknowledge(backend,control,key));c(control.view().protectedState&&!control.view().baseline&&!control.view().proposal&&!control.view().canUndo&&backend.writes==0);
 c(readback::acknowledge(backend,control,key)==120000000&&!control.view().protectedState);c(!control.view().baseline&&!control.view().canUndo);
 const auto stableReads=backend.reads;for(Key wrong:{Key{"B",1,1},Key{"A",2,1},Key{"A",1,2}})c(!readback::acknowledge(backend,control,wrong));c(backend.reads==stableReads&&backend.writes==0);
 backend.fail=true;c(!readback::acknowledge(backend,control,key));c(!control.view().offset&&control.view().protectedState);backend.fail=false;c(readback::acknowledge(backend,control,key)==120000000);
 c(control.deliver({key,1,Operation::Step,1000000})==Result::Rejected&&backend.writes==0); // Range remains unverified.
 control.close();const auto closedReads=backend.reads;c(!readback::acknowledge(backend,control,key));c(backend.reads==closedReads&&backend.writes==0);
 // Read-only display/selection/external-follow cases; no OBS runtime grant.
 {BackendFake b;DeliveryGate gate;Key a{"followA",11,1};ControlSession session(b,a,{-500000000,500000000,1,true},gate);
  c(readback::follow(b,session,a)==120000000);c(session.deliver({a,1,Operation::Manual,0})==Result::Applied);c(session.view().canUndo);
  c(readback::follow(b,session,a)==0&&session.view().canUndo); // unchanged redisplay retains Undo
  ++b.revision;c(readback::follow(b,session,a,true)==0&&!session.view().canUndo);
  ++b.revision;b.value=42000000;session.externalChange();c(readback::follow(b,session,a)==42000000);c(!session.view().baseline&&!session.view().proposal&&!session.view().canUndo);
  c(readback::follow(b,session,a,true)==42000000&&!session.view().canUndo);
  const auto writes=b.writes;b.notifyDuringRead=true;c(!readback::follow(b,session,a));c(session.view().protectedState&&b.writes==writes);
  b.fail=true;c(!readback::follow(b,session,a));b.fail=false;c(readback::follow(b,session,a)==42000000);
  session.close();c(!readback::follow(b,session,a));Key other{"followB",12,1};ControlSession next(b,other,{},gate);c(!readback::follow(b,next,a));c(readback::follow(b,next,other)==42000000&&b.writes==writes);
 }
 std::cout<<"PASS readonly Read/parser checks="<<checks<<" actualWrites=0 baselineMode=Unchosen productRange=Unverified\n";
}
