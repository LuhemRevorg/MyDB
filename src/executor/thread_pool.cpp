#include "executor/thread_pool.h"

ThreadPool::ThreadPool(size_t n_threads) {
  for (size_t i = 0; i < n_threads; ++i) {
    workers_.emplace_back([this] {
      while (true) {
        std::function<void()> task;
        {
          std::unique_lock lock(mutex_);
          task_cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
          if (stop_ && tasks_.empty()) return;
          task = std::move(tasks_.front());
          tasks_.pop();
        }
        task();
        {
          std::unique_lock lock(mutex_);
          if (--in_flight_ == 0) done_cv_.notify_all();
        }
      }
    });
  }
}

ThreadPool::~ThreadPool() {
  { std::unique_lock lock(mutex_); stop_ = true; }
  task_cv_.notify_all();
  for (auto& w : workers_) w.join();
}

void ThreadPool::Submit(std::function<void()> task) {
  {
    std::unique_lock lock(mutex_);
    ++in_flight_;
    tasks_.push(std::move(task));
  }
  task_cv_.notify_one();
}

void ThreadPool::WaitAll() {
  std::unique_lock lock(mutex_);
  done_cv_.wait(lock, [this] { return in_flight_ == 0; });
}
