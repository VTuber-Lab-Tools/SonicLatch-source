#pragma once
#include <stdexcept>
namespace soniclatch::product::win {
// Process-local conservative boundary, never persisted as transaction state.
inline bool protectedMutationMayHaveStarted=false;
inline void markProtectedMutationRisk(){protectedMutationMayHaveStarted=true;}
struct PreflightBlocked:std::runtime_error{using std::runtime_error::runtime_error;};
inline int blockedExitCode(){return protectedMutationMayHaveStarted?31:40;}
} // namespace soniclatch::product::win
