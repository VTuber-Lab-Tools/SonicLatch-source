#include "waveform_model.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace pitatuner::waveform {
namespace {
constexpr std::uint64_t billion=1'000'000'000ULL, umax=std::numeric_limits<std::uint64_t>::max();
bool add(std::uint64_t a,std::uint64_t b,std::uint64_t& out) { if(a>umax-b)return false;out=a+b;return true; }
bool shift(std::uint64_t a,std::int64_t s,std::uint64_t& out) {
 if(s>=0)return add(a,static_cast<std::uint64_t>(s),out);
 const auto magnitude=static_cast<std::uint64_t>(-(s+1))+1;
 if(a<magnitude)return false;
 out=a-magnitude;return true;
}
bool subtract(std::int64_t a,std::int64_t b,std::int64_t& out) {
 if((b>0 && a<std::numeric_limits<std::int64_t>::min()+b)||(b<0 && a>std::numeric_limits<std::int64_t>::max()+b))return false;
 out=a-b;return true;
}
bool stamp(const Track& t,std::uint64_t i,std::uint64_t& out) {
 auto q=i/t.rate, r=i%t.rate;
 if(q>umax/billion)return false;
 std::uint64_t delta;
 return add(q*billion,r*billion/t.rate,delta)&&add(t.origin_ns,delta,out);
}
bool same(const Key&a,const Key&b) { return a.source==b.source&&a.session==b.session&&a.epoch==b.epoch; }
Error validate(const Track&t) {
 if(t.key.source.empty()||!t.rate||t.rate>384000)return Error::InvalidMetadata;
 if(t.points.size()>max_points)return Error::InputLimit;
 bool first=true;std::uint64_t prev=0;
 for(const auto&p:t.points) {
  if(!first&&p.index<=prev)return Error::IndexOrder;
  std::uint64_t ts;
  if(!stamp(t,p.index,ts))return Error::TimestampOverflow;
  if(ts!=p.timestamp_ns)return Error::TimestampMismatch;
  prev=p.index;first=false;
 }
 return Error::None;
}
// ceil(delta * rate / 1e9), without multiplication overflow.
bool lower_index(std::uint64_t delta,std::uint32_t rate,std::uint64_t& out) {
 const auto q=delta/billion,r=delta%billion;
 if(q>umax/rate)return false;
 return add(q*rate,(r*rate+billion-1)/billion,out);
}
Error project(const Track&t,Axis a,Kind kind,std::int64_t s,Series& result) {
 result.kind=kind;result.shift_ns=s;
 std::uint64_t origin;
 if(!shift(t.origin_ns,s,origin))return Error::ShiftOverflow;
 result.bins.resize(a.width);
 const auto duration=a.end_ns-a.begin_ns;
 for(std::size_t i=0;i<a.width;++i) {
  auto&b=result.bins[i];
  b.begin_ns=a.begin_ns+duration*i/a.width;b.end_ns=a.begin_ns+duration*(i+1)/a.width;
  b.outside_timeline=b.begin_ns<origin;
  std::uint64_t lo=0,hi=0;
  if(b.begin_ns>origin&&!lower_index(b.begin_ns-origin,t.rate,lo))return Error::TimestampOverflow;
  if(b.end_ns>origin&&!lower_index(b.end_ns-origin,t.rate,hi))return Error::TimestampOverflow;
  b.expected=hi-lo;
 }
 for(const auto&p:t.points) {
  std::uint64_t ts;
  if(!shift(p.timestamp_ns,s,ts))return Error::ShiftOverflow;
  if(ts<a.begin_ns){++result.clipped_left;continue;}
  if(ts>=a.end_ns){++result.clipped_right;continue;}
  // upper_bound on actual integer boundaries handles nondivisible widths exactly.
  const auto it=std::upper_bound(result.bins.begin(),result.bins.end(),ts,[](std::uint64_t v,const Bin&b){return v<b.end_ns;});
  auto&b=*it;
  if(!p.valid||!std::isfinite(p.amplitude)){++b.invalid;continue;}
  ++b.finite;
  b.minimum=b.minimum?std::min(*b.minimum,p.amplitude):p.amplitude;
  b.maximum=b.maximum?std::max(*b.maximum,p.amplitude):p.amplitude;
 }
 for(auto&b:result.bins)b.missing=b.expected-b.finite-b.invalid;
 return Error::None;
}
}
Frame build(const Request&r) {
 Frame f;f.axis=r.axis;
 auto fail=[&](Error e){ f.error=e;f.series.clear();return f; };
 const auto a=r.axis;
 if(a.end_ns<=a.begin_ns||a.end_ns-a.begin_ns>max_window_ns||a.width>max_bins||a.width>a.end_ns-a.begin_ns)return fail(Error::InvalidAxis);
 if(!same(r.pre.key,r.post.key)||r.pre.rate!=r.post.rate||r.pre.channel!=r.post.channel||r.pre.origin_ns!=r.post.origin_ns)return fail(Error::InvalidMetadata);
 for(const auto*t:{&r.pre,&r.post})if(auto e=validate(*t);e!=Error::None)return fail(e);
 std::int64_t current,candidate=0;
 if(!subtract(r.current_offset,r.baseline_offset,current)||(r.candidate_offset&&!subtract(*r.candidate_offset,r.baseline_offset,candidate)))return fail(Error::ShiftOverflow);
 // Zero width means no drawable surface, but metadata/data still validated.
 if(!a.width)return f;
 const Track* tracks[]={&r.pre,&r.post,&r.post,&r.post};
 const Kind kinds[]={Kind::PreMeasured,Kind::PostMeasured,Kind::PostEstimatedCurrentPosition,Kind::PostEstimatedCandidatePosition};
 const std::int64_t shifts[]={0,0,current,candidate};
 for(std::size_t i=0;i<(r.candidate_offset?4U:3U);++i){
  Series series;if(auto e=project(*tracks[i],a,kinds[i],shifts[i],series);e!=Error::None)return fail(e);
  f.series.push_back(std::move(series));
 }
 return f;
}
}
