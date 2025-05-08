#ifndef PROPLINK_CLIENT_H_
#define PROPLINK_CLIENT_H_

#include <zmq.hpp>
#include <string>
#include <unordered_map>
#include <mutex>
#include <functional>
#include <queue>
#include <future>
#include "core.h"

namespace proplink {

class Client {
public:
  Client(const std::string& dealer_endpoint, const std::string& sub_endpoint);
  ~Client();
  bool Connect();
  void Disconnect();
  Value GetVariable(const std::string& name);
  std::unordered_map<std::string, Value> GetAllVariables();
  std::vector<std::string> GetAllTriggers();

  // @return Whether the command was successfully sent. 
  // It does not guarantee that the actual value change was successful 
  // (e.g. the read_only property returns true because the "send" of the command was successful, even if the value did not change).
  // @param connection_option Whether to wait for the server's response.
  // @param callback Callback to be called after the server responds.
  // It won't check the variable is read_only or not. 
  bool SetVariable(const std::string& name, const Value& value,
                   const ConnectionOptions connection_option = AsyncConnection, 
                   std::function<void(const ResponseMessage&)> callback = std::function<void(const ResponseMessage&)>());

  // @return Whether the command was successfully sent.
  // @param connection_option Whether to wait for the server's response.
  // @param callback Callback to be called after the server responds.
  bool ExecuteTrigger(const std::string& trigger_name,
                      const ConnectionOptions connection_option = AsyncConnection, 
                      std::function<void(const ResponseMessage&)> callback = std::function<void(const ResponseMessage&)>());

  bool IsConnected() const { return connected_; }

  // Registers a callback to be called when the value of Variable is changed by the server.
  void RegisterCallback(const std::string& name, 
                        VariableChangedCallback callback);

private:
  uint64_t __GetNextCommandId();
  ResponseMessage __SendCommandSync(const CommandMessage& cmd);
  void __SendCommandAsync(const CommandMessage& cmd, 
                          std::function<void(const ResponseMessage&)> callback = std::function<void(const ResponseMessage&)>());
  void __WorkerLoop();
  Value __ExtractValue(const VariableMessage& variable);
  void __SetValueToVariableMessage(VariableMessage* variable, const Value& value);
  
  // ZeroMQ
  zmq::context_t context_;
  std::unique_ptr<zmq::socket_t> dealer_;
  std::mutex dealer_mutex_;
  std::map<uint64_t, std::promise<ResponseMessage>> pending_responses_;
  std::map<uint64_t, std::function<void(const ResponseMessage&)>> async_responses_;
  std::unique_ptr<zmq::socket_t> subscriber_;
  std::unique_ptr<zmq::socket_t> inproc_socket_;
  std::string dealer_endpoint_;
  std::string sub_endpoint_;
  std::atomic<uint64_t> command_id_;
  
  // Worker threads.
  std::thread worker_thread_;
  std::atomic<bool> running_;

  // Callbacks to be called when the value of Variable is changed by the server.
  std::unordered_map<std::string, VariableChangedCallback> slots_;
  std::unordered_map<std::string, Value> slots_last_known_values_;
  std::mutex callbacks_mutex_;
  
  std::atomic<bool> connected_;
  int request_timeout_ms_;
};

}  // namespace proplink

#endif  // PROPLINK_CLIENT_H_