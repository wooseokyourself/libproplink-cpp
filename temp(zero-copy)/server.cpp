#include "server.h"
#include <iostream>
#include <chrono>
#include <cstring>
#include <memory>

namespace proplink {

// Server 구현
Server::Server(const std::string& req_rep_endpoint, const std::string& pub_endpoint)
    : running_(false),
      req_rep_endpoint_(req_rep_endpoint),
      pub_endpoint_(pub_endpoint),
      context_(1) {
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
        responder_ = std::make_unique<zmq::socket_t>(context_, ZMQ_REP);
        responder_->bind(req_rep_endpoint_);

        publisher_ = std::make_unique<zmq::socket_t>(context_, ZMQ_PUB);
        publisher_->bind(pub_endpoint_);

        inproc_socket_ = std::make_unique<zmq::socket_t>(context_, ZMQ_PAIR);
        inproc_socket_->bind("inproc://control");
        
        running_ = true;
        worker_thread_ = std::thread(&Server::__WorkerLoop, this);
    } catch (const zmq::error_t& e) {
        std::cerr << "ZeroMQ error in Start(): " << e.what() << " (errno: " << e.num() << ")" << std::endl;
        if (responder_) responder_->close();
        if (publisher_) publisher_->close();
        if (inproc_socket_) inproc_socket_->close();
    } catch (const std::exception& e) {
        std::cerr << "Exception in Start(): " << e.what() << std::endl;
        if (responder_) responder_->close();
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
        s.send(msg.data(), msg.size());

        if (worker_thread_.joinable()) worker_thread_.join();
        std::cout << "Server stopped" << std::endl;

        if (responder_) responder_->close();
        if (publisher_) publisher_->close();
        if (inproc_socket_) inproc_socket_->close();
        std::cout << "Socket released" << std::endl;
    }
}

void Server::RegisterVariable(const Variable& variable, 
                             VariableChangedCallback callback, 
                             const ConnectionOptions connection_option) {
    std::lock_guard<std::mutex> lock(variables_mutex_);
    variables_[variable.name].value = variable.value;
    variables_[variable.name].read_only = variable.read_only;
    variables_[variable.name].callback = callback;
    variables_[variable.name].callback_option = connection_option;
    std::cout << "Registered property: " << variable.name << std::endl;
}

void Server::RegisterTrigger(const Trigger& trigger, 
                            TriggerCallback callback,
                            const ConnectionOptions connection_option) {  
    {
        std::lock_guard<std::mutex> lock(triggers_mutex_);
        triggers_[trigger].callback = callback;
        triggers_[trigger].callback_option = connection_option;
    }
    
    std::cout << "Registered trigger: " << trigger << (connection_option == AsyncConnection ? " (Async)" : "") << std::endl;
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
            // Zero-copy 방식으로 변수 변경 알림 전송
            __SendVariableUpdate(name, it->second.value, it->second.read_only);
        }
    }
    else {
        std::cout << "No named registered variable " << name << std::endl;
        return;
    }
}

void Server::__WorkerLoop() {
    std::cout << "Server started with endpoints: " << req_rep_endpoint_ << " (REQ/REP)" << std::endl;
            
    zmq::pollitem_t items[] = {
        { static_cast<void*>(*responder_), 0, ZMQ_POLLIN, 0 },
        { static_cast<void*>(*inproc_socket_), 0, ZMQ_POLLIN, 0 }
    };
    
    while (running_) {
        zmq::poll(items, 2, -1);
        
        if (items[0].revents & ZMQ_POLLIN) {
            zmq::message_t request;
            responder_->recv(&request);
            
            // Zero-copy 방식으로 메시지 처리
            zmq::message_t reply = __HandleZeroCopyMessage(request);
            responder_->send(reply.data(), reply.size());
        } 
        
        if (items[1].revents & ZMQ_POLLIN) {
            zmq::message_t msg;
            inproc_socket_->recv(&msg);
            std::cout << "Inproc msg recved: " << static_cast<char*>(msg.data()) << std::endl;
            break;
        }
    }
    
    std::cout << "Worker thread stopped" << std::endl;
}

