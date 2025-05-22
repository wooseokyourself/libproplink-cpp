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
  // @brief Constructs a server with both internal and external endpoints.
  // @param internal_router_endpoint The endpoint for internal router socket (dealer/router pattern).
  // @param internal_pub_endpoint The endpoint for internal publisher socket (publish/subscribe pattern).
  // @param external_router_endpoint The endpoint for external router socket (dealer/router pattern).
  // @param external_pub_endpoint The endpoint for external publisher socket (publish/subscribe pattern).
  // @param threadpool_size The size of thread pool for handling requests. Defaults to hardware concurrency.
  Server(const std::string& internal_router_endpoint, 
         const std::string& internal_pub_endpoint,
         const std::string& external_router_endpoint,
         const std::string& external_pub_endpoint, 
         const size_t threadpool_size = std::thread::hardware_concurrency()); 
         
  // @brief Constructs a server with single router and publisher endpoints.
  // @param router_endpoint The endpoint for router socket (dealer/router pattern).
  // @param pub_endpoint The endpoint for publisher socket (publish/subscribe pattern).
  // @param threadpool_size The size of thread pool for handling requests. Defaults to hardware concurrency.
  Server(const std::string& router_endpoint, 
         const std::string& pub_endpoint, 
         const size_t threadpool_size = std::thread::hardware_concurrency());
         
  ~Server();
  
  // @brief Starts the server by binding sockets and launching worker thread.
  // @return Whether the server successfully started. Returns true if already running.
  bool Start();
  
  // @brief Stops the server by closing sockets and terminating worker thread.
  void Stop();
  
  /**
   * @brief Registers a variable with optional callback for client changes.
   * Callback function is only called when variable is changed by the Client.
   * Note on RegisterVariable type determination:
   * The type of Variable is determined at compile time based on the literal value format:
   * - For double values, always include the decimal point (e.g., Variable("name", 1.0))
   * - For integer values, omit the decimal point (e.g., Variable("name", 1))
   * - Alternatively, use explicit type casting: double(1) or int(1)
   * 
   * IMPORTANT: Using the wrong format will cause type mismatch errors when calling SetVariable later.
   * Example: If a variable is registered as an int, calling SetVariable("name", 0.3) will fail
   * with a type mismatch error.
   * @param variable The variable to register with name, value, and read_only flag.
   * @param callback Optional callback to be invoked when the variable is changed by a client.
   */
  void RegisterVariable(const Variable& variable, 
                        VariableChangedCallback callback = nullptr);
  
  // @brief Registers a trigger with a callback function.
  // Callback function is only called when trigger is executed by the Client.
  // @param trigger The trigger name to register.
  // @param callback The callback function to be invoked when the trigger is executed.
  void RegisterTrigger(const Trigger& trigger, 
                       TriggerCallback callback);
  
  // @brief Gets all registered variables as a name-value map.
  // @return Map containing all variable names and their current values.
  std::unordered_map<std::string, Value> GetVariables();
  
  // @brief Gets the value of a specific variable.
  // @param name The name of the variable to retrieve.
  // @return The value of the variable, or empty Value if the variable doesn't exist.
  Value GetVariable(const std::string& name);
  
  // @brief Sets the value of a variable from the server side.
  // Note that "read_only" variable can be changed by Server.
  // Also, registered callback is only called when variable is changed by the Client.
  // This method publishes the change to all connected clients.
  // @param name The name of the variable to set.
  // @param value The new value for the variable.
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
  // @brief Closes and cleans up all ZeroMQ sockets.
  void __CleanupSockets();
  
  // @brief Main worker loop that handles incoming messages and control signals.
  void __WorkerLoop();
  
  // @brief Handles incoming router messages by parsing and dispatching to thread pool.
  // @param router_socket The router socket that received the message.
  void __HandleRouterMessage(zmq::socket_t* router_socket);
  
  // @brief Processes a command message and returns appropriate response.
  // @param command The command message to process.
  // @return Response message containing the result of the command.
  ResponseMessage __HandleCommand(const CommandMessage& command);
  
  // @brief Handles GET_VARIABLE command.
  // @param command The command message containing variable name.
  // @param response The response message to populate with variable data or error.
  void __HandleGetVariable(const CommandMessage& command, ResponseMessage& response);
  
  // @brief Handles SET_VARIABLE command with type checking and callback invocation.
  // @param command The command message containing variable name and new value.
  // @param response The response message to populate with success status or error.
  void __HandleSetVariable(const CommandMessage& command, ResponseMessage& response);
  
  // @brief Handles GET_ALL_VARIABLES command.
  // @param command The command message.
  // @param response The response message to populate with all variables data.
  void __HandleGetAllVariables(const CommandMessage& command, ResponseMessage& response);
  
  // @brief Handles GET_ALL_TRIGGERS command.
  // @param command The command message.
  // @param response The response message to populate with all trigger names.
  void __HandleGetAllTriggers(const CommandMessage& command, ResponseMessage& response);
  
  // @brief Handles EXECUTE_TRIGGER command.
  // @param command The command message containing trigger name.
  // @param response The response message to populate with execution result.
  void __HandleExecuteTrigger(const CommandMessage& command, ResponseMessage& response);
  
  // @brief Executes a trigger by calling its registered callback.
  // @param trigger_name The name of the trigger to execute.
  // @return Whether the trigger was found and executed successfully.
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