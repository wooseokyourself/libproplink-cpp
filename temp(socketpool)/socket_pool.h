#ifndef SOCKET_POOL_H
#define SOCKET_POOL_H

#include <zmq.hpp>
#include <queue>
#include <mutex>
#include <condition_variable>
#include "core.h"

namespace proplink {

class SocketPool {
public: 
  SocketPool(zmq::context_t& context, const std::string& url, const int& port_start);
  ~SocketPool();
  void Connect();
  void Disconnect();
  void SendCommandAsync(const CommandMessage& cmd);
  ResponseMessage SendCommand(const CommandMessage& cmd);
  uint64_t GetNextCommandId();
private:
  const int timeout_ = -1;
  std::vector<std::shared_ptr<zmq::socket_t>> all_sockets_;
  std::queue<std::shared_ptr<zmq::socket_t>> available_sockets_;
  std::mutex available_sockets_mutex_;
  std::condition_variable available_sockets_cv_;
  std::atomic<uint64_t> command_id_;
};

};

#endif