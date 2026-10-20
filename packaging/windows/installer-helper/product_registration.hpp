#pragma once
#include "product_windows_store.hpp"
#include <sddl.h>

namespace soniclatch::product::win {
// One machine registration key; never enumerate/delete other products.
inline constexpr wchar_t uninstallKey[]=L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\SonicLatch";
struct RegKey {
 HKEY value{};~RegKey(){if(value)RegCloseKey(value);}
 RegKey()=default;RegKey(const RegKey&)=delete;RegKey& operator=(const RegKey&)=delete;
};
struct NewRegistrationSecurity {
 PSECURITY_DESCRIPTOR descriptor{};
 SECURITY_ATTRIBUTES attributes{};
 NewRegistrationSecurity(){
  // Atomic creation security for this product's new key only. Existing keys
  // and the shared Uninstall parent retain their current security descriptor.
  need(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;;KA;;;SY)(A;;KA;;;BA)(A;;KR;;;BU)",SDDL_REVISION_1,&descriptor,nullptr)!=FALSE,"new registration security descriptor");
  attributes.nLength=sizeof(attributes);attributes.lpSecurityDescriptor=descriptor;
  attributes.bInheritHandle=FALSE;
 }
 ~NewRegistrationSecurity(){if(descriptor)LocalFree(descriptor);}
 NewRegistrationSecurity(const NewRegistrationSecurity&)=delete;
 NewRegistrationSecurity& operator=(const NewRegistrationSecurity&)=delete;
};
inline std::optional<std::wstring> regString(HKEY key,const wchar_t* name){
 DWORD type{},size{};auto code=RegQueryValueExW(key,name,nullptr,&type,nullptr,&size);
 if(code==ERROR_FILE_NOT_FOUND)return {};
 need(code==ERROR_SUCCESS&&type==REG_SZ&&size>=sizeof(wchar_t)&&size<=8192&&size%sizeof(wchar_t)==0,"registration type/size");
 std::wstring result(size/sizeof(wchar_t),L'\0');
 need(RegQueryValueExW(key,name,nullptr,&type,reinterpret_cast<BYTE*>(result.data()),&size)==ERROR_SUCCESS&&result.back()==L'\0',"registration read");
 result.pop_back();need(result.find(L'\0')==result.npos,"registration embedded NUL");return result;
}
inline std::wstring widenAscii(const std::string& s){std::wstring out;for(unsigned char c:s){need(c>=32&&c<127,"ASCII identity required");out+=static_cast<wchar_t>(c);}return out;}
inline void setString(HKEY key,const wchar_t* name,const std::wstring& value){
 need(value.size()<4096&&RegSetValueExW(key,name,0,REG_SZ,reinterpret_cast<const BYTE*>(value.c_str()),static_cast<DWORD>((value.size()+1)*sizeof(wchar_t)))==ERROR_SUCCESS,"registration write");
 need(regString(key,name)==std::optional<std::wstring>{value},"registration readback");
}
inline std::map<std::wstring,std::wstring> registration(const Plan& plan,const fs::path& metadata){
 auto exe=metadata/L"Uninstall.exe";
 return {{L"DisplayName",L"SonicLatch"},{L"DisplayVersion",widenAscii(plan.version)},
 {L"UninstallString",L"\""+exe.wstring()+L"\""},{L"InstallLocation",metadata.wstring()},
 {L"SonicLatchOwner",L"SonicLatch-machine-v1"},{L"SonicLatchRoot",widenAscii(plan.rootIdentity)},
 {L"SonicLatchManifest",widenAscii(plan.newManifest)}};
}
inline void requireRegistrationOwner(HKEY key,const Plan& plan){
 need(regString(key,L"SonicLatchOwner")==std::optional<std::wstring>{L"SonicLatch-machine-v1"}&&
 regString(key,L"SonicLatchRoot")==std::optional<std::wstring>{widenAscii(plan.rootIdentity)},"unowned registration retained");
 auto manifest=regString(key,L"SonicLatchManifest");
 need(manifest&&(manifest==widenAscii(plan.oldManifest)||manifest==widenAscii(plan.newManifest)),"registration manifest mismatch");
}
inline void publishRegistration(const Plan& plan,const fs::path& metadata,const std::string& priorVersion){
 need(plan.operation!=Operation::Uninstall,"uninstall cannot publish registration");
 RegKey key;DWORD disposition{};NewRegistrationSecurity creationSecurity;
 need(RegCreateKeyExW(HKEY_LOCAL_MACHINE,uninstallKey,0,nullptr,0,READ_CONTROL|KEY_QUERY_VALUE|KEY_SET_VALUE|KEY_WOW64_64KEY,&creationSecurity.attributes,&key.value,&disposition)==ERROR_SUCCESS,"registration open/create");
 protectedAcl(key.value,false,SE_REGISTRY_KEY);
 if(disposition!=REG_CREATED_NEW_KEY){
  requireRegistrationOwner(key.value,plan);
  Plan prior=plan;prior.newManifest=plan.oldManifest;prior.version=priorVersion.empty()?plan.version:priorVersion;
  // Existing version is bound to the authenticated prior receipt by the host.
  auto before=registration(prior,metadata),after=registration(plan,metadata);
  for(const auto& [name,value]:after){auto actual=regString(key.value,name.c_str());
   need(!actual||*actual==value||*actual==before.at(name),"modified registration value retained");
  }
 }
 // Owner/root first make interrupted registration identifiable. An interrupted
 // pre-owner creation is deliberately not acquired or silently deleted.
 setString(key.value,L"SonicLatchOwner",L"SonicLatch-machine-v1");
 setString(key.value,L"SonicLatchRoot",widenAscii(plan.rootIdentity));
 setString(key.value,L"SonicLatchManifest",widenAscii(plan.newManifest));
 for(const auto& [name,value]:registration(plan,metadata))setString(key.value,name.c_str(),value);
 need(RegFlushKey(key.value)==ERROR_SUCCESS,"registration flush");
}
inline void removeRegistration(const Plan& plan,const fs::path& metadata){
 need(plan.operation==Operation::Uninstall,"only uninstall removes registration");
 RegKey key;auto status=RegOpenKeyExW(HKEY_LOCAL_MACHINE,uninstallKey,0,READ_CONTROL|KEY_QUERY_VALUE|KEY_SET_VALUE|KEY_ENUMERATE_SUB_KEYS|DELETE|KEY_WOW64_64KEY,&key.value);
 if(status==ERROR_FILE_NOT_FOUND)return;need(status==ERROR_SUCCESS,"registration open");protectedAcl(key.value,false,SE_REGISTRY_KEY);requireRegistrationOwner(key.value,plan);
 Plan prior=plan;prior.newManifest=plan.oldManifest;auto expected=registration(prior,metadata);
 DWORD subkeys{},values{};need(RegQueryInfoKeyW(key.value,nullptr,nullptr,nullptr,&subkeys,nullptr,nullptr,&values,nullptr,nullptr,nullptr,nullptr)==ERROR_SUCCESS&&subkeys==0,"unknown registry subkeys retained");
 // Never recursive deletion. Unknown or changed values stop before any deletion.
 for(DWORD i=0;i<values;++i){std::array<wchar_t,4096> name{};DWORD len=static_cast<DWORD>(name.size());need(RegEnumValueW(key.value,i,name.data(),&len,nullptr,nullptr,nullptr,nullptr)==ERROR_SUCCESS,"registration enumeration");auto found=expected.find(std::wstring(name.data(),len));need(found!=expected.end()&&regString(key.value,found->first.c_str())==std::optional<std::wstring>{found->second},"unknown/changed registration retained");}
 // Deleting this verified single key is atomic; no partially cleared values.
 need(RegDeleteKeyExW(HKEY_LOCAL_MACHINE,uninstallKey,KEY_WOW64_64KEY,0)==ERROR_SUCCESS,"owned registration deletion");
}
} // namespace soniclatch::product::win
