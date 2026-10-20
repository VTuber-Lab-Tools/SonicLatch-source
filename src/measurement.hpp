#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace latency {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4324) // Intentional cache-line padding for SPSC indices.
#endif
// Exactly one producer and one consumer per tap. Allocate before attaching.
// Never reset/reconfigure while callbacks or worker are using this queue.
template<class T, size_t Capacity> class SpscRing {
 static_assert(Capacity > 1);
 std::array<T, Capacity> slots_{};
 alignas(64) std::atomic<size_t> write_{0};
 alignas(64) std::atomic<size_t> read_{0};
public:
 template<class Fill>bool write(Fill fill) noexcept {
  const auto w=write_.load(std::memory_order_relaxed);
  const auto next=(w+1)%Capacity;
  if(next==read_.load(std::memory_order_acquire)) return false;
  fill(slots_[w]);
  write_.store(next,std::memory_order_release); return true;
 }
 // Reserve and publish a whole callback together: overflow must not expose a
 // prefix of a split packet. One producer owns write_; the consumer can only
 // increase the available space after this snapshot.
 template<class Fill>bool writeBatch(size_t count,Fill fill) noexcept {
  const auto w=write_.load(std::memory_order_relaxed);
  const auto r=read_.load(std::memory_order_acquire);
  const auto available=(r+Capacity-w-1)%Capacity;
  if(!count||count>available)return false;
  for(size_t i=0;i<count;i++)fill(slots_[(w+i)%Capacity],i);
  write_.store((w+count)%Capacity,std::memory_order_release);return true;
 }
 bool push(const T& value) noexcept {return write([&](T& slot){slot=value;});}
 bool pop(T& value) noexcept {
  const auto r=read_.load(std::memory_order_relaxed);
  if(r==write_.load(std::memory_order_acquire)) return false;
  value=slots_[r]; read_.store((r+1)%Capacity,std::memory_order_release); return true;
 }
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif
enum class Tap { Pre, Post };
// Worker only. Producer must publish done after its final push and never push again.
// The hook permits deterministic testing of the empty -> final push -> done boundary.
template<class Queue,class Value,class Consume,class BoundaryHook>
bool drainAndCheckDone(Queue& before,Queue& after,std::atomic<bool>& done,Value& value,Consume consume,BoundaryHook boundary){
 while(before.pop(value))consume(value,true);
 while(after.pop(value))consume(value,false);
 boundary();
 if(!done.load(std::memory_order_acquire))return false;
 // An earlier empty observation may precede the producer's final push.
 while(before.pop(value))consume(value,true);
 while(after.pop(value))consume(value,false);
 return true;
}
struct AudioBlock {
 std::array<char, 64> sourceUuid{}, sourceType{};
 uint64_t sessionId{}, configEpoch{}, mediaTimestampNs{}, arrivalMonotonicNs{}, sequence{}, invocationId{}, captureGeneration{};
 uint32_t sampleRate{}, channels{}, frames{}, invocationFrameOffset{};
 Tap tap{};
 bool gap{}, muted{};
 // Float planar only; validate format and pointers in the OBS adapter.
 std::array<std::array<float,1024>,8> pcm{};
};
struct Window {
 std::span<const float> pcm;
 int64_t mediaTimestampNs{};
 uint32_t sampleRate{};
 uint64_t epoch{};
 bool gap{};
};
struct Estimate {
 bool valid{};
 double delayMs=std::numeric_limits<double>::quiet_NaN();
 double confidence{}, peakRatio{};
 const char* reason="invalid";
};
// Worker only. No allocations, FFT, or correlation on the OBS audio thread.
inline void fft(std::vector<std::complex<double>>& a, bool inverse) {
 const size_t n=a.size();
 for(size_t i=1,j=0;i<n;i++) {
  size_t bit=n>>1; for(;j&bit;bit>>=1) j^=bit; j^=bit;
  if(i<j) std::swap(a[i],a[j]);
 }
 for(size_t len=2;len<=n;len<<=1) {
  const double angle=(inverse?2:-2)*3.14159265358979323846/static_cast<double>(len);
  const std::complex<double> root(std::cos(angle),std::sin(angle));
  for(size_t i=0;i<n;i+=len) {
   std::complex<double> w(1,0);
   for(size_t j=0;j<len/2;j++) {
    auto u=a[i+j],v=a[i+j+len/2]*w;
    a[i+j]=u+v;a[i+j+len/2]=u-v;w*=root;
   }
  }
 }
 if(inverse) for(auto& v:a) v/=static_cast<double>(n);
}
inline Estimate estimate(Window pre,Window post,double maxMs=500) {
 if(pre.epoch!=post.epoch||pre.gap||post.gap) return {false,std::numeric_limits<double>::quiet_NaN(),0,0,"epoch_or_gap"};
 if(pre.sampleRate!=post.sampleRate||!pre.sampleRate) return {false,std::numeric_limits<double>::quiet_NaN(),0,0,"format"};
 const auto n=pre.pcm.size();
 if(n*10<pre.sampleRate)return {false,std::numeric_limits<double>::quiet_NaN(),0,0,"insufficient_duration"};
 if(n!=post.pcm.size()||n<128||n>192000||!std::isfinite(maxMs)||maxMs<0||maxMs>500)
  return {false,std::numeric_limits<double>::quiet_NaN(),0,0,"window"};
 double ma=0,mb=0;
 for(size_t i=0;i<n;i++) {
  if(!std::isfinite(pre.pcm[i])||!std::isfinite(post.pcm[i])) return {false,std::numeric_limits<double>::quiet_NaN(),0,0,"nonfinite"};
  ma+=pre.pcm[i];mb+=post.pcm[i];
 }
 ma/=static_cast<double>(n);mb/=static_cast<double>(n);
 size_t fftN=1;while(fftN<2*n)fftN<<=1;
 std::vector<std::complex<double>> a(fftN),b(fftN);
 std::vector<double> ea(n+1),eb(n+1);
 for(size_t i=0;i<n;i++) {
  const double x=pre.pcm[i]-ma,y=post.pcm[i]-mb;
  a[n-1-i]=x;b[i]=y;ea[i+1]=ea[i]+x*x;eb[i+1]=eb[i]+y*y;
 }
 if(ea[n]/static_cast<double>(n)<1e-8||eb[n]/static_cast<double>(n)<1e-8)
  return {false,std::numeric_limits<double>::quiet_NaN(),0,0,"silence"};
 fft(a,false);fft(b,false);for(size_t i=0;i<fftN;i++)a[i]*=b[i];fft(a,true);
 const double baseMs=(static_cast<double>(post.mediaTimestampNs)-static_cast<double>(pre.mediaTimestampNs))/1e6;
 const int limit=static_cast<int>(n/2);
 std::vector<double> scores(static_cast<size_t>(2*limit+1),-1);
 double best=-1;int bestLag=0,firstLag=limit+1,lastLag=-limit-1;
 for(int lag=-limit;lag<=limit;lag++) {
  const double d=baseMs+1000.0*lag/pre.sampleRate;
  const auto start=static_cast<size_t>(std::max(0,-lag));
  const auto end=static_cast<size_t>(std::min(static_cast<int>(n),static_cast<int>(n)-lag));
  const auto ps=static_cast<size_t>(static_cast<int>(start)+lag),pe=static_cast<size_t>(static_cast<int>(end)+lag);
  const double den=std::sqrt((ea[end]-ea[start])*(eb[pe]-eb[ps]));
  const double score=den>1e-12?a[static_cast<size_t>(static_cast<int>(n)-1+lag)].real()/den:0;
  scores[static_cast<size_t>(lag+limit)]=score;
  if(std::abs(d)<=maxMs){
   firstLag=std::min(firstLag,lag);lastLag=lag;
   if(score>best){best=score;bestLag=lag;}
  }
 }
 double second=0;size_t sidelobeCount=0;
 const int exclusion=std::max(1,static_cast<int>(pre.sampleRate/1000));
 for(int lag=-limit;lag<=limit;lag++)if(std::abs(lag-bestLag)>exclusion){
  second=std::max(second,scores[static_cast<size_t>(lag+limit)]);sidelobeCount++;
 }
 if(sidelobeCount<std::max<size_t>(1,pre.sampleRate/100))return {false,std::numeric_limits<double>::quiet_NaN(),best,0,"insufficient_sidelobes"};
 const double ratio=best/std::max(second,1e-9);
 if(best<0.6||ratio<1.4)return {false,std::numeric_limits<double>::quiet_NaN(),best,ratio,"ambiguous_or_deformed"};
 // An endpoint peak is not bracketed by searched candidates on both sides.
 if(bestLag==firstLag||bestLag==lastLag)return {false,std::numeric_limits<double>::quiet_NaN(),best,ratio,"search_endpoint"};
 return {true,baseMs+1000.0*bestLag/pre.sampleRate,best,ratio,"ok"};
}
inline std::optional<int64_t> msToNs(double ms) {
 if(!std::isfinite(ms))return std::nullopt;
 const long double ns=std::round(static_cast<long double>(ms)*1000000.L);
 // 2^63 is exact even where long double == double. Upper bound is exclusive.
 if(ns>=9223372036854775808.L||ns< -9223372036854775808.L)return std::nullopt;
 return static_cast<int64_t>(ns);
}
// UI supplies its separately verified effective range; no unverified range default.
inline std::optional<int64_t> msToNsInRange(double ms,double minimum,double maximum) {
 if(!std::isfinite(minimum)||!std::isfinite(maximum)||minimum>maximum||ms<minimum||ms>maximum)return std::nullopt;
 const auto value=msToNs(ms);if(!value)return std::nullopt;
 const long double low=std::ceil(static_cast<long double>(minimum)*1000000.L);
 const long double high=std::floor(static_cast<long double>(maximum)*1000000.L);
 const long double written=static_cast<long double>(*value);
 if(written<low||written>high)return std::nullopt;
 return value;
}
}

