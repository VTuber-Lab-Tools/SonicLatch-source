// Test-only executable. No changes or root override in the shipped helper.
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <iostream>
#include <sddl.h>
static std::wstring fixtureData;
static HRESULT WINAPI fixtureKnownFolder(REFKNOWNFOLDERID id,DWORD,HANDLE,PWSTR* out){
 if(id!=FOLDERID_ProgramData||fixtureData.empty())return E_INVALIDARG;
 const auto bytes=(fixtureData.size()+1)*sizeof(wchar_t);*out=static_cast<PWSTR>(CoTaskMemAlloc(bytes));
 if(!*out)return E_OUTOFMEMORY;memcpy(*out,fixtureData.c_str(),bytes);return S_OK;
}
#define SHGetKnownFolderPath fixtureKnownFolder
#include "product_bootstrap.hpp"
#undef SHGetKnownFolderPath
using namespace soniclatch::product;
using namespace soniclatch::product::win;
struct FixtureSecurity {
 PSECURITY_DESCRIPTOR source{};SECURITY_DESCRIPTOR sd{};SECURITY_ATTRIBUTES sa{sizeof(sa),&sd,FALSE};
 FixtureSecurity(const fs::path& actualProgramData,PSID owner){
  PACL dacl{};need(GetNamedSecurityInfoW(actualProgramData.c_str(),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,&dacl,nullptr,&source)==ERROR_SUCCESS,"read ProgramData DACL");
  need(InitializeSecurityDescriptor(&sd,SECURITY_DESCRIPTOR_REVISION)&&SetSecurityDescriptorDacl(&sd,TRUE,dacl,FALSE)&&SetSecurityDescriptorOwner(&sd,owner,FALSE)&&SetSecurityDescriptorControl(&sd,SE_DACL_PROTECTED,SE_DACL_PROTECTED),"fixture descriptor in memory");
 }
 ~FixtureSecurity(){if(source)LocalFree(source);}
};
std::wstring security(const fs::path& path){
 PSECURITY_DESCRIPTOR raw{};need(GetNamedSecurityInfoW(path.c_str(),SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,nullptr,nullptr,nullptr,nullptr,&raw)==ERROR_SUCCESS,"fixture security read");
 std::unique_ptr<void,decltype(&LocalFree)> free(raw,LocalFree);LPWSTR text{};
 need(ConvertSecurityDescriptorToStringSecurityDescriptorW(raw,SDDL_REVISION_1,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,&text,nullptr),"fixture security format");
 std::unique_ptr<void,decltype(&LocalFree)> release(text,LocalFree);return text;
}
struct Interrupted:std::exception{};
// Delegate all I/O to the actual aa665dd WindowsStore. Only the explicit fault
// after the first payload write is injected by this test wrapper.
class CutAfterWrite final:public Store {
 WindowsStore& s;bool cut=true;
 public:explicit CutAfterWrite(WindowsStore& store):s(store){}
 std::string sha256(const Bytes& b)const override{return s.sha256(b);}
 void authorize(const Plan& p,const std::string& h)const override{s.authorize(p,h);}
 void guard(const Plan& p)const override{s.guard(p);}
 std::optional<Bytes> readOwned(const std::string& p)const override{return s.readOwned(p);}
 std::optional<Bytes> readReceipt()const override{return s.readReceipt();}
 Bytes readPlan()const override{return s.readPlan();}
 Bytes readJournal()const override{return s.readJournal();}
 Bytes readBlob(const std::string& h)const override{return s.readBlob(h);}
 void createPrepared(const Bytes& p,const std::map<std::string,Bytes>& b,const Bytes& j)override{s.createPrepared(p,b,j);}
 void writeJournalDurable(const Bytes& j)override{s.writeJournalDurable(j);}
 void changeOwned(const std::string& p,const Image& before,const std::optional<Bytes>& b)override{s.changeOwned(p,before,b);if(std::exchange(cut,false))throw Interrupted{};}
 void changeReceipt(const Image& before,const std::optional<Bytes>& b)override{s.changeReceipt(before,b);}
};
Ref reference(const Bytes& b){return {hashBytes(b),b.size()};}
void lifecycle(const Bootstrap& boot,const std::string& label){
 const Bytes dll="FIXTURE ONLY - NOT AN EXECUTABLE DLL",locale="fixture locale",receipt="fixture-owned-receipt";
 KnownRelease release{hashBytes("fixture-manifest"),hashBytes("fixture-source"),"fixture-product","SonicLatch","fixture",{{"SonicLatch/SonicLatch.dll",reference(dll)},{"SonicLatch/data/locale/en-US.ini",reference(locale)}}};
 const auto make=[&](unsigned number,Operation op){
  Plan p;p.transactionId=hashBytes(label+std::to_string(number)).substr(0,32);p.rootIdentity=boot.rootIdentity();p.moduleId=release.moduleId;p.productId=release.productId;p.version=release.version;p.sourceIdentity=release.source;p.operation=op;
  if(op==Operation::Install){p.newManifest=release.manifest;p.newReceipt=reference(receipt);for(const auto& [path,ref]:release.owned)p.steps.push_back({path,{},ref});}
  else{p.oldManifest=release.manifest;p.oldReceipt=reference(receipt);p.predecessorEvidence=hashBytes("fixture-predecessor");for(const auto& [path,ref]:release.owned)p.steps.push_back({path,ref,{}});}
  return p;
 };
 const auto policy=[&](const Plan& p){KnownTransition k{};k.operation=p.operation;k.rootIdentity=p.rootIdentity;k.predecessorEvidence=p.predecessorEvidence;k.actualAuthenticatedOldReceipt=p.oldReceipt;k.proposedAuthenticatedNewReceipt=p.newReceipt;if(p.operation==Operation::Install)k.newRelease=release;else k.oldRelease=release;return k;};
 const std::map<std::string,Bytes> blobs{{hashBytes(dll),dll},{hashBytes(locale),locale},{hashBytes(receipt),receipt}};
 auto interrupted=make(0,Operation::Install);
 {WindowsStore store(interrupted,hashBytes(encode(interrupted)),policy(interrupted));Engine::prepare(store,interrupted,blobs);CutAfterWrite cut(store);bool caught=false;try{Engine(cut).apply();}catch(const Interrupted&){caught=true;}need(caught,"fault injected after first actual write");}
 {WindowsStore store(interrupted,hashBytes(encode(interrupted)),policy(interrupted));auto result=Engine(store).recover();need(result.rolledBack&&!store.readReceipt(),"partial install rolls back");for(const auto& row:interrupted.steps)need(!store.readOwned(row.path),"interrupted payload absent");need(decodeJournal(store.readJournal(),interrupted,hashBytes(encode(interrupted))).state==State::RolledBack,"rollback journal retained");}
 auto install=make(1,Operation::Install);
 {WindowsStore store(install,hashBytes(encode(install)),policy(install));Engine::prepare(store,install,blobs);need(Engine(store).apply().payloadReceiptCommitted,"fixture install committed");need(Engine(store).recover().payloadReceiptCommitted,"committed recovery preserves commit");}
 auto uninstall=make(2,Operation::Uninstall);
 {WindowsStore store(uninstall,hashBytes(encode(uninstall)),policy(uninstall));Engine::prepare(store,uninstall,blobs);need(Engine(store).apply().payloadReceiptCommitted&&!store.readReceipt(),"fixture uninstall committed");for(const auto& row:uninstall.steps)need(!store.readOwned(row.path),"fixture payload removed");}
 // All three transactions and blobs remain for inspection; no recursive cleanup.
 for(const auto& p:{interrupted,install,uninstall})need(fs::is_regular_file(boot.metadata/L"transactions"/fs::path(p.transactionId)/L"plan.bin"),"transaction plan preserved");
}
int wmain(int argc,wchar_t** argv){try{
 if(argc!=3||std::wstring_view(argv[1])!=L"--run-admin-fixtures"||std::wstring_view(argv[2])!=L"--isolated-vm"){
  std::cout<<"PREPARED ONLY. Run in an isolated disposable VM only, with --run-admin-fixtures --isolated-vm and a normal elevated token. This is not ordinary Setup acceptance.\n";return 0;
 }
 need(IsUserAnAdmin(),"normal administrator token required; no elevation bypass");obsStopped();
 PWSTR known{};need(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData,0,nullptr,&known)),"actual ProgramData");fs::path actual(known);CoTaskMemFree(known);
 HANDLE rawToken{};need(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&rawToken),"current token");Handle token(rawToken);DWORD size{};GetTokenInformation(token.value,TokenUser,nullptr,0,&size);std::vector<BYTE> user(size);need(GetTokenInformation(token.value,TokenUser,user.data(),size,&size),"current token SID");
 std::array<BYTE,SECURITY_MAX_SID_SIZE> admins{};DWORD as=static_cast<DWORD>(admins.size());need(CreateWellKnownSid(WinBuiltinAdministratorsSid,nullptr,admins.data(),&as),"admin SID");
 FixtureSecurity adminSecurity(actual,admins.data()),userSecurity(actual,reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid);
 // A workspace below a user-owned profile is deliberately unsuitable for the
 // restored ancestor policy. Only a new VM-local fixture tree is prepared.
 const fs::path parent=L"C:\\SonicLatch-installer-QA";NewDirectorySecurity parentSecurity;
 if(!CreateDirectoryW(parent.c_str(),&parentSecurity.attributes))need(GetLastError()==ERROR_ALREADY_EXISTS,"fixture parent");
 Handle parentPin(CreateFileW(parent.c_str(),FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES|READ_CONTROL,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
 information(parentPin.value);need(equalPath(finalPath(parentPin.value),parent),"VM fixture parent alias");protectedAcl(parentPin.value);
 for(unsigned mode=0;mode<3;++mode){
  GUID guid{};need(CoCreateGuid(&guid)==S_OK,"fresh fixture identity");std::array<wchar_t,64> id{};StringFromGUID2(guid,id.data(),static_cast<int>(id.size()));
  const auto base=parent/(std::to_wstring(mode)+L"-"+id.data());need(CreateDirectoryW(base.c_str(),&adminSecurity.sa),"fresh isolated fixture base");fixtureData=base.wstring();
  const auto obs=base/L"obs-studio",plugins=obs/L"plugins";
  if(mode){auto* sa=mode==1?&userSecurity.sa:nullptr;need(CreateDirectoryW(obs.c_str(),sa)&&CreateDirectoryW(plugins.c_str(),sa),"create shared fixture state");}
  const auto oldObs=mode?security(obs):L"",oldPlugins=mode?security(plugins):L"";
  {Bootstrap boot(true);const auto obsAcl=security(obs),pluginsAcl=security(plugins);if(mode)need(oldObs==obsAcl&&oldPlugins==pluginsAcl,"preexisting shared owner/DACL unchanged");lifecycle(boot,std::to_string(mode)+base.string());need(security(obs)==obsAcl&&security(plugins)==pluginsAcl,"shared security unchanged after lifecycle");}
  std::wcout<<L"PASS fixture mode="<<mode<<L" evidence="<<base.wstring()<<L"\n";
 }
 std::cout<<"PASS three native fixtures. Product NSIS, real HKLM registration, normal Setup and three-machine acceptance NOT TESTED.\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
