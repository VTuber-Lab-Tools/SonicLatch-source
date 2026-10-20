#include "product_windows_store.hpp"
#include <winioctl.h>
#include <iostream>
using namespace soniclatch::product;
using namespace soniclatch::product::win;
int main(){try{
 std::array<wchar_t,32768> temp{};need(GetTempPathW(static_cast<DWORD>(temp.size()),temp.data()),"temporary root");GUID guid{};need(CoCreateGuid(&guid)==S_OK,"probe identity");std::array<wchar_t,64> id{};StringFromGUID2(guid,id.data(),static_cast<int>(id.size()));
 const fs::path base=fs::path(temp.data())/(L"SonicLatch-reparse-probe-"+std::wstring(id.data())),obs=base/L"obs-studio",plugins=obs/L"plugins",target=base/L"redirected";
 need(CreateDirectoryW(base.c_str(),nullptr)&&CreateDirectoryW(obs.c_str(),nullptr)&&CreateDirectoryW(plugins.c_str(),nullptr)&&CreateDirectoryW(target.c_str(),nullptr),"fresh probe directories");
 Handle pin(CreateFileW(plugins.c_str(),FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES|READ_CONTROL,directoryShareMode(plugins,base),nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));inspectDirectory(pin.value,plugins,base);
 Handle writer(CreateFileW(plugins.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
 const auto substitute=L"\\??\\"+target.wstring(),display=target.wstring();struct Mount {DWORD tag;WORD length,reserved,subOffset,subLength,printOffset,printLength;wchar_t paths[4096];} data{};
 data.tag=IO_REPARSE_TAG_MOUNT_POINT;data.subLength=static_cast<WORD>(substitute.size()*sizeof(wchar_t));data.printOffset=static_cast<WORD>(data.subLength+sizeof(wchar_t));data.printLength=static_cast<WORD>(display.size()*sizeof(wchar_t));need(substitute.size()+display.size()+2<4096,"mount buffer bound");std::copy(substitute.begin(),substitute.end(),data.paths);std::copy(display.begin(),display.end(),data.paths+substitute.size()+1);data.length=static_cast<WORD>(8+data.printOffset+data.printLength+sizeof(wchar_t));DWORD got{};
 const bool changed=DeviceIoControl(writer.value,FSCTL_SET_REPARSE_POINT,&data,8+data.length,nullptr,0,&got,nullptr)!=FALSE;const auto setError=changed?ERROR_SUCCESS:GetLastError();
 bool inspectionRejected=false,productionCreate=false,controlOutside=false,outsideCreated=false;std::string productionError;
 if(changed){
  try{inspectDirectory(pin.value,plugins,base);}catch(...){inspectionRejected=true;}
  // Use the production native primitive with default descriptor for this
  // non-admin control. Product BA ownership is a separate admin-only check.
  try{auto child=relativeDirectory(pin.value,L"SonicLatch",FILE_SHARE_READ,2);productionCreate=true;}catch(const std::exception& e){productionError=e.what();}
  outsideCreated=GetFileAttributesW((target/L"SonicLatch").c_str())!=INVALID_FILE_ATTRIBUTES;
  const auto control=plugins/L"probe-default-security-control";
  need(CreateDirectoryW(control.c_str(),nullptr),"default-security absolute-path control");controlOutside=GetFileAttributesW((target/L"probe-default-security-control").c_str())!=INVALID_FILE_ATTRIBUTES;
  need(RemoveDirectoryW((target/L"probe-default-security-control").c_str()),"control cleanup inside fresh probe root");
  if(outsideCreated)need(RemoveDirectoryW((target/L"SonicLatch").c_str()),"fresh candidate directory cleanup");
  struct DeleteTag {DWORD tag;WORD length,reserved;} remove{IO_REPARSE_TAG_MOUNT_POINT,0,0};need(DeviceIoControl(writer.value,FSCTL_DELETE_REPARSE_POINT,&remove,sizeof(remove),nullptr,0,&got,nullptr),"restore only fresh probe reparse tag");
 }
 if(productionCreate&&!outsideCreated)need(RemoveDirectoryW((plugins/L"SonicLatch").c_str()),"fresh relative child cleanup");writer.close();pin.close();need(RemoveDirectoryW(plugins.c_str())&&RemoveDirectoryW(obs.c_str())&&RemoveDirectoryW(target.c_str())&&RemoveDirectoryW(base.c_str()),"empty probe directory cleanup");
 std::cout<<"postPinReparse="<<changed<<" setStatus="<<setError<<" inspectionRejected="<<inspectionRejected<<" relativeCreateSucceeded="<<productionCreate<<" relativeOutsideCreate="<<outsideCreated<<" defaultAbsoluteControlOutside="<<controlOutside<<" nativeDiagnostic="<<productionError<<" actualProgramDataTouched=false fixtureCleaned=true; non-admin default-security control, BA product descriptor NOT RUN\n";
 need(changed&&inspectionRejected&&!outsideCreated&&controlOutside,"relative reparse defense not established");return 0;
}catch(const std::exception& e){std::cerr<<"PROBE INCOMPLETE "<<e.what()<<" lastError="<<GetLastError()<<"\n";return 1;}}
