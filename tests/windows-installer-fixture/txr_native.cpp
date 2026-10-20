#include "product_registration.hpp"
#include <iostream>
using namespace soniclatch::product;
using namespace soniclatch::product::win;
namespace {
constexpr wchar_t prefix[]=L"Software\\SonicLatch-Installer-TxR-QA-";
std::map<std::wstring,std::wstring> expected(){return {{L"DisplayName",L"SonicLatch native TxR fixture"},{L"DisplayVersion",L"fixture"},{L"InstallLocation",L"fixture-only"},{L"UninstallString",L"fixture-only"},{L"SonicLatchOwner",L"SonicLatch-TxR-QA-v1"},{L"SonicLatchRoot",L"fixture-root"},{L"SonicLatchManifest",std::wstring(64,L'a')}};}
std::wstring keyPath(const std::string& nonce){need(hex(nonce,32),"bounded fixture nonce required");return std::wstring(prefix)+widenAscii(nonce);}
std::wstring userSid(){HANDLE raw{};need(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&raw),"fixture token");Handle token(raw);DWORD size{};GetTokenInformation(token.value,TokenUser,nullptr,0,&size);std::vector<BYTE> bytes(size);need(GetTokenInformation(token.value,TokenUser,bytes.data(),size,&size),"fixture user");LPWSTR sid{};need(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid,&sid),"fixture SID");std::wstring result(sid);LocalFree(sid);return result;}
struct ProbeSecurity {
 PSECURITY_DESCRIPTOR descriptor{};SECURITY_ATTRIBUTES attributes{sizeof(attributes),nullptr,FALSE};
 ProbeSecurity(){
  HANDLE raw{};need(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&raw),"fixture token");Handle token(raw);DWORD size{};GetTokenInformation(token.value,TokenUser,nullptr,0,&size);std::vector<BYTE> bytes(size);need(GetTokenInformation(token.value,TokenUser,bytes.data(),size,&size),"fixture user");
  LPWSTR sid{};need(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid,&sid),"fixture SID");std::unique_ptr<void,decltype(&LocalFree)> release(sid,LocalFree);
  const auto sddl=L"O:"+std::wstring(sid)+L"D:P(A;;KA;;;SY)(A;;KA;;;BA)(A;;KA;;;"+sid+L")";
  need(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&descriptor,nullptr),"fixture key descriptor");attributes.lpSecurityDescriptor=descriptor;
 }
 ~ProbeSecurity(){if(descriptor)LocalFree(descriptor);}
};
void exact(HKEY key){DWORD children{},count{};need(RegQueryInfoKeyW(key,nullptr,nullptr,nullptr,&children,nullptr,nullptr,&count,nullptr,nullptr,nullptr,nullptr)==ERROR_SUCCESS&&children==0&&count==7,"native fixture shape");for(const auto& [name,value]:expected())need(regString(key,name.c_str())==std::optional<std::wstring>{value},"native fixture values/types");}
bool complete(const std::wstring& path){RegKey key;auto code=RegOpenKeyExW(HKEY_CURRENT_USER,path.c_str(),0,KEY_READ|KEY_WOW64_64KEY,&key.value);if(code==ERROR_FILE_NOT_FOUND)return false;need(code==ERROR_SUCCESS,"native fixture read");exact(key.value);return true;}
int child(const std::string& nonce,unsigned cut){
 RegistryTransaction transaction;RegKey parent,key;DWORD disposition{};ProbeSecurity security;
 // TxR supports explicit HKEY_USERS, not the HKCU predefined shortcut.
 const auto parentPath=userSid()+L"\\Software";
 const auto opened=RegOpenKeyTransactedW(HKEY_USERS,parentPath.c_str(),0,KEY_READ|KEY_CREATE_SUB_KEY|KEY_WOW64_64KEY,&parent.value,transaction.value,nullptr);
 if(opened!=ERROR_SUCCESS)throw std::runtime_error("native TxR parent open failed: "+std::to_string(opened));
 const auto name=std::wstring(L"SonicLatch-Installer-TxR-QA-")+widenAscii(nonce);
 const auto code=RegCreateKeyTransactedW(parent.value,name.c_str(),0,nullptr,REG_OPTION_NON_VOLATILE,KEY_READ|KEY_SET_VALUE|READ_CONTROL|KEY_WOW64_64KEY,&security.attributes,&key.value,&disposition,transaction.value,nullptr);
 if(code!=ERROR_SUCCESS)throw std::runtime_error("native RegCreateKeyTransactedW failed: "+std::to_string(code));need(disposition==REG_CREATED_NEW_KEY,"native fixture never acquires existing key");
 const auto kill=[](){TerminateProcess(GetCurrentProcess(),77);ExitProcess(78);};
 if(cut==0)kill();unsigned wrote=0;for(const auto& [field,value]:expected()){setString(key.value,field.c_str(),value);if(++wrote==cut)kill();}
 exact(key.value);if(cut==8)kill();transaction.commitAttempted=true;need(CommitTransaction(transaction.value),"native CommitTransaction failed");if(cut==9)kill();return 0;
}
unsigned launch(const std::string& nonce,unsigned cut){
 std::array<wchar_t,32768> executable{};need(GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size())),"native fixture executable");
 auto command=L"\""+std::wstring(executable.data())+L"\" --child "+widenAscii(nonce)+L" "+std::to_wstring(cut);STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
 need(CreateProcessW(executable.data(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi),"native fixture child launch");Handle process(pi.hProcess),thread(pi.hThread);
 need(WaitForSingleObject(process.value,30000)==WAIT_OBJECT_0,"native fixture child timeout; preserve and inspect");DWORD code{};need(GetExitCodeProcess(process.value,&code),"native fixture child status");return code;
}
}
int wmain(int argc,wchar_t** argv){try{
 if(argc==2&&(std::wstring_view(argv[1])==L"--probe-hklm-readonly"||std::wstring_view(argv[1])==L"--probe-hklm-readonly-record")){
  RegistryTransaction transaction;RegKey key;auto code=RegOpenKeyTransactedW(HKEY_LOCAL_MACHINE,uninstallParent,0,KEY_READ|KEY_WOW64_64KEY,&key.value,transaction.value,nullptr);
  std::cout<<"READONLY HKLM64 Uninstall TxR open status="<<code<<"; no registry key creation or value writes\n";
  if(std::wstring_view(argv[1])==L"--probe-hklm-readonly-record"){
   std::array<wchar_t,32768> executable{};need(GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size())),"probe location");const auto path=fs::path(executable.data()).parent_path()/(L"txr-hklm-readonly-"+std::to_wstring(GetCurrentProcessId())+L".json");
   const auto text=std::string("{\"admin\":")+(IsUserAnAdmin()?"true":"false")+",\"status\":"+std::to_string(code)+",\"registryWrites\":false}\n";Handle output(CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_FLAG_WRITE_THROUGH,nullptr));DWORD wrote{};need(WriteFile(output.value,text.data(),static_cast<DWORD>(text.size()),&wrote,nullptr)&&wrote==text.size()&&FlushFileBuffers(output.value),"probe record");
  }
  return code==ERROR_SUCCESS?0:1;
 }
 if(argc==4&&std::wstring_view(argv[1])==L"--child"){std::string nonce;for(wchar_t c:std::wstring_view(argv[2])){need((c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f'),"native nonce ASCII");nonce+=static_cast<char>(c);}keyPath(nonce);const auto cut=std::stoul(argv[3]);need(cut<=10,"native fixture cut bound");return child(nonce,static_cast<unsigned>(cut));}
 need(argc==2&&std::wstring_view(argv[1])==L"--run-hkcu-native","explicit --run-hkcu-native required");
 for(unsigned cut=0;cut<=9;++cut){
  std::array<unsigned char,16> random{};need(BCryptGenRandom(nullptr,random.data(),static_cast<ULONG>(random.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)==0,"native fixture nonce");std::string nonce;for(auto b:random){nonce+="0123456789abcdef"[b>>4];nonce+="0123456789abcdef"[b&15];}const auto path=keyPath(nonce);
  std::cout<<"BEGIN native cut="<<cut<<" nonce="<<nonce<<std::endl;
  need(!complete(path),"fresh fixture key must not exist");const auto exit=launch(nonce,cut);if(exit!=77)throw std::runtime_error("native injected child exit="+std::to_string(exit));
  const bool committed=complete(path);need(committed==(cut==9),"native transaction is absent or complete after process termination");
  if(!committed)need(launch(nonce,10)==0,"native absent-final retry completes");need(complete(path),"native recovery completes seven fields");
  // Delete only our exact seven-value, subkey-free, fresh nonce fixture.
  need(RegDeleteKeyExW(HKEY_CURRENT_USER,path.c_str(),KEY_WOW64_64KEY,0)==ERROR_SUCCESS&&!complete(path),"native owned fixture cleanup/readback");
  std::cout<<"PASS native cut="<<cut<<" interrupted="<<(committed?"committed":"absent")<<" recovery=complete cleanup=absent\n";
 }
 std::cout<<"PASS 10 real TxR process-termination cases in fresh HKCU fixture keys. Production HKLM/Setup/intent-management handoff NOT TESTED.\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<" lastError="<<GetLastError()<<"\n";return 1;}}
