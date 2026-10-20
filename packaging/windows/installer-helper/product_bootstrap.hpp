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
  need(IsUserAnAdmin(),"normal installer requires elevated administrator");obsStopped();
  PSECURITY_DESCRIPTOR raw{};need(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FRFX;;;BU)",SDDL_REVISION_1,&raw,nullptr),"bootstrap security descriptor");
  std::unique_ptr<void,decltype(&LocalFree)> security(raw,LocalFree);SECURITY_ATTRIBUTES sa{sizeof(sa),raw,FALSE};
  // Pin drive and ProgramData first; no environment-variable root override.
  for(const auto& path:{base.root_path(),base})pin(path,false);
  for(const auto& path:{base/L"obs-studio",root,base/L"SonicLatch",metadata,metadata/L"transactions"}){
   if(create&&!CreateDirectoryW(path.c_str(),&sa))need(GetLastError()==ERROR_ALREADY_EXISTS,"bootstrap create refused");
   pin(path,path==root||path==base/L"SonicLatch"||path==metadata||path==metadata/L"transactions");
  }
 }
 void pin(const fs::path& path,bool managed){
  Handle h(CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES|READ_CONTROL,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
  auto i=information(h.value);need(i.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY,"bootstrap directory");need(equalPath(finalPath(h.value),path),"bootstrap alias refused");if(path!=path.root_path())protectedAcl(h.value,!managed);pins.push_back(std::move(h));
 }
 void releaseDirectory(const fs::path& path){for(auto& h:pins)if(h.value!=INVALID_HANDLE_VALUE&&equalPath(finalPath(h.value),path))h.close();}
 std::string rootIdentity()const {for(const auto& h:pins)if(h.value!=INVALID_HANDLE_VALUE&&equalPath(finalPath(h.value),root))return identity(h.value);throw std::runtime_error("root not pinned");}
};
} // namespace soniclatch::product::win
