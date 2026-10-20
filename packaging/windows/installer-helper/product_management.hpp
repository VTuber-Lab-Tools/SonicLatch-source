#pragma once
#include "product_windows_store.hpp"
namespace soniclatch::product::win {
inline Image managementImage(const std::optional<Bytes>& b){return b?Image{Ref{hashBytes(*b),b->size()}}:Image{};}
// Registered is terminal for this exact tx/plan. Authenticate existing evidence
// before any self-helper replacement or sealed-slot replay; never write here.
template<class IO,class Marker,class VerifyRegistration>
bool registeredManagementComplete(IO& io,const Plan& p,const Marker& m,const std::optional<Bytes>& helper,const std::optional<Bytes>& uninstaller,VerifyRegistration verifyRegistration){
 if(m.state!="registered"||m.tx!=p.transactionId)return false;
 need(m.digest==hashBytes(encode(p)),"registered plan digest mismatch; retained");
 need(decodeJournal(io.readJournal(),p,hashBytes(encode(p))).state==State::Committed,"registered management requires committed journal");
 need(managementImage(io.readReceipt())==p.newReceipt,"registered receipt changed; retained");
 for(const auto& row:p.steps)need(managementImage(io.readOwned(row.path))==row.after,"registered payload changed; retained");
 need(helper&&hashBytes(*helper)==m.helper&&uninstaller&&!m.uninstaller.empty()&&hashBytes(*uninstaller)==m.uninstaller,"registered management files changed; retained");
 verifyRegistration();return true;
}
}
