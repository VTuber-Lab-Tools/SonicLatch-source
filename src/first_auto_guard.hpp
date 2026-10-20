#pragma once
#include "correction_control.hpp"
namespace pitatuner::autoguard {
inline std::optional<pita::Ns> firstTarget(const View& v,const std::optional<pita::Estimate>& e,
 const pita::Range& range,uint64_t accepted,pita::Ns now){
 if(v.closed||v.protectedState||v.baseline||!v.offset||!e||!e->seq||e->seq!=accepted||!(e->key==v.key)||!e->valid||!e->confident||!e->active||e->muted||now>=e->freshUntil||!range.permits(*v.offset))return{};
 const auto target=pita::sub(*v.offset,e->delay);
 return target&&range.permits(*target)?target:std::nullopt;
}
inline bool unchanged(const View& initial,const View& current,uint64_t initialRevision,uint64_t currentRevision,bool confirmed){
 return confirmed&&!current.closed&&!current.protectedState&&current.key==initial.key&&current.offset==initial.offset&&initialRevision==currentRevision;
}
}
