#ifndef PROPLINK_CLIENT_H
#define PROPLINK_CLIENT_H

#include <string>
#include <unordered_map>
#include <functional>
#include <variant>
#include <thread>
#include <mutex>
#include <memory>
#include <vector>
#include <zmq.hpp>
#include "core.h"

namespace proplink {

// 응답 처리를 위한 내부 구조체
struct Response;

// 콜백 메타데이터 구조체
struct CallbackMetadata {
    VariableChangedCallback callback;
    ConnectionOptions callback_option;
};

class Client {
public:
    Client(const std::string& req_endpoint, const std::string& sub_endpoint);
    ~Client();

    // 연결 관리 메서드
    bool Connect();
    void Disconnect();
    
    // 변수 접근 메서드
    Value GetVariable(const std::string& name);
    std::unordered_map<std::string, Value> GetAllVariables();
    bool SetVariable(const std::string& name, const Value& value);
    
    // 트리거 메서드
    std::vector<std::string> GetAllTriggers();
    bool ExecuteTrigger(const std::string& trigger_name);
    
    // 콜백 등록 메서드
    void RegisterCallback(const std::string& name, 
                         VariableChangedCallback callback, 
                         const ConnectionOptions connection_option = SyncConnection);

private:
    // 서버 엔드포인트
    std::string req_endpoint_;
    std::string sub_endpoint_;
    
    // ZeroMQ 컨텍스트 및 소켓
    zmq::context_t context_;
    std::unique_ptr<zmq::socket_t> requester_;
    std::unique_ptr<zmq::socket_t> subscriber_;
    std::unique_ptr<zmq::socket_t> inproc_socket_;
    
    // 명령 ID 관리
    uint64_t next_command_id_;
    uint64_t __GetNextCommandId();
    
    // 연결 상태 및 워커 스레드
    bool connected_;
    bool running_;
    std::thread worker_thread_;
    
    // 요청 타임아웃
    int request_timeout_ms_;
    
    // 콜백 관리
    std::mutex callbacks_mutex_;
    std::unordered_map<std::string, CallbackMetadata> callbacks_;
    
    // 워커 루프 함수
    void __WorkerLoop();
    
    // Zero-Copy 메시지 처리 함수
    void __HandleZeroCopyVariableUpdate(const zmq::message_t& zmqmsg);
    
    // 메시지 전송 및 응답 처리 함수
    Response __SendAndReceive(const zmq::message_t& request);
    Response __ParseResponse(const zmq::message_t& reply);
    
    // 명령어별 메시지 생성 및 전송 함수
    Response __SendGetVariableCommand(uint32_t cmd_id, const std::string& name);
    Response __SendSetVariableCommand(uint32_t cmd_id, const std::string& name, const Value& value);
    Response __SendGetAllVariablesCommand(uint32_t cmd_id);
    Response __SendGetAllTriggersCommand(uint32_t cmd_id);
    Response __SendExecuteTriggerCommand(uint32_t cmd_id, const std::string& trigger_name);
};

} // namespace proplink

#endif // PROPLINK_CLIENT_H