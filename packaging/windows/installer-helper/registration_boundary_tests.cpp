#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <map>
#include <string>
#include <vector>
#include <cstring>
namespace fake {
LSTATUS WINAPI create(HKEY,LPCWSTR,DWORD,LPWSTR,DWORD,REGSAM,const SECURITY_ATTRIBUTES*,PHKEY,LPDWORD);
LSTATUS WINAPI query(HKEY,LPCWSTR,LPDWORD,LPDWORD,LPBYTE,LPDWORD);
LSTATUS WINAPI set(HKEY,LPCWSTR,DWORD,DWORD,const BYTE*,DWORD);
LSTATUS WINAPI flush(HKEY);
LSTATUS WINAPI close(HKEY);
DWORD WINAPI security(HANDLE,SE_OBJECT_TYPE,SECURITY_INFORMATION,PSID*,PSID*,PACL*,PACL*,PSECURITY_DESCRIPTOR*);
}
// Execute the production registration function against an in-memory Win32
// boundary model. No HKEY is opened/created and no machine ACL is changed.
#define RegCreateKeyExW fake::create
#define RegQueryValueExW fake::query
#define RegSetValueExW fake::set
#define RegFlushKey fake::flush
#define RegCloseKey fake::close
#define GetSecurityInfo fake::security
#include "product_registration.hpp"
#undef RegCreateKeyExW
#undef RegQueryValueExW
#undef RegSetValueExW
#undef RegFlushKey
#undef RegCloseKey
#undef GetSecurityInfo
#include <iostream>
using namespace soniclatch::product;
using namespace soniclatch::product::win;
namespace fake {
struct Model {
 bool exists=false,failCreate=false,failFlush=false;
 unsigned failSetAt=0,createCalls=0,securityCalls=0,setCalls=0,closeCalls=0;
 std::wstring sddl=L"O:BAG:BAD:P(A;;KA;;;SY)(A;;KA;;;BA)(A;;KR;;;BU)";
 std::map<std::wstring,std::wstring> values;
 std::vector<std::wstring> writeOrder;
} model;
HKEY handle(){return reinterpret_cast<HKEY>(&model);}
LSTATUS WINAPI create(HKEY parent,LPCWSTR name,DWORD,LPWSTR,DWORD,REGSAM access,const SECURITY_ATTRIBUTES* attributes,PHKEY key,LPDWORD disposition){
 ++model.createCalls;
 if(parent!=HKEY_LOCAL_MACHINE||std::wstring(name)!=uninstallKey||!(access&KEY_WOW64_64KEY)||!attributes||!attributes->lpSecurityDescriptor)return ERROR_INVALID_PARAMETER;
 if(model.failCreate)return ERROR_ACCESS_DENIED;
 *key=handle();*disposition=model.exists?REG_OPENED_EXISTING_KEY:REG_CREATED_NEW_KEY;
 if(!model.exists){LPWSTR text{};if(!ConvertSecurityDescriptorToStringSecurityDescriptorW(attributes->lpSecurityDescriptor,SDDL_REVISION_1,OWNER_SECURITY_INFORMATION|GROUP_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,&text,nullptr))return ERROR_INVALID_SECURITY_DESCR;model.sddl=text;LocalFree(text);model.exists=true;}
 return ERROR_SUCCESS;
}
DWORD WINAPI security(HANDLE key,SE_OBJECT_TYPE type,SECURITY_INFORMATION,PSID* owner,PSID*,PACL* acl,PACL*,PSECURITY_DESCRIPTOR* raw){
 ++model.securityCalls;if(key!=handle()||type!=SE_REGISTRY_KEY)return ERROR_INVALID_HANDLE;
 if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(model.sddl.c_str(),SDDL_REVISION_1,raw,nullptr))return ERROR_INVALID_SECURITY_DESCR;
 BOOL def{},present{};if(!GetSecurityDescriptorOwner(*raw,owner,&def)||!GetSecurityDescriptorDacl(*raw,&present,acl,&def)){LocalFree(*raw);return ERROR_INVALID_SECURITY_DESCR;}return ERROR_SUCCESS;
}
LSTATUS WINAPI query(HKEY key,LPCWSTR name,LPDWORD,LPDWORD type,LPBYTE data,LPDWORD size){
 if(key!=handle())return ERROR_INVALID_HANDLE;auto i=model.values.find(name);if(i==model.values.end())return ERROR_FILE_NOT_FOUND;
 *type=REG_SZ;DWORD bytes=static_cast<DWORD>((i->second.size()+1)*sizeof(wchar_t));if(data&&*size<bytes)return ERROR_MORE_DATA;if(data)memcpy(data,i->second.c_str(),bytes);*size=bytes;return ERROR_SUCCESS;
}
LSTATUS WINAPI set(HKEY key,LPCWSTR name,DWORD,DWORD type,const BYTE* data,DWORD size){
 if(key!=handle()||type!=REG_SZ||size<sizeof(wchar_t))return ERROR_INVALID_PARAMETER;
 ++model.setCalls;if(model.failSetAt==model.setCalls)return ERROR_ACCESS_DENIED;
 model.values[name]=std::wstring(reinterpret_cast<const wchar_t*>(data),size/sizeof(wchar_t)-1);model.writeOrder.emplace_back(name);return ERROR_SUCCESS;
}
LSTATUS WINAPI flush(HKEY key){return key==handle()&&!model.failFlush?ERROR_SUCCESS:ERROR_ACCESS_DENIED;}
LSTATUS WINAPI close(HKEY key){if(key!=handle())return ERROR_INVALID_HANDLE;++model.closeCalls;return ERROR_SUCCESS;}
}
int main(){try{
 unsigned cases=0,assertions=0;const auto check=[&](bool ok,const char* why){need(ok,why);++assertions;};
 Plan plan;plan.operation=Operation::Install;plan.version="0.1.0";plan.rootIdentity="1:2:3";plan.newManifest=std::string(64,'a');plan.oldManifest=plan.newManifest;const fs::path metadata=L"C:\\ProgramData\\SonicLatch\\Installer";
 const auto invoke=[&](){try{publishRegistration(plan,metadata,"0.1.0");return std::string{};}catch(const std::exception& e){return std::string(e.what());}};
 const auto reset=[](){fake::model=fake::Model{};};
 reset();check(invoke().empty(),"new registration should succeed");check(fake::model.values==registration(plan,metadata),"complete values");check(fake::model.writeOrder.size()>=3&&fake::model.writeOrder[0]==L"SonicLatchOwner"&&fake::model.writeOrder[1]==L"SonicLatchRoot"&&fake::model.writeOrder[2]==L"SonicLatchManifest","ownership markers first");check(fake::model.sddl.find(L"D:P")!=std::wstring::npos,"atomic protected creation");++cases;
 reset();fake::model.failCreate=true;check(invoke()=="registration open/create","creation failure propagated");check(!fake::model.exists&&fake::model.setCalls==0&&fake::model.securityCalls==0,"creation failure has no writes/guard on invalid handle");++cases;
 reset();fake::model.exists=true;auto before=fake::model.sddl;check(invoke()=="unowned registration retained","empty existing key retained");check(fake::model.values.empty()&&fake::model.setCalls==0&&fake::model.sddl==before,"empty key values and ACL unchanged");++cases;
 reset();fake::model.exists=true;fake::model.values[L"SonicLatchOwner"]=L"OtherProduct";auto values=fake::model.values;check(invoke()=="unowned registration retained","foreign key rejected");check(fake::model.values==values&&fake::model.setCalls==0,"foreign key unchanged");++cases;
 reset();fake::model.exists=true;fake::model.values=registration(plan,metadata);fake::model.values[L"DisplayName"]=L"User edited";values=fake::model.values;check(invoke()=="modified registration value retained","modified known value rejected");check(fake::model.values==values&&fake::model.setCalls==0,"modified values unchanged");++cases;
 reset();fake::model.exists=true;fake::model.values=registration(plan,metadata);fake::model.values[L"SonicLatchManifest"]=std::wstring(64,L'b');values=fake::model.values;check(invoke()=="registration manifest mismatch","unknown manifest rejected");check(fake::model.values==values&&fake::model.setCalls==0,"unknown manifest unchanged");++cases;
 reset();fake::model.exists=true;fake::model.values=registration(plan,metadata);fake::model.sddl=L"O:BAG:BAD:P(A;;KA;;;SY)(A;;KA;;;BA)(A;;KR;;;BU)(A;;0x3001f;;;S-1-5-13)";before=fake::model.sddl;values=fake::model.values;check(invoke()=="untrusted write permission","actual writable ACE still rejected");check(fake::model.values==values&&fake::model.sddl==before&&fake::model.setCalls==0,"writable existing key not repaired");++cases;
 reset();fake::model.exists=true;fake::model.values=registration(plan,metadata);before=fake::model.sddl;check(invoke().empty(),"owned KEY_READ registration accepted");check(fake::model.sddl==before&&fake::model.values==registration(plan,metadata),"existing security untouched");++cases;
 reset();fake::model.failSetAt=1;check(invoke()=="registration write","first owner write failure propagated");check(fake::model.exists&&fake::model.values.empty()&&fake::model.closeCalls==1,"failed new key retained protected, handle closed");++cases;
 reset();fake::model.failSetAt=3;check(invoke()=="registration write","third marker failure propagated");check(fake::model.values.size()==2&&fake::model.values.count(L"SonicLatchOwner")&&fake::model.values.count(L"SonicLatchRoot"),"partial markers preserved, no implicit deletion");++cases;
 reset();fake::model.failFlush=true;check(invoke()=="registration flush","flush failure cannot claim success");check(fake::model.values==registration(plan,metadata)&&fake::model.closeCalls==1,"written values retained for recovery");++cases;
 std::cout<<"PASS "<<cases<<" boundary cases / "<<assertions<<" assertions; production function, in-memory Win32 model; no registry/ACL mutations\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
