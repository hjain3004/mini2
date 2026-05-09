#include "scheduler/WorkerPool.hpp"

#include <iostream>

namespace mini2 {

WorkerPool::WorkerPool(std::size_t num_threads) {
  if (num_threads == 0) num_threads = 1;
  threads_.reserve(num_threads);
  for (std::size_t i = 0; i < num_threads; ++i) {
    threads_.emplace_back([this] { worker_loop(); });
  }
}

WorkerPool::~WorkerPool() {
  stop();
}

void WorkerPool::submit(std::function<void()> task) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stop_) return;
    queue_.push_back(std::move(task));
  }
  cv_.notify_one();
}

void WorkerPool::stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stop_) return;
    stop_ = true;
  }
  cv_.notify_all();
  for (auto& t : threads_) {
    if (t.joinable()) t.join();
  }
  threads_.clear();
}

std::size_t WorkerPool::pending() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return queue_.size();
}

void WorkerPool::worker_loop() {
  for (;;) {
    std::function<void()> task;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
      if (stop_ && queue_.empty()) return;
      task = std::move(queue_.front());
      queue_.pop_front();
    }
    try {
      task();
    } catch (const std::exception& e) {
      std::cerr << "[worker_pool] task threw: " << e.what() << "\n";
    } catch (...) {
      std::cerr << "[worker_pool] task threw unknown exception\n";
    }
  }
}

}  // namespace mini2
