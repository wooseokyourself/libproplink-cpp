#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>
#include <atomic>

namespace proplink {

class ThreadPool {
public:
  ThreadPool(size_t threads);
  ~ThreadPool();
  template<class F, class... Args>
  auto Enqueue(F&& f, Args&&... args) 
    -> std::future<typename std::result_of<F(Args...)>::type> {
    
    using return_type = typename std::result_of<F(Args...)>::type;
    
    // 작업을 패키징하기 위한 공유 포인터 생성
    auto task = std::make_shared<std::packaged_task<return_type()>>(
      std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
    
    // 작업의 future 가져오기
    std::future<return_type> res = task->get_future();
    
    // 작업을 큐에 추가
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      
      // 소멸된 스레드 풀에 작업 추가 방지
      if(stop_) {
          throw std::runtime_error("enqueue on stopped ThreadPool");
      }
      
      ++active_tasks_; // 활성 작업 수 증가
      tasks_.emplace([task](){ (*task)(); });
    }
    
    // 조건 변수를 통해 작업자 스레드 깨우기
    condition_.notify_one();
    return res;
  }
  size_t GetActiveTasksCount() const;
  size_t GetPendingTasksCount();
    
private:
  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> tasks_;
  std::mutex queue_mutex_;
  std::condition_variable condition_;
  bool stop_;
  std::atomic<size_t> active_tasks_{0};
};

};

#endif // THREAD_POOL_H