#include "product_preflight.hpp"
#include <iostream>
using namespace soniclatch::product::win;
int main(){if(blockedExitCode()!=40)return 1;try{throw PreflightBlocked("OBS must be closed");}catch(const PreflightBlocked&){if(blockedExitCode()!=40)return 2;}markProtectedMutationRisk();try{throw PreflightBlocked("OBS started after mutation boundary");}catch(const PreflightBlocked&){if(blockedExitCode()!=31)return 3;}std::cout<<"PASS helper phase classification: no-change preflight40; after possible mutation31; phase cannot revert. No native mutation/UI performed.\n";return 0;}
