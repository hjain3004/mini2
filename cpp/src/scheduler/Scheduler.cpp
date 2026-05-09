#include "scheduler/Scheduler.hpp"

#include <chrono>
#include <iostream>

#include <grpcpp/grpcpp.h>

#include "mini2.grpc.pb.h"

namespace mini2 {

Scheduler::Scheduler(const Config& cfg,
                     GrpcClientPool& pool,
                     Policy policy,
                     OnLocalDoneFn on_local_done)
    : cfg_(cfg), pool_(pool), policy_(policy),
      on_local_done_(std::move(on_local_done)) {}

Scheduler::~Scheduler() { stop(); }

Scheduler::Policy Scheduler::policy_from_string(const std::string& s) {
  if (s == "greedy") return Policy::Greedy;
  return Policy::RoundRobin;  // default
}

void Scheduler::start() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (started_) return;
  started_ = true;
  driver_ = std::thread([this] { driver_loop(); });
}

void Scheduler::stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stop_) return;
    stop_ = true;
  }
  cv_.notify_all();
  if (driver_.joinable()) driver_.join();
}

void Scheduler::submit(const std::string& req_id,
                       const std::string& parent_node,
                       std::vector<ServiceRecord>&& results,
                       std::size_t chunk_records) {
  // Gateway case: no parent → no push. Service stores records itself; scheduler
  // just signals local_done.
  if (parent_node.empty()) {
    if (on_local_done_) on_local_done_(req_id);
    return;
  }

  // No records → still need to fire local_done so caller can emit source_done.
  if (results.empty()) {
    if (on_local_done_) on_local_done_(req_id);
    return;
  }

  std::size_t cap = chunk_records > 0
                        ? chunk_records
                        : static_cast<std::size_t>(cfg_.default_chunk_records);

  {
    std::lock_guard<std::mutex> lock(mutex_);
    Job j;
    j.req_id = req_id;
    j.parent_node = parent_node;
    j.results = std::move(results);
    j.chunk_records = cap;
    jobs_[req_id] = std::move(j);
    queue_.push_back(req_id);
  }
  cv_.notify_one();
}

void Scheduler::cancel(const std::string& req_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  cancelled_.insert(req_id);
  jobs_.erase(req_id);
  // queue_ entries for this req_id will be skipped by the driver.
  if (greedy_current_ == req_id) greedy_current_.clear();
}

std::size_t Scheduler::queue_depth() {
  std::lock_guard<std::mutex> lock(mutex_);
  return queue_.size();
}

void Scheduler::driver_loop() {
  for (;;) {
    Job snapshot;
    bool job_done = false;

    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
      if (stop_) return;

      // Pick the next req_id to serve based on policy.
      std::string pick;
      if (policy_ == Policy::Greedy && !greedy_current_.empty() &&
          jobs_.count(greedy_current_)) {
        pick = greedy_current_;
      } else {
        // Pop until we find a non-cancelled, present job.
        while (!queue_.empty()) {
          auto candidate = queue_.front();
          queue_.pop_front();
          if (cancelled_.count(candidate)) continue;
          if (!jobs_.count(candidate)) continue;
          pick = candidate;
          break;
        }
        if (pick.empty()) continue;  // nothing servable; loop and wait
        if (policy_ == Policy::Greedy) greedy_current_ = pick;
      }

      // Snapshot the chunk we'll push (records up to chunk_records).
      Job& job = jobs_[pick];
      std::size_t remaining = job.results.size() - job.cursor;
      std::size_t take = std::min(remaining, job.chunk_records);

      snapshot.req_id = pick;
      snapshot.parent_node = job.parent_node;
      snapshot.chunk_id = job.chunk_id;

      // Move out the slice we'll push (avoid copy).
      snapshot.results.reserve(take);
      for (std::size_t i = 0; i < take; ++i) {
        snapshot.results.push_back(std::move(job.results[job.cursor + i]));
      }
      job.cursor += take;
      job.chunk_id += 1;

      job_done = (job.cursor >= job.results.size());
      if (job_done) {
        jobs_.erase(pick);
        if (greedy_current_ == pick) greedy_current_.clear();
        // Remove any lingering queue entries for this req_id (RR may have one).
        for (auto it = queue_.begin(); it != queue_.end();) {
          if (*it == pick) it = queue_.erase(it);
          else ++it;
        }
      } else {
        if (policy_ == Policy::RoundRobin) {
          // Yield turn: requeue at the back so other ready jobs run first.
          queue_.push_back(pick);
        } else {
          // Greedy: stay locked on `greedy_current_`. We already popped this
          // entry from the queue; do NOT re-add (would balloon the queue).
          // Wake ourselves so the wait predicate doesn't block on an empty
          // queue when this is the only ready job.
          if (queue_.empty()) {
            queue_.push_back(pick);
          }
        }
      }
    }

    // Outside the mutex: do the actual gRPC push.
    auto stub = pool_.get_stub(snapshot.parent_node);
    if (stub) {
      PeerChunk chunk;
      chunk.set_request_id(snapshot.req_id);
      chunk.set_source_node(cfg_.node_id);
      chunk.set_destination_node(snapshot.parent_node);
      chunk.set_chunk_id(snapshot.chunk_id);
      for (auto& r : snapshot.results) {
        *chunk.add_records() = r.to_proto();
      }
      chunk.set_source_done(false);

      PeerChunkAck ack;
      grpc::ClientContext ctx;
      ctx.set_deadline(std::chrono::system_clock::now() +
                       std::chrono::milliseconds(cfg_.peer_timeout_ms));
      stub->PushPeerChunk(&ctx, chunk, &ack);
    }

    if (job_done && on_local_done_) {
      on_local_done_(snapshot.req_id);
    }
  }
}

bool Scheduler::push_one_chunk_unlocked(Job& /*job*/) {
  // Reserved for future inline-pushing optimizations. Currently unused.
  return false;
}

}  // namespace mini2
