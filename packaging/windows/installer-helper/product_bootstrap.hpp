#pragma once
#include "product_windows_store.hpp"
#include <sddl.h>
namespace soniclatch::product::win {
inline fs::path machineData(){PWSTR raw{};need(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData,KF_FLAG_DEFAULT,nullptr,&raw)),"ProgramData unavailable");std::unique_ptr<wchar_t,decltype(&CoTaskMemFree)> free(raw,CoTaskMemFree);return fs::path(raw);}
// Bootstrap owns only newly created directories. Existing security is inspected,
// never changed. Every ancestor is pinned until Bootstrap is destroyed.
class Bootstrap {
 std::vector<Handle> pins;
 public:
 const fs::path base=machineData();
 const fs::path root=base/L"obs-studio"/L"plugins";
 const fs::path metadata=base/L"SonicLatch"/L"Installer";
 explicit Bootstrap(bool create){
  if(!IsUserAnAdmin())throw PreflightBlocked("normal installer requires elevated administrator");obsStopped();
  if(create)markProtectedMutationRisk(); // OPEN_IF may create bootstrap directories.
  // Pin drive and ProgramData first; no environment-variable root override.
  for(const auto& path:{base.root_path(),base})pin(path);
  for(const auto& path:{base/L"obs-studio",root,base/L"SonicLatch",metadata,metadata/L"transactions"}){
   pin(path,create);
  }
 }
 void pin(const fs::path& path,bool create=false){
  for(const auto& h:pins)if(h.value!=INVALID_HANDLE_VALUE&&equalPath(finalPath(h.value),path)){inspectDirectory(h.value,path,base);return;}
  HANDLE parent=INVALID_HANDLE_VALUE;
  if(path!=path.root_path()){pin(path.parent_path());for(const auto& h:pins)if(h.value!=INVALID_HANDLE_VALUE&&equalPath(finalPath(h.value),path.parent_path())){parent=h.value;break;}}
  auto h=acquireDirectory(parent,path,base,create);pins.push_back(std::move(h));
 }
 void releaseDirectory(const fs::path& path){for(auto& h:pins)if(h.value!=INVALID_HANDLE_VALUE&&equalPath(finalPath(h.value),path))h.close();}
 std::string rootIdentity()const {for(const auto& h:pins)if(h.value!=INVALID_HANDLE_VALUE&&equalPath(finalPath(h.value),root))return identity(h.value);throw std::runtime_error("root not pinned");}
};
} // namespace soniclatch::product::win
