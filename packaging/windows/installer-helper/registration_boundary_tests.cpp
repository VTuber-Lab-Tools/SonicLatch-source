#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <map>
#include <string>
#include <cstring>
namespace fake {
LSTATUS WINAPI create(HKEY,LPCWSTR,DWORD,LPWSTR,DWORD,REGSAM,const SECURITY_ATTRIBUTES*,PHKEY,LPDWORD);
LSTATUS WINAPI query(HKEY,LPCWSTR,LPDWORD,LPDWORD,LPBYTE,LPDWORD);
LSTATUS WINAPI set(HKEY,LPCWSTR,DWORD,DWORD,const BYTE*,DWORD);
LSTATUS WINAPI flush(HKEY);LSTATUS WINAPI close(HKEY);LSTATUS WINAPI open(HKEY,LPCWSTR,DWORD,REGSAM,PHKEY);LSTATUS WINAPI rename(HKEY,LPCWSTR,LPCWSTR);
LSTATUS WINAPI info(HKEY,LPWSTR,LPDWORD,LPDWORD,LPDWORD,LPDWORD,LPDWORD,LPDWORD,LPDWORD,LPDWORD,LPDWORD,PFILETIME);
DWORD WINAPI security(HANDLE,SE_OBJECT_TYPE,SECURITY_INFORMATION,PSID*,PSID*,PACL*,PACL*,PSECURITY_DESCRIPTOR*);
}
// All Registry APIs used by publishRegistration are faked before inclusion.
#define RegCreateKeyExW fake::create
#define RegQueryValueExW fake::query
#define RegSetValueExW fake::set
#define RegFlushKey fake::flush
#define RegCloseKey fake::close
#define RegOpenKeyExW fake::open
#define RegRenameKey fake::rename
#define RegQueryInfoKeyW fake::info
#define GetSecurityInfo fake::security
#include "product_registration.hpp"
#undef RegCreateKeyExW
#undef RegQueryValueExW
#undef RegSetValueExW
#undef RegFlushKey
#undef RegCloseKey
#undef RegOpenKeyExW
#undef RegRenameKey
#undef RegQueryInfoKeyW
#undef GetSecurityInfo
#include <iostream>
using namespace soniclatch::product;using namespace soniclatch::product::win;
namespace fake {
using Values=std::map<std::wstring,std::wstring>;
struct Model {bool exists=false,parentFail=false,createCollision=false,renameBefore=false,renameAfter=false,renameCollision=false,intentBefore=false,intentAfter=false;unsigned sets=0,failSet=0,flushes=0,failFlush=0,renames=0,creates=0;Values final;std::map<std::wstring,Values> stages;std::optional<Bytes> intent;std::wstring lastWrite,sddl=L"O:BAG:BAD:P(A;;KA;;;SY)(A;;KA;;;BA)(A;;KR;;;BU)",badType;}m;
HKEY parent(){return reinterpret_cast<HKEY>(&m);}HKEY final(){return reinterpret_cast<HKEY>(&m.final);}HKEY h(Values& v){return reinterpret_cast<HKEY>(&v);}Values& values(HKEY k){if(k==final())return m.final;for(auto& [name,v]:m.stages)if(k==h(v))return v;throw std::runtime_error("fake key only");}
LSTATUS WINAPI open(HKEY root,LPCWSTR n,DWORD,REGSAM a,PHKEY out){if(root!=HKEY_LOCAL_MACHINE||!(a&KEY_WOW64_64KEY))return ERROR_INVALID_PARAMETER;std::wstring name(n);if(name==uninstallParent){if(m.parentFail)return ERROR_ACCESS_DENIED;*out=parent();return 0;}if(name==uninstallKey){if(!m.exists)return ERROR_FILE_NOT_FOUND;*out=final();return 0;}auto prefix=std::wstring(uninstallParent)+L"\\";if(!name.starts_with(prefix))return ERROR_INVALID_PARAMETER;auto it=m.stages.find(name.substr(prefix.size()));if(it==m.stages.end())return ERROR_FILE_NOT_FOUND;*out=h(it->second);return 0;}
LSTATUS WINAPI create(HKEY p,LPCWSTR name,DWORD,LPWSTR,DWORD,REGSAM a,const SECURITY_ATTRIBUTES* sd,PHKEY out,LPDWORD disposition){if(p!=parent()||!sd||!sd->lpSecurityDescriptor||sd->bInheritHandle||(a&(KEY_WRITE|DELETE|KEY_WOW64_64KEY))!=(KEY_WRITE|DELETE|KEY_WOW64_64KEY))return ERROR_INVALID_PARAMETER;++m.creates;auto [it,fresh]=m.stages.emplace(name,Values{});if(m.createCollision){it->second[L"foreign"]=L"preserve";fresh=false;}*out=h(it->second);*disposition=fresh?REG_CREATED_NEW_KEY:REG_OPENED_EXISTING_KEY;return 0;}
LSTATUS WINAPI rename(HKEY key,LPCWSTR old,LPCWSTR name){if(old||std::wstring(name)!=L"SonicLatch")return ERROR_INVALID_PARAMETER;++m.renames;if(m.renameBefore)return ERROR_ACCESS_DENIED;if(m.renameCollision){m.exists=true;m.final[L"foreign"]=L"preserve";}if(m.exists)return ERROR_ALREADY_EXISTS;for(auto it=m.stages.begin();it!=m.stages.end();++it)if(key==h(it->second)){m.final=it->second;m.exists=true;m.stages.erase(it);return m.renameAfter?ERROR_ACCESS_DENIED:ERROR_SUCCESS;}return ERROR_INVALID_HANDLE;}
LSTATUS WINAPI query(HKEY key,LPCWSTR name,LPDWORD,LPDWORD type,LPBYTE data,LPDWORD size){auto& v=values(key);auto it=v.find(name);if(it==v.end())return ERROR_FILE_NOT_FOUND;*type=m.badType==name?REG_DWORD:REG_SZ;DWORD bytes=static_cast<DWORD>((it->second.size()+1)*sizeof(wchar_t));if(data&&*size<bytes)return ERROR_MORE_DATA;if(data)memcpy(data,it->second.c_str(),bytes);*size=bytes;return 0;}
LSTATUS WINAPI set(HKEY key,LPCWSTR name,DWORD,DWORD type,const BYTE* data,DWORD size){if(type!=REG_SZ)return ERROR_INVALID_PARAMETER;if(++m.sets==m.failSet)return ERROR_ACCESS_DENIED;if(key!=final())need(ownershipName(name),"stage must contain ownership only");values(key)[name]=std::wstring(reinterpret_cast<const wchar_t*>(data),size/sizeof(wchar_t)-1);m.lastWrite=name;return 0;}
LSTATUS WINAPI info(HKEY key,LPWSTR,LPDWORD,LPDWORD,LPDWORD children,LPDWORD,LPDWORD,LPDWORD count,LPDWORD,LPDWORD,LPDWORD,PFILETIME){*children=0;*count=static_cast<DWORD>(values(key).size());return 0;}LSTATUS WINAPI flush(HKEY){return ++m.flushes==m.failFlush?ERROR_ACCESS_DENIED:ERROR_SUCCESS;}LSTATUS WINAPI close(HKEY){return 0;}
DWORD WINAPI security(HANDLE key,SE_OBJECT_TYPE type,SECURITY_INFORMATION,PSID* owner,PSID*,PACL* acl,PACL*,PSECURITY_DESCRIPTOR* raw){if(type!=SE_REGISTRY_KEY)return ERROR_INVALID_HANDLE;values(reinterpret_cast<HKEY>(key));if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(m.sddl.c_str(),SDDL_REVISION_1,raw,nullptr))return ERROR_INVALID_SECURITY_DESCR;BOOL present{},def{};need(GetSecurityDescriptorOwner(*raw,owner,&def)&&GetSecurityDescriptorDacl(*raw,&present,acl,&def),"fake SD");return 0;}
}
int main(){try{unsigned cases{},assertions{};const auto check=[&](bool b,const char* why){need(b,why);++assertions;};Plan p;p.operation=Operation::Install;p.transactionId=std::string(32,'1');p.rootIdentity="1:2:3";p.moduleId=p.productId="SonicLatch";p.version="0.1.0";p.newManifest=std::string(64,'a');p.sourceIdentity=std::string(64,'b');p.newReceipt=Ref{std::string(64,'c'),1};p.steps.push_back({"SonicLatch/SonicLatch.dll",{},Ref{std::string(64,'d'),1}});const fs::path metadata=L"C:\\ProgramData\\SonicLatch\\Installer";
 RegistrationIntentIO io{[]{return fake::m.intent;},[](const Image& expected,const Bytes& bytes){need(!expected&&!fake::m.intent,"intent CAS");if(fake::m.intentBefore)throw std::runtime_error("intent before");fake::m.intent=bytes;if(fake::m.intentAfter)throw std::runtime_error("intent after");}};const auto invoke=[&](){try{publishRegistration(p,metadata,"0.1.0",&io);return true;}catch(...){return false;}};const auto reset=[](){fake::m=fake::Model{};};
 reset();check(invoke()&&fake::m.final==registration(p,metadata)&&fake::m.lastWrite==L"DisplayName","fresh exact seven, display last");auto writes=fake::m.sets;check(invoke()&&fake::m.sets==writes,"idempotent complete");++cases;
 for(unsigned n=1;n<=7;++n){reset();fake::m.failSet=n;check(!invoke(),"each write interruption");auto orphans=fake::m.stages;if(n<=3)check(!fake::m.exists&&!fake::m.intent,"unsealed stage not published");else check(fake::m.exists&&fake::m.intent&&!fake::m.final.contains(L"DisplayName"),"partial final sealed");fake::m.failSet=0;check(invoke()&&fake::m.final==registration(p,metadata),"write retry");if(n<=3)check(fake::m.stages==orphans,"unsealed stages preserved");++cases;}
 for(unsigned n=1;n<=2;++n){reset();fake::m.failFlush=n;check(!invoke(),"flush interruption");fake::m.failFlush=0;check(invoke(),"flush retry");++cases;}
 for(bool after:{false,true}){reset();fake::m.intentBefore=!after;fake::m.intentAfter=after;check(!invoke()&&!fake::m.exists,"intent interruption");auto orphan=fake::m.stages;fake::m.intentBefore=fake::m.intentAfter=false;check(invoke(),"intent retry");if(!after)check(fake::m.stages==orphan,"pre-intent orphan preserved");++cases;}
 for(bool after:{false,true}){reset();fake::m.renameBefore=!after;fake::m.renameAfter=after;check(!invoke()&&fake::m.intent&&fake::m.exists==after,"rename boundary");fake::m.renameBefore=fake::m.renameAfter=false;check(invoke(),"rename retry");++cases;}
 reset();fake::m.renameCollision=true;check(!invoke()&&fake::m.final.at(L"foreign")==L"preserve","final collision");auto stages=fake::m.stages;writes=fake::m.sets;check(!invoke()&&fake::m.stages==stages&&fake::m.sets==writes,"both names preserved");++cases;
 reset();fake::m.parentFail=true;check(!invoke()&&!fake::m.intent&&fake::m.creates==0,"parent unavailable");++cases;
 reset();fake::m.createCollision=true;check(!invoke()&&!fake::m.intent&&!fake::m.exists,"stage collision");++cases;
 reset();fake::m.renameBefore=true;check(!invoke(),"sealed stage baseline");fake::m.renameBefore=false;fake::m.stages.begin()->second[L"foreign"]=L"preserve";stages=fake::m.stages;check(!invoke()&&fake::m.stages==stages,"unknown stage values preserved");++cases;
 reset();fake::m.renameAfter=true;check(!invoke(),"partial final baseline");fake::m.renameAfter=false;fake::m.final[L"foreign"]=L"preserve";auto final=fake::m.final;check(!invoke()&&fake::m.final==final,"unknown final values preserved");++cases;
 reset();fake::m.exists=true;fake::m.final[L"SonicLatchOwner"]=L"foreign";final=fake::m.final;check(!invoke()&&fake::m.final==final,"foreign final");++cases;
 reset();fake::m.exists=true;fake::m.final=registration(p,metadata);check(invoke()&&fake::m.renames==0&&fake::m.creates==0,"existing owned branch");++cases;
 reset();fake::m.intent="legacy-v1";check(!invoke()&&fake::m.creates==0,"no strategy fallback");++cases;
 reset();fake::m.renameBefore=true;check(!invoke(),"type baseline");fake::m.renameBefore=false;fake::m.badType=L"SonicLatchRoot";check(!invoke()&&!fake::m.exists,"wrong type refused");++cases;
 std::cout<<"PASS "<<cases<<" cases / "<<assertions<<" assertions; non-TxR protected stage/rename-v2; all publish Registry APIs faked; UI visibility NOT tested\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
