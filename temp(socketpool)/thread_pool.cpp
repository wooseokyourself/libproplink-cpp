#include "thread_pool.h"

namespace proplink {

ThreadPool::ThreadPool(size_t threads) : stop_(false) {
  for(size_t i = 0; i < threads; ++i) {
    workers_.emplace_back([this] {
      while(true) {
        std::function<void()> task;
        
        // 작업을 가져오거나 종료 신호를 기다립니다.
        {
          std::unique_lock<std::mutex> lock(this->queue_mutex_);
          this->condition_.wait(lock, [this] { 
            return this->stop_ || !this->tasks_.empty(); 
          });
          
          // stop이 true이고 작업이 없으면 스레드를 종료합니다.
          if(this->stop_ && this->tasks_.empty()) {
            return;
          }
          
          // 대기열에서 작업을 가져옵니다.
          task = std::move(this->tasks_.front());
          this->tasks_.pop();
        }
        
        // 작업을 실행합니다.
        task();
        
        // 활성 작업 수를 줄입니다.
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
  
  // 모든 스레드 깨우기
  condition_.notify_all();
  
  // 모든 스레드가 완료될 때까지 대기
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