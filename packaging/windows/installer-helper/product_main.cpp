#include "product_catalog.hpp"
#include "product_bootstrap.hpp"
#include "product_registration.hpp"
#include <iostream>
using namespace soniclatch::product;
using namespace soniclatch::product::win;
namespace {
std::optional<Bytes> readPinned(Bootstrap& bootstrap,const fs::path& path){
 auto prefix=bootstrap.base;
 auto relative=path.lexically_relative(prefix);need(!relative.empty()&&*relative.begin()!=L"..","outside known storage");
 for(auto it=relative.begin();it!=relative.end();++it){auto next=it;++next;if(next==relative.end())break;prefix/=*it;
  DWORD attrs=GetFileAttributesW(prefix.c_str());if(attrs==INVALID_FILE_ATTRIBUTES){auto error=GetLastError();need(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND,"inspect parent");return {};}
  bootstrap.pin(prefix,true);
 }
 auto normalized=path;normalized.make_preferred();HANDLE raw=CreateFileW(normalized.c_str(),GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
 if(raw==INVALID_HANDLE_VALUE){need(GetLastError()==ERROR_FILE_NOT_FOUND,"open protected input");return {};}
 Handle h(raw);auto info=information(h.value);need(!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&info.nNumberOfLinks==1,"protected input type");protectedAcl(h.value);need(equalPath(finalPath(h.value),normalized),"protected input alias");
 LARGE_INTEGER size{};need(GetFileSizeEx(h.value,&size)&&size.QuadPart>=0&&static_cast<ULONGLONG>(size.QuadPart)<=fileLimit,"input bound");Bytes bytes(static_cast<size_t>(size.QuadPart),'\0');DWORD got{};
 if(!bytes.empty())need(ReadFile(h.value,bytes.data(),static_cast<DWORD>(bytes.size()),&got,nullptr)&&got==bytes.size(),"read input");char extra{};need(ReadFile(h.value,&extra,1,&got,nullptr)&&!got,"input changed");return bytes;
}
Image reference(const std::optional<Bytes>& bytes){return bytes?Image{Ref{hashBytes(*bytes),bytes->size()}}:Image{};}
struct Management {std::string tx,digest,state,helper,uninstaller;};
Bytes encodeManagement(const Management& m){Writer w;w.text("SonicLatch-management-v2");for(const auto* value:{&m.tx,&m.digest,&m.state,&m.helper,&m.uninstaller})w.text(*value);return w.b;}
Management decodeManagement(const Bytes& raw){Reader r{raw};need(r.text()=="SonicLatch-management-v2","management schema");Management m{r.text(),r.text(),r.text(),r.text(),r.text()};r.end();need(hex(m.tx,32)&&hex(m.digest,64)&&hex(m.helper,64)&&(m.uninstaller.empty()||hex(m.uninstaller,64)),"management identity");need(m.state=="registered"||m.state=="awaiting-uninstaller"||m.state=="removed-residual"||m.state=="cleanup-ready","management state");return m;}
Bytes receipt(const KnownRelease& release,const std::string& root){Writer w;w.text("SonicLatch-owned-receipt-v1");w.text(root);w.text(release.manifest);w.text(release.source);w.text(release.productId);w.text(release.moduleId);w.text(release.version);return w.b;}
const KnownRelease* oldRelease(const Bytes& bytes,const std::string& root){
 if(bytes==receipt(releaseCatalog.current,root))return &releaseCatalog.current;
 for(const auto& previous:releaseCatalog.acceptedPrevious)if(bytes==receipt(previous,root))return &previous;
 throw std::runtime_error("receipt is not an accepted owned release");
}
std::string transactionId(){std::array<unsigned char,16> bytes{};need(BCryptGenRandom(nullptr,bytes.data(),static_cast<ULONG>(bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)==0,"transaction id");std::string out;for(auto c:bytes){out+="0123456789abcdef"[c>>4];out+="0123456789abcdef"[c&15];}return out;}
KnownTransition policyFor(const Plan& p){
 KnownTransition policy{};policy.operation=p.operation;policy.rootIdentity=p.rootIdentity;policy.predecessorEvidence=p.predecessorEvidence;
 if(!p.oldManifest.empty()){
  if(releaseCatalog.current.manifest==p.oldManifest)policy.oldRelease=releaseCatalog.current;
  for(const auto& release:releaseCatalog.acceptedPrevious)if(release.manifest==p.oldManifest)policy.oldRelease=release;
  need(policy.oldRelease.has_value(),"unknown predecessor release");policy.actualAuthenticatedOldReceipt=reference(receipt(*policy.oldRelease,p.rootIdentity));
 }
 if(p.operation!=Operation::Uninstall){
  if(p.newManifest==releaseCatalog.current.manifest)policy.newRelease=releaseCatalog.current;
  for(const auto& release:releaseCatalog.acceptedPrevious)if(p.newManifest==release.manifest)policy.newRelease=release;
  need(policy.newRelease.has_value(),"target outside fixed catalog");policy.proposedAuthenticatedNewReceipt=reference(receipt(*policy.newRelease,p.rootIdentity));
 }
 if(policy.oldRelease)need(p.predecessorEvidence==hashBytes(receipt(*policy.oldRelease,p.rootIdentity)),"predecessor receipt anchor");
 authorizeTransition(p,policy);return policy;
}
Bytes selfBytes(){
 std::array<wchar_t,32768> path{};auto n=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));need(n&&n<path.size(),"module path");
 Handle h(CreateFileW(path.data(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));auto i=information(h.value);need(i.nNumberOfLinks==1&&!(i.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY),"module file");LARGE_INTEGER size{};need(GetFileSizeEx(h.value,&size)&&size.QuadPart>0&&static_cast<ULONGLONG>(size.QuadPart)<=fileLimit,"module size");Bytes b(static_cast<size_t>(size.QuadPart),'\0');DWORD got{};need(ReadFile(h.value,b.data(),static_cast<DWORD>(b.size()),&got,nullptr)&&got==b.size(),"module bytes");return b;
}
int management(WindowsStore& store,const Plan& plan,bool finalize=false){
 store.guard(plan);auto rawMarker=store.readManagement("management.bin");
 std::optional<Management> marker=rawMarker?std::optional<Management>{decodeManagement(*rawMarker)}:std::nullopt;
 auto helper=store.readManagement("helper.exe"),uninstaller=store.readManagement("Uninstall.exe");
 if(plan.operation==Operation::Uninstall){
  need(marker&&helper&&hashBytes(*helper)==marker->helper&&uninstaller&&hashBytes(*uninstaller)==marker->uninstaller,"changed management file retained");
  removeRegistration(plan,store.metadataPath());
  Management done{plan.transactionId,hashBytes(encode(plan)),"removed-residual",marker->helper,marker->uninstaller};
  store.writeManagement("management.bin",reference(rawMarker),encodeManagement(done));
  std::cout<<"SONICLATCH/1 PAYLOAD_REMOVED CLEANUP_REQUIRED\n";return 32;
 }
 auto executable=selfBytes();bool helperChanged=(helper&&*helper!=executable)||(marker&&marker->helper!=hashBytes(executable));
 if(helper&&*helper!=executable)need(marker&&hashBytes(*helper)==marker->helper,"modified helper retained");
 if(!helper||*helper!=executable)store.writeManagement("helper.exe",reference(helper),executable);
 // A retained old NSIS uninstaller embeds the old helper/catalog. Replace it
 // only after exact prior ownership validation so upgrades use the new catalog.
 if(uninstaller&&!finalize&&marker&&marker->state!="awaiting-uninstaller"&&(helperChanged||plan.oldManifest!=plan.newManifest)){
  need(!marker->uninstaller.empty()&&hashBytes(*uninstaller)==marker->uninstaller,"modified old uninstaller retained");store.removeOwnedUninstaller(reference(uninstaller));uninstaller.reset();
 }
 std::string uninstallHash;
 if(uninstaller&&!finalize&&marker&&marker->state=="awaiting-uninstaller"&&marker->tx==plan.transactionId&&marker->digest==hashBytes(encode(plan))){std::cout<<"SONICLATCH/1 AWAITING_NSIS_UNINSTALLER\n";return 33;}
 if(uninstaller){
  if(finalize){need(marker&&marker->state=="awaiting-uninstaller"&&marker->tx==plan.transactionId&&marker->digest==hashBytes(encode(plan))&&marker->helper==hashBytes(executable),"uninstaller handoff not pending");}
  else need(marker&&!marker->uninstaller.empty()&&hashBytes(*uninstaller)==marker->uninstaller,"unknown uninstaller retained");
  uninstallHash=hashBytes(*uninstaller);
 }
 Management state{plan.transactionId,hashBytes(encode(plan)),uninstaller?"registered":"awaiting-uninstaller",hashBytes(executable),uninstallHash};
 if(!uninstaller){store.writeManagement("management.bin",reference(rawMarker),encodeManagement(state));std::cout<<"SONICLATCH/1 AWAITING_NSIS_UNINSTALLER\n";return 33;}
 auto known=policyFor(plan);publishRegistration(plan,store.metadataPath(),known.oldRelease?known.oldRelease->version:std::string{});
 store.writeManagement("management.bin",reference(rawMarker),encodeManagement(state));
 std::cout<<"SONICLATCH/1 PAYLOAD_COMMITTED REGISTERED\n";return 0;
}
void erasePinned(Bootstrap& bootstrap,const fs::path& file,const Image& expected){
 auto data=readPinned(bootstrap,file);if(!data)return;
 need(reference(data)==expected,"modified evidence retained");need(DeleteFileW(file.c_str()),"owned cleanup deletion failed");
}
void removeEmptyOwnedDirectory(Bootstrap& bootstrap,const fs::path& path){
 auto attributes=GetFileAttributesW(path.c_str());if(attributes==INVALID_FILE_ATTRIBUTES){auto error=GetLastError();need(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND,"inspect cleanup directory");return;}
 need((attributes&FILE_ATTRIBUTE_DIRECTORY)&&!(attributes&FILE_ATTRIBUTE_REPARSE_POINT),"unsafe cleanup directory retained");bootstrap.pin(path,true);bootstrap.releaseDirectory(path);
 if(!RemoveDirectoryW(path.c_str()))need(GetLastError()==ERROR_DIR_NOT_EMPTY,"empty directory cleanup failed");
}
int cleanupUninstall(Bootstrap& bootstrap){
 auto raw=readPinned(bootstrap,bootstrap.metadata/L"management.bin");need(raw.has_value(),"cleanup ownership missing");auto m=decodeManagement(*raw);
 need(m.state=="removed-residual"||m.state=="cleanup-ready","uninstall cleanup not authorized");need(!readPinned(bootstrap,bootstrap.metadata/L"receipt.bin"),"installed receipt still present");
 if(m.state=="removed-residual"){
  auto rawPlan=readPinned(bootstrap,bootstrap.metadata/L"transactions"/fs::path(m.tx)/L"plan.bin");need(rawPlan&&hashBytes(*rawPlan)==m.digest,"uninstall plan anchor");auto plan=decodePlan(*rawPlan);need(plan.operation==Operation::Uninstall,"uninstall plan required");
  WindowsStore store(plan,m.digest,policyFor(plan));need(Engine(store).recover().payloadReceiptCommitted,"uninstall payload incomplete");
  m.state="cleanup-ready";store.writeManagement("management.bin",reference(raw),encodeManagement(m));
 }
 // Native worker runs from the NSIS temporary directory. Retain ownership
 // marker until NSIS has deleted both fixed executables after this process exits.
 Handle lock(CreateFileW((bootstrap.metadata/L"transaction.lock").c_str(),GENERIC_READ|GENERIC_WRITE|READ_CONTROL,0,nullptr,OPEN_ALWAYS,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
 auto li=information(lock.value);need(li.nNumberOfLinks==1&&!(li.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY),"cleanup lock type");protectedAcl(lock.value);obsStopped();
 for(const auto& pair:{std::pair{L"helper.exe",m.helper},std::pair{L"Uninstall.exe",m.uninstaller}}){auto bytes=readPinned(bootstrap,bootstrap.metadata/pair.first);need(!bytes||hashBytes(*bytes)==pair.second,"changed bootstrap executable retained");}
 WIN32_FIND_DATAW found{};HANDLE search=FindFirstFileW((bootstrap.metadata/L"transactions"/L"*").c_str(),&found);
 need(search!=INVALID_HANDLE_VALUE,"transaction enumeration");
 try {do {std::wstring name=found.cFileName;if(name==L"."||name==L"..")continue;std::string id(name.begin(),name.end());if(!hex(id,32))continue;
   need(!(found.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT),"unsafe transaction entry retained");if(!(found.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))continue;
   auto dir=bootstrap.metadata/L"transactions"/name;auto rawPlan=readPinned(bootstrap,dir/L"plan.bin");if(!rawPlan){removeEmptyOwnedDirectory(bootstrap,dir/L"blobs");removeEmptyOwnedDirectory(bootstrap,dir);continue;}
   auto plan=decodePlan(*rawPlan);need(plan.transactionId==id&&plan.rootIdentity==bootstrap.rootIdentity(),"transaction cleanup identity");policyFor(plan);
   auto journal=readPinned(bootstrap,dir/L"journal.bin");if(journal)decodeJournal(*journal,plan,hashBytes(*rawPlan));
   std::map<std::string,Ref> owned;auto add=[&](const Image& ref){if(ref)owned.emplace(ref->sha256,*ref);};for(const auto& step:plan.steps){add(step.before);add(step.after);}add(plan.oldReceipt);add(plan.newReceipt);
   for(const auto& [digest,ref]:owned)erasePinned(bootstrap,dir/L"blobs"/fs::path(digest),ref);
   if(journal)erasePinned(bootstrap,dir/L"journal.bin",reference(journal));
   erasePinned(bootstrap,dir/L"plan.bin",reference(rawPlan));
   // Release only these inspected children; parent remains pinned. Unknown
   // children make nonrecursive removal fail harmlessly and remain untouched.
   removeEmptyOwnedDirectory(bootstrap,dir/L"blobs");removeEmptyOwnedDirectory(bootstrap,dir);
  }while(FindNextFileW(search,&found));need(GetLastError()==ERROR_NO_MORE_FILES,"transaction enumeration incomplete");
 }catch(...){FindClose(search);throw;}FindClose(search);
 auto active=readPinned(bootstrap,bootstrap.metadata/L"active.bin");if(active){Reader r{*active};need(r.text()=="SonicLatch-active-v1"&&r.text()==m.tx,"active cleanup identity");r.end();erasePinned(bootstrap,bootstrap.metadata/L"active.bin",reference(active));}
 std::cout<<"SONICLATCH/1 CLEANUP_READY\n";return 0;
}
int run(int argc,wchar_t** argv){
 // Fail before directories/locks/registry are changed when catalog is absent.
 if(!releaseCatalog.packageApproved){std::cout<<"SONICLATCH/1 DRAFT_BLOCKED\n";return 90;}
 validateKnown(releaseCatalog.current);need(releaseCatalog.current.productId==releaseCatalog.productId,"catalog product");
 need(argc==2||argc==3,"unsupported arguments");const std::wstring command=argv[1];
 const bool finalizing=command==L"--finalize-registration";const bool cleanup=command==L"--cleanup-uninstall";const bool recovering=command==L"--recover"||command==L"--recover-active"||finalizing;need((command==L"--recover"&&argc==3)||((command==L"--recover-active"||finalizing||cleanup)&&argc==2)||(!recovering&&argc==2&&(command==L"--install"||command==L"--repair"||command==L"--uninstall")),"fixed product command required");
 Bootstrap bootstrap(command==L"--install");
 if(cleanup)return cleanupUninstall(bootstrap);
 if(command==L"--install"){auto raw=readPinned(bootstrap,bootstrap.metadata/L"management.bin");if(raw&&decodeManagement(*raw).state=="cleanup-ready"){std::cout<<"SONICLATCH/1 CLEANUP_PENDING\n";return 34;}}
 if(command==L"--uninstall"){auto raw=readPinned(bootstrap,bootstrap.metadata/L"management.bin");if(raw){auto m=decodeManagement(*raw);if(m.state=="cleanup-ready"||m.state=="removed-residual"){std::cout<<"SONICLATCH/1 PAYLOAD_REMOVED CLEANUP_REQUIRED\n";return 32;}}}
 if(recovering){std::string tx;
  if(command==L"--recover-active"||finalizing){auto active=readPinned(bootstrap,bootstrap.metadata/L"active.bin");need(active.has_value(),"no active transaction");Reader reader{*active};need(reader.text()=="SonicLatch-active-v1","active schema");tx=reader.text();reader.end();}
  else {std::wstring value=argv[2];tx.assign(value.begin(),value.end());}
  need(hex(tx,32),"recovery transaction identity");auto raw=readPinned(bootstrap,bootstrap.metadata/L"transactions"/fs::path(tx)/L"plan.bin");need(raw.has_value(),"recovery plan missing");auto p=decodePlan(*raw);need(p.transactionId==tx&&p.rootIdentity==bootstrap.rootIdentity(),"recovery root identity");auto policy=policyFor(p);WindowsStore store(p,hashBytes(*raw),policy);auto result=Engine(store).recover();if(result.rolledBack)return 30;return management(store,p,finalizing);}
 auto priorReceipt=readPinned(bootstrap,bootstrap.metadata/L"receipt.bin");const KnownRelease* prior=priorReceipt?oldRelease(*priorReceipt,bootstrap.rootIdentity()):nullptr;
 need(command!=L"--uninstall"||prior,"no owned installation");need(command!=L"--repair"||(prior&&prior->manifest==releaseCatalog.current.manifest),"repair current release only");
 if(!prior){auto marker=readPinned(bootstrap,bootstrap.metadata/L"management.bin");auto uninstall=readPinned(bootstrap,bootstrap.metadata/L"Uninstall.exe");auto helper=readPinned(bootstrap,bootstrap.metadata/L"helper.exe");need((!uninstall&&!helper)||marker.has_value(),"unknown bootstrap files retained");}
 Plan p;p.transactionId=transactionId();p.rootIdentity=bootstrap.rootIdentity();
 p.operation=command==L"--uninstall"?Operation::Uninstall:command==L"--repair"?Operation::Repair:prior?Operation::Upgrade:Operation::Install;
 const auto& target=p.operation==Operation::Uninstall?*prior:releaseCatalog.current;
 p.moduleId=target.moduleId;p.productId=target.productId;p.version=target.version;p.sourceIdentity=target.source;
 p.oldManifest=prior?prior->manifest:"";p.newManifest=p.operation==Operation::Uninstall?"":target.manifest;
 p.oldReceipt=reference(priorReceipt);p.predecessorEvidence=priorReceipt?hashBytes(*priorReceipt):"";
 std::optional<Bytes> nextReceipt=p.operation==Operation::Uninstall?std::nullopt:std::optional<Bytes>{receipt(target,p.rootIdentity)};p.newReceipt=reference(nextReceipt);
 std::map<std::string,Bytes> blobs;std::set<std::string> paths;
 if(prior)for(const auto& [path,image]:prior->owned){(void)image;paths.insert(path);}
 if(nextReceipt)for(const auto& [path,image]:target.owned){(void)image;paths.insert(path);}
 for(const auto& path:paths){need(safePath(path),"catalog path");auto before=readPinned(bootstrap,bootstrap.root/fs::path(path));Image after;
  if(nextReceipt){auto i=target.owned.find(path);if(i!=target.owned.end()){after=i->second;auto found=releaseCatalog.blobs.find(after->sha256);need(found!=releaseCatalog.blobs.end()&&reference(found->second)==after,"compiled payload bytes mismatch");blobs.emplace(found->first,found->second);}}
  p.steps.push_back({path,reference(before),after});if(before)blobs.emplace(hashBytes(*before),*before);
 }
 if(priorReceipt)blobs.emplace(hashBytes(*priorReceipt),*priorReceipt);if(nextReceipt)blobs.emplace(hashBytes(*nextReceipt),*nextReceipt);
 auto policy=policyFor(p);WindowsStore store(p,hashBytes(encode(p)),policy);
 // Refuse a previous incomplete transaction instead of silently starting again.
 auto active=store.readManagement("active.bin");if(active){Reader r{*active};need(r.text()=="SonicLatch-active-v1","active schema");auto tx=r.text();r.end();need(hex(tx,32),"active identity");auto journal=readPinned(bootstrap,bootstrap.metadata/L"transactions"/fs::path(tx)/L"journal.bin");auto oldPlan=readPinned(bootstrap,bootstrap.metadata/L"transactions"/fs::path(tx)/L"plan.bin");need(journal&&oldPlan,"incomplete preparation retained");auto previous=decodePlan(*oldPlan);auto state=decodeJournal(*journal,previous,hashBytes(*oldPlan)).state;need(state==State::Committed||state==State::RolledBack,"recover previous transaction first");
  if(state==State::Committed){auto done=store.readManagement("management.bin");need(done.has_value(),"recover incomplete registration first");auto m=decodeManagement(*done);need(m.tx==tx&&m.digest==hashBytes(*oldPlan)&&m.state==(previous.operation==Operation::Uninstall?"removed-residual":"registered"),"recover previous management first");auto helper=store.readManagement("helper.exe"),uninstaller=store.readManagement("Uninstall.exe");need(helper&&hashBytes(*helper)==m.helper&&uninstaller&&hashBytes(*uninstaller)==m.uninstaller,"modified management files retained");}
 }
 Engine::prepare(store,p,blobs);
 Writer anchor;anchor.text("SonicLatch-active-v1");anchor.text(p.transactionId);store.writeManagement("active.bin",reference(active),anchor.b);
 Engine(store).apply();return management(store,p);
}
}
int wmain(int argc,wchar_t** argv){try{return run(argc,argv);}catch(const std::exception& e){std::cerr<<"SonicLatch: "<<e.what()<<"\n";std::cout<<"SONICLATCH/1 STOPPED_RECOVERY_MAY_BE_REQUIRED\n";return 31;}}
