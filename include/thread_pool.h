#ifndef PROPLINK_THREAD_POOL_H
#define PROPLINK_THREAD_POOL_H

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
  /**
  * Enqueues a task to be executed by the thread pool.
  * Thread-safe: Can be called from multiple threads concurrently.
  * @throws std::runtime_error if called after the thread pool has been stopped
  */
  template<class F, class... Args>
  auto Enqueue(F&& f, Args&&... args) 
    -> std::future<typename std::result_of<F(Args...)>::type> {
    using return_type = typename std::result_of<F(Args...)>::type;
    auto task = std::make_shared<std::packaged_task<return_type()>>(
      std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
    std::future<return_type> res = task->get_future();
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      if (stop_) throw std::runtime_error("enqueue on stopped ThreadPool");
      ++active_tasks_;
      tasks_.emplace([task](){ (*task)(); });
    }
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

#endif // PROPLINK_THREAD_POOL_H