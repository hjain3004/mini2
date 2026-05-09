#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "config/ConfigManager.hpp"
#include "grpc/ClientPool.hpp"
#include "model/ServiceRequest.hpp"

namespace mini2 {

// Per-request chunk-push scheduler. Owns a single driver thread that
// dequeues "push jobs" according to a fairness policy and pushes one
// chunk per turn upward to the request's parent node.
//
// Policies:
//  * Greedy      — drain a single request's chunks fully before serving the next.
//  * RoundRobin  — each request gets one chunk per cycle, then yields.
//
// When a job's records are exhausted, the scheduler invokes
// `on_local_done(req_id)` so the service can update local_done state and
// emit `source_done` upward via its existing aggregation helper.
class Scheduler {
public:
  enum class Policy { Greedy, RoundRobin };

  using OnLocalDoneFn = std::function<void(const std::string&)>;

  Scheduler(const Config& cfg,
            GrpcClientPool& pool,
            Policy policy,
            OnLocalDoneFn on_local_done);
  ~Scheduler();

  Scheduler(const Scheduler&) = delete;
  Scheduler& operator=(const Scheduler&) = delete;

  static Policy policy_from_string(const std::string& s);

  void start();
  void stop();

  // Hand a finished result set to the scheduler for chunked push to `parent_node`.
  // `chunk_records` is the per-chunk record cap (0 → use config default).
  // If `parent_node` is empty (gateway), the records are *not* pushed; the
  // scheduler immediately invokes on_local_done and returns.
  void submit(const std::string& req_id,
              const std::string& parent_node,
              std::vector<ServiceRecord>&& results,
              std::size_t chunk_records);

  // Mark a request cancelled — any pending chunks for it are dropped.
  void cancel(const std::string& req_id);

  // Diagnostics
  std::size_t queue_depth();

private:
  struct Job {
    std::string req_id;
    std::string parent_node;
    std::vector<ServiceRecord> results;
    std::size_t cursor = 0;
    std::size_t chunk_records = 500;
    int chunk_id = 0;
  };

  const Config& cfg_;
  GrpcClientPool& pool_;
  Policy policy_;
  OnLocalDoneFn on_local_done_;

  std::mutex mutex_;
  std::condition_variable cv_;
  std::thread driver_;
  bool stop_ = false;
  bool started_ = false;

  std::deque<std::string> queue_;                          // ready order
  std::unordered_map<std::string, Job> jobs_;              // job state
  std::unordered_set<std::string> cancelled_;
  std::string greedy_current_;                             // active job in Greedy mode

  void driver_loop();
  // Returns true if `req_id` still has more chunks to push.
  bool push_one_chunk_unlocked(Job& job);
};

}  // namespace mini2