zmq::message_t Server::__HandleZeroCopyMessage(const zmq::message_t& request) {
    if (request.size() < sizeof(MessageHeader)) {
        return __CreateErrorResponse(0, "Invalid message format: too small");
    }
    
    const MessageHeader* header = static_cast<const MessageHeader*>(request.data());
    uint32_t msg_id = header->msg_id;
    
    switch (header->msg_type) {
        case MSG_GET_VARIABLE:
            return __HandleGetVariableZeroCopy(request);
        case MSG_SET_VARIABLE:
            return __HandleSetVariableZeroCopy(request);
        case MSG_GET_ALL_VARIABLES:
            return __HandleGetAllVariablesZeroCopy(msg_id);
        case MSG_GET_ALL_TRIGGERS:
            return __HandleGetAllTriggersZeroCopy(msg_id);
        case MSG_EXECUTE_TRIGGER:
            return __HandleExecuteTriggerZeroCopy(request);
        default:
            return __CreateErrorResponse(msg_id, "Unknown command type");
    }
}

zmq::message_t Server::__CreateErrorResponse(uint32_t msg_id, const std::string& error_message) {
    size_t total_size = sizeof(MessageHeader) + error_message.size() + 1;
    ZmqBuffer* buffer = new ZmqBuffer(total_size);
    
    MessageHeader* header = reinterpret_cast<MessageHeader*>(buffer->data());
    header->msg_type = RESP_ERROR;
    header->msg_id = msg_id;
    header->payload_size = error_message.size() + 1;
    
    // 에러 메시지 복사
    char* error_ptr = reinterpret_cast<char*>(buffer->data() + sizeof(MessageHeader));
    memcpy(error_ptr, error_message.c_str(), error_message.size() + 1);
    
    return zmq::message_t(buffer->data(), buffer->size(), ZmqBuffer::FreeBuffer, buffer);
}

zmq::message_t Server::__CreateSuccessResponse(uint32_t msg_id, const void* payload, size_t payload_size) {
    size_t total_size = sizeof(MessageHeader) + payload_size;
    ZmqBuffer* buffer = new ZmqBuffer(total_size);
    
    MessageHeader* header = reinterpret_cast<MessageHeader*>(buffer->data());
    header->msg_type = RESP_SUCCESS;
    header->msg_id = msg_id;
    header->payload_size = payload_size;
    
    // 페이로드 복사 (있는 경우)
    if (payload && payload_size > 0) {
        memcpy(buffer->data() + sizeof(MessageHeader), payload, payload_size);
    }
    
    return zmq::message_t(buffer->data(), buffer->size(), ZmqBuffer::FreeBuffer, buffer);
}

zmq::message_t Server::__HandleGetVariableZeroCopy(const zmq::message_t& request) {
    const MessageHeader* header = static_cast<const MessageHeader*>(request.data());
    uint32_t msg_id = header->msg_id;
    
    // 변수 이름 추출
    const char* var_name = static_cast<const char*>(request.data()) + sizeof(MessageHeader);
    std::string prop_name(var_name);
    
    std::lock_guard<std::mutex> lock(variables_mutex_);
    auto it = variables_.find(prop_name);
    
    if (it != variables_.end()) {
        // 변수 값 직렬화를 위한 버퍼 크기 계산
        size_t value_size = 0;
        const Value& value = it->second.value;
        
        if (std::holds_alternative<double>(value)) {
            value_size = sizeof(uint8_t) + sizeof(double);
        } else if (std::holds_alternative<bool>(value)) {
            value_size = sizeof(uint8_t) + sizeof(bool);
        } else {
            const std::string& str_val = std::get<std::string>(value);
            value_size = sizeof(uint8_t) + sizeof(uint32_t) + str_val.size();
        }
        
        // 전체 페이로드 크기 = 변수 이름 길이 + 1(널 문자) + read_only 플래그 + 값 크기
        size_t payload_size = prop_name.size() + 1 + sizeof(bool) + value_size;
        
        ZmqBuffer* buffer = new ZmqBuffer(sizeof(MessageHeader) + payload_size);
        uint8_t* data_ptr = buffer->data();
        
        // 헤더 설정
        MessageHeader* resp_header = reinterpret_cast<MessageHeader*>(data_ptr);
        resp_header->msg_type = RESP_SUCCESS;
        resp_header->msg_id = msg_id;
        resp_header->payload_size = payload_size;
        
        // 페이로드 시작 위치
        uint8_t* payload = data_ptr + sizeof(MessageHeader);
        
        // 변수 이름 복사
        strcpy(reinterpret_cast<char*>(payload), prop_name.c_str());
        payload += prop_name.size() + 1;
        
        // read_only 플래그 복사
        *reinterpret_cast<bool*>(payload) = it->second.read_only;
        payload += sizeof(bool);
        
        // 값 타입과 데이터 복사
        if (std::holds_alternative<double>(value)) {
            *payload = VAL_TYPE_NUMERIC;
            payload += sizeof(uint8_t);
            *reinterpret_cast<double*>(payload) = std::get<double>(value);
        } else if (std::holds_alternative<bool>(value)) {
            *payload = VAL_TYPE_BOOL;
            payload += sizeof(uint8_t);
            *reinterpret_cast<bool*>(payload) = std::get<bool>(value);
        } else {
            *payload = VAL_TYPE_STRING;
            payload += sizeof(uint8_t);
            
            const std::string& str_val = std::get<std::string>(value);
            uint32_t str_len = str_val.size();
            *reinterpret_cast<uint32_t*>(payload) = str_len;
            payload += sizeof(uint32_t);
            
            memcpy(payload, str_val.c_str(), str_len);
        }
        
        return zmq::message_t(buffer->data(), buffer->size(), ZmqBuffer::FreeBuffer, buffer);
    } else {
        return __CreateErrorResponse(msg_id, "Variable not found: " + prop_name);
    }
}

