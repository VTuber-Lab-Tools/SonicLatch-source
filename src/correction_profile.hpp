#pragma once
#include "correction_control.hpp"
namespace pitatuner::qualification {
// SonicLatch write limits, independent of the wider OBS input domain.
// These bounds are not an all-values output proof.
inline constexpr pita::Ns minimumOffsetNs=-950000000,maximumOffsetNs=1000000000;
inline pita::Range select(bool sourceApiAvailable,bool supportedStereoFormat,pita::Ns /*current*/){
 pita::Range r{minimumOffsetNs,maximumOffsetNs,6001,false};
 r.hostContract=sourceApiAvailable&&supportedStereoFormat;
 return r;
}
// Source identity, paired packet, readback and revision guards are checked by
// the owner/backend. The zero-start transaction additionally requires activity.
inline bool targetAllowed(const pita::Range& r,pita::Ns current,pita::Ns target,bool streamActive){return r.permits(target)&&(current!=0||target==0||streamActive);}
}
