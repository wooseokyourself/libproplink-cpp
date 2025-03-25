#ifndef PROPLINK_SERVER_H_
#define PROPLINK_SERVER_H_

#include <zmq.hpp>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <unordered_map>
#include <string>
#include <vector>
#include <memory>
#include <future>
#include <set>
#include "core.h"
#include "thread_pool.h"

namespace proplink {

class Server {
 public:
  Server(const std::string& internal_router_endpoint, 
         const std::string& internal_pub_endpoint,
         const std::string& external_router_endpoint,
         const std::string& external_pub_endpoint, 
         const size_t threadpool_size = std::thread::hardware_concurrency()); 
  Server(const std::string& router_endpoint, 
         const std::string& pub_endpoint, 
         const size_t threadpool_size = std::thread::hardware_concurrency());
  ~Server();
  bool Start();
  void Stop();
  
  // Callback function is only be called when variable is changed by the Client.
  // If connection_option = SyncConnection, socket listening is blocked until the callback returns.
  void RegisterVariable(const Variable& variable, 
                        VariableChangedCallback callback = nullptr);
  
  // Callback function is only be called when variable is changed by the Client.
  void RegisterTrigger(const Trigger& trigger, 
                       TriggerCallback callback);
  
  std::unordered_map<std::string, Value> GetVariables();
  
  Value GetVariable(const std::string& name);
  
  // Note that "read_only" variable can be changed by Server.
  // Also, registered callback is only be called when variable is changed by the Client.
  void SetVariable(const std::string& name, const Value& value);

private:
  struct PropertyWithCallback {
    Value value;
    bool read_only;
    VariableChangedCallback callback;
  };
  struct TriggerWithCallback {
    TriggerCallback callback;
  };

private:
  void __CleanupSockets();
  void __WorkerLoop();
  void __HandleRouterMessage(zmq::socket_t* router_socket);
  ResponseMessage __HandleCommand(const CommandMessage& command);
  void __HandleGetVariable(const CommandMessage& command, ResponseMessage& response);
  void __HandleSetVariable(const CommandMessage& command, ResponseMessage& response);
  void __HandleGetAllVariables(const CommandMessage& command, ResponseMessage& response);
  void __HandleGetAllTriggers(const CommandMessage& command, ResponseMessage& response);
  void __HandleExecuteTrigger(const CommandMessage& command, ResponseMessage& response);
  bool __ExecuteTrigger(const std::string& trigger_name);

private:
  zmq::context_t context_;
  bool has_external_endpoints_;
  std::unique_ptr<zmq::socket_t> inproc_socket_;
  
  std::string internal_router_endpoint_;
  std::string external_router_endpoint_;
  std::unique_ptr<zmq::socket_t> internal_router_;
  std::unique_ptr<zmq::socket_t> external_router_;

  std::mutex router_mutex_;

  std::string internal_pub_endpoint_;
  std::string external_pub_endpoint_;
  std::unique_ptr<zmq::socket_t> internal_publisher_; 
  std::unique_ptr<zmq::socket_t> external_publisher_;
  
  ThreadPool thread_pool_;
  std::thread worker_thread_;
  std::atomic<bool> running_;
  
  std::mutex variables_mutex_;
  std::unordered_map<std::string, PropertyWithCallback> variables_;
  
  std::mutex triggers_mutex_;
  std::unordered_map<std::string, TriggerWithCallback> triggers_;
};

}

#endif