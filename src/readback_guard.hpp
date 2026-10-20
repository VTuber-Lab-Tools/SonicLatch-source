#pragma once
#include "correction_control.hpp"
namespace pitatuner::readback {
// Read acknowledgement is an owner operation, never write permission. Reject
// a notification arriving anywhere across the read/acknowledgement snapshot.
inline std::optional<pita::Ns> acknowledge(pita::Backend& backend,ControlSession& control,const pita::Key& expectedKey){
 const auto initial=control.view();
 if(initial.closed||!(initial.key==expectedKey))return {};
 const auto revision=backend.externalRevision();
 const bool acknowledged=control.acknowledgeRead();
 const auto observed=control.view();
 if(!acknowledged||observed.closed||observed.protectedState||!(observed.key==expectedKey)||!observed.offset||backend.externalRevision()!=revision){control.externalChange();return {};}
 return observed.offset;
}
// Display/open/selection follow-up is read-only. A stable re-show preserves Undo;
// queued external changes (including same-value) invalidate before acknowledging.
inline std::optional<pita::Ns> follow(pita::Backend& backend,ControlSession& control,const pita::Key& key,bool externalChanged=false){
 auto before=control.view();if(before.closed||!(before.key==key))return {};
 if(before.pending&&!externalChanged)return before.offset; // never acknowledge an intermediate setting as a committed value
 if(externalChanged){control.externalChange();before=control.view();}
 const auto revision=backend.externalRevision();const auto value=backend.read();
 if(!value||backend.externalRevision()!=revision){control.externalChange();return {};}
 if(!before.protectedState&&before.offset==value)return value;
 control.externalChange();return acknowledge(backend,control,key);
}
}
