#pragma once
#include "product_trust.hpp"
namespace soniclatch::product {
// Build input, never loaded from a user-supplied JSON/CLI path. Immutable payload
// bytes are compiled into the helper. Only reviewed release inventories belong
// here. Source identity = SHA256(canonical(manifest.sourceContext)).
struct Catalog {
 bool packageApproved=false;
 std::string productId;
 KnownRelease current;
 std::vector<KnownRelease> acceptedPrevious;
 std::map<std::string,Bytes> blobs;
};
#ifdef SONICLATCH_REVIEWED_CATALOG
#include "product_release.generated.hpp" // defines inline const Catalog releaseCatalog
#else
inline const Catalog releaseCatalog{}; // default build has no write authority
#endif
}
