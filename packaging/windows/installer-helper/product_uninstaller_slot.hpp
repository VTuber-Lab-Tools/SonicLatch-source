#pragma once
#include "product_windows_store.hpp"
namespace soniclatch::product::win {
struct SlotIntent {std::string tx,plan,helper,slot;};
inline Bytes encodeSlotIntent(const SlotIntent& s){Writer w;w.text("SonicLatch-uninstaller-slot-v1");for(const auto* value:{&s.tx,&s.plan,&s.helper,&s.slot})w.text(*value);return w.b;}
inline SlotIntent decodeSlotIntent(const Bytes& bytes){Reader r{bytes};need(r.text()=="SonicLatch-uninstaller-slot-v1","slot intent schema");SlotIntent s{r.text(),r.text(),r.text(),r.text()};r.end();need(hex(s.tx,32)&&hex(s.plan,64)&&hex(s.helper,64)&&hex(s.slot,32),"slot intent identity");return s;}
inline Bytes encodeSlotSeal(const Bytes& intent,const WindowsStore::Artifact& file){Writer w;w.text("SonicLatch-uninstaller-seal-v1");w.text(hashBytes(intent));w.text(file.id);w.ref(file.image);return w.b;}
inline WindowsStore::Artifact decodeSlotSeal(const Bytes& bytes,const Bytes& intent){Reader r{bytes};need(r.text()=="SonicLatch-uninstaller-seal-v1"&&r.text()==hashBytes(intent),"slot seal binding");auto id=r.text();auto image=r.ref();r.end();need(!id.empty()&&image.has_value(),"slot artifact seal");refValid(image);return {*image,id};}
inline Image recordRef(const std::optional<Bytes>& bytes){return bytes?Image{Ref{hashBytes(*bytes),bytes->size()}}:Image{};}
// Template only to exercise the identical state transitions in an in-memory
// model. The product host supplies WindowsStore with fixed machine paths.
template<class SlotStore> class UninstallerSlots {
 SlotStore& store;const Plan& plan;std::string helper;
 std::pair<SlotIntent,Bytes> active(){
  auto raw=store.readUninstallerSlotAnchor();need(raw.has_value(),"active uninstaller slot missing");auto s=decodeSlotIntent(*raw);
  need(s.tx==plan.transactionId&&s.plan==hashBytes(encode(plan))&&s.helper==helper,"slot transaction/helper mismatch");
  need(store.readSlotRecord(s.slot,"intent.bin")==raw,"slot intent readback mismatch");return {s,*raw};
 }
 public:
 UninstallerSlots(SlotStore& io,const Plan& p,const std::string& helperHash):store(io),plan(p),helper(helperHash){need(hex(helper,64),"slot helper hash");}
 WindowsStore::Artifact promote(const std::string& requested){
  auto [s,raw]=active();need(s.slot==requested,"inactive slot retained");auto seal=store.readSlotRecord(s.slot,"seal.bin");need(seal.has_value(),"NSIS completion seal missing");auto expected=decodeSlotSeal(*seal,raw);
  need(store.readSlotArtifact(s.slot)==std::optional<WindowsStore::Artifact>{expected},"changed original sealed stage retained");
  return store.promoteUninstallerSlot(s.slot,expected);
 }
 // true means a previous sealed artifact was promoted/reconciled; false means
 // a fresh slot was issued. Unsealed leftovers are preserved, never adopted.
 std::pair<std::string,bool> prepare(const std::string& fresh){
  need(hex(fresh,32),"fresh slot nonce");auto old=store.readUninstallerSlotAnchor();
  if(old){auto [s,raw]=active();(void)raw;if(store.readSlotRecord(s.slot,"seal.bin")){promote(s.slot);return {s.slot,true};}}
  need(!store.readUninstallerArtifact(),"unknown final uninstaller retained");
  const auto raw=encodeSlotIntent({plan.transactionId,hashBytes(encode(plan)),helper,fresh});store.createUninstallerSlot(fresh,raw);store.writeUninstallerSlotAnchor(recordRef(old),raw);return {fresh,false};
 }
 void ready(const std::string& requested){auto [s,raw]=active();(void)raw;need(s.slot==requested&&!store.readSlotRecord(s.slot,"seal.bin")&&!store.readSlotArtifact(s.slot)&&!store.readUninstallerArtifact(),"slot is not fresh for NSIS write");}
 void sealAfterNsisSuccess(const std::string& requested){
  auto [s,raw]=active();need(s.slot==requested&&!store.readSlotRecord(s.slot,"seal.bin")&&!store.readUninstallerArtifact(),"slot seal handoff refused");auto file=store.readSlotArtifact(s.slot);need(file.has_value(),"completed NSIS artifact missing");store.sealUninstallerSlot(s.slot,encodeSlotSeal(raw,*file));
 }
 bool hasSeal(){auto raw=store.readUninstallerSlotAnchor();if(!raw)return false;auto [s,intent]=active();(void)intent;return store.readSlotRecord(s.slot,"seal.bin").has_value();}
 WindowsStore::Artifact resumeSealed(){auto [s,raw]=active();(void)raw;return promote(s.slot);}
};
} // namespace soniclatch::product::win
