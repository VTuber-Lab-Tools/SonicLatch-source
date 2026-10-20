#pragma once
#include "product_transaction.hpp"
#include <set>
namespace soniclatch::product {
// These values must be populated by the host from authenticated, immutable
// release records or reviewed compiled-in identities. Never from CLI/receipt
// declarations alone. This module verifies consistency, not signatures.
struct KnownRelease {
 std::string manifest, source, productId, moduleId, version;
 std::map<std::string,Ref> owned;
};
struct KnownTransition {
 Operation operation;
 std::string rootIdentity, predecessorEvidence;
 std::optional<KnownRelease> oldRelease, newRelease;
 Image actualAuthenticatedOldReceipt, proposedAuthenticatedNewReceipt;
};
inline void validateKnown(const KnownRelease& r) {
 need(hex(r.manifest,64)&&hex(r.source,64)&&!r.productId.empty()&&!r.moduleId.empty()&&!r.version.empty(),"release identity incomplete");
 need(!r.owned.empty()&&r.owned.size()<=512&&(r.owned.contains("SonicLatch/SonicLatch.dll")!=r.owned.contains("SonicLatch/bin/64bit/SonicLatch.dll")),"release inventory needs exactly one known module layout");
 for(const auto& [path,ref]:r.owned){need(safePath(path),"release namespace");refValid(ref);}
}
inline void authorizeTransition(const Plan& p,const KnownTransition& known) {
 validate(p);need(p.operation==known.operation&&p.rootIdentity==known.rootIdentity,"operation/root policy disagreement");
 need(p.oldReceipt==known.actualAuthenticatedOldReceipt&&p.newReceipt==known.proposedAuthenticatedNewReceipt,"receipt policy disagreement");
 const KnownRelease* old=known.oldRelease?&*known.oldRelease:nullptr;
 const KnownRelease* next=known.newRelease?&*known.newRelease:nullptr;
 if(p.operation==Operation::Install)need(!old&&next,"install transition policy");
 else if(p.operation==Operation::Uninstall)need(old&&!next,"uninstall transition policy");
 else need(old&&next,"update/repair transition policy");
 if(old){validateKnown(*old);need(p.oldManifest==old->manifest&&p.predecessorEvidence==known.predecessorEvidence,"predecessor policy disagreement");}
 if(next){validateKnown(*next);need(p.newManifest==next->manifest,"candidate policy disagreement");}
 const auto& identity=next?*next:*old;
 need(p.productId==identity.productId&&p.moduleId==identity.moduleId&&p.version==identity.version&&p.sourceIdentity==identity.source,"product/source policy disagreement");
 if(old&&next)need(old->productId==next->productId&&old->moduleId==next->moduleId,"cross-product transition refused");
 std::set<std::string> required;
 if(old)for(const auto& [path,ref]:old->owned){(void)ref;required.insert(path);}
 if(next)for(const auto& [path,ref]:next->owned){(void)ref;required.insert(path);}
 need(required.size()==p.steps.size(),"plan omitted owned destination");
 for(const auto& s:p.steps){need(required.erase(s.path)==1,"unexpected/duplicate plan destination");
  Image prior,target;
  if(old){auto i=old->owned.find(s.path);if(i!=old->owned.end())prior=i->second;}
  if(next){auto i=next->owned.find(s.path);if(i!=next->owned.end())target=i->second;}
  need(s.after==target,"after image not authenticated inventory");
  // Missing prior owned files are valid absent baselines. Modified files never
  // acquire rollback authority from an untrusted observed hash.
  need(!s.before||(prior&&s.before==prior),"unowned/modified baseline refused");
 }
 need(required.empty(),"incomplete owned plan");
}
} // namespace soniclatch::product