zmq::message_t Server::__HandleSetVariableZeroCopy(const zmq::message_t& request) {
    const MessageHeader* header = static_cast<const MessageHeader*>(request.data());
    uint32_t msg_id = header->msg_id;
    
    // 데이터 포인터 초기화
    const uint8_t* data = static_cast<const uint8_t*>(request.data()) + sizeof(MessageHeader);
    
    // 변수 이름 추출
    std::string prop_name(reinterpret_cast<const char*>(data));
    data += prop_name.size() + 1;
    
    // 값 타입 추출
    uint8_t value_type = *data;
    data += sizeof(uint8_t);
    
    // 값 추출
    Value new_value;
    
    if (value_type == VAL_TYPE_NUMERIC) {
        new_value = *reinterpret_cast<const double*>(data);
    } else if (value_type == VAL_TYPE_BOOL) {
        new_value = *reinterpret_cast<const bool*>(data);
    } else if (value_type == VAL_TYPE_STRING) {
        uint32_t str_len = *reinterpret_cast<const uint32_t*>(data);
        data += sizeof(uint32_t);
        new_value = std::string(reinterpret_cast<const char*>(data), str_len);
    } else {
        return __CreateErrorResponse(msg_id, "Invalid value type");
    }
    
    std::lock_guard<std::mutex> lock(variables_mutex_);
    auto it = variables_.find(prop_name);
    
    if (it == variables_.end()) {
        return __CreateErrorResponse(msg_id, "Variable not found: " + prop_name);
    }
    
    if (it->second.read_only) {
        return __CreateErrorResponse(msg_id, "Variable " + prop_name + " is READ ONLY");
    }
    
    // 값 업데이트
    bool changed = false;
    if (it->second.value != new_value) {
        it->second.value = new_value;
        changed = true;
    }
    
    // 콜백 호출
    if (changed && it->second.callback != nullptr) {
        if (it->second.callback_option == AsyncConnection) {
            std::thread([=]() {
                it->second.callback(it->second.value);
            }).detach();
        } else {
            it->second.callback(it->second.value);
        }
    }
    
    // 성공 응답 생성
    std::string success_msg = "Variable updated: " + prop_name;
    return __CreateSuccessResponse(msg_id, success_msg.c_str(), success_msg.size() + 1);
}

