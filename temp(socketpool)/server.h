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
#include "thread_pool.h"
#include "core.h"

namespace proplink {

class Server {
 public:
  // 생성자: 엔드포인트 설정
  Server(const std::string& rep_url, const int rep_port_start, const std::string& pub_endpoint);
  
  // 소멸자: 자원 해제
  ~Server();
  
  // 서버 시작
  void Start();
  
  // 서버 중지
  void Stop();
  
  // Callback function is only be called when variable is changed by the Client.
  // If connection_option = SyncConnection, socket listening is blocked until the callback returns.
  void RegisterVariable(const Variable& variable, 
                        VariableChangedCallback callback = nullptr);
  
  // Callback function is only be called when variable is changed by the Client.
  void RegisterTrigger(const Trigger& trigger, 
                       TriggerCallback callback);
  
  std::unordered_map<std::string, Value> GetVariables();
  
  Value GetVariable(const std::string& name);
  
  // 특정 프로퍼티 값 설정하기
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
  void __WorkerLoop();
  ResponseMessage __HandleCommand(const CommandMessage& command);
  void __HandleGetVariable(const CommandMessage& command, ResponseMessage& response);
  void __HandleSetVariable(const CommandMessage& command, ResponseMessage& response);
  void __HandleGetAllVariables(const CommandMessage& command, ResponseMessage& response);
  void __HandleGetAllTriggers(const CommandMessage& command, ResponseMessage& response);
  void __HandleExecuteTrigger(const CommandMessage& command, ResponseMessage& response);
  bool __ExecuteTrigger(const std::string& trigger_name);

  zmq::context_t context_;
  //std::unique_ptr<zmq::socket_t> responder_;  // REQ/REP 소켓
  std::vector< std::shared_ptr<zmq::socket_t> > responders_;
  std::unique_ptr<zmq::socket_t> publisher_; 
  std::unique_ptr<zmq::socket_t> inproc_socket_;  // 서버종료용 소켓
  std::string rep_url_;
  const int rep_port_start_;
  std::string pub_endpoint_;
  
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