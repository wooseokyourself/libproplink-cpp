#include "server.h"
#include <iostream>
#include <chrono>

namespace proplink {

Server::Server(const std::string& router_endpoint, const std::string& pub_endpoint)
    : running_(false),
      router_endpoint_(router_endpoint),
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
    router_ = std::make_unique<zmq::socket_t>(context_, ZMQ_ROUTER);
    router_->bind(router_endpoint_);

    publisher_ = std::make_unique<zmq::socket_t>(context_, ZMQ_PUB);
    publisher_->bind(pub_endpoint_);

    inproc_socket_ = std::make_unique<zmq::socket_t>(context_, ZMQ_PAIR);
    inproc_socket_->bind("inproc://control");
    
    running_ = true;
    worker_thread_ = std::thread(&Server::__WorkerLoop, this);
  } catch (const zmq::error_t& e) {
    std::cerr << "ZeroMQ error in Start(): " << e.what() << " (errno: " << e.num() << ")" << std::endl;
    if (router_) router_->close();
    if (publisher_) publisher_->close();
    if (inproc_socket_) inproc_socket_->close();
  } catch (const std::exception& e) {
    std::cerr << "Exception in Start(): " << e.what() << std::endl;
    if (router_) router_->close();
    if (publisher_) publisher_->close();
    if (inproc_socket_) inproc_socket_->close();
  }
}

void Server::Stop() {
  if (running_) {
    running_ = false;

    zmq::socket_t s(context_, ZMQ_PAIR);
    s.connect("inproc://control");
    zmq::message_t msg(5);
    memcpy(msg.data(), "STOP", 5);
    s.send(msg);

    if (worker_thread_.joinable()) worker_thread_.join();
    std::cout << "Server stopped" << std::endl;

    if (router_) router_->close();
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
    if (it->second.value == value) return; // Prevents binding loop
    it->second.value = value;
    
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
    zmq::pollitem_t items[] = {
      { static_cast<void*>(*router_), 0, ZMQ_POLLIN, 0 },
      { static_cast<void*>(*inproc_socket_), 0, ZMQ_POLLIN, 0 }
    };
    zmq::pollitem_t& router_poll = items[0];
    zmq::pollitem_t& inproc_poll = items[1];
    
    while (running_) {
      zmq::poll(items, 2, -1);

      // Checks req/res sockets
      if (router_poll.revents & ZMQ_POLLIN) {
        zmq::message_t identity;
        zmq::message_t empty;
        zmq::message_t request;
        
        router_->recv(&identity);
        router_->recv(&empty);
        router_->recv(&request);

        CommandMessage command;
        command.ParseFromArray(request.data(), request.size());
        // zmq::message_t cannot be copied, so copy its data.
        std::vector<char> identity_data(static_cast<char*>(identity.data()), 
                                        static_cast<char*>(identity.data()) + identity.size());
        std::vector<char> empty_data(static_cast<char*>(empty.data()), 
                                    static_cast<char*>(empty.data()) + empty.size());

        thread_pool_.Enqueue([this, command, identity_data, empty_data]() {
          ResponseMessage response = this->__HandleCommand(command);
          
          zmq::message_t reply(response.ByteSizeLong());
          response.SerializeToArray(reply.data(), reply.size());
          
          zmq::message_t identity_msg(identity_data.size());
          memcpy(identity_msg.data(), identity_data.data(), identity_data.size());
          
          zmq::message_t empty_msg(empty_data.size());
          memcpy(empty_msg.data(), empty_data.data(), empty_data.size());
          
          std::lock_guard<std::mutex> lock(this->router_mutex_);
          this->router_->send(identity_msg, ZMQ_SNDMORE);
          this->router_->send(empty_msg, ZMQ_SNDMORE);
          this->router_->send(reply);
        });
      }

      // Checks control sockets
      if (inproc_poll.revents & ZMQ_POLLIN) {
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
    if (router_) router_->close();
    if (publisher_) publisher_->close();
    if (inproc_socket_) inproc_socket_->close();
  } catch (const std::exception& e) {
    std::cerr << "Exception in Start(): " << e.what() << std::endl;
    if (router_) router_->close();
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
  if (std::holds_alternative<double>(value)) {
    if (prop.value_case() == VariableMessage::kNumericValue) {
      double new_value = prop.numeric_value();
      if (std::get<double>(value) != new_value) {
        value = new_value;
        changed = true;
      }
    } else {
      response.set_success(false);
      response.set_error_message("Type mismatch: Variable '" + prop_name + 
                                "' is numeric, but received non-numeric value");
      return;
    }
  } 
  else if (std::holds_alternative<bool>(value)) {
    if (prop.value_case() == VariableMessage::kBoolValue) {
      bool new_value = prop.bool_value();
      if (std::get<bool>(value) != new_value) {
        value = new_value;
        changed = true;
      }
    } else {
      response.set_success(false);
      response.set_error_message("Type mismatch: Variable '" + prop_name + 
                                "' is boolean, but received non-boolean value");
      return;
    }
  } 
  else if (std::holds_alternative<std::string>(value)) {
    if (prop.value_case() == VariableMessage::kStringValue) {
      std::string new_value = prop.string_value();
      if (std::get<std::string>(value) != new_value) {
        value = new_value;
        changed = true;
      }
    } else {
      response.set_success(false);
      response.set_error_message("Type mismatch: Variable '" + prop_name + 
                                "' is string, but received non-string value");
      return;
    }
  }

  if (changed && it->second.callback)
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