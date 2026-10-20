#include "product_windows_store.hpp"
#include <iostream>
using namespace soniclatch::product;
using namespace soniclatch::product::win;
void write(const fs::path& path,const Bytes& bytes,DWORD extra=0,HANDLE* retained=nullptr){Handle h(CreateFileW(path.c_str(),GENERIC_WRITE|READ_CONTROL|extra,0,nullptr,CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_WRITE_THROUGH,nullptr));DWORD wrote{};need(WriteFile(h.value,bytes.data(),static_cast<DWORD>(bytes.size()),&wrote,nullptr)&&wrote==bytes.size()&&FlushFileBuffers(h.value),"fixture write/flush");if(retained)*retained=std::exchange(h.value,INVALID_HANDLE_VALUE);}
Bytes read(const fs::path& path){Handle h(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));std::array<char,32> data{};DWORD got{};need(ReadFile(h.value,data.data(),static_cast<DWORD>(data.size()),&got,nullptr),"fixture read");return {data.data(),got};}
int main(){try{
 std::array<wchar_t,32768> temp{};need(GetTempPathW(static_cast<DWORD>(temp.size()),temp.data()),"fixture temp");GUID guid{};need(CoCreateGuid(&guid)==S_OK,"fixture identity");std::array<wchar_t,64> id{};StringFromGUID2(guid,id.data(),static_cast<int>(id.size()));const fs::path root=fs::path(temp.data())/(L"SonicLatch-atomic-share-"+std::wstring(id.data()));need(CreateDirectoryW(root.c_str(),nullptr),"fresh fixture root");unsigned nativePassed{},win32ReadPassed{};
 for(unsigned mode=0;mode<2;++mode)for(unsigned replacing=0;replacing<2;++replacing)for(unsigned native=0;native<2;++native){
  const auto dir=root/(std::to_wstring(mode)+L"-"+std::to_wstring(replacing)+L"-"+std::to_wstring(native));need(CreateDirectoryW(dir.c_str(),nullptr),"fresh fixture directory");const auto source=dir/L"new.tmp",final=dir/L"final.bin";if(replacing)write(final,"before");
  const DWORD sharing=FILE_SHARE_READ|(mode?FILE_SHARE_WRITE:0);Handle pin(CreateFileW(dir.c_str(),FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES|READ_CONTROL,sharing,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));auto identityBefore=identity(pin.value);HANDLE raw=INVALID_HANDLE_VALUE;write(source,"after",native?DELETE:0,native?&raw:nullptr);Handle held;if(native)held=Handle(raw);
  bool success=false;std::string error;try{if(native){renameHeldAtomicFile(held.value,source,final,replacing!=0);need(FlushFileBuffers(held.value),"post-rename flush");held.close();}else moveAtomicFile(source,final,replacing!=0);success=true;}catch(const std::exception& e){error=e.what();held.close();}
  need(identity(pin.value)==identityBefore,"fixture parent identity retained");if(success)need(read(final)=="after"&&GetFileAttributesW(source.c_str())==INVALID_FILE_ATTRIBUTES,"rename readback");else{need(read(source)=="after","failed rename temp preserved");if(replacing)need(read(final)=="before","failed replacement final preserved");}
  if(native&&success)++nativePassed;if(!native&&!mode&&success)++win32ReadPassed;
  std::cout<<"case share="<<(mode?"READ_WRITE":"READ")<<" replace="<<replacing<<" native="<<native<<" success="<<success<<" "<<error<<"\n";
 }
 std::cout<<"fixture="<<diagnosticPath(root)<<" nativePassed="<<nativePassed<<"/4 win32ReadPassed="<<win32ReadPassed<<"/2 token="<<(IsUserAnAdmin()?"admin":"non-admin")<<"; dedicated BA descriptor/product-state test NOT RUN; all fixture files retained\n";
 need(nativePassed==4,"native candidate not established for all same-parent cases");
 unsigned recoveryPassed{};
 for(unsigned replacing=0;replacing<2;++replacing)for(unsigned cut=0;cut<2;++cut){
  const auto dir=root/(L"recovery-"+std::to_wstring(replacing)+L"-"+std::to_wstring(cut));need(CreateDirectoryW(dir.c_str(),nullptr),"recovery directory");const auto source=dir/L"new.tmp",final=dir/L"final.bin",unknown=dir/L"unknown.tmp";
  if(replacing)write(final,"before");write(unknown,"unowned-evidence");Handle pin(CreateFileW(dir.c_str(),FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES|READ_CONTROL,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
  {HANDLE raw{};write(source,"after",DELETE,&raw);Handle held(raw);if(cut)renameHeldAtomicFile(held.value,source,final,replacing!=0);}
  // Simulate process loss by releasing the source handle at the durable write
  // or rename boundary. Reopen only the known exact source and reconcile bytes.
  if(GetFileAttributesW(source.c_str())!=INVALID_FILE_ATTRIBUTES){need(read(source)=="after","recovery stage mismatch");if(replacing)need(read(final)=="before","recovery final drift");else need(GetFileAttributesW(final.c_str())==INVALID_FILE_ATTRIBUTES,"recovery collision");Handle held(CreateFileW(source.c_str(),GENERIC_WRITE|READ_CONTROL|DELETE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_WRITE_THROUGH,nullptr));renameHeldAtomicFile(held.value,source,final,replacing!=0);need(FlushFileBuffers(held.value),"recovery flush");}
  need(read(final)=="after"&&GetFileAttributesW(source.c_str())==INVALID_FILE_ATTRIBUTES&&read(unknown)=="unowned-evidence","recovery final/evidence readback");++recoveryPassed;
 }
 std::cout<<"interruptionBoundaryReconciliation="<<recoveryPassed<<"/4; released-handle simulation, NOT process-kill or product-transaction recovery; unknown temp preserved\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
