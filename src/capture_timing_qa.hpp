#pragma once
// QA-only, enabled solely in the isolated stage45 module, not normal product builds.
#include <array>
#include <chrono>
#include <cstdint>
#include <windows.h>
namespace pitatuner::capture::timingqa {
inline uint64_t clock(){return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
constexpr size_t capacity=30064;
struct Row {uint64_t sequence{},timestamp{},bodyStart{},bodyEnd{},enqueueStart{},enqueueEnd{},storeStart{},storeEnd{};DWORD pid{},tid{};};
inline std::array<std::array<Row,capacity>,2> rows{};inline size_t count[2]{};inline uint64_t lost{};
struct Pending {uint64_t timestamp{},enqueueStart{},enqueueEnd{};};inline thread_local Pending pending;
struct Scope {
 bool pre,enqueue;uint64_t start;
 Scope(bool before,bool inner):pre(before),enqueue(inner),start(clock()){}
 ~Scope(){const auto end=clock();if(enqueue){pending.enqueueStart=start;pending.enqueueEnd=end;return;}
  const auto storeStart=clock();const size_t tap=pre?0:1;const auto i=count[tap]++;if(i>=capacity){++lost;return;}
  auto& row=rows[tap][i];row={i,pending.timestamp,start,end,pending.enqueueStart,pending.enqueueEnd,storeStart,0,GetCurrentProcessId(),GetCurrentThreadId()};const auto storeEnd=clock();row.storeEnd=storeEnd;
 }
};
}
