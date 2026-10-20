// Four layout-specific plan validations; no installer or native Store writes.
#include "product_windows_store.hpp"
#include <iostream>
using namespace soniclatch::product;using namespace soniclatch::product::win;
#include "migration_known_inputs.generated.hpp"
Bytes receiptFor(const KnownRelease& release){Writer w;w.text("SonicLatch-owned-receipt-v1");for(const auto* s:{&fixtureRootIdentity,&release.manifest,&release.source,&release.productId,&release.moduleId,&release.version})w.text(*s);return w.b;}
int main(){try{unsigned cases{};for(auto operation:{Operation::Install,Operation::Repair,Operation::Uninstall,Operation::Upgrade}){
 auto target=newRelease();std::optional<KnownRelease> old=operation==Operation::Install?std::nullopt:std::optional<KnownRelease>{operation==Operation::Upgrade?previousBinRelease():target};std::optional<KnownRelease> next=operation==Operation::Uninstall?std::nullopt:std::optional<KnownRelease>{target};const auto& identity=next?*next:*old;
 Plan p;p.transactionId=hashBytes("bin64 validation "+std::to_string(static_cast<unsigned>(operation))).substr(0,32);p.rootIdentity=fixtureRootIdentity;p.moduleId=identity.moduleId;p.productId=identity.productId;p.version=identity.version;p.sourceIdentity=identity.source;p.operation=operation;
 if(old){auto raw=receiptFor(*old);p.oldManifest=old->manifest;p.oldReceipt=Ref{hashBytes(raw),raw.size()};p.predecessorEvidence=hashBytes(raw);}if(next){auto raw=receiptFor(*next);p.newManifest=next->manifest;p.newReceipt=Ref{hashBytes(raw),raw.size()};}
 std::set<std::string> paths;if(old)for(const auto& [name,image]:old->owned){(void)image;paths.insert(name);}if(next)for(const auto& [name,image]:next->owned){(void)image;paths.insert(name);}
 for(const auto& name:paths){Image before,after;if(old){auto found=old->owned.find(name);if(found!=old->owned.end())before=found->second;}if(next){auto found=next->owned.find(name);if(found!=next->owned.end())after=found->second;}p.steps.push_back({name,before,after});}
 need(p.steps.size()==43&&std::none_of(p.steps.begin(),p.steps.end(),[](const Step& s){return s.path=="SonicLatch/SonicLatch.dll";}),"bin64-only plan required");KnownTransition policy{operation,p.rootIdentity,p.predecessorEvidence,old,next,p.oldReceipt,p.newReceipt};validate(p);authorizeTransition(p,policy);need(encode(decodePlan(encode(p)))==encode(p),"plan serialization");
 auto missing=p;std::erase_if(missing.steps,[](const Step& s){return s.path=="SonicLatch/bin/64bit/SonicLatch.dll";});bool refused=false;try{validate(missing);}catch(const std::exception& e){refused=std::string(e.what())=="DLL ownership row required";}need(refused,"ownership row remains mandatory");++cases;
 }
 auto both=newRelease();both.owned.emplace("SonicLatch/SonicLatch.dll",both.owned.at("SonicLatch/bin/64bit/SonicLatch.dll"));bool refused=false;try{validateKnown(both);}catch(...){refused=true;}need(refused,"exact-one-layout catalog rule retained");std::cout<<"PASS 4 bin64-only operations: install, repair, uninstall, bin64-to-bin64 upgrade; validate/auth/roundtrip + missing-DLL refusal for each; dual-layout catalog refused. Plan validation only, no native Setup or mutation.\n";return cases==4?0:1;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
