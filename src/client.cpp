#include "client.h"
#include <iostream>
#include <chrono>

namespace proplink {

Client::Client(const std::string& dealer_endpoint, const std::string& sub_endpoint)
    : dealer_endpoint_(dealer_endpoint),
      sub_endpoint_(sub_endpoint), 
      context_(1),
      connected_(false),
      command_id_(0),
      request_timeout_ms_(-1) {
}

Client::~Client() {
  Disconnect();
}

bool Client::Connect() {
  if (connected_) {
    return true;
  }
  
  try {
    std::cout << "Creating socket to connect to " << dealer_endpoint_ << std::endl;
    dealer_ = std::make_unique<zmq::socket_t>(context_, ZMQ_DEALER);
    dealer_->setsockopt(ZMQ_RCVTIMEO, &request_timeout_ms_, sizeof(request_timeout_ms_));
    dealer_->setsockopt(ZMQ_SNDTIMEO, &request_timeout_ms_, sizeof(request_timeout_ms_));
    dealer_->connect(dealer_endpoint_);
    
    std::cout << "Creating socket to connect to " << sub_endpoint_ << std::endl;
    subscriber_ = std::make_unique<zmq::socket_t>(context_, ZMQ_SUB);
    subscriber_->setsockopt(ZMQ_SUBSCRIBE, "", 0); // Subscribes all messages.
    std::cout << "Connecting to server at " << sub_endpoint_ << std::endl;
    subscriber_->connect(sub_endpoint_);
    
    inproc_socket_ = std::make_unique<zmq::socket_t>(context_, ZMQ_PAIR);
    inproc_socket_->bind("inproc://control");

    if (dealer_->connected() && subscriber_->connected()) {
      connected_ = true;
      std::cout << "Connected to server" << std::endl;
      running_ = true;
      worker_thread_ = std::thread(&Client::__WorkerLoop, this);
    } else {
      std::cerr << "Failed to connect to server" << std::endl;
      if (dealer_) dealer_->close();
      if (subscriber_) subscriber_->close();
      if (inproc_socket_) inproc_socket_->close();
    }
    return connected_;
  } catch (const zmq::error_t& e) {
    std::cerr << "ZeroMQ error in Connect(): " << e.what() << " (errno: " << e.num() << ")" << std::endl;
    if (dealer_) dealer_->close();
    if (subscriber_) subscriber_->close();
    if (inproc_socket_) inproc_socket_->close();
    connected_ = false;
    return false;
  } catch (const std::exception& e) {
    std::cerr << "Exception in Connect(): " << e.what() << std::endl;
    if (dealer_) dealer_->close();
    if (subscriber_) subscriber_->close();
    if (inproc_socket_) inproc_socket_->close();
    connected_ = false;
    return false;
  }
}

void Client::Disconnect() {
  if (running_) {
    running_ = false;

    zmq::socket_t s(context_, ZMQ_PAIR);
    s.connect("inproc://control");
    zmq::message_t msg(5);
    memcpy(msg.data(), "STOP", 5);
    s.send(msg);

    if (worker_thread_.joinable()) worker_thread_.join();
    std::cout << "Client subscriber stopped" << std::endl;

    if (dealer_) dealer_->close();
    if (subscriber_) subscriber_->close();
    if (inproc_socket_) inproc_socket_->close();
    std::cout << "Socket released" << std::endl;
  }
  if (connected_) connected_ = false;
}

Value Client::GetVariable(const std::string& name) {
  if (!connected_ && !Connect()) {
    std::cerr << "Not connected to server" << std::endl;
    return Value{};
  }
  
  CommandMessage cmd;
  cmd.set_command_id(__GetNextCommandId());
  cmd.set_command_type(CommandMessage::GET_VARIABLE);
  cmd.set_variable_name(name);
  
  ResponseMessage response = __SendCommandSync(cmd);
  
  if (response.success() && response.has_variable()) {
    return __ExtractValue(response.variable());
  }
  
  if (!response.success()) {
    std::cerr << "Error getting variable '" << name << "': " 
              << response.error_message() << std::endl;
  }
  
  return Value{};
}

std::unordered_map<std::string, Value> Client::GetAllVariables() {
  std::unordered_map<std::string, Value> result;
  
  if (!connected_ && !Connect()) {
    std::cerr << "Not connected to server" << std::endl;
    return result;
  }
  
  CommandMessage cmd;
  cmd.set_command_id(__GetNextCommandId());
  cmd.set_command_type(CommandMessage::GET_ALL_VARIABLES);
  
  ResponseMessage response = __SendCommandSync(cmd);
  
  if (response.success()) {
    for (int i = 0; i < response.variables_size(); i++) {
      const VariableMessage& var = response.variables(i);
      result[var.name()] = __ExtractValue(var);
    }
  } else {
    std::cerr << "Error getting all variables: " << response.error_message() << std::endl;
  }
  
  return result;
}

std::vector<std::string> Client::GetAllTriggers() {
  std::vector<std::string> result;
  
  if (!connected_ && !Connect()) {
    std::cerr << "Not connected to server" << std::endl;
    return result;
  }
  
  CommandMessage cmd;
  cmd.set_command_id(__GetNextCommandId());
  cmd.set_command_type(CommandMessage::GET_ALL_TRIGGERS);
  
  ResponseMessage response = __SendCommandSync(cmd);
  
  if (response.success()) {
    for (int i = 0; i < response.triggers_size(); i++) {
      const TriggerMessage& trigger = response.triggers(i);
      result.push_back(trigger.name());
    }
  } else {
    std::cerr << "Error getting all triggers: " << response.error_message() << std::endl;
  }
  
  return result;
}

bool Client::SetVariable(const std::string& name, 
                         const Value& value, 
                         const ConnectionOptions connection_option, 
                         std::function<void(const ResponseMessage&)> callback) {
  if (!connected_ && !Connect()) {
    std::cerr << "Not connected to server" << std::endl;
    return false;
  }
  
  CommandMessage cmd;
  cmd.set_command_id(__GetNextCommandId());
  cmd.set_command_type(CommandMessage::SET_VARIABLE);
  
  VariableMessage* var = cmd.mutable_variable();
  var->set_name(name);
  __SetValueToVariableMessage(var, value);
  
  if (connection_option == AsyncConnection) {
    __SendCommandAsync(cmd, callback);
    return true;
  }
  else {
    ResponseMessage response = __SendCommandSync(cmd);
    callback(response);
    return true;
  }
}

bool Client::ExecuteTrigger(const std::string& trigger_name, 
                            const ConnectionOptions connection_option, 
                            std::function<void(const ResponseMessage&)> callback) {
  if (!connected_ && !Connect()) {
    std::cerr << "Not connected to server" << std::endl;
    return false;
  }
  
  CommandMessage cmd;
  cmd.set_command_id(__GetNextCommandId());
  cmd.set_command_type(CommandMessage::EXECUTE_TRIGGER);
  
  TriggerMessage* trigger = cmd.mutable_trigger();
  trigger->set_name(trigger_name);
  
  if (connection_option == AsyncConnection) {
    __SendCommandAsync(cmd, callback);
    return true;
  }
  else {
    ResponseMessage response = __SendCommandSync(cmd);
    callback(response);
    return true;
  }
}

void Client::RegisterCallback(const std::string& name, 
                              VariableChangedCallback callback) {
  std::lock_guard<std::mutex> lock(callbacks_mutex_);
  slots_[name] = callback;
  slots_last_known_values_[name] = Value();
}

uint64_t Client::__GetNextCommandId() {
  return command_id_++;
}

ResponseMessage Client::__SendCommandSync(const CommandMessage& cmd) {
  const uint64_t cmd_id = cmd.command_id();
  
  std::cout << "__SendCommandSync id=" << cmd.command_id() << " : ";
  switch (cmd.command_type()) {
    case CommandMessage::GET_ALL_TRIGGERS: 
      std::cout << "GET_ALL_TRIGGERS" << std::endl; break;
    case CommandMessage::SET_VARIABLE: 
      std::cout << "SET_VARIABLE" << std::endl; break;
    case CommandMessage::GET_VARIABLE: 
      std::cout << "GET_VARIABLE" << std::endl; break;
    case CommandMessage::GET_ALL_VARIABLES: 
      std::cout << "GET_ALL_VARIABLES" << std::endl; break;
    case CommandMessage::EXECUTE_TRIGGER: 
      std::cout << "EXECUTE_TRIGGER" << std::endl; break;
  }
  
  std::promise<ResponseMessage> response_promise;
  std::future<ResponseMessage> response_future = response_promise.get_future();
  try {
    std::lock_guard<std::mutex> lock(dealer_mutex_);
    pending_responses_[cmd_id] = std::move(response_promise);
    dealer_->send(zmq::message_t(), ZMQ_SNDMORE);
    zmq::message_t request(cmd.ByteSizeLong());
    cmd.SerializeToArray(request.data(), request.size());
    dealer_->send(request);
  }
  catch (const zmq::error_t& e) {
    if (e.num() == EAGAIN) {
      std::cerr << "Send timeout for command ID " << cmd_id << std::endl;
      
      std::lock_guard<std::mutex> lock(dealer_mutex_);
      pending_responses_.erase(cmd_id);
      
      ResponseMessage timeout_response;
      timeout_response.set_command_id(cmd_id);
      timeout_response.set_success(false);
      timeout_response.set_error_message("Send timeout");
      return timeout_response;
    } else {
      std::cerr << "ZeroMQ error in SendCommandSync: " << e.what() << " (errno: " << e.num() << ")" << std::endl;
      
      std::lock_guard<std::mutex> lock(dealer_mutex_);
      pending_responses_.erase(cmd_id);
      
      ResponseMessage error_response;
      error_response.set_command_id(cmd_id);
      error_response.set_success(false);
      error_response.set_error_message(std::string("ZeroMQ error: ") + e.what());
      return error_response;
    }
  }
  
  try {
    return response_future.get();
  }
  catch (const std::exception& e) {
    // Exceptions from future.get()
    std::cerr << "Exception while waiting for response: " << e.what() << std::endl;
    
    ResponseMessage error_response;
    error_response.set_command_id(cmd_id);
    error_response.set_success(false);
    error_response.set_error_message(std::string("Response error: ") + e.what());
    return error_response;
  }
}

void Client::__SendCommandAsync(const CommandMessage& cmd, 
                                std::function<void(const ResponseMessage&)> callback) {
  const int64_t cmd_id = cmd.command_id();

  std::cout << "__SendCommandAsync id=" << cmd.command_id() << " : ";
  switch (cmd.command_type()) {
    case CommandMessage::GET_ALL_TRIGGERS: 
      std::cout << "GET_ALL_TRIGGERS" << std::endl; break;
    case CommandMessage::SET_VARIABLE: 
      std::cout << "SET_VARIABLE" << std::endl; break;
    case CommandMessage::GET_VARIABLE: 
      std::cout << "GET_VARIABLE" << std::endl; break;
    case CommandMessage::GET_ALL_VARIABLES: 
      std::cout << "GET_ALL_VARIABLES" << std::endl; break;
    case CommandMessage::EXECUTE_TRIGGER: 
      std::cout << "EXECUTE_TRIGGER" << std::endl; break;
  }

  {
    std::lock_guard<std::mutex> lock(dealer_mutex_);
    async_responses_[cmd_id] = callback;
    dealer_->send(zmq::message_t(), ZMQ_SNDMORE);
    zmq::message_t request(cmd.ByteSizeLong());
    cmd.SerializeToArray(request.data(), request.size());
    dealer_->send(request);
  }
}

void Client::__WorkerLoop() {
  std::cout << "Client started with endpoints: " << sub_endpoint_ << " (SUB)" << std::endl;
            
  zmq::pollitem_t items[] = {
    { static_cast<void*>(*dealer_), 0, ZMQ_POLLIN, 0 },
    { static_cast<void*>(*subscriber_), 0, ZMQ_POLLIN, 0 },
    { static_cast<void*>(*inproc_socket_), 0, ZMQ_POLLIN, 0 }
  };
  zmq::pollitem_t& dealer_poll = items[0];
  zmq::pollitem_t& subscriber_poll = items[1];
  zmq::pollitem_t& inproc_socket_poll = items[2];

  // Variables for reconnection.
  int reconnect_attempts = 0;
  const int max_reconnect_attempts = 5;
  const int initial_reconnect_delay_ms = 100;
  const int max_reconnect_delay_ms = 5000;
  auto last_reconnect_time = std::chrono::steady_clock::now();
  bool need_reconnect = false;

  while (running_) {
    if (need_reconnect) {
      if (reconnect_attempts < max_reconnect_attempts) {
        int delay_ms = std::min(
            initial_reconnect_delay_ms * (1 << reconnect_attempts),
            max_reconnect_delay_ms);
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_reconnect_time).count();
        
        if (elapsed >= delay_ms) {
          std::cout << "Attempting to reconnect (attempt " << reconnect_attempts + 1 
                    << " of " << max_reconnect_attempts << ")..." << std::endl;
          
          try {
            dealer_->close();
            subscriber_->close();
            
            dealer_ = std::make_unique<zmq::socket_t>(context_, ZMQ_DEALER);
            dealer_->setsockopt(ZMQ_RCVTIMEO, &request_timeout_ms_, sizeof(request_timeout_ms_));
            dealer_->setsockopt(ZMQ_SNDTIMEO, &request_timeout_ms_, sizeof(request_timeout_ms_));
            dealer_->connect(dealer_endpoint_);
            
            subscriber_ = std::make_unique<zmq::socket_t>(context_, ZMQ_SUB);
            subscriber_->setsockopt(ZMQ_SUBSCRIBE, "", 0);
            subscriber_->setsockopt(ZMQ_RCVTIMEO, &request_timeout_ms_, sizeof(request_timeout_ms_));
            subscriber_->connect(sub_endpoint_);
            
            items[0] = { static_cast<void*>(*dealer_), 0, ZMQ_POLLIN, 0 };
            items[1] = { static_cast<void*>(*subscriber_), 0, ZMQ_POLLIN, 0 };
            
            std::cout << "Reconnection successful" << std::endl;
            reconnect_attempts = 0;
            need_reconnect = false;
            connected_ = true;
            
            // Sends errro message to pending requests.
            std::lock_guard<std::mutex> lock(dealer_mutex_);
            for (auto& [cmd_id, promise] : pending_responses_) {
              ResponseMessage error_response;
              error_response.set_command_id(cmd_id);
              error_response.set_success(false);
              error_response.set_error_message("Connection reset during operation");
              promise.set_value(error_response);
            }
            pending_responses_.clear();
            
            // Sends errro message to async requests.
            for (auto& [cmd_id, callback] : async_responses_) {
              if (callback) {
                ResponseMessage error_response;
                error_response.set_command_id(cmd_id);
                error_response.set_success(false);
                error_response.set_error_message("Connection reset during operation");
                callback(error_response);
              }
            }
            async_responses_.clear();
          }
          catch (const zmq::error_t& e) {
            std::cerr << "Failed to reconnect: " << e.what() << std::endl;
            reconnect_attempts++;
            last_reconnect_time = std::chrono::steady_clock::now();
          }
        }
      }
      else {
        std::cerr << "Max reconnection attempts reached. Giving up." << std::endl;
        connected_ = false;
        
        // Sends failure to all requests.
        std::lock_guard<std::mutex> lock(dealer_mutex_);
        for (auto& [cmd_id, promise] : pending_responses_) {
          ResponseMessage error_response;
          error_response.set_command_id(cmd_id);
          error_response.set_success(false);
          error_response.set_error_message("Failed to reconnect after maximum attempts");
          promise.set_value(error_response);
        }
        pending_responses_.clear();
        async_responses_.clear();
        
        running_ = false;
        break;
      }
    }
    
    zmq::poll(items, 3, -1);
    if (dealer_poll.revents & ZMQ_POLLIN) {
      try {
        zmq::message_t empty;
        zmq::message_t reply;
        {
          std::lock_guard<std::mutex> lock(dealer_mutex_);
          dealer_->recv(&empty);
          dealer_->recv(&reply);
        }
        
        ResponseMessage response;
        response.ParseFromArray(reply.data(), reply.size());
        uint64_t cmd_id = response.command_id();
        
        // Handles sync communication.
        {
          std::lock_guard<std::mutex> lock(dealer_mutex_);
          auto it = pending_responses_.find(cmd_id);
          if (it != pending_responses_.end()) {
            it->second.set_value(response);
            pending_responses_.erase(it);
            continue;
          }
        }
        
        // Handles async communication.
        {
          std::lock_guard<std::mutex> lock(dealer_mutex_);
          auto it = async_responses_.find(cmd_id);
          if (it != async_responses_.end()) {
              // 콜백 함수 호출
              it->second(response);
              async_responses_.erase(it);
          }
        }
      }
      catch (const zmq::error_t& e) {
        if (e.num() == EAGAIN) {
          std::cerr << "Receive timeout on dealer socket" << std::endl;
          need_reconnect = true;
          last_reconnect_time = std::chrono::steady_clock::now();
        } else {
          std::cerr << "ZeroMQ error in dealer recv: " << e.what() << " (errno: " << e.num() << ")" << std::endl;
          need_reconnect = true;
          last_reconnect_time = std::chrono::steady_clock::now();
        }
      }
    }
    if (subscriber_poll.revents & ZMQ_POLLIN) {
      try {
        zmq::message_t zmqmsg;
        subscriber_->recv(&zmqmsg);
        
        VariableMessage varmsg;
        if (varmsg.ParseFromArray(zmqmsg.data(), zmqmsg.size())) {
          const std::string& name = varmsg.name();
          std::lock_guard<std::mutex> lock(callbacks_mutex_);
          auto it = slots_.find(name);
          if (it != slots_.end()) {
            Value value = __ExtractValue(varmsg);
            const bool& read_only = varmsg.read_only();
            
            // Callback function is only be called when changed value is different from the previous one. 
            auto last_value_it = slots_last_known_values_.find(name);
            if (last_value_it != slots_last_known_values_.end() && last_value_it->second == value) {
              continue;
            }
            else {
              it->second(value);
            }
          }
        }
      }
      catch (const zmq::error_t& e) {
        if (e.num() == EAGAIN) {
          std::cerr << "Receive timeout on subscriber socket" << std::endl;
          // SUB socket timeout may not be a fatal error, so continue without reconnecting.
        } else {
          std::cerr << "ZeroMQ error in subscriber recv: " << e.what() << " (errno: " << e.num() << ")" << std::endl;
          need_reconnect = true;
          last_reconnect_time = std::chrono::steady_clock::now();
        }
      }
    }
    if (inproc_socket_poll.revents & ZMQ_POLLIN) {
      zmq::message_t msg;
      inproc_socket_->recv(&msg);
      std::cout << "Inproc msg recved: " << msg << std::endl;
      break;
    }
  }
  std::cout << "Worker thread stopped" << std::endl;
}

Value Client::__ExtractValue(const VariableMessage& variable) {
  switch (variable.value_case()) {
  case VariableMessage::kStringValue: 
    return variable.string_value();
  case VariableMessage::kDoubleValue: 
    return variable.double_value();
  case VariableMessage::kIntValue: 
    return variable.int_value();
  case VariableMessage::kBoolValue: 
    return variable.bool_value();
  default:
    return Value{};
  }
}

void Client::__SetValueToVariableMessage(VariableMessage* variable, const Value& value) {
  if (std::holds_alternative<std::string>(value)) {
    variable->set_string_value(std::get<std::string>(value));
  } else if (std::holds_alternative<double>(value)) {
    variable->set_double_value(std::get<double>(value));
  } else if (std::holds_alternative<int>(value)) {
    variable->set_int_value(std::get<int>(value));
  } else if (std::holds_alternative<bool>(value)) {
    variable->set_bool_value(std::get<bool>(value));
  }
}

}  // namespace proplink