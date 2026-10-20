#pragma once
#ifndef _WIN32
#error WindowsStore requires Windows
#endif
#include "product_trust.hpp"
#include "product_ipc.hpp"
#include "product_preflight.hpp"
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <winternl.h>
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
inline std::string diagnosticPath(const fs::path& path){auto bytes=path.u8string();return {reinterpret_cast<const char*>(bytes.data()),bytes.size()};}
inline void moveAtomicFile(const fs::path& temp,const fs::path& final,bool replace){
 if(!MoveFileExW(temp.c_str(),final.c_str(),MOVEFILE_WRITE_THROUGH|(replace?MOVEFILE_REPLACE_EXISTING:0))){
  const auto error=GetLastError();throw std::runtime_error("atomic promotion failed; Win32="+std::to_string(error)+"; temp="+diagnosticPath(temp)+"; final="+diagnosticPath(final));
 }
}
// Same-parent rename through the retained source handle. A single basename and
// null RootDirectory keep resolution in the source directory; no path fallback.
inline void renameHeldAtomicFile(HANDLE source,const fs::path& temp,const fs::path& final,bool replace){
 need(equalPath(temp.parent_path(),final.parent_path()),"native atomic rename requires same parent");const auto leaf=final.filename().wstring();
 need(!leaf.empty()&&leaf!=L"."&&leaf!=L".."&&leaf.find_first_of(L"\\/:")==leaf.npos&&leaf.size()<=32766,"native rename requires one bounded basename");
 using SetInfo=NTSTATUS(NTAPI*)(HANDLE,PIO_STATUS_BLOCK,PVOID,ULONG,FILE_INFORMATION_CLASS);
 auto set=reinterpret_cast<SetInfo>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtSetInformationFile"));need(set!=nullptr,"native rename unavailable; no fallback");
 struct Rename {BOOLEAN replace;HANDLE root;ULONG bytes;WCHAR name[1];};static_assert(offsetof(Rename,name)==(sizeof(void*)==8?20:12));
 const auto count=leaf.size()*sizeof(wchar_t),size=offsetof(Rename,name)+count;std::vector<unsigned char> data(size);auto* rename=reinterpret_cast<Rename*>(data.data());rename->replace=replace?TRUE:FALSE;rename->root=nullptr;rename->bytes=static_cast<ULONG>(count);std::memcpy(rename->name,leaf.data(),count);IO_STATUS_BLOCK io{};
 const auto status=set(source,&io,rename,static_cast<ULONG>(size),static_cast<FILE_INFORMATION_CLASS>(10));
 if(status!=0)throw std::runtime_error("atomic native promotion failed; NTSTATUS="+std::to_string(static_cast<long>(status))+"; temp="+diagnosticPath(temp)+"; final="+diagnosticPath(final));
}
inline std::string identity(HANDLE h){auto i=information(h);return std::to_string(i.dwVolumeSerialNumber)+":"+std::to_string(i.nFileIndexHigh)+":"+std::to_string(i.nFileIndexLow);}
// Inspect existing security only. Never SetSecurityInfo, elevate, or repair ACLs.
// Managed storage rejects untrusted writes; shared OBS ancestors use path and
// pinned-handle checks without changing their existing security.
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

