#pragma once
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace pita {
using Ns = std::int64_t;
inline std::optional<Ns> add(Ns a, Ns b) {
  if ((b > 0 && a > INT64_MAX-b) || (b < 0 && a < INT64_MIN-b)) return {};
  return a+b;
}
inline std::optional<Ns> sub(Ns a, Ns b) {
  if ((b > 0 && a < INT64_MIN+b) || (b < 0 && a > INT64_MAX+b)) return {};
  return a-b;
}
// Exact decimal milliseconds, ASCII only, no exponent/unit/whitespace.
inline std::optional<Ns> parse_ms(const std::string &s) {
  if (s.empty()) return {};
  std::size_t i=0; bool neg=false;
  if (s[i]=='+' || s[i]=='-') { neg=s[i]=='-'; ++i; }
  if (i==s.size()) return {};
  std::uint64_t whole=0, frac=0; unsigned digits=0; bool have=false;
  const std::uint64_t limit=neg ? std::uint64_t(INT64_MAX)+1 : std::uint64_t(INT64_MAX);
  while (i<s.size() && s[i]>='0' && s[i]<='9') {
    have=true; auto d=unsigned(s[i++]-'0');
    if (whole>(limit/1000000-d)/10) return {};
    whole=whole*10+d;
  }
  if (!have) return {};
  if (i<s.size() && s[i]=='.') {
    ++i;
    while (i<s.size() && s[i]>='0' && s[i]<='9') {
      if (++digits>6) return {};
      frac=frac*10+unsigned(s[i++]-'0');
    }
    if (!digits) return {};
  }
  if (i!=s.size()) return {};
  while (digits++<6) frac*=10;
  if (whole>limit/1000000) return {};
  auto n=whole*1000000;
  if (frac>limit-n) return {};
  n+=frac;
  if (neg && n==std::uint64_t(INT64_MAX)+1) return INT64_MIN;
  return neg ? -Ns(n) : Ns(n);
}
struct Key {
  std::string uuid; std::uint64_t session=0, epoch=0;
  bool operator==(const Key &k) const { return uuid==k.uuid && session==k.session && epoch==k.epoch; }
};
struct Estimate {
  Key key; std::uint64_t seq=0; Ns delay=0, freshUntil=0;
  bool valid=true, confident=true, active=true, muted=false;
};
// Mode is an explicit caller choice; the model never chooses or calibrates it.
enum class Mode { Unchosen, MeasuredReference, ExplicitPreTarget };
struct Baseline { Mode mode=Mode::Unchosen; Ns s0=0, d0=0, m=0; std::uint64_t id=0; };
struct Range { Ns min=0,max=0; std::uint64_t policy=0; bool verified=false;
  bool negativeBandWithZero=false; Ns bandMin=0,bandMax=0;
  // Host API input contract, distinct from output-verified fixture evidence.
  bool hostContract=false;
  bool permits(Ns n) const { return (verified || hostContract) && min<=max && n>=min && n<=max && (!negativeBandWithZero || (bandMin<=bandMax && (n==0 || (n>=bandMin && n<=bandMax)))); }
};
struct Backend {
  virtual ~Backend()=default;
  virtual std::optional<Ns> read()=0;
  virtual std::uint64_t externalRevision() const=0;
  virtual void write(Ns)=0;
  // Existing synchronous backends need no changes. Pending completion is owner-polled.
  virtual bool writePending() const { return false; }
  virtual void pollWrite(Ns) {}
  virtual void cancelWrite() {}
};
struct Proposal {
  std::uint64_t id=0, estimateSeq=0, baselineId=0, modelRevision=0, policy=0;
  Key key; Ns before=0, target=0, residualNow=0, residualTarget=0, freshUntil=0;
  bool absolutePreTarget=false;
};
struct Transaction { Key key; Ns before=0,after=0; std::optional<Baseline> priorBaseline; };
enum class Result { Applied, Noop, Rejected, Protected, Busy, Pending };
class Model {
  Backend &backend;
  std::uint64_t external=0, revision=0, nextProposal=0, lastIntent=0;
  bool claimIntent(std::uint64_t id) {
    if(id==0 || id<=lastIntent) return false;
    lastIntent=id; return true; // Ordered, globally monotonic control-stream IDs.
  }
  bool busy=false;
  struct PendingWrite {Ns before,target;std::optional<Baseline> oldbase,after;Key key;std::uint64_t revision;bool undoing,automatic;Ns freshUntil;};
  std::optional<PendingWrite> writing;
  void protect() { if(writing){backend.cancelWrite();writing.reset();busy=false;} protectedState=true; baseline.reset(); undoEntry.reset(); offered.reset(); ++revision; }
  bool observe() {
    auto n=backend.read();
    if (!n || !expected || *n!=*expected || backend.externalRevision()!=external) {
      expected=n;
      protect(); return false;
    }
    return true;
  }
  Result completeWrite(const PendingWrite& p) {
    auto got=backend.read();auto ext=backend.externalRevision();busy=false;writing.reset();
    if(!(p.key==key)||p.revision!=revision||!alive||closing||protectedState||!got||*got!=p.target||ext!=external){expected=got;protect();return Result::Protected;}
    expected=got;baseline=p.after;++revision;offered.reset();
    if(p.undoing)undoEntry.reset();else undoEntry=Transaction{key,p.before,p.target,p.oldbase};
    return Result::Applied;
  }
  Result writeTo(Ns target,std::optional<Baseline> after,bool undoing,bool automatic=false,Ns freshUntil=INT64_MAX,bool stateOnly=false) {
    if(busy)return Result::Busy;
    if(!alive||closing||protectedState||!range.permits(target))return Result::Rejected;
    if(!observe())return Result::Protected;
    PendingWrite p{*expected,target,baseline,after,key,revision,undoing,automatic,freshUntil};
    // Reset/Undo may change only reference state. Still use final readback and
    // revision validation, but do not emit an unnecessary offset write.
    if(target==*expected)return stateOnly?completeWrite(p):Result::Noop;
    busy=true;backend.write(target);
    if(backend.writePending()){writing=p;offered.reset();return Result::Pending;}
    return completeWrite(p);
  }

public:
  Key key; Range range; bool alive=true, closing=false, protectedState=false;
  std::optional<Ns> expected;
  std::optional<Baseline> baseline;
  std::optional<Proposal> offered;
  std::optional<Transaction> undoEntry;
  explicit Model(Backend &b, Key k, Range r):backend(b),key(std::move(k)),range(r) { acknowledgeRead(); }
  bool pendingWrite() const {return writing.has_value();}
  std::optional<Result> pollPending(const std::optional<Estimate>& current,Ns now,bool automaticEnvironment=true) {
    if(!writing)return {};
    const auto p=*writing;
    if(p.automatic&&(!automaticEnvironment||!current||!eligible(*current,now)||now>=p.freshUntil)){protect();expected=backend.read();return Result::Protected;}
    backend.pollWrite(now);if(backend.writePending())return Result::Pending;
    return completeWrite(p);
  }
  bool acknowledgeRead() { // Explicit user read/ack; never revives old baseline or Undo.
    if(busy || !alive || closing) return false;
    baseline.reset(); undoEntry.reset(); offered.reset(); ++revision;
    expected=backend.read(); external=backend.externalRevision();
    protectedState=!expected.has_value(); return !protectedState;
  }
  bool registerBaseline(Baseline b) { // Caller must obtain mode-specific confirmation.
    if(busy || !alive || closing || protectedState || b.mode==Mode::Unchosen || !observe()) return false;
    if(b.s0!=*expected || (b.mode==Mode::ExplicitPreTarget && b.d0!=0)) return false;
    b.m=0; baseline=b; undoEntry.reset(); offered.reset(); ++revision; return true;
  }
  bool eligible(const Estimate &e,Ns now) const {
    return e.key==key && e.valid && e.confident && e.active && !e.muted && now<e.freshUntil;
  }
  std::optional<Proposal> propose(const Estimate &e,Ns now,bool absolutePreTarget=false) {
    offered.reset();
    if(busy || !alive || closing || protectedState || (!absolutePreTarget&&!baseline) || !eligible(e,now)) { offered.reset(); return {}; }
    if(!observe()) return {};
    // Normal product Auto is the absolute PRE target -D, independent of S.
    // The reference formula remains only for existing model/isolated callers.
    const auto origin=absolutePreTarget?Ns{0}:baseline->s0;
    auto delta=sub(e.delay,absolutePreTarget?Ns{0}:baseline->d0); if(!delta) return {};
    auto raw=sub(origin,*delta); if(!raw) return {};
    auto t=add(*raw,absolutePreTarget?Ns{0}:baseline->m); if(!t || !range.permits(*t)) return {};
    auto shift=sub(*expected,origin); if(!shift) return {};
    auto rn=add(*delta,*shift); if(!rn) return {};
    auto ts=sub(*t,origin); if(!ts) return {};
    auto rt=add(*delta,*ts); if(!rt) return {};
    offered=Proposal{++nextProposal,e.seq,absolutePreTarget?0:baseline->id,revision,range.policy,key,*expected,*t,*rn,*rt,e.freshUntil,absolutePreTarget};
    return offered;
  }
  Result apply(const Proposal &p,const Estimate &current,Ns now) {
    if(busy) return Result::Busy;
    if(!offered || p.id!=offered->id) return Result::Rejected;
    // Only the internally held snapshot is authoritative, including its expiration.
    auto frozen=*offered;
    const bool referenceMatches=frozen.absolutePreTarget?frozen.baselineId==0:(baseline&&frozen.baselineId==baseline->id);
    if(!(frozen.key==key) || !referenceMatches || frozen.modelRevision!=revision || frozen.policy!=range.policy || now>=frozen.freshUntil || !eligible(current,now)) return Result::Rejected;
    auto result=writeTo(frozen.target,frozen.absolutePreTarget?std::nullopt:baseline,false,true,frozen.freshUntil);
    if(result==Result::Noop) offered.reset();
    return result;
  }
private:
  Result manualTarget(Ns target) {
    if(busy) return Result::Busy;
    if(!expected) return Result::Rejected;
    auto after=baseline;
    if(after) {
      auto change=sub(target,*expected); if(!change) return Result::Rejected;
      auto m=add(after->m,*change); if(!m) return Result::Rejected;
      after->m=*m;
    }
    return writeTo(target,after,false);
  }
public:
  Result manual(Ns target,std::uint64_t intent) {
    if(!claimIntent(intent)) return Result::Rejected;
    return manualTarget(target);
  }
  Result reset(std::uint64_t intent) {
    if(!claimIntent(intent)) return Result::Rejected;
    // Reset is not a manual trim: clear the reference only after success.
    return writeTo(0,std::nullopt,false,false,INT64_MAX,baseline.has_value());
  }
  Result step(Ns delta,std::uint64_t intent) {
    if(!claimIntent(intent)) return Result::Rejected;
    if(busy) return Result::Busy;
    if(!expected) return Result::Rejected;
    auto t=add(*expected,delta); return t ? manualTarget(*t) : Result::Rejected;
  }
  Result undo(std::uint64_t intent) {
    if(!claimIntent(intent)) return Result::Rejected;
    if(busy) return Result::Busy;
    if(!undoEntry) return Result::Rejected;
    if(!(undoEntry->key==key) || !expected || *expected!=undoEntry->after) { protect(); return Result::Protected; }
    auto t=*undoEntry; auto result=writeTo(t.before,t.priorBaseline,true,false,INT64_MAX,true);
    // A rejected restoration must not revive after a policy/lifetime change.
    if(result==Result::Rejected) { protect(); return Result::Protected; }
    return result;
  }
  void changeSelection(Key next) {
    key=std::move(next); protect(); // Explicit subsequent read/ack and new baseline required.
  }
};
} // namespace pita
