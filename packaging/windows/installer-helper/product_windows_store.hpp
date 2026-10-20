#pragma once
#ifndef _WIN32
#error WindowsStore requires Windows
#endif
#include "product_trust.hpp"
#include "product_ipc.hpp"
#include <windows.h>
#include <aclapi.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <array>
#include <filesystem>
#include <memory>
#include <utility>

namespace soniclatch::product::win {
namespace fs = std::filesystem;
struct Handle {
 HANDLE value=INVALID_HANDLE_VALUE;
 Handle()=default;
 explicit Handle(HANDLE h):value(h){need(h!=INVALID_HANDLE_VALUE,"Windows open failed");}
 Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
 Handle(Handle&& other) noexcept:value(std::exchange(other.value,INVALID_HANDLE_VALUE)){}
 Handle& operator=(Handle&& other) noexcept {if(this!=&other){close();value=std::exchange(other.value,INVALID_HANDLE_VALUE);}return *this;}
 void close() noexcept {if(value!=INVALID_HANDLE_VALUE)CloseHandle(std::exchange(value,INVALID_HANDLE_VALUE));}
 ~Handle(){close();}
};
inline BY_HANDLE_FILE_INFORMATION information(HANDLE h){
 BY_HANDLE_FILE_INFORMATION i{};
 need(GetFileType(h)==FILE_TYPE_DISK&&GetFileInformationByHandle(h,&i),"disk identity unavailable");
 need(!(i.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT),"reparse refused");return i;
}
inline std::wstring finalPath(HANDLE h){
 std::array<wchar_t,32768> buffer{};auto n=GetFinalPathNameByHandleW(h,buffer.data(),static_cast<DWORD>(buffer.size()),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
 need(n&&n<buffer.size(),"canonical path unavailable");std::wstring p(buffer.data(),n);
 need(p.size()>6&&p.rfind(L"\\\\?\\",0)==0&&p[5]==L':',"local drive required");return p.substr(4);
}
inline bool equalPath(fs::path a,fs::path b){a.make_preferred();b.make_preferred();return CompareStringOrdinal(a.c_str(),-1,b.c_str(),-1,TRUE)==CSTR_EQUAL;}
inline std::string identity(HANDLE h){auto i=information(h);return std::to_string(i.dwVolumeSerialNumber)+":"+std::to_string(i.nFileIndexHigh)+":"+std::to_string(i.nFileIndexLow);}
// Inspect existing security only. Never SetSecurityInfo, elevate, or repair ACLs.
// Normal-machine mode intentionally rejects user-writable storage. Bootstrap
// must supply protected existing directories; portable mode is not enabled.
inline DWORD untrustedMutationMask(SE_OBJECT_TYPE type,bool ancestor=false){
 if(type==SE_REGISTRY_KEY)return KEY_SET_VALUE|KEY_CREATE_SUB_KEY|KEY_CREATE_LINK|DELETE|WRITE_DAC|WRITE_OWNER|GENERIC_WRITE|GENERIC_ALL|MAXIMUM_ALLOWED;
 need(type==SE_FILE_OBJECT,"unsupported security object type");
 return ancestor ? (FILE_DELETE_CHILD|DELETE|WRITE_DAC|WRITE_OWNER|GENERIC_ALL|MAXIMUM_ALLOWED) : FILE_WRITE_DATA|FILE_APPEND_DATA|FILE_WRITE_EA|FILE_WRITE_ATTRIBUTES|FILE_DELETE_CHILD|DELETE|WRITE_DAC|WRITE_OWNER|GENERIC_WRITE|GENERIC_ALL|MAXIMUM_ALLOWED;
}
inline void protectedAcl(HANDLE h,bool ancestor=false,SE_OBJECT_TYPE type=SE_FILE_OBJECT){
 PSID owner{};PACL acl{};PSECURITY_DESCRIPTOR raw{};
 need(GetSecurityInfo(h,type,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,&owner,nullptr,&acl,nullptr,&raw)==ERROR_SUCCESS,"security descriptor unavailable");
 std::unique_ptr<void,decltype(&LocalFree)> descriptor(raw,LocalFree);
 std::array<BYTE,SECURITY_MAX_SID_SIZE> admin{},system{};DWORD as=static_cast<DWORD>(admin.size()),ss=static_cast<DWORD>(system.size());
 need(CreateWellKnownSid(WinBuiltinAdministratorsSid,nullptr,admin.data(),&as)&&CreateWellKnownSid(WinLocalSystemSid,nullptr,system.data(),&ss),"well-known SID unavailable");
 const auto trusted=[&](PSID s){return IsValidSid(s)&&(EqualSid(s,admin.data())||EqualSid(s,system.data()));};
 need(owner&&trusted(owner)&&acl,"unprotected owner or null DACL");
 const DWORD writes=untrustedMutationMask(type,ancestor);
 for(DWORD index=0;index<acl->AceCount;++index){void* rawAce{};need(GetAce(acl,index,&rawAce),"invalid ACL");auto* header=static_cast<ACE_HEADER*>(rawAce);if(header->AceFlags&INHERIT_ONLY_ACE)continue;
  if(header->AceType==ACCESS_ALLOWED_ACE_TYPE){auto* ace=static_cast<ACCESS_ALLOWED_ACE*>(rawAce);need(!(ace->Mask&writes)||trusted(&ace->SidStart),"untrusted write permission");}
  else need(header->AceType==ACCESS_DENIED_ACE_TYPE,"unsupported ACL ACE refused");
 }
}
inline std::string hashBytes(const Bytes& b){
 need(b.size()<=fileLimit,"hash input bound");BCRYPT_ALG_HANDLE alg{};BCRYPT_HASH_HANDLE hash{};
 need(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)==0,"SHA provider");
 DWORD size{},got{};bool ok=BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&size),sizeof(size),&got,0)==0;
 std::vector<unsigned char> object(ok?size:0);std::array<unsigned char,32> digest{};
 ok=ok&&BCryptCreateHash(alg,&hash,object.data(),size,nullptr,0,0)==0;
 ok=ok&&BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<char*>(b.data())),static_cast<ULONG>(b.size()),0)==0;
 ok=ok&&BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)==0;
 if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(alg,0);need(ok,"SHA failure");
 std::string result;for(auto c:digest){result+="0123456789abcdef"[c>>4];result+="0123456789abcdef"[c&15];}return result;
}
inline void obsStopped(){
 Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
 need(Process32FirstW(snapshot.value,&entry),"process enumeration unavailable");
 do {need(_wcsicmp(entry.szExeFile,L"obs64.exe")!=0&&_wcsicmp(entry.szExeFile,L"obs32.exe")!=0,"OBS must be closed");}while(Process32NextW(snapshot.value,&entry));
 need(GetLastError()==ERROR_NO_MORE_FILES,"process enumeration incomplete");
}
// Concrete machine Store: no arbitrary root argument or CLI directory override.
// Policy and expected plan digest must come from the trusted host, never merely
// from the plan/receipt being validated. Values are copied for the whole lease.
class WindowsStore final:public Store {
 fs::path root,metadata,transaction;std::string tx,expectedDigest,rootId;
 KnownTransition policy;
 mutable std::map<std::wstring,Handle> pins;
 Handle lock;
 const Plan boundPlan;
 bool managedDirectory(const fs::path& path) const {
  auto inside=[&](const fs::path& base){auto rel=path.lexically_relative(base);return equalPath(path,base)||(!rel.empty()&&*rel.begin()!=L".."&&!rel.is_absolute());};
  return inside(root)||inside(metadata);
 }
 void pinDirectory(const fs::path& path,bool create=false) const {
  auto key=path.wstring();if(pins.contains(key))return;
  if(path.has_relative_path())pinDirectory(path.parent_path(),create);
  if(create&&managedDirectory(path)&&!CreateDirectoryW(path.c_str(),nullptr))need(GetLastError()==ERROR_ALREADY_EXISTS,"create owned parent failed");
  Handle h(CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES|READ_CONTROL,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
  auto i=information(h.value);need(i.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY,"directory required");need(equalPath(finalPath(h.value),path),"directory alias refused");
  // Volume root is pinned for identity but can have broad create-folder rights;
  // all actual managed directories below it require restricted write ACLs.
  if(path!=path.root_path())protectedAcl(h.value,!managedDirectory(path));
  pins.emplace(std::move(key),std::move(h));
 }
 void parent(const fs::path& file,bool create=false) const {pinDirectory(file.parent_path(),create);}
 std::optional<Bytes> read(const fs::path& file) const {
  parent(file);HANDLE raw=CreateFileW(file.c_str(),GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  if(raw==INVALID_HANDLE_VALUE){need(GetLastError()==ERROR_FILE_NOT_FOUND,"file open failed");return {};}
  Handle h(raw);auto i=information(h.value);need(!(i.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&i.nNumberOfLinks==1,"nonregular/hardlink refused");protectedAcl(h.value);
  need(equalPath(finalPath(h.value),file),"file alias refused");LARGE_INTEGER size{};need(GetFileSizeEx(h.value,&size)&&size.QuadPart>=0&&static_cast<ULONGLONG>(size.QuadPart)<=fileLimit,"file bound");
  Bytes b(static_cast<std::size_t>(size.QuadPart),'\0');DWORD got{};
  if(!b.empty())need(ReadFile(h.value,b.data(),static_cast<DWORD>(b.size()),&got,nullptr)&&got==b.size(),"short read");
  char extra{};need(ReadFile(h.value,&extra,1,&got,nullptr)&&got==0,"growing file refused");return b;
 }
 Image ref(const std::optional<Bytes>& bytes) const {return bytes?Image{Ref{hashBytes(*bytes),bytes->size()}}:Image{};}
 void atomic(const fs::path& file,const Bytes& bytes,bool replace){
  need(bytes.size()<=fileLimit,"write bound");parent(file);
  // Unique temp creation; retain any interrupted temp as recovery evidence.
  std::array<unsigned char,16> random{};need(BCryptGenRandom(nullptr,random.data(),static_cast<ULONG>(random.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)==0,"temporary identity");
  std::wstring suffix=L".sl-";for(auto c:random){suffix+=L"0123456789abcdef"[c>>4];suffix+=L"0123456789abcdef"[c&15];}suffix+=L".tmp";
  fs::path temp=file.wstring()+suffix;
  Handle h(CreateFileW(temp.c_str(),GENERIC_WRITE|READ_CONTROL,0,nullptr,CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_WRITE_THROUGH,nullptr));
  auto i=information(h.value);need(i.nNumberOfLinks==1&&!(i.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY),"unsafe new file");protectedAcl(h.value);
  DWORD written{};need(WriteFile(h.value,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(h.value),"write/flush failed");h.close();
  need(MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_WRITE_THROUGH|(replace?MOVEFILE_REPLACE_EXISTING:0)),"atomic promotion failed");
  need(read(file)==std::optional<Bytes>{bytes},"atomic readback mismatch");
 }
 fs::path owned(const std::string& path) const {
  need(safePath(path),"owned path rejected");need(std::any_of(boundPlan.steps.begin(),boundPlan.steps.end(),[&](const Step& s){return s.path==path;}),"path outside frozen plan");auto result=root/fs::path(path);result.make_preferred();return result;
 }
 void change(const fs::path& file,const Image& expected,const std::optional<Bytes>& desired){
  guard(boundPlan);parent(file,true);need(ref(read(file))==expected,"modified file retained");
  if(desired)atomic(file,*desired,expected.has_value());
  else if(expected){need(DeleteFileW(file.c_str()),"owned deletion failed");need(!read(file),"deletion readback failed");}
 }
 public:
 WindowsStore(const Plan& plan,const std::string& approvedPlanDigest,const KnownTransition& authenticatedPolicy)
  :tx(plan.transactionId),expectedDigest(approvedPlanDigest),policy(authenticatedPolicy),boundPlan(plan){
  validate(plan);need(hex(expectedDigest,64)&&hashBytes(encode(plan))==expectedDigest,"trusted plan anchor required");authorizeTransition(plan,policy);
  PWSTR raw{};need(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData,KF_FLAG_DEFAULT,nullptr,&raw)),"ProgramData unavailable");
  std::unique_ptr<wchar_t,decltype(&CoTaskMemFree)> known(raw,CoTaskMemFree);fs::path base(raw);
  root=base/L"obs-studio"/L"plugins";metadata=base/L"SonicLatch"/L"Installer";transaction=metadata/L"transactions"/fs::path(tx);
  // Existing protected bootstrap only. No security setting changes here.
  pinDirectory(root);pinDirectory(metadata);pinDirectory(metadata/L"transactions");
  rootId=identity(pins.at(root.wstring()).value);need(rootId==plan.rootIdentity,"known root identity differs");
  need(information(pins.at(root.wstring()).value).dwVolumeSerialNumber==information(pins.at(metadata.wstring()).value).dwVolumeSerialNumber,"stage must share volume");
  lock=Handle(CreateFileW((metadata/L"transaction.lock").c_str(),GENERIC_READ|GENERIC_WRITE|READ_CONTROL,0,nullptr,OPEN_ALWAYS,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
  auto li=information(lock.value);need(li.nNumberOfLinks==1&&!(li.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY),"unsafe lock");protectedAcl(lock.value);
  guard(plan);
 }
 const fs::path& metadataPath()const{return metadata;}
 std::optional<Bytes> readManagement(const std::string& name)const {
  need(name=="helper.exe"||name=="Uninstall.exe"||name=="active.bin"||name=="management.bin","management filename");return read(metadata/fs::path(name));
 }
 void removeOwnedUninstaller(const Image& expected){change(metadata/L"Uninstall.exe",expected,std::nullopt);}
 void writeManagement(const std::string& name,const Image& expected,const Bytes& bytes){
  need(name=="helper.exe"||name=="Uninstall.exe"||name=="active.bin"||name=="management.bin","management filename");change(metadata/fs::path(name),expected,bytes);
 }
 std::string sha256(const Bytes& b)const override{return hashBytes(b);}
 void authorize(const Plan& p,const std::string& digest)const override {need(digest==expectedDigest&&encode(p)==encode(boundPlan),"plan anchor mismatch");authorizeTransition(p,policy);}
 void guard(const Plan& p)const override {
  need(p.transactionId==tx&&p.rootIdentity==rootId,"transaction root mismatch");
  for(const auto& [path,h]:pins){need(equalPath(finalPath(h.value),fs::path(path)),"pinned directory moved");information(h.value);if(fs::path(path)!=fs::path(path).root_path())protectedAcl(h.value,!managedDirectory(fs::path(path)));}
  need(identity(pins.at(root.wstring()).value)==rootId,"root identity changed");obsStopped();
 }
 std::optional<Bytes> readOwned(const std::string& path)const override {
  auto file=owned(path);
  // Missing directory is explicit absence, but an existing prefix is always
  // opened/pinned and inspected. Never traverse a reparse point for existence.
  fs::path prefix=root;auto relative=file.lexically_relative(root);
  for(auto i=relative.begin();i!=relative.end();++i){auto next=i;++next;if(next==relative.end())break;prefix/=*i;
   auto attrs=GetFileAttributesW(prefix.c_str());if(attrs==INVALID_FILE_ATTRIBUTES){need(GetLastError()==ERROR_FILE_NOT_FOUND||GetLastError()==ERROR_PATH_NOT_FOUND,"parent inspection failed");return {};}
   pinDirectory(prefix);
  }
  return read(file);
 }
 std::optional<Bytes> readReceipt()const override{return read(metadata/L"receipt.bin");}
 Bytes readPlan()const override {auto b=read(transaction/L"plan.bin");need(b.has_value(),"plan missing");need(hashBytes(*b)==expectedDigest,"persisted plan mismatch");return *b;}
 Bytes readJournal()const override{auto b=read(transaction/L"journal.bin");need(b.has_value(),"journal missing");decodeJournal(*b,boundPlan,expectedDigest);return *b;}
 Bytes readBlob(const std::string& digest)const override{need(hex(digest,64),"blob key");auto b=read(transaction/L"blobs"/fs::path(digest));need(b&&hashBytes(*b)==digest,"blob missing or changed");return *b;}
 void createPrepared(const Bytes& plan,const std::map<std::string,Bytes>& blobs,const Bytes& journal)override{
  authorize(decodePlan(plan),hashBytes(plan));guard(boundPlan);auto j=decodeJournal(journal,boundPlan,expectedDigest);need(j.state==State::Prepared&&j.step==0&&j.phase==0,"fresh journal required");
  need(CreateDirectoryW(transaction.c_str(),nullptr),"transaction exists; preserve and recover");pinDirectory(transaction);need(CreateDirectoryW((transaction/L"blobs").c_str(),nullptr),"blob directory create");pinDirectory(transaction/L"blobs");
  std::uint64_t total=0;for(const auto& [key,b]:blobs){need(hex(key,64)&&hashBytes(b)==key,"staging digest");total+=b.size();need(total<=totalLimit+2*fileLimit,"stage bound");atomic(transaction/L"blobs"/fs::path(key),b,false);}
  atomic(transaction/L"plan.bin",plan,false);atomic(transaction/L"journal.bin",journal,false);
 }
 void writeJournalDurable(const Bytes& journal)override{guard(boundPlan);decodeJournal(journal,boundPlan,expectedDigest);need(read(transaction/L"journal.bin").has_value(),"journal missing");atomic(transaction/L"journal.bin",journal,true);}
 void changeOwned(const std::string& path,const Image& expected,const std::optional<Bytes>& desired)override{
  auto file=owned(path);auto i=std::find_if(boundPlan.steps.begin(),boundPlan.steps.end(),[&](const Step& s){return s.path==path;});
  auto target=ref(desired);need((expected==i->before&&target==i->after)||(expected==i->after&&target==i->before),"mutation outside frozen before/after");change(file,expected,desired);
 }
 void changeReceipt(const Image& expected,const std::optional<Bytes>& desired)override{
  auto target=ref(desired);need((expected==boundPlan.oldReceipt&&target==boundPlan.newReceipt)||(expected==boundPlan.newReceipt&&target==boundPlan.oldReceipt),"receipt mutation outside frozen transition");change(metadata/L"receipt.bin",expected,desired);
 }
};
// Native host bridge. Does not parse a trust policy from IPC or enable a CLI.
// Recovery is an explicit host decision; repeated apply is never automatic.
inline Result applyTrustedRequest(const Request& request,const Plan& approvedPlan,
 const std::string& approvedDigest,const KnownTransition& authenticatedPolicy,
 const std::map<std::string,Bytes>& blobs) {
 matchRequest(request,approvedPlan,approvedDigest);
 WindowsStore store(approvedPlan,approvedDigest,authenticatedPolicy);
 Engine::prepare(store,approvedPlan,blobs);return Engine(store).apply();
}
inline Result recoverTrustedRequest(const Request& request,const Plan& approvedPlan,
 const std::string& approvedDigest,const KnownTransition& authenticatedPolicy) {
 matchRequest(request,approvedPlan,approvedDigest);
 WindowsStore store(approvedPlan,approvedDigest,authenticatedPolicy);return Engine(store).recover();
}
} // namespace soniclatch::product::win