// Shared OBS ancestors retain their existing owner and inherited write rights.
// Only our module and management trees are private installer storage.
inline bool withinDirectory(const fs::path& path,const fs::path& base){
 auto p=path.wstring(),b=base.wstring();
 return equalPath(path,base)||(p.size()>b.size()&&p[b.size()]==L'\\'&&CompareStringOrdinal(p.c_str(),static_cast<int>(b.size()),b.c_str(),static_cast<int>(b.size()),TRUE)==CSTR_EQUAL);
}
inline bool managedDirectory(const fs::path& path,const fs::path& base){
 return withinDirectory(path,base/L"obs-studio"/L"plugins"/L"SonicLatch")||withinDirectory(path,base/L"SonicLatch");
}
inline DWORD directoryShareMode(const fs::path& path,const fs::path& base){return managedDirectory(path,base)?FILE_SHARE_READ:FILE_SHARE_READ|FILE_SHARE_WRITE;}
inline bool sharedDirectory(const fs::path& path,const fs::path& base){
 return equalPath(path,base/L"obs-studio")||equalPath(path,base/L"obs-studio"/L"plugins");
}
inline bool ancestorDirectory(const fs::path& path,const fs::path& base){
 for(auto ancestor=base;;ancestor=ancestor.parent_path()){
  if(equalPath(path,ancestor))return true;if(ancestor==ancestor.root_path())return false;
 }
}
inline void inspectDirectory(HANDLE h,const fs::path& path,const fs::path& base){
 try {
  auto i=information(h);need(i.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY,"directory required");
  need(equalPath(finalPath(h),path),"directory alias refused");
  need(equalPath(path,path.lexically_normal()),"non-normal directory path refused");
  if(managedDirectory(path,base))protectedAcl(h);
  else if(!sharedDirectory(path,base)){
   need(ancestorDirectory(path,base),"directory outside installer scope");
   if(path!=path.root_path())protectedAcl(h,true);
  }
 }catch(const std::exception& e){throw std::runtime_error(std::string(e.what())+": "+path.string());}
}
struct NewDirectorySecurity {
 PSECURITY_DESCRIPTOR descriptor{};
 SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES),nullptr,FALSE};
 NewDirectorySecurity(){
  need(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FRFX;;;BU)",SDDL_REVISION_1,&descriptor,nullptr),"directory security descriptor");
  attributes.lpSecurityDescriptor=descriptor;
 }
 NewDirectorySecurity(const NewDirectorySecurity&)=delete;
 NewDirectorySecurity& operator=(const NewDirectorySecurity&)=delete;
 ~NewDirectorySecurity(){if(descriptor)LocalFree(descriptor);}
};
inline Handle relativeDirectory(HANDLE parent,const std::wstring& leaf,DWORD sharing,ULONG disposition,PSECURITY_DESCRIPTOR security=nullptr){
 need(parent!=INVALID_HANDLE_VALUE&&!leaf.empty()&&leaf!=L"."&&leaf!=L".."&&leaf.find_first_of(L"\\/:")==leaf.npos&&leaf.size()<=32767,"one bounded directory basename required");
 using Create=NTSTATUS(NTAPI*)(PHANDLE,ACCESS_MASK,POBJECT_ATTRIBUTES,PIO_STATUS_BLOCK,PLARGE_INTEGER,ULONG,ULONG,ULONG,ULONG,PVOID,ULONG);
 auto create=reinterpret_cast<Create>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtCreateFile"));need(create!=nullptr,"native directory acquisition unavailable; no fallback");
 UNICODE_STRING name{};name.Length=static_cast<USHORT>(leaf.size()*sizeof(wchar_t));name.MaximumLength=name.Length;name.Buffer=const_cast<PWSTR>(leaf.data());
 OBJECT_ATTRIBUTES attributes{};attributes.Length=sizeof(attributes);attributes.RootDirectory=parent;attributes.ObjectName=&name;attributes.Attributes=0x40|0x1000;attributes.SecurityDescriptor=security; // CASE_INSENSITIVE | DONT_REPARSE
 HANDLE raw=INVALID_HANDLE_VALUE;IO_STATUS_BLOCK io{};
 const auto status=create(&raw,FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES|READ_CONTROL|SYNCHRONIZE,&attributes,&io,nullptr,FILE_ATTRIBUTE_DIRECTORY,sharing,disposition,0x1|0x20|0x200000,nullptr,0); // DIRECTORY | SYNCHRONOUS_NONALERT | OPEN_REPARSE_POINT
 if(status<0){if(raw!=INVALID_HANDLE_VALUE&&raw!=nullptr)CloseHandle(raw);throw std::runtime_error("relative directory acquisition failed; NTSTATUS="+std::to_string(static_cast<long>(status))+"; basename="+diagnosticPath(fs::path(leaf)));}
 return Handle(raw);
}
inline Handle acquireDirectory(HANDLE parent,const fs::path& path,const fs::path& base,bool create=false,bool fresh=false){
 need(equalPath(path,path.lexically_normal())&&(managedDirectory(path,base)||sharedDirectory(path,base)||ancestorDirectory(path,base)),"directory acquisition outside normalized scope");
 if(path==path.root_path()){
  need(parent==INVALID_HANDLE_VALUE&&!create&&!fresh,"only existing volume root may use absolute acquisition");
  Handle h(CreateFileW(path.c_str(),FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES|READ_CONTROL|SYNCHRONIZE,directoryShareMode(path,base),nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));inspectDirectory(h.value,path,base);return h;
 }
 inspectDirectory(parent,path.parent_path(),base);NewDirectorySecurity security;
 auto h=relativeDirectory(parent,path.filename().wstring(),directoryShareMode(path,base),fresh?2:create?3:1,(create||fresh)&&managedDirectory(path,base)?security.descriptor:nullptr);
 inspectDirectory(h.value,path,base);return h;
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
struct UninstallerPublish {std::string nonce,id;Ref image;};
inline fs::path uninstallerPublishName(const std::string& tx,const std::string& slot,const std::string& nonce){need(hex(tx,32)&&hex(slot,32)&&hex(nonce,32),"publish basename identity");return fs::path("Uninstall.publish-"+tx+"-"+slot+"-"+nonce+".tmp");}
inline Bytes encodeUninstallerPublish(const std::string& tx,const std::string& plan,const std::string& slot,const Bytes& nsisSeal,const UninstallerPublish& copy){
 uninstallerPublishName(tx,slot,copy.nonce);need(!copy.id.empty(),"publish file identity");refValid(Image{copy.image});Writer w;w.text("SonicLatch-uninstaller-publish-v2");for(const auto* value:{&tx,&plan,&slot})w.text(*value);w.text(hashBytes(nsisSeal));w.text(copy.nonce);w.text(copy.id);w.ref(copy.image);return w.b;
}
inline UninstallerPublish decodeUninstallerPublish(const Bytes& raw,const std::string& tx,const std::string& plan,const std::string& slot,const Bytes& nsisSeal){
 Reader r{raw};need(r.text()=="SonicLatch-uninstaller-publish-v2"&&r.text()==tx&&r.text()==plan&&r.text()==slot&&r.text()==hashBytes(nsisSeal),"publish seal binding");auto nonce=r.text(),id=r.text();auto image=r.ref();r.end();need(image.has_value()&&!id.empty(),"publish artifact required");UninstallerPublish result{nonce,id,*image};need(raw==encodeUninstallerPublish(tx,plan,slot,nsisSeal,result),"publish seal encoding");return result;
}
inline void obsStopped(){
 const auto raw=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(raw==INVALID_HANDLE_VALUE)throw PreflightBlocked("OBS process enumeration unavailable");Handle snapshot(raw);PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
 if(!Process32FirstW(snapshot.value,&entry))throw PreflightBlocked("OBS process enumeration unavailable");
 do {if(_wcsicmp(entry.szExeFile,L"obs64.exe")==0||_wcsicmp(entry.szExeFile,L"obs32.exe")==0)throw PreflightBlocked("OBS must be closed");}while(Process32NextW(snapshot.value,&entry));
 if(GetLastError()!=ERROR_NO_MORE_FILES)throw PreflightBlocked("OBS process enumeration incomplete");
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
 void pinDirectory(const fs::path& path,bool create=false,bool fresh=false) const {
  auto key=path.wstring();if(pins.contains(key)){need(!fresh,"fresh directory already pinned");return;}
  if(path.has_relative_path())pinDirectory(path.parent_path(),create);
  auto h=acquireDirectory(path==path.root_path()?INVALID_HANDLE_VALUE:pins.at(path.parent_path().wstring()).value,path,root.parent_path().parent_path(),path!=path.root_path()&&create,fresh);
  pins.emplace(std::move(key),std::move(h));
 }
 void parent(const fs::path& file,bool create=false) const {pinDirectory(file.parent_path(),create);}
 std::optional<Bytes> read(const fs::path& file,std::string* fileId=nullptr) const {
  parent(file);HANDLE raw=CreateFileW(file.c_str(),GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  if(raw==INVALID_HANDLE_VALUE){need(GetLastError()==ERROR_FILE_NOT_FOUND,"file open failed");return {};}
  Handle h(raw);auto i=information(h.value);need(!(i.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&i.nNumberOfLinks==1,"nonregular/hardlink refused");protectedAcl(h.value);
  need(equalPath(finalPath(h.value),file),"file alias refused");LARGE_INTEGER size{};need(GetFileSizeEx(h.value,&size)&&size.QuadPart>=0&&static_cast<ULONGLONG>(size.QuadPart)<=fileLimit,"file bound");
  Bytes b(static_cast<std::size_t>(size.QuadPart),'\0');DWORD got{};
  if(!b.empty())need(ReadFile(h.value,b.data(),static_cast<DWORD>(b.size()),&got,nullptr)&&got==b.size(),"short read");
  char extra{};need(ReadFile(h.value,&extra,1,&got,nullptr)&&got==0,"growing file refused");if(fileId)*fileId=identity(h.value);return b;
 }
 Image ref(const std::optional<Bytes>& bytes) const {return bytes?Image{Ref{hashBytes(*bytes),bytes->size()}}:Image{};}
 void atomic(const fs::path& file,const Bytes& bytes,bool replace){
  need(bytes.size()<=fileLimit,"write bound");parent(file);
  // Unique temp creation; retain any interrupted temp as recovery evidence.
  std::array<unsigned char,16> random{};need(BCryptGenRandom(nullptr,random.data(),static_cast<ULONG>(random.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)==0,"temporary identity");
  std::wstring suffix=L".sl-";for(auto c:random){suffix+=L"0123456789abcdef"[c>>4];suffix+=L"0123456789abcdef"[c&15];}suffix+=L".tmp";
  fs::path temp=file.wstring()+suffix;
  Handle h(CreateFileW(temp.c_str(),GENERIC_WRITE|READ_CONTROL|DELETE,0,nullptr,CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_WRITE_THROUGH,nullptr));
  auto i=information(h.value);need(i.nNumberOfLinks==1&&!(i.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY),"unsafe new file");protectedAcl(h.value);
  DWORD written{};need(WriteFile(h.value,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(h.value),"write/flush failed");
  renameHeldAtomicFile(h.value,temp,file,replace);
  need(FlushFileBuffers(h.value),"atomic post-rename flush failed");h.close();
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
 struct Artifact {Ref image;std::string id;bool operator==(const Artifact&)const=default;};
 private:
 std::optional<Artifact> artifact(const fs::path& path)const {
  std::string id;auto bytes=read(path,&id);if(!bytes)return {};
  need(id.substr(0,id.find(':'))==rootId.substr(0,rootId.find(':')),"artifact must share root volume");
  return Artifact{Ref{hashBytes(*bytes),bytes->size()},id};
 }
 fs::path slotPath(const std::string& slot)const {need(hex(slot,32),"bounded slot identity");return transaction/L"uninstaller-slots"/fs::path(slot);}
 void committedGuard()const {guard(boundPlan);need(decodeJournal(readJournal(),boundPlan,expectedDigest).state==State::Committed,"management requires committed payload");}
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
  markProtectedMutationRisk();lock=Handle(CreateFileW((metadata/L"transaction.lock").c_str(),GENERIC_READ|GENERIC_WRITE|READ_CONTROL,0,nullptr,OPEN_ALWAYS,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
  auto li=information(lock.value);need(li.nNumberOfLinks==1&&!(li.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY),"unsafe lock");protectedAcl(lock.value);
  guard(plan);
 }
 const fs::path& metadataPath()const{return metadata;}
 std::optional<Bytes> readUninstallerSlotAnchor()const{return read(transaction/L"uninstaller-slot.bin");}
 void writeUninstallerSlotAnchor(const Image& expected,const Bytes& bytes){committedGuard();change(transaction/L"uninstaller-slot.bin",expected,bytes);}
 void createUninstallerSlot(const std::string& slot,const Bytes& intent){
  committedGuard();pinDirectory(transaction/L"uninstaller-slots",true);const auto dir=slotPath(slot);pinDirectory(dir,true,true);atomic(dir/L"intent.bin",intent,false);
 }
 std::optional<Bytes> readSlotRecord(const std::string& slot,const std::string& name)const {
  need(name=="intent.bin"||name=="seal.bin"||name=="publish.bin","slot record name");return read(slotPath(slot)/fs::path(name));
 }
 void sealUninstallerSlot(const std::string& slot,const Bytes& bytes){committedGuard();atomic(slotPath(slot)/L"seal.bin",bytes,false);}
 std::optional<Artifact> readSlotArtifact(const std::string& slot)const{return artifact(slotPath(slot)/L"Uninstall.pending.exe");}
 std::optional<Artifact> readUninstallerArtifact()const{return artifact(metadata/L"Uninstall.exe");}
 Artifact promoteUninstallerSlot(const std::string& slot,const Artifact& expected){
  committedGuard();const auto stage=slotPath(slot)/L"Uninstall.pending.exe",final=metadata/L"Uninstall.exe";
  need(artifact(stage)==std::optional<Artifact>{expected},"changed original sealed stage retained");auto nsisSeal=readSlotRecord(slot,"seal.bin");need(nsisSeal.has_value(),"NSIS seal missing");
  const auto verifyHeld=[&](HANDLE handle,const fs::path& path,const Artifact& trusted){
   auto info=information(handle);need(!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&info.nNumberOfLinks==1&&identity(handle)==trusted.id&&equalPath(finalPath(handle),path),"publish held identity/type/path mismatch");protectedAcl(handle);LARGE_INTEGER size{},zero{};need(GetFileSizeEx(handle,&size)&&size.QuadPart>=0&&static_cast<ULONGLONG>(size.QuadPart)==trusted.image.size&&trusted.image.size<=fileLimit&&SetFilePointerEx(handle,zero,nullptr,FILE_BEGIN),"publish held size/seek");Bytes bytes(static_cast<std::size_t>(size.QuadPart),'\0');DWORD got{};need((bytes.empty()||(ReadFile(handle,bytes.data(),static_cast<DWORD>(bytes.size()),&got,nullptr)&&got==bytes.size()))&&hashBytes(bytes)==trusted.image.sha256,"publish held hash mismatch");return bytes;
  };
  auto sealed=readSlotRecord(slot,"publish.bin");UninstallerPublish published{};Handle copy;fs::path copyPath;
  if(sealed){published=decodeUninstallerPublish(*sealed,tx,expectedDigest,slot,*nsisSeal);need(published.image==expected.image&&published.id!=expected.id,"copy/original seal identity mismatch");copyPath=metadata/uninstallerPublishName(tx,slot,published.nonce);auto stagedCopy=artifact(copyPath),current=artifact(final);Artifact copied{published.image,published.id};
   if(!stagedCopy){need(current==std::optional<Artifact>{copied},"published final missing/changed; retained");return copied;}
   need(!current&&*stagedCopy==copied,"both publish copy/final present or changed; retained");copy=Handle(CreateFileW(copyPath.c_str(),GENERIC_READ|GENERIC_WRITE|READ_CONTROL|DELETE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_WRITE_THROUGH,nullptr));verifyHeld(copy.value,copyPath,copied);
  }else{
   need(!artifact(final),"existing final uninstaller retained");Handle source(CreateFileW(stage.c_str(),GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));auto bytes=verifyHeld(source.value,stage,expected);
   std::array<unsigned char,16> random{};need(BCryptGenRandom(nullptr,random.data(),static_cast<ULONG>(random.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)==0,"publish nonce");for(auto c:random){published.nonce+="0123456789abcdef"[c>>4];published.nonce+="0123456789abcdef"[c&15];}copyPath=metadata/uninstallerPublishName(tx,slot,published.nonce);copy=Handle(CreateFileW(copyPath.c_str(),GENERIC_READ|GENERIC_WRITE|READ_CONTROL|DELETE,0,nullptr,CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_WRITE_THROUGH,nullptr));
   DWORD wrote{};need(WriteFile(copy.value,bytes.data(),static_cast<DWORD>(bytes.size()),&wrote,nullptr)&&wrote==bytes.size()&&FlushFileBuffers(copy.value),"bounded publish copy/flush failed");published.id=identity(copy.value);published.image=expected.image;need(published.id!=expected.id,"publish copy needs new file identity");verifyHeld(copy.value,copyPath,{published.image,published.id});
   const auto record=encodeUninstallerPublish(tx,expectedDigest,slot,*nsisSeal,published);atomic(slotPath(slot)/L"publish.bin",record,false);need(readSlotRecord(slot,"publish.bin")==std::optional<Bytes>{record},"publish seal readback");
  }
  renameHeldAtomicFile(copy.value,copyPath,final,false);need(FlushFileBuffers(copy.value),"publish rename flush");copy.close();Artifact copied{published.image,published.id};need(!artifact(copyPath)&&artifact(final)==std::optional<Artifact>{copied}&&artifact(stage)==std::optional<Artifact>{expected},"publish copy/final/original readback");return copied;
 }
 std::optional<Bytes> readRegistrationIntent()const{return read(transaction/L"registration-intent.bin");}
 void writeRegistrationIntent(const Image& expected,const Bytes& bytes){
  need(decodeJournal(readJournal(),boundPlan,expectedDigest).state==State::Committed,"registration requires committed payload");
  change(transaction/L"registration-intent.bin",expected,bytes);
 }
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
  for(const auto& [path,h]:pins){need(equalPath(finalPath(h.value),fs::path(path)),"pinned directory moved");inspectDirectory(h.value,fs::path(path),root.parent_path().parent_path());}
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
  pinDirectory(transaction,true,true);pinDirectory(transaction/L"blobs",true,true);
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
