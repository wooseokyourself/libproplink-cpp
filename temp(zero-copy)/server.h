#ifndef PROPLINK_SERVER_H
#define PROPLINK_SERVER_H

#include <string>
#include <unordered_map>
#include <functional>
#include <variant>
#include <thread>
#include <mutex>
#include <memory>
#include <zmq.hpp>
#include "core.h"

namespace proplink {

// 변수 구조체
struct Variable {
    std::string name;
    Value value;
    bool read_only = false;
};

// 변수 메타데이터 구조체
struct VariableMetadata {
    Value value;
    bool read_only;
    VariableChangedCallback callback;
    ConnectionOptions callback_option;
};

// 트리거 메타데이터 구조체
struct TriggerMetadata {
    TriggerCallback callback;
    ConnectionOptions callback_option;
};

class Server {
public:
    Server(const std::string& req_rep_endpoint, const std::string& pub_endpoint);
    ~Server();

    // 서버 제어 메서드
    void Start();
    void Stop();

    // 변수 및 트리거 등록 메서드
    void RegisterVariable(const Variable& variable, 
                         VariableChangedCallback callback = nullptr, 
                         const ConnectionOptions connection_option = SyncConnection);
    
    void RegisterTrigger(const Trigger& trigger, 
                        TriggerCallback callback,
                        const ConnectionOptions connection_option = SyncConnection);
    
    // 변수 접근 메서드
    std::unordered_map<std::string, Value> GetVariables();
    Value GetVariable(const std::string& name);
    void SetVariable(const std::string& name, const Value& value);

private:
    // 서버 설정
    bool running_;
    std::string req_rep_endpoint_;
    std::string pub_endpoint_;
    zmq::context_t context_;
    
    // ZMQ 소켓
    std::unique_ptr<zmq::socket_t> responder_;
    std::unique_ptr<zmq::socket_t> publisher_;
    std::unique_ptr<zmq::socket_t> inproc_socket_;
    
    // 워커 스레드
    std::thread worker_thread_;
    
    // 데이터 저장소
    std::mutex variables_mutex_;
    std::unordered_map<std::string, VariableMetadata> variables_;
    
    std::mutex triggers_mutex_;
    std::unordered_map<Trigger, TriggerMetadata> triggers_;
    
    // 워커 스레드 메인 루프
    void __WorkerLoop();
    
    // Zero-Copy 메시지 처리 메서드
    zmq::message_t __HandleZeroCopyMessage(const zmq::message_t& request);
    zmq::message_t __CreateErrorResponse(uint32_t msg_id, const std::string& error_message);
    zmq::message_t __CreateSuccessResponse(uint32_t msg_id, const void* payload, size_t payload_size);
    
    // 명령 처리 메서드
    zmq::message_t __HandleGetVariableZeroCopy(const zmq::message_t& request);
    zmq::message_t __HandleSetVariableZeroCopy(const zmq::message_t& request);
    zmq::message_t __HandleGetAllVariablesZeroCopy(uint32_t msg_id);
    zmq::message_t __HandleGetAllTriggersZeroCopy(uint32_t msg_id);
    zmq::message_t __HandleExecuteTriggerZeroCopy(const zmq::message_t& request);
    
    // 변수 업데이트 공지 메서드
    void __SendVariableUpdate(const std::string& name, const Value& value, bool read_only);
    
    // 트리거 실행 헬퍼
    bool __ExecuteTrigger(const std::string& trigger_name);
};

} // namespace proplink

#endif // PROPLINK_SERVER_H