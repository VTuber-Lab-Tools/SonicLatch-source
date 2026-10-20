#include "product_catalog.hpp"
#include "product_bootstrap.hpp"
#include "product_registration.hpp"
#include "product_uninstaller_slot.hpp"
#include "product_management.hpp"
#include <iostream>
using namespace soniclatch::product;
using namespace soniclatch::product::win;
namespace {
std::optional<Bytes> readPinned(Bootstrap& bootstrap,const fs::path& path,std::string* fileId=nullptr){
 auto prefix=bootstrap.base;
 auto relative=path.lexically_relative(prefix);need(!relative.empty()&&*relative.begin()!=L"..","outside known storage");
 for(auto it=relative.begin();it!=relative.end();++it){auto next=it;++next;if(next==relative.end())break;prefix/=*it;
  DWORD attrs=GetFileAttributesW(prefix.c_str());if(attrs==INVALID_FILE_ATTRIBUTES){auto error=GetLastError();need(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND,"inspect parent");return {};}
  bootstrap.pin(prefix);
 }
 auto normalized=path;normalized.make_preferred();HANDLE raw=CreateFileW(normalized.c_str(),GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
 if(raw==INVALID_HANDLE_VALUE){need(GetLastError()==ERROR_FILE_NOT_FOUND,"open protected input");return {};}
 Handle h(raw);auto info=information(h.value);need(!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&info.nNumberOfLinks==1,"protected input type");protectedAcl(h.value);need(equalPath(finalPath(h.value),normalized),"protected input alias");
 LARGE_INTEGER size{};need(GetFileSizeEx(h.value,&size)&&size.QuadPart>=0&&static_cast<ULONGLONG>(size.QuadPart)<=fileLimit,"input bound");Bytes bytes(static_cast<size_t>(size.QuadPart),'\0');DWORD got{};
 if(!bytes.empty())need(ReadFile(h.value,bytes.data(),static_cast<DWORD>(bytes.size()),&got,nullptr)&&got==bytes.size(),"read input");char extra{};need(ReadFile(h.value,&extra,1,&got,nullptr)&&!got,"input changed");if(fileId)*fileId=identity(h.value);return bytes;
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
 if(marker&&registeredManagementComplete(store,plan,*marker,helper,uninstaller,[&]{RegKey key;need(RegOpenKeyExW(HKEY_LOCAL_MACHINE,uninstallKey,0,READ_CONTROL|KEY_QUERY_VALUE|KEY_WOW64_64KEY,&key.value)==ERROR_SUCCESS,"registered key missing; retained");exactRegistration(key.value,plan,store.metadataPath());})){
  std::cout<<"SONICLATCH/1 PAYLOAD_COMMITTED REGISTERED\n";return 0;
 }
 auto executable=selfBytes();bool helperChanged=(helper&&*helper!=executable)||(marker&&marker->helper!=hashBytes(executable));
 if(marker&&marker->state=="awaiting-uninstaller")need(marker->helper==hashBytes(executable),"resume pending handoff with matching reviewed helper");
 if(helper&&*helper!=executable)need(marker&&hashBytes(*helper)==marker->helper,"modified helper retained");
 if(!helper||*helper!=executable)store.writeManagement("helper.exe",reference(helper),executable);
 // A retained old NSIS uninstaller embeds the old helper/catalog. Replace it
 // only after exact prior ownership validation so upgrades use the new catalog.
 if(uninstaller&&!finalize&&marker&&marker->state!="awaiting-uninstaller"&&(helperChanged||plan.oldManifest!=plan.newManifest)){
  need(!marker->uninstaller.empty()&&hashBytes(*uninstaller)==marker->uninstaller,"modified old uninstaller retained");store.removeOwnedUninstaller(reference(uninstaller));uninstaller.reset();
 }
 std::string uninstallHash;const auto helperHash=hashBytes(executable);
 if(marker&&marker->state=="awaiting-uninstaller"){
  need(marker->tx==plan.transactionId&&marker->digest==hashBytes(encode(plan))&&marker->helper==helperHash,"uninstaller handoff identity");
  UninstallerSlots slots(store,plan,helperHash);
  if(slots.hasSeal()){slots.resumeSealed();uninstaller=store.readManagement("Uninstall.exe");}
  else need(!uninstaller,"unsealed final uninstaller retained");
 }
 if(uninstaller){
  if(marker&&marker->state=="awaiting-uninstaller"){UninstallerSlots slots(store,plan,helperHash);need(slots.resumeSealed().image==*reference(uninstaller),"sealed uninstaller readback");}
  else need(marker&&!marker->uninstaller.empty()&&hashBytes(*uninstaller)==marker->uninstaller,"unknown uninstaller retained");
  uninstallHash=hashBytes(*uninstaller);
 }
 Management state{plan.transactionId,hashBytes(encode(plan)),uninstaller?"registered":"awaiting-uninstaller",hashBytes(executable),uninstallHash};
 if(!uninstaller){store.writeManagement("management.bin",reference(rawMarker),encodeManagement(state));std::cout<<"SONICLATCH/1 AWAITING_NSIS_UNINSTALLER\n";return 33;}
 need(decodeJournal(store.readJournal(),plan,hashBytes(encode(plan))).state==State::Committed,"registration requires committed payload");
 RegistrationIntentIO intent{[&]{return store.readRegistrationIntent();},[&](const Image& expected,const Bytes& bytes){store.writeRegistrationIntent(expected,bytes);}};
 auto known=policyFor(plan);publishRegistration(plan,store.metadataPath(),known.oldRelease?known.oldRelease->version:std::string{},&intent);
 store.writeManagement("management.bin",reference(rawMarker),encodeManagement(state));
 std::cout<<"SONICLATCH/1 PAYLOAD_COMMITTED REGISTERED\n";return 0;
}
std::string boundedHex(const wchar_t* text){std::string out;for(wchar_t c:std::wstring_view(text)){need((c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f'),"hex command identity required");out+=static_cast<char>(c);}need(hex(out,32),"bounded command identity required");return out;}
int uninstallerSlotCommand(WindowsStore& store,const Plan& plan,const std::wstring& command,const std::string& requested){
 need(plan.operation!=Operation::Uninstall&&decodeJournal(store.readJournal(),plan,hashBytes(encode(plan))).state==State::Committed,"slot requires committed install");
 auto markerBytes=store.readManagement("management.bin");need(markerBytes.has_value(),"slot management ownership missing");auto marker=decodeManagement(*markerBytes);const auto helperHash=hashBytes(selfBytes());
 need(marker.state=="awaiting-uninstaller"&&marker.tx==plan.transactionId&&marker.digest==hashBytes(encode(plan))&&marker.helper==helperHash,"slot handoff not pending");
 auto helper=store.readManagement("helper.exe");need(helper&&hashBytes(*helper)==helperHash,"slot helper changed");UninstallerSlots slots(store,plan,helperHash);
 if(command==L"--prepare-uninstaller-slot"){
  auto [slot,resumed]=slots.prepare(transactionId());if(resumed){std::cout<<"SONICLATCH/1 UNINSTALLER_PROMOTED\n";return 36;}
  std::cout<<"SONICLATCH/1 UNINSTALLER_SLOT "<<plan.transactionId<<" "<<slot<<"\n";return 35;
 }
 if(command==L"--verify-uninstaller-slot"){slots.ready(requested);std::cout<<"SONICLATCH/1 UNINSTALLER_SLOT_VERIFIED\n";return 0;}
 if(command==L"--seal-uninstaller-slot"){slots.sealAfterNsisSuccess(requested);std::cout<<"SONICLATCH/1 UNINSTALLER_SEALED\n";return 0;}
 need(command==L"--promote-uninstaller-slot","fixed slot command required");slots.promote(requested);std::cout<<"SONICLATCH/1 UNINSTALLER_PROMOTED\n";return 0;
}
void erasePinned(Bootstrap& bootstrap,const fs::path& file,const Image& expected){
 auto data=readPinned(bootstrap,file);if(!data)return;
 need(reference(data)==expected,"modified evidence retained");need(DeleteFileW(file.c_str()),"owned cleanup deletion failed");
}
void removeEmptyOwnedDirectory(Bootstrap& bootstrap,const fs::path& path){
 auto attributes=GetFileAttributesW(path.c_str());if(attributes==INVALID_FILE_ATTRIBUTES){auto error=GetLastError();need(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND,"inspect cleanup directory");return;}
 need((attributes&FILE_ATTRIBUTE_DIRECTORY)&&!(attributes&FILE_ATTRIBUTE_REPARSE_POINT),"unsafe cleanup directory retained");bootstrap.pin(path);bootstrap.releaseDirectory(path);
 if(!RemoveDirectoryW(path.c_str()))need(GetLastError()==ERROR_DIR_NOT_EMPTY,"empty directory cleanup failed");
}
bool cleanupUninstallerSlots(Bootstrap& bootstrap,const fs::path& transaction,const Plan& plan){
 const auto slots=transaction/L"uninstaller-slots";auto anchor=readPinned(bootstrap,transaction/L"uninstaller-slot.bin");
 const auto attributes=GetFileAttributesW(slots.c_str());
 if(attributes==INVALID_FILE_ATTRIBUTES){const auto error=GetLastError();need(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND,"slot cleanup inspection");need(!anchor,"slot anchor without directory retained");return true;}
 need((attributes&FILE_ATTRIBUTE_DIRECTORY)&&!(attributes&FILE_ATTRIBUTE_REPARSE_POINT),"unsafe slot directory retained");bootstrap.pin(slots);
 struct Record {fs::path dir;Bytes intent;std::optional<Bytes> seal,publish;std::optional<WindowsStore::Artifact> stage,copy;fs::path copyPath;};
 std::vector<std::wstring> unknownCopies;WIN32_FIND_DATAW candidate{};HANDLE copies=FindFirstFileW((bootstrap.metadata/L"*").c_str(),&candidate);need(copies!=INVALID_HANDLE_VALUE,"publish cleanup enumeration");
 try{do{std::wstring name=candidate.cFileName;if(name.starts_with(L"Uninstall.publish-"+widenAscii(plan.transactionId)+L"-"))unknownCopies.push_back(name);}while(FindNextFileW(copies,&candidate));need(GetLastError()==ERROR_NO_MORE_FILES,"publish enumeration incomplete");}catch(...){FindClose(copies);throw;}FindClose(copies);
 std::vector<Record> records;bool residual=false;WIN32_FIND_DATAW entry{};HANDLE search=FindFirstFileW((slots/L"*").c_str(),&entry);need(search!=INVALID_HANDLE_VALUE,"slot cleanup enumeration");
 try {do {
  std::wstring name=entry.cFileName;if(name==L"."||name==L"..")continue;
  if(name.size()!=32||!std::all_of(name.begin(),name.end(),[](wchar_t c){return(c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f');})){residual=true;continue;}
  need((entry.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&!(entry.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT),"unsafe slot entry retained");std::string id;for(wchar_t c:name)id+=static_cast<char>(c);
  auto dir=slots/name;bootstrap.pin(dir);auto intent=readPinned(bootstrap,dir/L"intent.bin");if(!intent){residual=true;continue;}
  auto binding=decodeSlotIntent(*intent);need(binding.tx==plan.transactionId&&binding.plan==hashBytes(encode(plan))&&binding.slot==id,"slot cleanup ownership mismatch");
  auto seal=readPinned(bootstrap,dir/L"seal.bin");std::string fileId;auto stageBytes=readPinned(bootstrap,dir/L"Uninstall.pending.exe",&fileId);std::optional<WindowsStore::Artifact> stage;
  if(stageBytes)stage=WindowsStore::Artifact{*reference(stageBytes),fileId};
  if(seal){auto expected=decodeSlotSeal(*seal,*intent);need(!stage||*stage==expected,"changed sealed slot retained");}
  else if(stage)residual=true; // Preserve the entire transaction and its proof.
  auto publish=readPinned(bootstrap,dir/L"publish.bin");std::optional<WindowsStore::Artifact> copy;fs::path copyPath;
  if(publish){need(seal.has_value(),"publish without NSIS seal retained");auto published=decodeUninstallerPublish(*publish,plan.transactionId,hashBytes(encode(plan)),id,*seal);auto original=decodeSlotSeal(*seal,*intent);need(published.image==original.image&&published.id!=original.id,"publish/original identity mismatch");copyPath=bootstrap.metadata/uninstallerPublishName(plan.transactionId,id,published.nonce);std::erase(unknownCopies,copyPath.filename().wstring());std::string copyId;auto bytes=readPinned(bootstrap,copyPath,&copyId);if(bytes){copy=WindowsStore::Artifact{*reference(bytes),copyId};need(copy->id==published.id&&copy->image==published.image,"changed publish copy retained");need(!readPinned(bootstrap,bootstrap.metadata/L"Uninstall.exe"),"both publish copy/final present; cleanup retained");}}
  WIN32_FIND_DATAW child{};HANDLE children=FindFirstFileW((dir/L"*").c_str(),&child);need(children!=INVALID_HANDLE_VALUE,"slot child enumeration");
  try {do {std::wstring childName=child.cFileName;if(childName==L"."||childName==L"..")continue;if(childName!=L"intent.bin"&&childName!=L"seal.bin"&&childName!=L"publish.bin"&&childName!=L"Uninstall.pending.exe")residual=true;}while(FindNextFileW(children,&child));need(GetLastError()==ERROR_NO_MORE_FILES,"slot child enumeration incomplete");}catch(...){FindClose(children);throw;}FindClose(children);
  records.push_back({dir,*intent,seal,publish,stage,copy,copyPath});
 }while(FindNextFileW(search,&entry));need(GetLastError()==ERROR_NO_MORE_FILES,"slot enumeration incomplete");}catch(...){FindClose(search);throw;}FindClose(search);
 if(anchor){auto binding=decodeSlotIntent(*anchor);need(binding.tx==plan.transactionId&&binding.plan==hashBytes(encode(plan)),"slot anchor cleanup mismatch");need(std::any_of(records.begin(),records.end(),[&](const Record& r){return r.intent==*anchor;}),"slot anchor record missing");}
 if(residual||!unknownCopies.empty()){std::cerr<<"SonicLatch: unsealed/unknown slot or publish copy retained with plan, journal and blobs\n";return false;}
 // Clear the authenticated anchor before deleting any referenced slot proof.
 // After an interruption, surviving records still authenticate against plan;
 // an anchor can never point at an intent that this cleanup already removed.
 if(anchor)erasePinned(bootstrap,transaction/L"uninstaller-slot.bin",reference(anchor));
 for(const auto& record:records){
  if(record.copy){std::string id;auto actual=readPinned(bootstrap,record.copyPath,&id);need(actual&&id==record.copy->id&&reference(actual)==Image{record.copy->image},"publish copy changed before cleanup");erasePinned(bootstrap,record.copyPath,record.copy->image);}
  if(record.publish)erasePinned(bootstrap,record.dir/L"publish.bin",reference(record.publish));
  if(record.stage){std::string id;auto actual=readPinned(bootstrap,record.dir/L"Uninstall.pending.exe",&id);need(actual&&id==record.stage->id&&reference(actual)==Image{record.stage->image},"slot changed before cleanup");erasePinned(bootstrap,record.dir/L"Uninstall.pending.exe",record.stage->image);}
  if(record.seal)erasePinned(bootstrap,record.dir/L"seal.bin",reference(record.seal));
  erasePinned(bootstrap,record.dir/L"intent.bin",reference(record.intent));removeEmptyOwnedDirectory(bootstrap,record.dir);
 }
 removeEmptyOwnedDirectory(bootstrap,slots);return true;
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
 markProtectedMutationRisk();Handle lock(CreateFileW((bootstrap.metadata/L"transaction.lock").c_str(),GENERIC_READ|GENERIC_WRITE|READ_CONTROL,0,nullptr,OPEN_ALWAYS,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
 auto li=information(lock.value);need(li.nNumberOfLinks==1&&!(li.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY),"cleanup lock type");protectedAcl(lock.value);obsStopped();
 for(const auto& pair:{std::pair{L"helper.exe",m.helper},std::pair{L"Uninstall.exe",m.uninstaller}}){auto bytes=readPinned(bootstrap,bootstrap.metadata/pair.first);need(!bytes||hashBytes(*bytes)==pair.second,"changed bootstrap executable retained");}
 WIN32_FIND_DATAW found{};HANDLE search=FindFirstFileW((bootstrap.metadata/L"transactions"/L"*").c_str(),&found);
 need(search!=INVALID_HANDLE_VALUE,"transaction enumeration");
 try {do {std::wstring name=found.cFileName;if(name==L"."||name==L"..")continue;std::string id(name.begin(),name.end());if(!hex(id,32))continue;
   need(!(found.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT),"unsafe transaction entry retained");if(!(found.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))continue;
   auto dir=bootstrap.metadata/L"transactions"/name;auto rawPlan=readPinned(bootstrap,dir/L"plan.bin");if(!rawPlan){removeEmptyOwnedDirectory(bootstrap,dir/L"blobs");removeEmptyOwnedDirectory(bootstrap,dir);continue;}
   auto plan=decodePlan(*rawPlan);need(plan.transactionId==id&&plan.rootIdentity==bootstrap.rootIdentity(),"transaction cleanup identity");policyFor(plan);
   auto journal=readPinned(bootstrap,dir/L"journal.bin");if(journal)decodeJournal(*journal,plan,hashBytes(*rawPlan));
   if(!cleanupUninstallerSlots(bootstrap,dir,plan))continue;
   auto regIntent=readPinned(bootstrap,dir/L"registration-intent.bin");if(regIntent){need(plan.operation!=Operation::Uninstall,"unexpected uninstall registration intent retained");registrationStage(*regIntent,plan,bootstrap.metadata);}
   std::map<std::string,Ref> owned;auto add=[&](const Image& ref){if(ref)owned.emplace(ref->sha256,*ref);};for(const auto& step:plan.steps){add(step.before);add(step.after);}add(plan.oldReceipt);add(plan.newReceipt);
   for(const auto& [digest,ref]:owned)erasePinned(bootstrap,dir/L"blobs"/fs::path(digest),ref);
   if(journal)erasePinned(bootstrap,dir/L"journal.bin",reference(journal));
   if(regIntent)erasePinned(bootstrap,dir/L"registration-intent.bin",reference(regIntent));
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
 need(argc>=2&&argc<=4,"unsupported arguments");const std::wstring command=argv[1];
 const bool slotPrepare=command==L"--prepare-uninstaller-slot",slotAction=command==L"--verify-uninstaller-slot"||command==L"--seal-uninstaller-slot"||command==L"--promote-uninstaller-slot",slotCommand=slotPrepare||slotAction;
 const bool finalizing=command==L"--finalize-registration";const bool cleanup=command==L"--cleanup-uninstall";const bool recovering=command==L"--recover"||command==L"--recover-active"||finalizing||slotCommand;need((command==L"--recover"&&argc==3)||(slotAction&&argc==4)||((command==L"--recover-active"||finalizing||cleanup||slotPrepare)&&argc==2)||(!recovering&&argc==2&&(command==L"--install"||command==L"--repair"||command==L"--uninstall")),"fixed product command required");
 Bootstrap bootstrap(command==L"--install");
 if(cleanup)return cleanupUninstall(bootstrap);
 if(command==L"--install"){auto raw=readPinned(bootstrap,bootstrap.metadata/L"management.bin");if(raw&&decodeManagement(*raw).state=="cleanup-ready"){std::cout<<"SONICLATCH/1 CLEANUP_PENDING\n";return 34;}}
 if(command==L"--uninstall"){auto raw=readPinned(bootstrap,bootstrap.metadata/L"management.bin");if(raw){auto m=decodeManagement(*raw);if(m.state=="cleanup-ready"||m.state=="removed-residual"){std::cout<<"SONICLATCH/1 PAYLOAD_REMOVED CLEANUP_REQUIRED\n";return 32;}}}
 if(recovering){std::string tx;
  if(command==L"--recover-active"||finalizing||slotCommand){auto active=readPinned(bootstrap,bootstrap.metadata/L"active.bin");need(active.has_value(),"no active transaction");Reader reader{*active};need(reader.text()=="SonicLatch-active-v1","active schema");tx=reader.text();reader.end();if(slotAction)need(tx==boundedHex(argv[2]),"slot transaction is not active");}
  else {std::wstring value=argv[2];tx.assign(value.begin(),value.end());}
  need(hex(tx,32),"recovery transaction identity");auto raw=readPinned(bootstrap,bootstrap.metadata/L"transactions"/fs::path(tx)/L"plan.bin");need(raw.has_value(),"recovery plan missing");auto p=decodePlan(*raw);need(p.transactionId==tx&&p.rootIdentity==bootstrap.rootIdentity(),"recovery root identity");auto policy=policyFor(p);WindowsStore store(p,hashBytes(*raw),policy);if(slotCommand)return uninstallerSlotCommand(store,p,command,slotAction?boundedHex(argv[3]):std::string{});auto result=Engine(store).recover();if(result.rolledBack)return 30;return management(store,p,finalizing);}
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
int wmain(int argc,wchar_t** argv){try{return run(argc,argv);}catch(const PreflightBlocked& e){std::cerr<<"SonicLatch: "<<e.what()<<"\n";const auto code=blockedExitCode();std::cout<<(code==40?"SONICLATCH/1 PREFLIGHT_BLOCKED_NO_CHANGES\n":"SONICLATCH/1 STOPPED_RECOVERY_MAY_BE_REQUIRED\n");return code;}catch(const std::exception& e){std::cerr<<"SonicLatch: "<<e.what()<<"\n";std::cout<<"SONICLATCH/1 STOPPED_RECOVERY_MAY_BE_REQUIRED\n";return 31;}}
