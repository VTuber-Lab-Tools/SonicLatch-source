#pragma once
#include "correction_control.hpp"
#include <optional>
#include <string>
namespace pitatuner {
// Presentation-only memory: never feeds ControlSession, proposal or write actions.
class LastSuccessfulDelay {
 struct Value{pita::Key key;pita::Ns delay{};uint64_t captured{};};std::optional<Value> value_;
public:
 struct Held{pita::Ns delay{};uint64_t ageNs{};};
 // Explicitly diagnostic across reacquisition of the same UUID. This method
 // must never feed an Estimate, proposal, offset or write-authority decision.
 std::optional<Held> diagnosticForSource(const std::string& uuid,uint64_t now)const{if(uuid.empty()||!value_||value_->key.uuid!=uuid||now<value_->captured)return{};return Held{value_->delay,now-value_->captured};}
 void clear(){value_.reset();}
 void observe(const pita::Key& key,pita::Ns delay,uint64_t captured,bool valid){if(valid&&captured)value_=Value{key,delay,captured};}
 std::optional<Held> held(const pita::Key& key,uint64_t now)const{if(!value_||!(value_->key==key)||now<value_->captured)return{};return Held{value_->delay,now-value_->captured};}
};
}
