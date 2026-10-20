#pragma once
#include <windows.h>
#include <obs-module.h>
#include <util/text-lookup.h>
#include <qt_metrics.hpp>
#include <stdexcept>
// Equivalent default en-US lookup semantics to OBS_MODULE_USE_DEFAULT_LOCALE,
// using the existing process OBS exports rather than another runtime/import SDK.
namespace pitatuner::locale {
struct Api {
 decltype(&obs_module_load_locale) load{};decltype(&text_lookup_getstr) get{};decltype(&text_lookup_destroy) destroy{};
 Api(){auto dll=GetModuleHandleW(L"obs.dll");load=reinterpret_cast<decltype(load)>(GetProcAddress(dll,"obs_module_load_locale"));get=reinterpret_cast<decltype(get)>(GetProcAddress(dll,"text_lookup_getstr"));destroy=reinterpret_cast<decltype(destroy)>(GetProcAddress(dll,"text_lookup_destroy"));if(!load||!get||!destroy)throw std::runtime_error("locale API unavailable");}
};
inline const char* key(qt::Text role){
 switch(role){
 case qt::Text::Delay:return "LabelDelay";case qt::Text::Current:return "LabelCurrentOffset";case qt::Text::Recommended:return "LabelTargetOffset";case qt::Text::Residual:return "LabelCurrentResidual";case qt::Text::Confidence:return "LabelConfidence";case qt::Text::State:return "LabelMeasurement";case qt::Text::Milliseconds:return "FormatMilliseconds";
 case qt::Text::Unknown:return "ConfidenceUnknown";case qt::Text::Stable:return "ConfidenceStable";case qt::Text::Invalid:return "StatusInvalidEstimate";case qt::Text::Silent:return "StatusSilent";case qt::Text::Muted:return "StatusMuted";case qt::Text::Low:return "ConfidenceLow";case qt::Text::Read:return "BtnRefreshOffset";case qt::Text::Apply:return "BtnRecommend";case qt::Text::Reset:return "BtnResetOffset";case qt::Text::Settings:return "BtnSettings";case qt::Text::Undo:return "BtnUndo";case qt::Text::Manual:return "BtnApplyOffset";case qt::Text::Minus5:return "BtnMinus5";case qt::Text::Minus1:return "BtnMinus1";case qt::Text::Plus1:return "BtnPlus1";case qt::Text::Plus5:return "BtnPlus5";case qt::Text::Waiting:return "StatusMeasuring";case qt::Text::NoBaseline:return "StatusNoBaseline";case qt::Text::Protected:return "StatusProtected";case qt::Text::Expired:return "StatusStale";case qt::Text::Unconnected:return "StatusManualUnavailable";case qt::Text::NoSource:return "StatusNoSource";
 }return "ConfidenceUnknown";
}
}
