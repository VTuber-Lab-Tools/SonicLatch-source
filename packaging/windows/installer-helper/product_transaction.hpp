#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Native variable-plan engine. No command-line entry, fixture bypass or built-in
// trust allowlist. Windows protected I/O must implement Store's strict contract.
namespace soniclatch::product {
using Bytes = std::string;
struct Ref { std::string sha256; std::uint64_t size{}; bool operator==(const Ref&) const = default; };
using Image = std::optional<Ref>;
enum class Operation : std::uint32_t { Install, Upgrade, Repair, Uninstall };
struct Step { std::string path; Image before, after; };
struct Plan {
 std::string transactionId, rootIdentity, moduleId, productId, version;
 std::string oldManifest, newManifest, sourceIdentity, predecessorEvidence;
 Operation operation{};
 std::vector<Step> steps;
 Image oldReceipt, newReceipt;
};
enum class State : std::uint32_t { Prepared, Applying, PayloadVerified, ReceiptPublished, Committed, RollingBack, RolledBack };
struct Journal { std::string transactionId, planDigest; State state{}; std::uint32_t step{}, phase{}; };
struct Result { bool payloadReceiptCommitted{}; bool rolledBack{}; bool managementComplete=false; bool releaseAllowed=false; };
inline void need(bool ok, const char* why) { if(!ok) throw std::runtime_error(why); }
inline bool hex(std::string_view s, std::size_t n) { return s.size()==n && std::all_of(s.begin(),s.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}); }
inline std::string fold(std::string s) { for(auto& c:s) if(c>='A'&&c<='Z') c=static_cast<char>(c+32); return s; }
inline bool safePath(const std::string& s) {
 if(s!="SonicLatch/SonicLatch.dll" && s!="SonicLatch/bin/64bit/SonicLatch.dll" && s.rfind("SonicLatch/data/",0)!=0) return false;
 if(s.size()>240 || s.back()=='/') return false;
 for(std::size_t p=0;p<s.size();) {
  auto end=s.find('/',p); if(end==s.npos) end=s.size(); auto part=s.substr(p,end-p);
  if(part.empty() || part=="." || part==".." || part.back()=='.') return false;
  for(char c:part) if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.')) return false;
  auto stem=fold(part.substr(0,part.find('.')));
  if(stem=="con"||stem=="prn"||stem=="aux"||stem=="nul"||(stem.size()==4&&(stem.substr(0,3)=="com"||stem.substr(0,3)=="lpt")&&stem[3]>='1'&&stem[3]<='9')) return false;
  p=end+1;
 } return true;
}
constexpr std::uint64_t fileLimit=32ULL*1024*1024, totalLimit=128ULL*1024*1024;
inline void refValid(const Image& r) { if(r) need(hex(r->sha256,64)&&r->size<=fileLimit,"invalid image reference"); }
inline void validate(const Plan& p) {
 need(hex(p.transactionId,32)&&!p.rootIdentity.empty()&&p.rootIdentity.size()<=1024,"transaction/root identity required");
 need(!p.moduleId.empty()&&!p.productId.empty()&&!p.version.empty(),"product identity required");
 need(static_cast<unsigned>(p.operation)<=3,"invalid operation");
 need(!p.steps.empty()&&p.steps.size()<=512&&hex(p.sourceIdentity,64),"bounded plan/source identity required");
 if(p.operation==Operation::Install) need(p.oldManifest.empty()&&!p.oldReceipt&&hex(p.newManifest,64)&&p.newReceipt.has_value(),"install context");
 else { need(hex(p.oldManifest,64)&&p.oldReceipt&&hex(p.predecessorEvidence,64),"owned receipt/trust context required");
  if(p.operation==Operation::Uninstall) need(p.newManifest.empty()&&!p.newReceipt,"uninstall receipt must be absent");
  else need(hex(p.newManifest,64)&&p.newReceipt.has_value(),"new owned receipt required");
 }
 refValid(p.oldReceipt);refValid(p.newReceipt);
 if(p.operation==Operation::Repair) need(p.oldManifest==p.newManifest,"repair identity mismatch");
 std::vector<std::string> paths; std::uint64_t total=0; bool dll=false;
 for(const auto& s:p.steps) {
  need(safePath(s.path),"payload path refused");auto key=fold(s.path);
  for(const auto& prior:paths) need(key!=prior&&key.rfind(prior+'/',0)!=0&&prior.rfind(key+'/',0)!=0,"path collision");
  paths.push_back(key);dll|=s.path=="SonicLatch/SonicLatch.dll"||s.path=="SonicLatch/bin/64bit/SonicLatch.dll";refValid(s.before);refValid(s.after);
  if(p.operation==Operation::Install) need(!s.before,"install cannot acquire prior file");
  if(p.operation==Operation::Uninstall) need(!s.after,"uninstall cannot create payload");
  total+=(s.before?s.before->size:0)+(s.after?s.after->size:0);need(total<=totalLimit,"plan byte limit");
 }
 need(dll,"DLL ownership row required");
}
// Canonical length-prefixed binary format, independent of compiler struct padding.
struct Writer {
 Bytes b;
 void number(std::uint64_t v) { for(unsigned i=0;i<8;++i)b.push_back(static_cast<char>((v>>(i*8))&255)); }
 void text(const std::string& s) { need(s.size()<=4096,"field too long");number(s.size());b+=s; }
 void ref(const Image& r) { number(r?1:0);if(r){text(r->sha256);number(r->size);} }
};
struct Reader {
 std::string_view b;std::size_t at{};
 std::uint64_t number(){need(b.size()-at>=8,"truncated record");std::uint64_t n=0;for(unsigned i=0;i<8;++i)n|=std::uint64_t(static_cast<unsigned char>(b[at++]))<<(i*8);return n;}
 std::string text(){auto n=number();need(n<=4096&&n<=b.size()-at,"invalid field length");auto s=std::string(b.substr(at,static_cast<std::size_t>(n)));at+=static_cast<std::size_t>(n);return s;}
 Image ref(){auto present=number();need(present<=1,"invalid presence");if(!present)return {};Ref r{text(),number()};refValid(r);return r;}
 void end(){need(at==b.size(),"trailing record bytes");}
};
inline Bytes encode(const Plan& p) {
 validate(p);Writer w;w.text("SonicLatch-product-plan-v1");
 for(const auto* s:{&p.transactionId,&p.rootIdentity,&p.moduleId,&p.productId,&p.version,&p.oldManifest,&p.newManifest,&p.sourceIdentity,&p.predecessorEvidence})w.text(*s);
 w.number(static_cast<unsigned>(p.operation));w.ref(p.oldReceipt);w.ref(p.newReceipt);w.number(p.steps.size());
 for(const auto& s:p.steps){w.text(s.path);w.ref(s.before);w.ref(s.after);}return w.b;
}
inline Plan decodePlan(const Bytes& data) {
 need(data.size()<=1024*1024,"plan record too large");Reader r{data};need(r.text()=="SonicLatch-product-plan-v1","plan schema");Plan p;
 for(auto* s:{&p.transactionId,&p.rootIdentity,&p.moduleId,&p.productId,&p.version,&p.oldManifest,&p.newManifest,&p.sourceIdentity,&p.predecessorEvidence})*s=r.text();
 auto op=r.number();need(op<=3,"operation overflow");p.operation=static_cast<Operation>(op);p.oldReceipt=r.ref();p.newReceipt=r.ref();auto n=r.number();need(n>0&&n<=512,"step count");
 for(std::uint64_t i=0;i<n;++i)p.steps.push_back({r.text(),r.ref(),r.ref()});r.end();validate(p);need(encode(p)==data,"noncanonical plan");return p;
}
inline Bytes encode(const Journal& j) {
 Writer w;w.text("SonicLatch-product-journal-v1");w.text(j.transactionId);w.text(j.planDigest);w.number(static_cast<unsigned>(j.state));w.number(j.step);w.number(j.phase);return w.b;
}
inline Journal decodeJournal(const Bytes& data,const Plan& p,const std::string& digest) {
 need(data.size()<=4096,"journal limit");Reader r{data};need(r.text()=="SonicLatch-product-journal-v1","journal schema");Journal j;j.transactionId=r.text();j.planDigest=r.text();auto state=r.number(),step=r.number(),phase=r.number();r.end();
 need(j.transactionId==p.transactionId&&j.planDigest==digest&&state<=6&&step<=p.steps.size()&&phase<=2,"journal identity/state");
 j.state=static_cast<State>(state);j.step=static_cast<std::uint32_t>(step);j.phase=static_cast<std::uint32_t>(phase);return j;
}
// Required protected I/O boundary. No permissive/default implementation exists.
// The implementing Windows host must hold exclusive transaction/root locks for
// the WHOLE Engine call, pin all parents, reject reparse/hardlink substitutions,
// preserve unknown/modified files, and check OBS preflight before each mutation.
struct Store {
 virtual ~Store()=default;
 virtual std::string sha256(const Bytes&) const=0;
 virtual void guard(const Plan&) const=0;
 // Authenticate immutable manifest/receipt/predecessor and the protected plan
 // anchor. A hash supplied by the package itself is NOT authorization.
 virtual void authorize(const Plan&,const std::string& planDigest) const=0;
 virtual std::optional<Bytes> readOwned(const std::string& path) const=0;
 virtual std::optional<Bytes> readReceipt() const=0;
 virtual Bytes readPlan() const=0;
 virtual Bytes readJournal() const=0;
 virtual Bytes readBlob(const std::string& digest) const=0;
 // Exclusive new transaction; durable verified plan+blobs+Prepared journal,
 // same-volume protected storage. Failure preserves evidence, no root mutation.
 virtual void createPrepared(const Bytes& plan,const std::map<std::string,Bytes>& blobs,const Bytes& journal)=0;
 virtual void writeJournalDurable(const Bytes&)=0;
 // Compare current bytes to expected under held handles/locks and atomically
 // replace/remove only this owned file. Reject drift; never recursively delete.
 virtual void changeOwned(const std::string&,const Image& expected,const std::optional<Bytes>& desired)=0;
 virtual void changeReceipt(const Image& expected,const std::optional<Bytes>& desired)=0;
};
class Engine {
 Store& store;Plan p;std::string digest;Journal j;
 Image image(const std::optional<Bytes>& b) const { if(!b)return {};need(b->size()<=fileLimit,"read limit");return Ref{store.sha256(*b),b->size()}; }
 std::optional<Bytes> blob(const Image& ref) const { if(!ref)return {};auto b=store.readBlob(ref->sha256);need(image(b)==ref,"stage/backup bytes drift");return b; }
 bool matches(bool after) const {for(const auto& s:p.steps)if(image(store.readOwned(s.path))!=(after?s.after:s.before))return false;return true;}
 void journal(State state,std::size_t step=0,std::uint32_t phase=0) {store.guard(p);j={p.transactionId,digest,state,static_cast<std::uint32_t>(step),phase};store.writeJournalDurable(encode(j));}
 public:
 explicit Engine(Store& s):store(s) {auto raw=store.readPlan();p=decodePlan(raw);digest=store.sha256(raw);need(hex(digest,64),"plan hash");store.authorize(p,digest);store.guard(p);j=decodeJournal(store.readJournal(),p,digest);}
 static void prepare(Store& s,const Plan& plan,const std::map<std::string,Bytes>& blobs) {
  auto raw=encode(plan);auto digest=s.sha256(raw);need(hex(digest,64),"plan hash");s.authorize(plan,digest);s.guard(plan);
  std::map<std::string,Bytes> required;
  const auto add=[&](const Image& ref){if(!ref)return;auto i=blobs.find(ref->sha256);need(i!=blobs.end()&&i->second.size()==ref->size&&s.sha256(i->second)==ref->sha256,"missing/changed staged bytes");required.emplace(i->first,i->second);};
  const auto actual=[&](const std::optional<Bytes>& b)->Image {if(!b)return {};need(b->size()<=fileLimit,"baseline size");return Ref{s.sha256(*b),b->size()};};
  for(const auto& row:plan.steps){need(actual(s.readOwned(row.path))==row.before,"baseline drift");add(row.before);add(row.after);}need(actual(s.readReceipt())==plan.oldReceipt,"receipt drift");add(plan.oldReceipt);add(plan.newReceipt);
  s.createPrepared(raw,required,encode(Journal{plan.transactionId,digest,State::Prepared,0,0}));
 }
 Result apply() {
  need(j.state==State::Prepared,"apply only fresh transaction");store.guard(p);need(matches(false)&&image(store.readReceipt())==p.oldReceipt,"baseline drift");
  for(const auto& s:p.steps){blob(s.before);blob(s.after);}blob(p.oldReceipt);blob(p.newReceipt);
  journal(State::Applying);
  for(std::size_t i=0;i<p.steps.size();++i){const auto& s=p.steps[i];if(s.before==s.after)continue;journal(State::Applying,i,1);store.guard(p);store.changeOwned(s.path,s.before,blob(s.after));need(image(store.readOwned(s.path))==s.after,"write readback");journal(State::Applying,i,2);}
  need(matches(true),"payload mismatch");journal(State::PayloadVerified);store.changeReceipt(p.oldReceipt,blob(p.newReceipt));need(image(store.readReceipt())==p.newReceipt,"receipt readback");journal(State::ReceiptPublished);journal(State::Committed);return {true,false};
 }
 Result recover() {
  store.guard(p);
  if(j.state==State::RolledBack){need(matches(false)&&image(store.readReceipt())==p.oldReceipt,"rolled-back drift");return {false,true};}
  if(j.state==State::Committed){need(matches(true)&&image(store.readReceipt())==p.newReceipt,"committed drift");return {true,false};}
  if((j.state==State::PayloadVerified||j.state==State::ReceiptPublished)&&matches(true)&&image(store.readReceipt())==p.newReceipt){journal(State::Committed);return {true,false};}
  // Record rollback direction before any reversal; recovery interruptions resume
  // by comparing each actual file with its frozen before/after images.
  journal(State::RollingBack);
  for(std::size_t n=p.steps.size();n>0;--n){const auto& s=p.steps[n-1];auto current=image(store.readOwned(s.path));if(current==s.before)continue;need(current==s.after&&s.before!=s.after,"modified owned file retained");journal(State::RollingBack,n-1,1);store.guard(p);store.changeOwned(s.path,s.after,blob(s.before));need(image(store.readOwned(s.path))==s.before,"rollback readback");journal(State::RollingBack,n-1,2);}
  auto receipt=image(store.readReceipt());need(receipt==p.oldReceipt||receipt==p.newReceipt,"modified receipt retained");if(receipt!=p.oldReceipt)store.changeReceipt(p.newReceipt,blob(p.oldReceipt));need(matches(false)&&image(store.readReceipt())==p.oldReceipt,"rollback mismatch");journal(State::RolledBack);return {false,true};
 }
};
} // namespace soniclatch::product
