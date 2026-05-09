#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace mini2 {

// Fixed-size thread pool with a FIFO task queue.
// Used for one-shot async tasks (PeerQuery forwarding, etc.) that don't
// need fairness policy. Chunk pushes go through Scheduler instead.
class WorkerPool {
public:
  explicit WorkerPool(std::size_t num_threads);
  ~WorkerPool();

  WorkerPool(const WorkerPool&) = delete;
  WorkerPool& operator=(const WorkerPool&) = delete;

  // Submit a task. Tasks are run in submission order.
  // After stop() the pool drops new submissions silently.
  void submit(std::function<void()> task);

  // Drain remaining tasks and join all threads. Idempotent.
  void stop();

  std::size_t pending() const;

private:
  void worker_loop();

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<std::function<void()>> queue_;
  std::vector<std::thread> threads_;
  bool stop_ = false;
};

}  // namespace mini2
