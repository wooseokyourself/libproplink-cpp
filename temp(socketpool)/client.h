#ifndef PROPLINK_CLIENT_H_
#define PROPLINK_CLIENT_H_

#include <zmq.hpp>
#include <string>
#include <unordered_map>
#include <mutex>
#include <functional>
#include <queue>
#include "core.h"
#include "socket_pool.h"

namespace proplink {

class Client {
 public:
  Client(const std::string& req_url, const int& req_port_start, const std::string& sub_endpoint);
  ~Client();
  bool Connect();
  void Disconnect();
  Value GetVariable(const std::string& name);
  std::unordered_map<std::string, Value> GetAllVariables();
  std::vector<std::string> GetAllTriggers();
  bool SetVariable(const std::string& name, const Value& value,
                   const ConnectionOptions connection_option = AsyncConnection);
  bool ExecuteTrigger(const std::string& trigger_name,
                      const ConnectionOptions connection_option = AsyncConnection);
  bool IsConnected() const { return connected_; }
  void RegisterCallback(const std::string& name, 
                        VariableChangedCallback callback, 
                        const ConnectionOptions connection_option = AsyncConnection);

 private:
  void __WorkerLoop();
  Value __ExtractValue(const VariableMessage& variable);
  void __SetValueToVariableMessage(VariableMessage* variable, const Value& value);
  
  // ZeroMQ 관련
  zmq::context_t context_;
  // std::unique_ptr<zmq::socket_t> requester_;  // REQ 소켓
  std::unique_ptr<SocketPool> requesters_;
  std::unique_ptr<zmq::socket_t> subscriber_;  // SUB 소켓
  std::unique_ptr<zmq::socket_t> inproc_socket_;  // 종료용 소켓
  std::string req_url_;
  int req_port_start_;
  std::string sub_endpoint_;
  
  // 스레드 관련
  std::thread worker_thread_;
  std::atomic<bool> running_;

  struct Callback {
    VariableChangedCallback callback;
    ConnectionOptions callback_option; 
  };
  std::unordered_map<std::string, Callback> callbacks_;
  std::unordered_map<std::string, Value> last_known_values_;
  std::mutex callbacks_mutex_;
  
  std::atomic<bool> connected_;
  int request_timeout_ms_;
};

}  // namespace proplink

#endif  // PROPLINK_CLIENT_H_