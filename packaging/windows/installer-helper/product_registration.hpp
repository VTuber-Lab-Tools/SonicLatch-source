#pragma once
#include "product_windows_store.hpp"
#include <sddl.h>
#include <functional>

namespace soniclatch::product::win {
// One machine registration key; never enumerate/delete other products.
inline constexpr wchar_t uninstallKey[]=L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\SonicLatch";
struct RegKey {
 HKEY value{};void close(){if(value)RegCloseKey(std::exchange(value,nullptr));}~RegKey(){close();}
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
inline constexpr wchar_t uninstallParent[]=L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
inline Bytes registrationIntent(const Plan& plan,const fs::path& metadata,const std::string& stage){
 const auto prefix="SonicLatch-stage-"+plan.transactionId+"-";need(stage.starts_with(prefix)&&hex(stage.substr(prefix.size()),32),"bounded registration stage name");
 Writer values;for(const auto& [name,value]:registration(plan,metadata))for(const auto* text:{&name,&value}){values.number(text->size());for(wchar_t c:*text)values.number(static_cast<unsigned short>(c));}
 Writer out;out.text("SonicLatch-registration-rename-v2");out.text(plan.transactionId);out.text(hashBytes(encode(plan)));out.text(hashBytes(values.b));out.text(stage);return out.b;
}
inline std::string registrationStage(const Bytes& raw,const Plan& plan,const fs::path& metadata){
 Reader r{raw};need(r.text()=="SonicLatch-registration-rename-v2","unsupported registration intent strategy retained");need(r.text()==plan.transactionId&&r.text()==hashBytes(encode(plan)),"registration intent binding mismatch");r.text();auto stage=r.text();r.end();need(raw==registrationIntent(plan,metadata,stage),"registration intent mismatch retained");return stage;
}
struct RegistrationIntentIO {
 std::function<std::optional<Bytes>()> read;
 std::function<void(const Image&,const Bytes&)> write;
};
inline void exactRegistration(HKEY key,const Plan& plan,const fs::path& metadata){
 protectedAcl(key,false,SE_REGISTRY_KEY);DWORD children{},values{};
 need(RegQueryInfoKeyW(key,nullptr,nullptr,nullptr,&children,nullptr,nullptr,&values,nullptr,nullptr,nullptr,nullptr)==ERROR_SUCCESS&&children==0&&values==7,"incomplete/unknown registration retained");
 for(const auto& [name,value]:registration(plan,metadata))need(regString(key,name.c_str())==std::optional<std::wstring>{value},"registration recovery mismatch");
}
inline bool ownershipName(const std::wstring& name){return name==L"SonicLatchOwner"||name==L"SonicLatchRoot"||name==L"SonicLatchManifest";}
inline void exactOwnershipStage(HKEY key,const Plan& plan,const fs::path& metadata){
 protectedAcl(key,false,SE_REGISTRY_KEY);DWORD children{},count{};need(RegQueryInfoKeyW(key,nullptr,nullptr,nullptr,&children,nullptr,nullptr,&count,nullptr,nullptr,nullptr,nullptr)==ERROR_SUCCESS&&!children&&count==3,"registration stage must have exactly three ownership values");
 for(const auto& [name,value]:registration(plan,metadata))if(ownershipName(name))need(regString(key,name.c_str())==std::optional<std::wstring>{value},"registration stage mismatch retained");
}
inline void validatePartialFinal(HKEY key,const Plan& plan,const fs::path& metadata){
 protectedAcl(key,false,SE_REGISTRY_KEY);DWORD children{},count{},known{};need(RegQueryInfoKeyW(key,nullptr,nullptr,nullptr,&children,nullptr,nullptr,&count,nullptr,nullptr,nullptr,nullptr)==ERROR_SUCCESS&&!children,"unknown registration children retained");
 for(const auto& [name,value]:registration(plan,metadata)){auto actual=regString(key,name.c_str());if(actual){++known;need(*actual==value,"registration recovery mismatch");}else need(!ownershipName(name),"registration ownership missing");}
 need(count==known,"unknown registration values retained");
}
inline void finishRegistration(HKEY key,const Plan& plan,const fs::path& metadata){
 validatePartialFinal(key,plan,metadata);const auto expected=registration(plan,metadata);
 for(const auto& [name,value]:expected)if(name!=L"DisplayName"&&!regString(key,name.c_str()))setString(key,name.c_str(),value);
 if(!regString(key,L"DisplayName"))setString(key,L"DisplayName",expected.at(L"DisplayName")); // visibility last
 exactRegistration(key,plan,metadata);need(RegFlushKey(key)==ERROR_SUCCESS,"registration flush");exactRegistration(key,plan,metadata);
}
inline void publishRegistration(const Plan& plan,const fs::path& metadata,const std::string& priorVersion,RegistrationIntentIO* intents=nullptr){
 need(plan.operation!=Operation::Uninstall,"uninstall cannot publish registration");
 RegKey key;const auto access=READ_CONTROL|KEY_QUERY_VALUE|KEY_SET_VALUE|KEY_WOW64_64KEY;
 auto status=RegOpenKeyExW(HKEY_LOCAL_MACHINE,uninstallKey,0,access,&key.value);
 need(status==ERROR_SUCCESS||status==ERROR_FILE_NOT_FOUND,"registration open");
 auto intent=intents?intents->read():std::optional<Bytes>{};const auto stage=intent?registrationStage(*intent,plan,metadata):std::string{};
 if(status==ERROR_SUCCESS){
  if(intent){RegKey remaining;const auto path=std::wstring(uninstallParent)+L"\\"+widenAscii(stage);const auto check=RegOpenKeyExW(HKEY_LOCAL_MACHINE,path.c_str(),0,READ_CONTROL|KEY_QUERY_VALUE|KEY_WOW64_64KEY,&remaining.value);need(check==ERROR_FILE_NOT_FOUND,"both registration stage/final present or stage observation failed; retained");finishRegistration(key.value,plan,metadata);return;}
  protectedAcl(key.value,false,SE_REGISTRY_KEY);
  requireRegistrationOwner(key.value,plan);
  Plan prior=plan;prior.newManifest=plan.oldManifest;prior.version=priorVersion.empty()?plan.version:priorVersion;
  // Existing version is bound to the authenticated prior receipt by the host.
  auto before=registration(prior,metadata),after=registration(plan,metadata);
  DWORD known{},children{},count{};
  for(const auto& [name,value]:after){auto actual=regString(key.value,name.c_str());if(actual)++known;
   need(!actual||*actual==value||*actual==before.at(name),"modified registration value retained");
  }
  need(RegQueryInfoKeyW(key.value,nullptr,nullptr,nullptr,&children,nullptr,nullptr,&count,nullptr,nullptr,nullptr,nullptr)==ERROR_SUCCESS&&!children&&count==known,"unknown registration values/children retained");
 // Existing owned-key updates retain their previous validation and writes.
 setString(key.value,L"SonicLatchOwner",L"SonicLatch-machine-v1");
 setString(key.value,L"SonicLatchRoot",widenAscii(plan.rootIdentity));
 setString(key.value,L"SonicLatchManifest",widenAscii(plan.newManifest));
 for(const auto& [name,value]:registration(plan,metadata))if(name!=L"DisplayName")setString(key.value,name.c_str(),value);
 setString(key.value,L"DisplayName",L"SonicLatch");
 need(RegFlushKey(key.value)==ERROR_SUCCESS,"registration flush");
 exactRegistration(key.value,plan,metadata);
  return;
 }
 need(intents!=nullptr,"new registration requires protected intent");
 // The parent is opened, never created or assigned different permissions.
 RegKey parent;need(RegOpenKeyExW(HKEY_LOCAL_MACHINE,uninstallParent,0,KEY_READ|KEY_CREATE_SUB_KEY|KEY_WOW64_64KEY,&parent.value)==ERROR_SUCCESS,"existing 64-bit Uninstall parent unavailable");
 RegKey created;std::string allocated=stage;
 if(!intent){
  std::array<unsigned char,16> nonce{};need(BCryptGenRandom(nullptr,nonce.data(),static_cast<ULONG>(nonce.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)==0,"registration stage identity");allocated="SonicLatch-stage-"+plan.transactionId+"-";for(auto c:nonce){allocated+="0123456789abcdef"[c>>4];allocated+="0123456789abcdef"[c&15];}
  DWORD disposition{};NewRegistrationSecurity security;need(RegCreateKeyExW(parent.value,widenAscii(allocated).c_str(),0,nullptr,REG_OPTION_NON_VOLATILE,access|KEY_WRITE|DELETE,&security.attributes,&created.value,&disposition)==ERROR_SUCCESS,"registration stage create");need(disposition==REG_CREATED_NEW_KEY,"existing registration stage retained");protectedAcl(created.value,false,SE_REGISTRY_KEY);
  // An interrupted unsealed stage has no DisplayName and is never adopted or
  // deleted by a later attempt. Each new attempt allocates a distinct name.
  for(const auto& [name,value]:registration(plan,metadata))if(ownershipName(name))setString(created.value,name.c_str(),value);
  exactOwnershipStage(created.value,plan,metadata);need(RegFlushKey(created.value)==ERROR_SUCCESS,"registration stage flush");exactOwnershipStage(created.value,plan,metadata);
  const auto sealed=registrationIntent(plan,metadata,allocated);intents->write({},sealed);need(intents->read()==std::optional<Bytes>{sealed},"registration intent readback");
 }else{const auto path=std::wstring(uninstallParent)+L"\\"+widenAscii(allocated);need(RegOpenKeyExW(HKEY_LOCAL_MACHINE,path.c_str(),0,access|KEY_WRITE|DELETE,&created.value)==ERROR_SUCCESS,"sealed registration stage missing; retained");exactOwnershipStage(created.value,plan,metadata);}
 // New final only. No overwrite/delete and no TxR fallback. A failed rename is
 // reconciled on a later invocation from the sealed intent and both key names.
 const auto renamed=RegRenameKey(created.value,nullptr,L"SonicLatch");need(renamed==ERROR_SUCCESS,"registration stage rename failed; inspect/retry from intent");created.close();
 status=RegOpenKeyExW(HKEY_LOCAL_MACHINE,uninstallKey,0,access,&key.value);
 need(status==ERROR_SUCCESS,"renamed registration missing; retained");finishRegistration(key.value,plan,metadata);
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
