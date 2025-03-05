#include "libproplink/thread_pool.h"

namespace proplink {

ThreadPool::ThreadPool(size_t threads) : stop_(false) {
  for(size_t i = 0; i < threads; ++i) {
    workers_.emplace_back([this] {
      while(true) {
        std::function<void()> task;
        
        // Fetches a task or waits for a termination signal.
        {
          std::unique_lock<std::mutex> lock(this->queue_mutex_);
          this->condition_.wait(lock, [this] { 
            return this->stop_ || !this->tasks_.empty(); 
          });
          if (this->stop_ && this->tasks_.empty()) return;

          // Get a job from the queue.
          task = std::move(this->tasks_.front());
          this->tasks_.pop();
        }
        task();
        --active_tasks_;
      }
    });
  }
}

ThreadPool::~ThreadPool() {
  {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    stop_ = true;
  }
  condition_.notify_all();
  for(std::thread &worker: workers_) {
    if(worker.joinable()) {
      worker.join();
    }
  }
}

size_t ThreadPool::GetActiveTasksCount() const {
  return active_tasks_;
}

size_t ThreadPool::GetPendingTasksCount() {
  std::unique_lock<std::mutex> lock(queue_mutex_);
  return tasks_.size();
}

};