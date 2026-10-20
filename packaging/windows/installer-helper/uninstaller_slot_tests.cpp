#include "product_uninstaller_slot.hpp"
#include <iostream>
using namespace soniclatch::product;
using namespace soniclatch::product::win;
struct Model {
 using Artifact=WindowsStore::Artifact;
 struct Slot {Bytes intent;std::optional<Bytes> seal;std::optional<Artifact> stage;};
 std::map<std::string,Slot> slots;std::optional<Bytes> anchor;std::optional<Artifact> final;std::string fault;
 std::optional<Bytes> readUninstallerSlotAnchor(){return anchor;}
 void writeUninstallerSlotAnchor(const Image& before,const Bytes& bytes){need(recordRef(anchor)==before,"model anchor CAS");anchor=bytes;}
 void createUninstallerSlot(const std::string& id,const Bytes& intent){need(!slots.contains(id),"exclusive new slot");slots.emplace(id,Slot{intent,{},{}});}
 std::optional<Bytes> readSlotRecord(const std::string& id,const std::string& name){auto& s=slots.at(id);return name=="intent.bin"?std::optional<Bytes>{s.intent}:s.seal;}
 void sealUninstallerSlot(const std::string& id,const Bytes& bytes){if(fault=="seal-before")throw std::runtime_error("cut");slots.at(id).seal=bytes;if(fault=="seal-after")throw std::runtime_error("cut");}
 std::optional<Artifact> readSlotArtifact(const std::string& id){return slots.at(id).stage;}
 std::optional<Artifact> readUninstallerArtifact(){return final;}
 std::optional<Artifact> published,copy;std::vector<Artifact> unsealed;unsigned serial=10;
 Artifact promoteUninstallerSlot(const std::string& id,const Artifact& expected){
  need(slots.at(id).stage==std::optional<Artifact>{expected},"original seal retained");
  if(!published){need(!final,"unknown final retained");copy=Artifact{expected.image,"1:2:"+std::to_string(++serial)};if(fault=="copy-after"||fault=="publish-seal-before"){unsealed.push_back(*copy);copy.reset();throw std::runtime_error("cut");}published=copy;if(fault=="publish-seal-after")throw std::runtime_error("cut");}
  if(!copy){need(final==published,"published final mismatch");return *published;}
  need(!final&&copy==published,"both present or changed copy retained");if(fault=="promote-before")throw std::runtime_error("cut");final=copy;copy.reset();if(fault=="promote-after")throw std::runtime_error("cut");return *published;
 }
};
int main(){try{
 unsigned cases{},assertions{};const auto check=[&](bool ok,const char* why){need(ok,why);++assertions;};
 Plan p;p.transactionId=std::string(32,'1');p.rootIdentity="1:2:3";p.moduleId="SonicLatch";p.productId="fixture";p.version="fixture";p.sourceIdentity=std::string(64,'a');p.newManifest=std::string(64,'b');p.newReceipt=Ref{std::string(64,'c'),1};p.steps.push_back({"SonicLatch/SonicLatch.dll",{},Ref{std::string(64,'d'),1}});
 const auto a=std::string(32,'a'),b=std::string(32,'b'),helper=std::string(64,'e');const Model::Artifact file{{hashBytes("NSIS completed fixture"),22},"1:2:3"};
 const auto fails=[](auto&& fn){try{fn();return false;}catch(...){return true;}};
 {Model m;UninstallerSlots slots(m,p,helper);check(slots.prepare(a)==std::pair{a,false},"fresh slot issued");slots.ready(a);m.slots.at(a).stage=file;slots.sealAfterNsisSuccess(a);auto promoted=slots.promote(a);check(promoted==*m.published&&m.final==m.published&&m.slots.at(a).stage==file,"normal seal/promotion");check(slots.promote(a)==*m.published,"post-promotion replay recognizes exact final");++cases;}
 for(const auto* point:{"partial-write","completed-before-seal"}){Model m;UninstallerSlots slots(m,p,helper);slots.prepare(a);m.slots.at(a).stage=file;check(slots.prepare(b)==std::pair{b,false},"unsealed stage gets fresh retry slot");check(m.slots.at(a).stage==file&&!m.slots.at(a).seal&&!m.final,"unsealed prior artifact retained without adoption");slots.ready(b);m.slots.at(b).stage=file;slots.sealAfterNsisSuccess(b);slots.promote(b);check(m.final==m.published&&m.final->image==file.image&&m.final->id!=file.id,"fresh retry finishes recovery");(void)point;++cases;}
 {Model m;UninstallerSlots slots(m,p,helper);slots.prepare(a);m.slots.at(a).stage=file;m.fault="seal-before";check(fails([&]{slots.sealAfterNsisSuccess(a);}),"seal-before cut");m.fault.clear();check(slots.prepare(b)==std::pair{b,false},"no durable seal means fresh slot");m.slots.at(b).stage=file;slots.sealAfterNsisSuccess(b);slots.promote(b);check(m.final==m.published&&m.final->image==file.image&&m.final->id!=file.id,"seal-before recovery completes");++cases;}
 {Model m;UninstallerSlots slots(m,p,helper);slots.prepare(a);m.slots.at(a).stage=file;m.fault="seal-after";check(fails([&]{slots.sealAfterNsisSuccess(a);}),"seal-after cut");m.fault.clear();check(slots.prepare(b)==std::pair{a,true}&&m.final==m.published&&m.final->image==file.image&&m.final->id!=file.id,"durable seal resumes promotion to completion");++cases;}
 for(const auto* point:{"copy-after","publish-seal-before","publish-seal-after","promote-before","promote-after"}){Model m;UninstallerSlots slots(m,p,helper);slots.prepare(a);m.slots.at(a).stage=file;slots.sealAfterNsisSuccess(a);m.fault=point;check(fails([&]{slots.promote(a);}),"promotion cut");m.fault.clear();check(slots.prepare(b)==std::pair{a,true}&&m.final==m.published&&m.final->image==file.image&&m.final->id!=file.id&&m.slots.at(a).stage==file,"promotion replay completes by identity/hash");++cases;}
 {Model m;UninstallerSlots slots(m,p,helper);m.final=file;check(fails([&]{slots.prepare(a);})&&m.final==file,"unknown final preserved");++cases;}
 {Model m;UninstallerSlots slots(m,p,helper);slots.prepare(a);m.slots.at(a).stage=file;slots.sealAfterNsisSuccess(a);m.slots.at(a).stage->id="1:2:4";check(fails([&]{slots.promote(a);})&&!m.final,"same hash different identity refused");++cases;}
 {Model m;UninstallerSlots slots(m,p,helper);slots.prepare(a);m.slots.at(a).stage=file;slots.sealAfterNsisSuccess(a);slots.promote(a);m.final->image.sha256=hashBytes("changed");check(fails([&]{slots.prepare(b);})&&m.final.has_value(),"changed final retained");++cases;}
 {Model m;UninstallerSlots slots(m,p,helper);slots.prepare(a);m.slots.at(a).stage=file;slots.prepare(b);check(fails([&]{slots.sealAfterNsisSuccess(a);})&&!m.slots.at(a).seal,"inactive old stage cannot be sealed");++cases;}
 {Model m;UninstallerSlots slots(m,p,helper);slots.prepare(a);UninstallerSlots other(m,p,std::string(64,'f'));check(fails([&]{other.ready(a);}),"helper binding enforced");++cases;}
 for(bool both:{false,true}){Model m;UninstallerSlots slots(m,p,helper);slots.prepare(a);m.slots.at(a).stage=file;slots.sealAfterNsisSuccess(a);m.fault="publish-seal-after";check(fails([&]{slots.promote(a);}),"published copy baseline");m.fault.clear();if(both)m.final=m.copy;else m.copy->id="different-copy-id";auto copy=m.copy,final=m.final;check(fails([&]{slots.promote(a);})&&m.copy==copy&&m.final==final,"both present/changed copy preserved");++cases;}
 {const auto intent=encodeSlotIntent({p.transactionId,hashBytes(encode(p)),helper,a});const auto nsis=encodeSlotSeal(intent,file);UninstallerPublish copy{b,"1:2:100",file.image};auto seal=encodeUninstallerPublish(p.transactionId,hashBytes(encode(p)),a,nsis,copy);auto parsed=decodeUninstallerPublish(seal,p.transactionId,hashBytes(encode(p)),a,nsis);check(parsed.id==copy.id&&parsed.image==file.image&&copy.id!=file.id,"separate publish identity binding");check(fails([&]{decodeUninstallerPublish(seal,p.transactionId,hashBytes(encode(p)),b,nsis);})&&fails([&]{decodeUninstallerPublish(seal,p.transactionId,hashBytes(encode(p)),a,nsis+"changed");}),"cross-slot/NSIS-seal mismatch refused");++cases;}
 std::cout<<"PASS "<<cases<<" slot cases / "<<assertions<<" assertions; memory protocol + production publish seal codec, native bounded-copy/NSIS cleanup NOT RUN\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
