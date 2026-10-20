#pragma once
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <atomic>
#include <functional>
#include <chrono>
namespace pitatuner::capture {
inline uint64_t steadyNs(){return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
inline void atomicMax(std::atomic<uint64_t>& destination,uint64_t value){auto old=destination.load(std::memory_order_relaxed);while(old<value&&!destination.compare_exchange_weak(old,value,std::memory_order_relaxed)){};}
struct RuntimeStats {
 std::atomic<uint64_t> submitted{},replaced{},completed{},computeTotalNs{},computeMaxNs{},queueMaxNs{},ingestMaxDwellNs{},wavePublished{},waveIntervalMaxNs{};
};
// One computing job + at most one pending job. A slow estimator never blocks the
// capture/wave worker and never creates an unbounded queue. Replacements are QA
// failures to count, not hidden as successful 250 ms updates.
template<class Job>class LatestAnalysisLane {
 struct Pending {Job value;uint64_t queued;};std::mutex mutex_;std::condition_variable cv_;std::optional<Pending> pending_;bool done_{};
 RuntimeStats& stats_;std::function<void(Job&)> process_;std::function<void()> failed_;std::thread thread_;
 void run(){for(;;){std::optional<Pending> job;{std::unique_lock lock(mutex_);cv_.wait(lock,[&]{return done_||pending_.has_value();});if(!pending_)return;job=std::move(pending_);pending_.reset();}
   const auto begin=steadyNs();atomicMax(stats_.queueMaxNs,begin-job->queued);try{process_(job->value);}catch(...){failed_();}
   const auto elapsed=steadyNs()-begin;stats_.computeTotalNs.fetch_add(elapsed);atomicMax(stats_.computeMaxNs,elapsed);stats_.completed.fetch_add(1);
  }}
public:
 LatestAnalysisLane(RuntimeStats& stats,std::function<void(Job&)> process,std::function<void()> failed):stats_(stats),process_(std::move(process)),failed_(std::move(failed)),thread_([this]{run();}){}
 ~LatestAnalysisLane(){{std::lock_guard lock(mutex_);done_=true;}cv_.notify_one();thread_.join();}
 void submit(Job value){std::lock_guard lock(mutex_);if(pending_)stats_.replaced.fetch_add(1);pending_=Pending{std::move(value),steadyNs()};stats_.submitted.fetch_add(1);cv_.notify_one();}
};
}
