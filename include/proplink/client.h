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
  // @brief Constructs a client with dealer and subscriber endpoints.
  // @param dealer_endpoint The endpoint of dealer socket of dealer/router pattern of ZeroMQ.
  // @param sub_endpoint The endpoint of subscribe socket of publish/subscribe pattern of ZeroMQ.
  Client(const std::string& dealer_endpoint, const std::string& sub_endpoint);

  ~Client();

  // @brief Opens sockets and sets timeout. It does not check whether communication with the actual server was successful.
  // @param socket_timeout_ms The time in milliseconds to wait for a response from the dealer socket. This value is valid until the socket is closed.
  // @return Whether sockets are open for dealer_endpoint and sub_endpoint.
  bool Open(const int socket_timeout_ms = 1000);

  // @brief Closes all sockets and terminates worker thread.
  void Close();

  // @brief Queries the value of a variable from the server using synchronous connection.
  // @param name The name of variable to retrieve.
  // @return The value of the variable 'name', or empty Value() if 'name' does not exist.
  Value GetVariable(const std::string& name);

  // @brief Queries the names and values of all variables that exist from the server using synchronous connection.
  // @return Map containing name-value pairs of all variables registered in the server.
  std::unordered_map<std::string, Value> GetAllVariables();

  // @brief Queries the names of all triggers that exist from the server using synchronous connection.
  // @return Vector containing names of all triggers registered in the server.
  std::vector<std::string> GetAllTriggers();

  // @brief Sets the value of a variable on the server.
  // @param name The name of the variable to set.
  // @param value The new value for the variable.
  // @param connection_option Whether to wait for the server's response (SyncConnection or AsyncConnection).
  // @param callback Optional callback to be called after the server responds (only for async connections).
  // @return Whether the command was successfully sent. It does not guarantee that the actual value change was successful.
  // (e.g. changing the value of read only property returns true because the "send" of the command was successful, even if the value did not change).
  // The response can be handled in the callback. It won't check if the variable is read_only or not.
  bool SetVariable(const std::string& name, const Value& value,
                   const ConnectionOptions connection_option = AsyncConnection, 
                   std::function<void(const ResponseMessage&)> callback = nullptr);

  // @brief Executes a trigger on the server.
  // @param trigger_name The name of the trigger to execute.
  // @param connection_option Whether to wait for the server's response (SyncConnection or AsyncConnection).
  // @param callback Optional callback to be called after the server responds (only for async connections).
  // @return Whether the command was successfully sent.
  bool ExecuteTrigger(const std::string& trigger_name,
                      const ConnectionOptions connection_option = AsyncConnection, 
                      std::function<void(const ResponseMessage&)> callback = nullptr);

  // @brief Checks whether sockets have been opened. It does not check whether communication with the actual server was successful.
  // @return Whether sockets are open for dealer_endpoint and sub_endpoint.
  bool IsOpened() const;

  // @brief Registers a callback to be called when the value of a variable is changed by the server.
  // @param name The name of the variable to monitor for changes.
  // @param callback The callback function to be invoked when the variable value changes.
  void RegisterCallback(const std::string& name, 
                        VariableChangedCallback callback);

private:
  // @brief Generates the next unique command ID for request tracking.
  // @return A unique command ID.
  uint64_t __GetNextCommandId();
  
  // @brief Sends a command synchronously and waits for response.
  // @param cmd The command message to send.
  // @return The response message from the server.
  ResponseMessage __SendCommandSync(const CommandMessage& cmd);
  
  // @brief Sends a command asynchronously without waiting for response.
  // @param cmd The command message to send.
  // @param callback Optional callback to be called when response is received.
  void __SendCommandAsync(const CommandMessage& cmd, 
                          std::function<void(const ResponseMessage&)> callback = nullptr);
                          
  // @brief Main worker loop that handles incoming messages and responses.
  void __WorkerLoop();
  
  // @brief Extracts Value from VariableMessage based on the message type.
  // @param variable The variable message to extract value from.
  // @return The extracted Value object.
  Value __ExtractValue(const VariableMessage& variable);
  
  // @brief Sets Value to VariableMessage based on the value type.
  // @param variable The variable message to set value to.
  // @param value The value to set in the message.
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
  
  std::atomic<bool> opened_;
  int request_timeout_ms_;
};

}  // namespace proplink

#endif  // PROPLINK_CLIENT_H_