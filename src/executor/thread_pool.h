#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class ThreadPool {
 public:
  explicit ThreadPool(size_t n_threads);
  ~ThreadPool();

  // Submit a task. Returns immediately; task runs on a worker thread.
  void Submit(std::function<void()> task);

  // Block until all submitted tasks have finished.
  void WaitAll();

  size_t NumThreads() const { return workers_.size(); }

 private:
  std::vector<std::thread>          workers_;
  std::queue<std::function<void()>> tasks_;
  std::mutex                        mutex_;
  std::condition_variable           task_cv_;   // workers wait here for tasks
  std::condition_variable           done_cv_;   // WaitAll waits here
  bool                              stop_{false};
  int                               in_flight_{0};  // tasks running or queued
};
