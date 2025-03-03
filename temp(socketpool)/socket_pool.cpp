#include "socket_pool.h"

namespace proplink {

SocketPool::SocketPool(zmq::context_t& context, const std::string& url, const int& port_start) 
  : available_sockets_mutex_(), command_id_(1) {
  // 소켓 풀 초기화
  for (size_t i = 0; i < PROPLINK_SOCK_POOL_SIZE; i++) {
    auto socket = std::make_shared<zmq::socket_t>(context, ZMQ_REQ);
    socket->setsockopt(ZMQ_LINGER, 0);  // Closes socket without polling.
    socket->setsockopt(ZMQ_RCVTIMEO, &timeout_, sizeof(timeout_));
    socket->setsockopt(ZMQ_SNDTIMEO, &timeout_, sizeof(timeout_));
    socket->connect(url + ":" + std::to_string(port_start + i));
    available_sockets_.push(socket);
    all_sockets_.push_back(socket);
  }
}

SocketPool::~SocketPool() {
  for (size_t i = 0; i < all_sockets_.size(); i++) {
    all_sockets_[i]->close();
  }
}

void SocketPool::SendCommandAsync(const CommandMessage& cmd) {
  std::thread([=]() {
    this->SendCommand(cmd);
  }).detach();
}

ResponseMessage SocketPool::SendCommand(const CommandMessage& cmd) {
  std::shared_ptr<zmq::socket_t> socket;
  
  std::cout << "SendCommand id=" << cmd.command_id() << " : ";
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
  // 사용 가능한 소켓 가져오기
  {
    std::cout << " > 1" << std::endl;
    std::unique_lock<std::mutex> lock(available_sockets_mutex_);
    available_sockets_cv_.wait(lock, [this] { return !available_sockets_.empty(); });
    
    socket = available_sockets_.front();
    available_sockets_.pop();
    std::cout << " > 2" << std::endl;
  }
  
  // 소켓을 사용하여 명령 전송 및 응답 수신
  ResponseMessage response;
  try {
    std::cout << " > 3" << std::endl;
    zmq::message_t request(cmd.ByteSizeLong());
    if (!cmd.SerializeToArray(request.data(), request.size())) {
      std::cerr << "Failed to serialize request message: ByteSizeLong=" << cmd.ByteSizeLong() 
          << ", zmq::message_t::size=" << request.size() << std::endl;
      response.set_success(false);
      response.set_error_message("Failed to serialize command");
      return response;
    }
    std::cout << " > 4" << std::endl;
    socket->send(request);
    std::cout << " > 5" << std::endl;
    
    zmq::message_t reply;
    socket->recv(&reply);
    std::cout << " > 6" << std::endl;
    response.ParseFromArray(reply.data(), reply.size());
    std::cout << " > 7" << std::endl;
  }
  catch (const std::exception& e) {
    response.set_success(false);
    response.set_error_message(std::string("Communication error: ") + e.what());
  }
  
  {
    std::lock_guard<std::mutex> lock(available_sockets_mutex_);
    std::cout << " > 8" << std::endl;
    available_sockets_.push(socket);
    std::cout << " > 9" << std::endl;
    available_sockets_cv_.notify_one();
  }
  
  return response;
}

uint64_t SocketPool::GetNextCommandId() {
  return command_id_++;
}

};