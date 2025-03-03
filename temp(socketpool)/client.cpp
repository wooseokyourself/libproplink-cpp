#include "client.h"
#include <iostream>
#include <chrono>
#include <future>

namespace proplink {

Client::Client(const std::string& req_url, const int& req_port_start, const std::string& sub_endpoint)
    : req_url_(req_url),
      req_port_start_(req_port_start), 
      sub_endpoint_(sub_endpoint), 
      context_(1),
      connected_(false),
      request_timeout_ms_(-1) {
}

Client::~Client() {
  Disconnect();
}

bool Client::Connect() {
  if (connected_) {
    return true;  // 이미 연결됨
  }
  
  try {
    //std::cout << "Creating socket to connect to " << req_endpoint_ << std::endl;
    //requester_ = std::make_unique<zmq::socket_t>(context_, ZMQ_REQ);
    //requester_->setsockopt(ZMQ_LINGER, 0);  // Closes socket without polling.
    //requester_->setsockopt(ZMQ_RCVTIMEO, &request_timeout_ms_, sizeof(request_timeout_ms_));
    //requester_->setsockopt(ZMQ_SNDTIMEO, &request_timeout_ms_, sizeof(request_timeout_ms_));
    requesters_ = std::make_unique<SocketPool>(context_, req_url_, req_port_start_);
    std::cout << "Connecting to server at " << req_url_ << ":" + std::to_string(req_port_start_) << "~" << std::to_string(req_port_start_ + PROPLINK_SOCK_POOL_SIZE) << std::endl;
    //requester_->connect(req_endpoint_);
    
    std::cout << "Creating socket to connect to " << sub_endpoint_ << std::endl;
    subscriber_ = std::make_unique<zmq::socket_t>(context_, ZMQ_SUB);
    subscriber_->setsockopt(ZMQ_SUBSCRIBE, "", 0); // Subscribes all messages.
    std::cout << "Connecting to server at " << sub_endpoint_ << std::endl;
    subscriber_->connect(sub_endpoint_);
    
    
    // 연결 테스트 (GetAllVariables 요청)
    CommandMessage cmd;
    cmd.set_command_id(requesters_->GetNextCommandId());
    cmd.set_command_type(CommandMessage::GET_ALL_VARIABLES);

    connected_ = true;
    
    ResponseMessage response = requesters_->SendCommand(cmd);
    std::cout << "response.command_id()=" << response.command_id() << std::endl;
    std::cout << "cmd.command_id()=" << cmd.command_id() << std::endl;
    
    connected_ = response.command_id() == cmd.command_id();  // 정상 응답 확인
    
    inproc_socket_ = std::make_unique<zmq::socket_t>(context_, ZMQ_PAIR);
    inproc_socket_->bind("inproc://control");

    if (connected_) {
      std::cout << "Connected to server" << std::endl;
      running_ = true;
      worker_thread_ = std::thread(&Client::__WorkerLoop, this);
    } else {
      std::cerr << "Failed to connect to server" << std::endl;
      //if (requester_) requester_->close();
      requesters_.release();
      if (subscriber_) subscriber_->close();
      if (inproc_socket_) inproc_socket_->close();
    }
    
    return connected_;
  } catch (const zmq::error_t& e) {
    std::cerr << "ZeroMQ error in Connect(): " << e.what() << " (errno: " << e.num() << ")" << std::endl;
    //if (requester_) requester_->close();
    requesters_.release();
    if (subscriber_) subscriber_->close();
    if (inproc_socket_) inproc_socket_->close();
    connected_ = false;
    return false;
  } catch (const std::exception& e) {
    std::cerr << "Exception in Connect(): " << e.what() << std::endl;
    //if (requester_) requester_->close();
    requesters_.release();
    if (subscriber_) subscriber_->close();
    if (inproc_socket_) inproc_socket_->close();
    connected_ = false;
    return false;
  }
}

void Client::Disconnect() {
  if (running_) {
    running_ = false;

    // 내부 제어 소켓에 종료 메시지 보내기
    zmq::socket_t s(context_, ZMQ_PAIR);
    s.connect("inproc://control");
    zmq::message_t msg(5);
    memcpy(msg.data(), "STOP", 5);
    s.send(msg);

    if (worker_thread_.joinable()) worker_thread_.join();
    std::cout << "Client subscriber stopped" << std::endl;

    //if (requester_) requester_->close();
    requesters_.release();
    if (subscriber_) subscriber_->close();
    if (inproc_socket_) inproc_socket_->close();
    std::cout << "Socket released" << std::endl;
  }
  if (connected_) connected_ = false;
}

Value Client::GetVariable(const std::string& name) {
  if (!connected_ && !Connect()) {
    std::cerr << "Not connected to server" << std::endl;
    return Value{};  // 기본값 반환
  }
  
  CommandMessage cmd;
  cmd.set_command_id(requesters_->GetNextCommandId());
  cmd.set_command_type(CommandMessage::GET_VARIABLE);
  cmd.set_variable_name(name);
  
  ResponseMessage response = requesters_->SendCommand(cmd);
  
  if (response.success() && response.has_variable()) {
    return __ExtractValue(response.variable());
  }
  
  if (!response.success() && response.has_error_message()) {
    std::cerr << "Error getting variable '" << name << "': " 
              << response.error_message() << std::endl;
  }
  
  return Value{};  // 기본값 반환
}

std::unordered_map<std::string, Value> Client::GetAllVariables() {
  std::unordered_map<std::string, Value> result;
  
  if (!connected_ && !Connect()) {
    std::cerr << "Not connected to server" << std::endl;
    return result;
  }
  
  CommandMessage cmd;
  cmd.set_command_id(requesters_->GetNextCommandId());
  cmd.set_command_type(CommandMessage::GET_ALL_VARIABLES);
  
  ResponseMessage response = requesters_->SendCommand(cmd);
  
  if (response.success()) {
    for (int i = 0; i < response.variables_size(); i++) {
      const VariableMessage& var = response.variables(i);
      result[var.name()] = __ExtractValue(var);
    }
  } else if (response.has_error_message()) {
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
  cmd.set_command_id(requesters_->GetNextCommandId());
  cmd.set_command_type(CommandMessage::GET_ALL_TRIGGERS);
  
  ResponseMessage response = requesters_->SendCommand(cmd);
  
  if (response.success()) {
    for (int i = 0; i < response.triggers_size(); i++) {
      const TriggerMessage& trigger = response.triggers(i);
      result.push_back(trigger.name());
    }
  } else if (response.has_error_message()) {
    std::cerr << "Error getting all triggers: " << response.error_message() << std::endl;
  }
  
  return result;
}

bool Client::SetVariable(const std::string& name, const Value& value, const ConnectionOptions connection_option) {
  if (!connected_ && !Connect()) {
    std::cerr << "Not connected to server" << std::endl;
    return false;
  }
  
  CommandMessage cmd;
  cmd.set_command_id(requesters_->GetNextCommandId());
  cmd.set_command_type(CommandMessage::SET_VARIABLE);
  
  VariableMessage* var = cmd.mutable_variable();
  var->set_name(name);
  __SetValueToVariableMessage(var, value);
  
  if (connection_option == AsyncConnection) {
    requesters_->SendCommandAsync(cmd);
    return true;
  }
  else {
    ResponseMessage response = requesters_->SendCommand(cmd);
    if (!response.success() && response.has_error_message()) {
      std::cerr << "Error setting variable '" << name << "': " 
                << response.error_message() << std::endl;
    }
    return response.success();
  }
}

bool Client::ExecuteTrigger(const std::string& trigger_name, const ConnectionOptions connection_option) {
  if (!connected_ && !Connect()) {
    std::cerr << "Not connected to server" << std::endl;
    return false;
  }
  
  CommandMessage cmd;
  cmd.set_command_id(requesters_->GetNextCommandId());
  cmd.set_command_type(CommandMessage::EXECUTE_TRIGGER);
  
  TriggerMessage* trigger = cmd.mutable_trigger();
  trigger->set_name(trigger_name);
  
  if (connection_option == AsyncConnection) {
    requesters_->SendCommandAsync(cmd);
    return true;
  }
  else {
    ResponseMessage response = requesters_->SendCommand(cmd);
    if (!response.success() && response.has_error_message()) {
      std::cerr << "Error executing trigger '" << trigger_name << "': " 
                << response.error_message() << std::endl;
    }
    return response.success();
  }
}

void Client::RegisterCallback(const std::string& name, 
                              VariableChangedCallback callback, 
                              const ConnectionOptions connection_option) {
  std::lock_guard<std::mutex> lock(callbacks_mutex_);
  callbacks_[name].callback = callback;
  callbacks_[name].callback_option = connection_option;
  last_known_values_[name] = Value();
}

void Client::__WorkerLoop() {
  std::cout << "Client started with endpoints: " << sub_endpoint_ << " (SUB)" << std::endl;
            
  zmq::pollitem_t items[] = {
    { static_cast<void*>(*subscriber_), 0, ZMQ_POLLIN, 0 },
    { static_cast<void*>(*inproc_socket_), 0, ZMQ_POLLIN, 0 }
  };
  
  while (running_) {
    zmq::poll(items, 2, -1);
    
    if (items[0].revents & ZMQ_POLLIN) {
      zmq::message_t zmqmsg;
      subscriber_->recv(&zmqmsg);
      
      VariableMessage varmsg;
      if (varmsg.ParseFromArray(zmqmsg.data(), zmqmsg.size())) {
        const std::string& name = varmsg.name();
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        auto it = callbacks_.find(name);
        if (it != callbacks_.end()) {
          Value value = __ExtractValue(varmsg);
          const bool& read_only = varmsg.read_only();
          
          // Callback function is only be called when changed value is different from the previous one. 
          auto last_value_it = last_known_values_.find(name);
          if (last_value_it != last_known_values_.end() && last_value_it->second == value) {
            continue;
          }
          else {
            last_known_values_[name] = value;
            if (it->second.callback_option == AsyncConnection) {
              std::thread([=]() {
                it->second.callback(value);
              }).detach();
            }
            else {
              it->second.callback(value);
            }
          }
        }
      }
    }
    if (items[1].revents & ZMQ_POLLIN) {
      zmq::message_t msg;
      inproc_socket_->recv(&msg);
      std::cout << "Inproc msg recved: " << msg << std::endl;
      break;
    }
  }
  std::cout << "Worker thread stopped" << std::endl;
}

Value Client::__ExtractValue(const VariableMessage& variable) {
  if (variable.has_string_value()) {
    return variable.string_value();
  } else if (variable.has_numeric_value()) {
    return variable.numeric_value();
  } else if (variable.has_bool_value()) {
    return variable.bool_value();
  }
  return Value{};  // 기본값
}

void Client::__SetValueToVariableMessage(VariableMessage* variable, const Value& value) {
  if (std::holds_alternative<std::string>(value)) {
    variable->set_string_value(std::get<std::string>(value));
  } else if (std::holds_alternative<double>(value)) {
    variable->set_numeric_value(std::get<double>(value));
  } else if (std::holds_alternative<bool>(value)) {
    variable->set_bool_value(std::get<bool>(value));
  }
}

}  // namespace proplink