#include "server.h"
#include <iostream>
#include <chrono>

namespace proplink {

// Server 구현
Server::Server(const std::string& rep_url, const int rep_port_start, const std::string& pub_endpoint)
    : running_(false),
      rep_url_(rep_url),
      rep_port_start_(rep_port_start),
      pub_endpoint_(pub_endpoint),
      context_(1), 
      thread_pool_(std::thread::hardware_concurrency()) {
}

Server::~Server() {
  Stop();
}

void Server::Start() {
  if (running_) {
    std::cout << "Server is already running" << std::endl;
    return;
  }
  try {
    //responder_ = std::make_unique<zmq::socket_t>(context_, ZMQ_REP);
    //responder_->bind(req_rep_endpoint_);

    if (!responders_.empty()) {
      for (auto& r : responders_) r->close(); 
      responders_.clear();
    }
    for (int i = 0; i < PROPLINK_SOCK_POOL_SIZE; i++) {
      auto socket = std::make_shared<zmq::socket_t>(context_, ZMQ_REP);
      socket->setsockopt(ZMQ_LINGER, 0);
      std::string endpoint = rep_url_ + ":" + std::to_string(rep_port_start_ + i);
      std::cout << "Binding to: " << endpoint << std::endl;  // 디버깅 로그 추가
      socket->bind(endpoint);
      responders_.push_back(socket);
    }

    publisher_ = std::make_unique<zmq::socket_t>(context_, ZMQ_PUB);
    publisher_->bind(pub_endpoint_);

    inproc_socket_ = std::make_unique<zmq::socket_t>(context_, ZMQ_PAIR);
    inproc_socket_->bind("inproc://control");
    
    running_ = true;
    worker_thread_ = std::thread(&Server::__WorkerLoop, this);
  } catch (const zmq::error_t& e) {
    std::cerr << "ZeroMQ error in Start(): " << e.what() << " (errno: " << e.num() << ")" << std::endl;
    // if (responder_) responder_->close();
    for (auto& r : responders_) r->close(); 
    responders_.clear();
    if (publisher_) publisher_->close();
    if (inproc_socket_) inproc_socket_->close();
  } catch (const std::exception& e) {
    std::cerr << "Exception in Start(): " << e.what() << std::endl;
    // if (responder_) responder_->close();
    for (auto& r : responders_) r->close(); 
    responders_.clear();
    if (publisher_) publisher_->close();
    if (inproc_socket_) inproc_socket_->close();
  }
}

void Server::Stop() {
  if (running_) {
    running_ = false;

    // 내부 제어 소켓에 종료 메시지 보내기
    zmq::socket_t s(context_, ZMQ_PAIR);
    s.connect("inproc://control");
    zmq::message_t msg(5);
    memcpy(msg.data(), "STOP", 5);
    s.send(msg);

    if (worker_thread_.joinable()) worker_thread_.join();
    std::cout << "Server stopped" << std::endl;

    // if (responder_) responder_->close();
    for (auto& r : responders_) r->close(); 
    responders_.clear();
    if (publisher_) publisher_->close();
    if (inproc_socket_) inproc_socket_->close();
    std::cout << "Socket released" << std::endl;
  }
}

void Server::RegisterVariable(const Variable& variable, 
                              VariableChangedCallback callback) {
  std::lock_guard<std::mutex> lock(variables_mutex_);
  variables_[variable.name].value = variable.value;
  variables_[variable.name].read_only = variable.read_only;
  variables_[variable.name].callback = callback;
  std::cout << "Registered property: " << variable.name << std::endl;
}

void Server::RegisterTrigger(const Trigger& trigger, 
                             TriggerCallback callback) {  
  {
    std::lock_guard<std::mutex> lock(triggers_mutex_);
    triggers_[trigger].callback = callback;
  }
  
  std::cout << "Registered trigger: " << trigger << std::endl;
}

std::unordered_map<std::string, Value> Server::GetVariables() {
  std::lock_guard<std::mutex> lock(variables_mutex_);
  
  std::unordered_map<std::string, Value> result;
  
  for (const auto& p : variables_) {
    result[p.first] = p.second.value;
  }
  
  return result;
}

Value Server::GetVariable(const std::string& name) {
  std::lock_guard<std::mutex> lock(variables_mutex_);
  
  auto it = variables_.find(name);
  if (it != variables_.end()) {
    return it->second.value;
  }
  
  Value empty;
  return empty;
}

void Server::SetVariable(const std::string& name, const Value& value) {
  std::lock_guard<std::mutex> lock(variables_mutex_);
  auto it = variables_.find(name);
  if (it != variables_.end()) {
    // if (it->second.read_only) // Note that "read_only" variable can be changed by Server.
    if (it->second.value == value) return; // Prevents binding loop
    it->second.value = value;
    /*
    // Callback function is only be called when variable is changed by the Client.
    if (it->second.callback != nullptr) {
      if (it->second.callback_option == AsyncConnection) {
        std::thread([callback = it->second.callback, value = it->second.value]() {
          callback(value);
        }).detach();
      } else {
        it->second.callback(it->second.value);
      }
    }
    */
    
    // Notify the Client that the variable is changed by the Server.
    if (running_) {
      VariableMessage var;
      var.set_name(name);
      if (std::holds_alternative<double>(value)) {
        var.set_numeric_value(std::get<double>(value));
      } else if (std::holds_alternative<bool>(value)) {
        var.set_bool_value(std::get<bool>(value));
      } else {
        var.set_string_value(std::get<std::string>(value));
      }
      var.set_read_only(it->second.read_only);
      zmq::message_t msg(var.ByteSizeLong());
      if (!var.SerializeToArray(msg.data(), msg.size())) {
        std::cerr << "Failed to serialize publisher message: ByteSizeLong=" << var.ByteSizeLong() 
          << ", zmq::message_t::size=" << msg.size() << std::endl;
        return;
      }
      publisher_->send(msg);
    }
  }
  else {
    std::cout << "No named registered variable " << name << std::endl;
    return;
  }
}

void Server::__WorkerLoop() {
  try {
    std::cout << "Server started with endpoints: " << rep_url_ << ":" + std::to_string(rep_port_start_) << "~" << std::to_string(rep_port_start_ + PROPLINK_SOCK_POOL_SIZE - 1) << std::endl;

    // 모든 REP 소켓을 poll 항목으로 설정
    std::vector<zmq::pollitem_t> items;
    items.reserve(responders_.size() + 1);
    
    for (const auto& socket : responders_) {
      items.push_back({ static_cast<void*>(*socket), 0, ZMQ_POLLIN, 0 });
    }
    
    // 제어 소켓 추가
    items.push_back({ static_cast<void*>(*inproc_socket_), 0, ZMQ_POLLIN, 0 });
    
    while (running_) {
      zmq::poll(items.data(), items.size(), -1);

      // Checks req/res sockets
      for (size_t i = 0; i < responders_.size(); i++) {
        if (items[i].revents & ZMQ_POLLIN) {
          std::cout << "[__WorkerLoop] 1. Responders " << i << " recv message." << std::endl;
          zmq::message_t request;
          responders_[i]->recv(&request);
          CommandMessage command;
          if (!command.ParseFromArray(request.data(), request.size())) {
            std::cout << "[__WorkerLoop] 2. INVALID COMMAND RECEIVED" << std::endl;
            ResponseMessage response;
            response.set_success(false);
            response.set_error_message("Invalid request format");
            zmq::message_t reply(response.ByteSizeLong());
            if (!response.SerializeToArray(reply.data(), reply.size()))  {
              std::cerr << "Failed to serialize response message: ByteSizeLong=" << response.ByteSizeLong() 
                << ", zmq::message_t::size=" << reply.size() << std::endl;
            }
            else {
              responders_[i]->send(reply);
            }
            continue;
          }
          std::cout << "[__WorkerLoop] 2. RecvCommand id=" << command.command_id() << " : ";
          switch (command.command_type()) {
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
          auto socket = responders_[i];
          thread_pool_.Enqueue([this, socket, command]() {
            std::cout << "[__WorkerLoop] 3. Responder thread started" << std::endl;
            ResponseMessage response = __HandleCommand(command);
            std::cout << "[__WorkerLoop] 4. __HandleCommand done" << std::endl;
            zmq::message_t reply(response.ByteSizeLong());
            if (!response.SerializeToArray(reply.data(), reply.size())) {
              std::cerr << "Failed to serialize response message: ByteSizeLong=" << response.ByteSizeLong() 
                << ", zmq::message_t::size=" << reply.size() << std::endl;
            }
            else {
              socket->send(reply);
            }
          });
        } 
      }

      // Checks control sockets
      if (items[1].revents & ZMQ_POLLIN) {
        zmq::message_t msg;
        inproc_socket_->recv(&msg);
        std::cout << "Inproc msg recved: " << msg << std::endl;
        break;
      }
    }
    std::cout << "Worker thread stopped" << std::endl;
  }
  catch (const zmq::error_t& e) {
    std::cerr << "ZeroMQ error in Start(): " << e.what() << " (errno: " << e.num() << ")" << std::endl;
    // if (responder_) responder_->close();
    for (auto& r : responders_) r->close(); 
    responders_.clear();
    if (publisher_) publisher_->close();
    if (inproc_socket_) inproc_socket_->close();
  } catch (const std::exception& e) {
    std::cerr << "Exception in Start(): " << e.what() << std::endl;
    // if (responder_) responder_->close();
    for (auto& r : responders_) r->close(); 
    responders_.clear();
    if (publisher_) publisher_->close();
    if (inproc_socket_) inproc_socket_->close();
  }
}

ResponseMessage Server::__HandleCommand(const CommandMessage& command) {
  ResponseMessage response;
  response.set_command_id(command.command_id());
  
  switch (command.command_type()) {
    case CommandMessage::GET_VARIABLE:
      __HandleGetVariable(command, response);
      break;
    case CommandMessage::SET_VARIABLE:
      __HandleSetVariable(command, response);
      break;
    case CommandMessage::GET_ALL_VARIABLES:
      __HandleGetAllVariables(command, response);
      break;
    case CommandMessage::GET_ALL_TRIGGERS:
      __HandleGetAllTriggers(command, response);
      break;
    case CommandMessage::EXECUTE_TRIGGER:
      __HandleExecuteTrigger(command, response);
      break;
    
    default:
      response.set_success(false);
      response.set_error_message("Unknown command type");
      break;
  }
  
  return response;
}

void Server::__HandleGetVariable(const CommandMessage& command, ResponseMessage& response) {
  if (!command.has_variable_name()) {
    response.set_success(false);
    response.set_error_message("Variable name not specified");
    return;
  }
  
  std::string prop_name = command.variable_name();
  
  std::lock_guard<std::mutex> lock(variables_mutex_);
  auto it = variables_.find(prop_name);
  
  if (it != variables_.end()) {
    response.set_success(true);

    VariableMessage* prop = response.mutable_variable();
    prop->set_name(prop_name);
    prop->set_read_only(it->second.read_only);
    const Value& value = it->second.value;
    if (std::holds_alternative<double>(value)) {
      prop->set_numeric_value(std::get<double>(value));
    } else if (std::holds_alternative<bool>(value)) {
      prop->set_bool_value(std::get<bool>(value));
    } else {
      prop->set_string_value(std::get<std::string>(value));
    }
  } else {
    response.set_success(false);
    response.set_error_message("Variable not found: " + prop_name);
  }
}

void Server::__HandleSetVariable(const CommandMessage& command, ResponseMessage& response) {
  if (!command.has_variable()) {
    response.set_success(false);
    response.set_error_message("Variable not specified");
    return;
  }
  
  const VariableMessage& prop = command.variable();
  std::string prop_name = prop.name();
  
  std::lock_guard<std::mutex> lock(variables_mutex_);
  auto it = variables_.find(prop_name);
  
  if (it == variables_.end()) {
    response.set_success(false);
    response.set_error_message("Variable not found: " + prop_name);
    return;
  }
  
  if (it->second.read_only) {
    response.set_success(false);
    response.set_error_message("Variable " + prop_name + " is READ ONLY");
    return;
  }

  Value& value = it->second.value;
  bool changed = false;
  if (std::holds_alternative<double>(value) \
      && std::get<double>(value) != prop.numeric_value()) {
    value = prop.numeric_value();
    changed = true;
  } else if (std::holds_alternative<bool>(value) 
             && std::get<bool>(value) != prop.bool_value()) {
    value = prop.bool_value();
    changed = true;
  } else if (std::holds_alternative<std::string>(value) 
             && std::get<std::string>(value) != prop.string_value()) {
    value = prop.string_value();
    changed = true;
  }
  it->second.callback(it->second.value);
  response.set_success(true);
  response.set_message("Variable updated: " + prop_name);
}

void Server::__HandleGetAllVariables(const CommandMessage& command, ResponseMessage& response) {
  std::lock_guard<std::mutex> lock(variables_mutex_);
  response.set_success(true);
  for (const auto& it : variables_) {
    VariableMessage* prop = response.add_variables();
    prop->set_name(it.first);
    prop->set_read_only(it.second.read_only);
    const Value& value = it.second.value;
    if (std::holds_alternative<double>(value)) {
      prop->set_numeric_value(std::get<double>(value));
    } else if (std::holds_alternative<bool>(value)) {
      prop->set_bool_value(std::get<bool>(value));
    } else {
      prop->set_string_value(std::get<std::string>(value));
    }
  }
}

void Server::__HandleGetAllTriggers(const CommandMessage& command, ResponseMessage& response) {
  std::lock_guard<std::mutex> lock(variables_mutex_);
  response.set_success(true);
  for (const auto& it : triggers_) {
    TriggerMessage* prop = response.add_triggers();
    prop->set_name(it.first);
  }
}

void Server::__HandleExecuteTrigger(const CommandMessage& command, ResponseMessage& response) {
  if (!command.has_trigger()) {
    response.set_success(false);
    response.set_error_message("Trigger name not specified");
    return;
  }
  std::string trigger_name = command.trigger().name();
  bool success = __ExecuteTrigger(trigger_name);
  response.set_success(success);
  if (success) {
    response.set_message("Trigger executed: " + trigger_name);
  } else {
    response.set_error_message("Failed to execute trigger: " + trigger_name);
  }
}

bool Server::__ExecuteTrigger(const std::string& trigger_name) {
  std::function<void()> callback;
  int options;
  {
    std::lock_guard<std::mutex> lock(triggers_mutex_);
    auto it = triggers_.find(trigger_name);
    if (it == triggers_.end()) return false;
    callback = it->second.callback;
  }
  callback();
  return true;
}

}