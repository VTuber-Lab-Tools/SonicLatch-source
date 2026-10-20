#include "product_windows_store.hpp"
#include <iostream>
#include <winioctl.h>
#include <atomic>
#include <thread>
using namespace soniclatch::product;
using namespace soniclatch::product::win;
void junction(const fs::path& path,const fs::path& target){
 need(CreateDirectoryW(path.c_str(),nullptr),"fresh junction fixture");
 const std::wstring substitute=L"\\??\\"+target.wstring(),display=target.wstring();
 struct Mount {DWORD tag;WORD length,reserved,subOffset,subLength,printOffset,printLength;wchar_t paths[4096];} data{};
 data.tag=IO_REPARSE_TAG_MOUNT_POINT;data.subLength=static_cast<WORD>(substitute.size()*sizeof(wchar_t));
 data.printOffset=static_cast<WORD>(data.subLength+sizeof(wchar_t));data.printLength=static_cast<WORD>(display.size()*sizeof(wchar_t));
 need(substitute.size()+display.size()+2<4096,"junction bound");
 std::copy(substitute.begin(),substitute.end(),data.paths);std::copy(display.begin(),display.end(),data.paths+substitute.size()+1);
 data.length=static_cast<WORD>(8+data.printOffset+data.printLength+sizeof(wchar_t));
 Handle h(CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));DWORD got{};
 need(DeviceIoControl(h.value,FSCTL_SET_REPARSE_POINT,&data,8+data.length,nullptr,0,&got,nullptr),"junction fixture create");
}
int main(){try{
 unsigned assertions{};const auto check=[&](bool ok,const char* why){need(ok,why);++assertions;};
 std::array<wchar_t,32768> temp{};need(GetTempPathW(static_cast<DWORD>(temp.size()),temp.data()),"temporary directory");
 GUID guid{};need(CoCreateGuid(&guid)==S_OK,"fixture identity");std::array<wchar_t,64> id{};StringFromGUID2(guid,id.data(),static_cast<int>(id.size()));
 const fs::path base=fs::path(temp.data())/(L"SonicLatch-directory-policy-"+std::wstring(id.data()));
 need(CreateDirectoryW(base.c_str(),nullptr),"fresh fixture directory");
 const auto obs=base/L"obs-studio",plugins=obs/L"plugins",module=plugins/L"SonicLatch";
 check(!managedDirectory(obs,base)&&!managedDirectory(plugins,base),"OBS parents are shared");
 check(directoryShareMode(base.root_path(),base)==(FILE_SHARE_READ|FILE_SHARE_WRITE)&&directoryShareMode(base,base)==(FILE_SHARE_READ|FILE_SHARE_WRITE)&&directoryShareMode(obs,base)==(FILE_SHARE_READ|FILE_SHARE_WRITE)&&directoryShareMode(plugins,base)==(FILE_SHARE_READ|FILE_SHARE_WRITE),"shared/ancestor pins permit read/write sharing without delete");
 check(directoryShareMode(module,base)==FILE_SHARE_READ&&directoryShareMode(base/L"SonicLatch"/L"Installer",base)==FILE_SHARE_READ,"managed pins keep strict sharing");
 check(sharedDirectory(obs,base)&&sharedDirectory(plugins,base),"only the two OBS parents are shared exceptions");
 check(!sharedDirectory(base,base)&&!sharedDirectory(base.parent_path(),base),"ProgramData and ancestors are not ACL exceptions");
 {
  Handle h(CreateFileW(base.c_str(),FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES|READ_CONTROL,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
  bool rejected=false;try{inspectDirectory(h.value,base,base);}catch(...){rejected=true;}
  check(rejected,"untrusted simulated ProgramData retains ancestor ACL refusal");
 }
 check(managedDirectory(module,base)&&managedDirectory(module/L"data"/L"locale",base),"module scope");
 check(managedDirectory(base/L"SonicLatch",base)&&managedDirectory(base/L"SonicLatch"/L"Installer"/L"transactions",base),"management scope");
 check(!managedDirectory(plugins/L"OtherPlugin",base)&&!managedDirectory(plugins/L"SonicLatch-unknown",base),"other plugins excluded");
 check(!managedDirectory(base/L"SonicLatch-other",base),"management prefix boundary");
 // Missing shared parents use normal inherited security, even for an ordinary
 // user. No existing ACL setter, real ProgramData, registry or OBS is touched.
 {Handle b(CreateFileW(base.c_str(),FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES|READ_CONTROL,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));auto o=relativeDirectory(b.value,L"obs-studio",FILE_SHARE_READ|FILE_SHARE_WRITE,3);auto p=relativeDirectory(o.value,L"plugins",FILE_SHARE_READ|FILE_SHARE_WRITE,3);}
 std::string rootIdentity;
 for(const auto& path:{obs,plugins}){
  Handle pin(CreateFileW(path.c_str(),FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES|READ_CONTROL,directoryShareMode(path,base),nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
  inspectDirectory(pin.value,path,base);++assertions;
  check(!RemoveDirectoryW(path.c_str()),"pinned shared directory cannot be deleted");
  check(!MoveFileExW(path.c_str(),(path.wstring()+L"-moved").c_str(),0),"pinned shared directory cannot be renamed");
  if(path==plugins)rootIdentity=identity(pin.value);
 }
 Handle pluginsPin(CreateFileW(plugins.c_str(),FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES|READ_CONTROL,directoryShareMode(plugins,base),nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
 check(identity(pluginsPin.value)==rootIdentity,"plugins identity retained");
 bool unknown=false;try{inspectDirectory(pluginsPin.value,plugins,base/L"different");}catch(...){unknown=true;}
 check(unknown,"unknown directory scope rejected");
 // A reparse setter needs a writable directory handle. Compete for that
 // handle while the initially empty shared parent gains our product child.
 std::atomic<bool> started{},created{};std::atomic<unsigned> opened{},otherErrors{},attempts{};
 std::thread contender([&]{started=true;do{
  HANDLE raw=CreateFileW(plugins.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  if(raw!=INVALID_HANDLE_VALUE){++opened;CloseHandle(raw);}else if(GetLastError()!=ERROR_SHARING_VIOLATION)++otherErrors;
  ++attempts;
 }while(!created||attempts<1000);});
 while(!started)std::this_thread::yield();
 const bool childCreated=CreateDirectoryW(module.c_str(),nullptr)!=FALSE;created=true;contender.join();
 check(childCreated,"create product child while shared parent pinned");
 check(opened>0&&otherErrors==0&&attempts>=1000,"shared pins intentionally allow concurrent write-open; reparse mutation race not proven by this test");
 {
  Handle h(CreateFileW(module.c_str(),FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES|READ_CONTROL,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
  bool rejected=false;try{inspectDirectory(h.value,module,base);}catch(const std::exception& e){rejected=std::string(e.what()).find(module.string())!=std::string::npos;}
  check(rejected,"unprotected managed directory rejected with exact path");
 }
 check(RemoveDirectoryW(module.c_str()),"empty untrusted module cleanup");
 const auto target=base/L"junction-target";need(CreateDirectoryW(target.c_str(),nullptr),"junction target fixture");junction(module,target);
 {
  Handle h(CreateFileW(module.c_str(),FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES|READ_CONTROL,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
  bool rejected=false;try{inspectDirectory(h.value,module,base);}catch(...){rejected=true;}
  check(rejected,"managed root junction refused");
 }
 check(RemoveDirectoryW(module.c_str())&&RemoveDirectoryW(target.c_str()),"junction fixture cleanup without target traversal");
 NewDirectorySecurity security;
 SECURITY_DESCRIPTOR_CONTROL control{};DWORD revision{};
 check(GetSecurityDescriptorControl(security.descriptor,&control,&revision)&&(control&SE_DACL_PROTECTED),"new managed DACL protected");
 PACL acl{};BOOL present{},defaulted{};
 check(GetSecurityDescriptorDacl(security.descriptor,&present,&acl,&defaulted)&&present&&acl&&acl->AceCount==3,"managed descriptor exact principals");
 PSID owner{};check(GetSecurityDescriptorOwner(security.descriptor,&owner,&defaulted),"managed owner available");
 std::array<BYTE,SECURITY_MAX_SID_SIZE> admins{},users{};DWORD as=static_cast<DWORD>(admins.size()),us=static_cast<DWORD>(users.size());
 check(CreateWellKnownSid(WinBuiltinAdministratorsSid,nullptr,admins.data(),&as)&&CreateWellKnownSid(WinBuiltinUsersSid,nullptr,users.data(),&us),"well known SIDs");
 check(EqualSid(owner,admins.data()),"new managed owner administrators");
 unsigned userRead{};for(DWORD i=0;i<acl->AceCount;++i){void* raw{};need(GetAce(acl,i,&raw),"descriptor ACE");auto* ace=static_cast<ACCESS_ALLOWED_ACE*>(raw);if(EqualSid(&ace->SidStart,users.data())){check(!(ace->Mask&untrustedMutationMask(SE_FILE_OBJECT)),"users cannot mutate managed tree");++userRead;}}
 check(userRead==1,"single users read entry");
 pluginsPin.close();check(RemoveDirectoryW(plugins.c_str())&&RemoveDirectoryW(obs.c_str())&&RemoveDirectoryW(base.c_str()),"empty fixture cleanup");
 std::cout<<"PASS "<<assertions<<" assertions; native shared-directory pins and managed descriptor; elevated lifecycle/three-machine-state tests NOT RUN\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