zmq::message_t Server::__HandleGetAllVariablesZeroCopy(uint32_t msg_id) {
    std::lock_guard<std::mutex> lock(variables_mutex_);
    
    // 먼저 모든 변수의 직렬화 크기 계산
    size_t total_size = sizeof(uint32_t); // 변수 개수
    
    for (const auto& it : variables_) {
        // 변수 이름 + 널 문자
        total_size += it.first.size() + 1;
        
        // read_only 플래그
        total_size += sizeof(bool);
        
        // 값 타입 + 값
        const Value& value = it.second.value;
        if (std::holds_alternative<double>(value)) {
            total_size += sizeof(uint8_t) + sizeof(double);
        } else if (std::holds_alternative<bool>(value)) {
            total_size += sizeof(uint8_t) + sizeof(bool);
        } else {
            const std::string& str_val = std::get<std::string>(value);
            total_size += sizeof(uint8_t) + sizeof(uint32_t) + str_val.size();
        }
    }
    
    // 버퍼 생성
    ZmqBuffer* buffer = new ZmqBuffer(sizeof(MessageHeader) + total_size);
    uint8_t* data_ptr = buffer->data();
    
    // 헤더 설정
    MessageHeader* resp_header = reinterpret_cast<MessageHeader*>(data_ptr);
    resp_header->msg_type = RESP_SUCCESS;
    resp_header->msg_id = msg_id;
    resp_header->payload_size = total_size;
    
    // 페이로드 시작 위치
    uint8_t* payload = data_ptr + sizeof(MessageHeader);
    
    // 변수 개수 설정
    *reinterpret_cast<uint32_t*>(payload) = variables_.size();
    payload += sizeof(uint32_t);
    
    // 각 변수 직렬화
    for (const auto& it : variables_) {
        // 변수 이름 복사
        strcpy(reinterpret_cast<char*>(payload), it.first.c_str());
        payload += it.first.size() + 1;
        
        // read_only 플래그 복사
        *reinterpret_cast<bool*>(payload) = it.second.read_only;
        payload += sizeof(bool);
        
        // 값 타입과 데이터 복사
        const Value& value = it.second.value;
        if (std::holds_alternative<double>(value)) {
            *payload = VAL_TYPE_NUMERIC;
            payload += sizeof(uint8_t);
            *reinterpret_cast<double*>(payload) = std::get<double>(value);
            payload += sizeof(double);
        } else if (std::holds_alternative<bool>(value)) {
            *payload = VAL_TYPE_BOOL;
            payload += sizeof(uint8_t);
            *reinterpret_cast<bool*>(payload) = std::get<bool>(value);
            payload += sizeof(bool);
        } else {
            *payload = VAL_TYPE_STRING;
            payload += sizeof(uint8_t);
            
            const std::string& str_val = std::get<std::string>(value);
            uint32_t str_len = str_val.size();
            *reinterpret_cast<uint32_t*>(payload) = str_len;
            payload += sizeof(uint32_t);
            
            memcpy(payload, str_val.c_str(), str_len);
            payload += str_len;
        }
    }
    
    return zmq::message_t(buffer->data(), buffer->size(), ZmqBuffer::FreeBuffer, buffer);
}

zmq::message_t Server::__HandleGetAllTriggersZeroCopy(uint32_t msg_id) {
    std::lock_guard<std::mutex> lock(triggers_mutex_);
    
    // 먼저 모든 트리거 이름의 직렬화 크기 계산
    size_t total_size = sizeof(uint32_t); // 트리거 개수
    
    for (const auto& it : triggers_) {
        // 트리거 이름 + 널 문자
        total_size += it.first.size() + 1;
    }
    
    // 버퍼 생성
    ZmqBuffer* buffer = new ZmqBuffer(sizeof(MessageHeader) + total_size);
    uint8_t* data_ptr = buffer->data();
    
    // 헤더 설정
    MessageHeader* resp_header = reinterpret_cast<MessageHeader*>(data_ptr);
    resp_header->msg_type = RESP_SUCCESS;
    resp_header->msg_id = msg_id;
    resp_header->payload_size = total_size;
    
    // 페이로드 시작 위치
    uint8_t* payload = data_ptr + sizeof(MessageHeader);
    
    // 트리거 개수 설정
    *reinterpret_cast<uint32_t*>(payload) = triggers_.size();
    payload += sizeof(uint32_t);
    
    // 각 트리거 이름 직렬화
    for (const auto& it : triggers_) {
        // 트리거 이름 복사
        strcpy(reinterpret_cast<char*>(payload), it.first.c_str());
        payload += it.first.size() + 1;
    }
    
    return zmq::message_t(buffer->data(), buffer->size(), ZmqBuffer::FreeBuffer, buffer);
}

zmq::message_t Server::__HandleExecuteTriggerZeroCopy(const zmq::message_t& request) {
    const MessageHeader* header = static_cast<const MessageHeader*>(request.data());
    uint32_t msg_id = header->msg_id;
    
    // 트리거 이름 추출
    const char* trigger_name_ptr = static_cast<const char*>(request.data()) + sizeof(MessageHeader);
    std::string trigger_name(trigger_name_ptr);
    
    // 트리거 실행
    bool success = __ExecuteTrigger(trigger_name);
    
    if (success) {
        std::string success_msg = "Trigger executed: " + trigger_name;
        return __CreateSuccessResponse(msg_id, success_msg.c_str(), success_msg.size() + 1);
    } else {
        return __CreateErrorResponse(msg_id, "Failed to execute trigger: " + trigger_name);
    }
}

void Server::__SendVariableUpdate(const std::string& name, const Value& value, bool read_only) {
    // 값 크기 계산
    size_t value_size = 0;
    
    if (std::holds_alternative<double>(value)) {
        value_size = sizeof(uint8_t) + sizeof(double);
    } else if (std::holds_alternative<bool>(value)) {
        value_size = sizeof(uint8_t) + sizeof(bool);
    } else {
        const std::string& str_val = std::get<std::string>(value);
        value_size = sizeof(uint8_t) + sizeof(uint32_t) + str_val.size();
    }
    
    // 전체 페이로드 크기 = 변수 이름 길이 + 1(널 문자) + read_only 플래그 + 값 크기
    size_t payload_size = name.size() + 1 + sizeof(bool) + value_size;
    
    // 버퍼 생성
    ZmqBuffer* buffer = new ZmqBuffer(sizeof(MessageHeader) + payload_size);
    uint8_t* data_ptr = buffer->data();
    
    // 헤더 설정
    MessageHeader* header = reinterpret_cast<MessageHeader*>(data_ptr);
    header->msg_type = MSG_VARIABLE_UPDATE;
    header->msg_id = 0; // 공지에는 ID가 필요 없음
    header->payload_size = payload_size;
    
    // 페이로드 시작 위치
    uint8_t* payload = data_ptr + sizeof(MessageHeader);
    
    // 변수 이름 복사
    strcpy(reinterpret_cast<char*>(payload), name.c_str());
    payload += name.size() + 1;
    
    // read_only 플래그 복사
    *reinterpret_cast<bool*>(payload) = read_only;
    payload += sizeof(bool);
    
    // 값 타입과 데이터 복사
    if (std::holds_alternative<double>(value)) {
        *payload = VAL_TYPE_NUMERIC;
        payload += sizeof(uint8_t);
        *reinterpret_cast<double*>(payload) = std::get<double>(value);
    } else if (std::holds_alternative<bool>(value)) {
        *payload = VAL_TYPE_BOOL;
        payload += sizeof(uint8_t);
        *reinterpret_cast<bool*>(payload) = std::get<bool>(value);
    } else {
        *payload = VAL_TYPE_STRING;
        payload += sizeof(uint8_t);
        
        const std::string& str_val = std::get<std::string>(value);
        uint32_t str_len = str_val.size();
        *reinterpret_cast<uint32_t*>(payload) = str_len;
        payload += sizeof(uint32_t);
        
        memcpy(payload, str_val.c_str(), str_len);
    }
    
    // ZMQ 메시지 생성 및 전송
    zmq::message_t msg(buffer->data(), buffer->size(), ZmqBuffer::FreeBuffer, buffer);
    publisher_->send(msg.data(), msg.size());
}

bool Server::__ExecuteTrigger(const std::string& trigger_name) {
    std::function<void()> callback;
    int options;
    
    {
        std::lock_guard<std::mutex> lock(triggers_mutex_);
        auto it = triggers_.find(trigger_name);
        
        if (it == triggers_.end()) {
            return false;  // 트리거 콜백 없음
        }
        
        callback = it->second.callback;
        options = it->second.callback_option;
    }
    
    // 트리거 콜백 실행
    if (options == AsyncConnection) {
        // 비동기 실행
        std::thread([callback]() {
            callback();
        }).detach();
    } else {
        // 동기 실행
        callback();
    }
    
    return true;
}

} // namespace proplink