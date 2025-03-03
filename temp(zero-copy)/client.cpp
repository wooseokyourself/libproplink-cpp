#include "client.h"
#include <iostream>
#include <chrono>
#include <future>
#include <cstring>

namespace proplink {

// 응답 처리를 위한 헬퍼 구조체
struct Response {
    bool success;
    std::string error_message;
    std::vector<std::pair<std::string, Value>> variables;
    std::vector<std::string> triggers;
    Value value;
};

Client::Client(const std::string& req_endpoint, const std::string& sub_endpoint)
    : req_endpoint_(req_endpoint),
      sub_endpoint_(sub_endpoint), 
      context_(1),
      next_command_id_(1),
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
        std::cout << "Creating socket to connect to " << req_endpoint_ << std::endl;
        requester_ = std::make_unique<zmq::socket_t>(context_, ZMQ_REQ);
        requester_->setsockopt(ZMQ_LINGER, 0);  // Closes socket without polling.
        requester_->setsockopt(ZMQ_RCVTIMEO, &request_timeout_ms_, sizeof(request_timeout_ms_));
        requester_->setsockopt(ZMQ_SNDTIMEO, &request_timeout_ms_, sizeof(request_timeout_ms_));
        std::cout << "Connecting to server at " << req_endpoint_ << std::endl;
        requester_->connect(req_endpoint_);
        
        std::cout << "Creating socket to connect to " << sub_endpoint_ << std::endl;
        subscriber_ = std::make_unique<zmq::socket_t>(context_, ZMQ_SUB);
        subscriber_->setsockopt(ZMQ_SUBSCRIBE, "", 0); // Subscribes all messages.
        std::cout << "Connecting to server at " << sub_endpoint_ << std::endl;
        subscriber_->connect(sub_endpoint_);
        
        // 연결 테스트 (GetAllVariables 요청)
        uint32_t cmd_id = __GetNextCommandId();
        Response response = __SendGetAllVariablesCommand(cmd_id);
        
        connected_ = response.success;  // 정상 응답 확인
        
        inproc_socket_ = std::make_unique<zmq::socket_t>(context_, ZMQ_PAIR);
        inproc_socket_->bind("inproc://control");

        if (connected_) {
            std::cout << "Connected to server" << std::endl;
            running_ = true;
            worker_thread_ = std::thread(&Client::__WorkerLoop, this);
        } else {
            std::cerr << "Failed to connect to server" << std::endl;
            if (requester_) requester_->close();
            if (subscriber_) subscriber_->close();
            if (inproc_socket_) inproc_socket_->close();
        }
        
        return connected_;
    } catch (const zmq::error_t& e) {
        std::cerr << "ZeroMQ error in Connect(): " << e.what() << " (errno: " << e.num() << ")" << std::endl;
        if (requester_) requester_->close();
        if (subscriber_) subscriber_->close();
        if (inproc_socket_) inproc_socket_->close();
        connected_ = false;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Exception in Connect(): " << e.what() << std::endl;
        if (requester_) requester_->close();
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
        s.send(msg.data(), msg.size());

        if (worker_thread_.joinable()) worker_thread_.join();
        std::cout << "Client subscriber stopped" << std::endl;

        if (requester_) requester_->close();
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
    
    uint32_t cmd_id = __GetNextCommandId();
    Response response = __SendGetVariableCommand(cmd_id, name);
    
    if (!response.success && !response.error_message.empty()) {
        std::cerr << "Error getting variable '" << name << "': " 
                << response.error_message << std::endl;
    }
    
    return response.value;  // 성공적이지 않으면 기본값 반환
}

std::unordered_map<std::string, Value> Client::GetAllVariables() {
    std::unordered_map<std::string, Value> result;
    
    if (!connected_ && !Connect()) {
        std::cerr << "Not connected to server" << std::endl;
        return result;
    }
    
    uint32_t cmd_id = __GetNextCommandId();
    Response response = __SendGetAllVariablesCommand(cmd_id);
    
    if (response.success) {
        for (const auto& var : response.variables) {
            result[var.first] = var.second;
        }
    } else if (!response.error_message.empty()) {
        std::cerr << "Error getting all variables: " << response.error_message << std::endl;
    }
    
    return result;
}

std::vector<std::string> Client::GetAllTriggers() {
    std::vector<std::string> result;
    
    if (!connected_ && !Connect()) {
        std::cerr << "Not connected to server" << std::endl;
        return result;
    }
    
    uint32_t cmd_id = __GetNextCommandId();
    Response response = __SendGetAllTriggersCommand(cmd_id);
    
    if (response.success) {
        result = response.triggers;
    } else if (!response.error_message.empty()) {
        std::cerr << "Error getting all triggers: " << response.error_message << std::endl;
    }
    
    return result;
}

bool Client::SetVariable(const std::string& name, const Value& value) {
    if (!connected_ && !Connect()) {
        std::cerr << "Not connected to server" << std::endl;
        return false;
    }
    
    uint32_t cmd_id = __GetNextCommandId();
    Response response = __SendSetVariableCommand(cmd_id, name, value);
    
    if (!response.success && !response.error_message.empty()) {
        std::cerr << "Error setting variable '" << name << "': " 
                << response.error_message << std::endl;
    }
    
    return response.success;
}

bool Client::ExecuteTrigger(const std::string& trigger_name) {
    if (!connected_ && !Connect()) {
        std::cerr << "Not connected to server" << std::endl;
        return false;
    }
    
    uint32_t cmd_id = __GetNextCommandId();
    Response response = __SendExecuteTriggerCommand(cmd_id, trigger_name);
    
    if (!response.success && !response.error_message.empty()) {
        std::cerr << "Error executing trigger '" << trigger_name << "': " 
                << response.error_message << std::endl;
    }
    
    return response.success;
}

void Client::RegisterCallback(const std::string& name, 
                              VariableChangedCallback callback, 
                              const ConnectionOptions connection_option) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    callbacks_[name].callback = callback;
    callbacks_[name].callback_option = connection_option;
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
            
            // Zero-copy 방식으로 메시지 처리
            __HandleZeroCopyVariableUpdate(zmqmsg);
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

void Client::__HandleZeroCopyVariableUpdate(const zmq::message_t& zmqmsg) {
    if (zmqmsg.size() < sizeof(MessageHeader)) {
        std::cerr << "Invalid variable update message: too small" << std::endl;
        return;
    }
    
    const MessageHeader* header = static_cast<const MessageHeader*>(zmqmsg.data());
    
    // 변수 업데이트 메시지 확인
    if (header->msg_type != MSG_VARIABLE_UPDATE) {
        std::cerr << "Unknown message type: " << static_cast<int>(header->msg_type) << std::endl;
        return;
    }
    
    // 페이로드 파싱
    const uint8_t* data = static_cast<const uint8_t*>(zmqmsg.data()) + sizeof(MessageHeader);
    
    // 변수 이름 추출
    std::string name(reinterpret_cast<const char*>(data));
    data += name.size() + 1;
    
    // read_only 플래그 (필요하지 않을 수 있음)
    bool read_only = *reinterpret_cast<const bool*>(data);
    data += sizeof(bool);
    
    // 값 타입 및 데이터 추출
    uint8_t value_type = *data;
    data += sizeof(uint8_t);
    
    Value value;
    
    if (value_type == VAL_TYPE_NUMERIC) {
        value = *reinterpret_cast<const double*>(data);
    } else if (value_type == VAL_TYPE_BOOL) {
        value = *reinterpret_cast<const bool*>(data);
    } else if (value_type == VAL_TYPE_STRING) {
        uint32_t str_len = *reinterpret_cast<const uint32_t*>(data);
        data += sizeof(uint32_t);
        value = std::string(reinterpret_cast<const char*>(data), str_len);
    }
    
    // 콜백 호출
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    auto it = callbacks_.find(name);
    if (it != callbacks_.end()) {
        if (it->second.callback_option == AsyncConnection) {
            std::thread([callback = it->second.callback, value = std::move(value)]() {
                callback(value);
            }).detach();
        } else {
            it->second.callback(value);
        }
    }
}

uint64_t Client::__GetNextCommandId() {
    return next_command_id_++;
}

// 메시지 전송 및 응답 처리 기본 메서드
Response Client::__SendAndReceive(const zmq::message_t& request) {
    Response empty_response = {false, "Not connected to server"};
    
    if (!connected_) {
        return empty_response;
    }
    
    try {
        // 요청 전송
        bool sent = requester_->send(request.data(), request.size());
        if (!sent) {
            empty_response.error_message = "Failed to send command";
            return empty_response;
        }
        
        // 응답 수신
        zmq::message_t reply;
        bool received = requester_->recv(&reply);
        if (!received) {
            empty_response.error_message = "Timeout waiting for response";
            return empty_response;
        }
        
        // 응답 처리
        return __ParseResponse(reply);
        
    } catch (const std::exception& e) {
        empty_response.error_message = std::string("Communication error: ") + e.what();
        connected_ = false;  // 에러 발생시 연결 상태 업데이트
        return empty_response;
    }
}

// 응답 파싱 메서드
Response Client::__ParseResponse(const zmq::message_t& reply) {
    Response response = {false};
    
    if (reply.size() < sizeof(MessageHeader)) {
        response.error_message = "Invalid response: message too small";
        return response;
    }
    
    const MessageHeader* header = static_cast<const MessageHeader*>(reply.data());
    
    // 에러 메시지 처리
    if (header->msg_type == RESP_ERROR) {
        const char* error_msg = static_cast<const char*>(reply.data()) + sizeof(MessageHeader);
        response.error_message = error_msg;
        return response;
    }
    
    // 성공 응답 처리
    if (header->msg_type == RESP_SUCCESS) {
        response.success = true;
        
        // 페이로드가 있는 경우 처리
        if (header->payload_size > 0) {
            const uint8_t* payload = static_cast<const uint8_t*>(reply.data()) + sizeof(MessageHeader);
            
            // GetAllVariables 응답 처리
            if (*(payload) > 0 && reply.size() > sizeof(MessageHeader) + sizeof(uint32_t)) {
                uint32_t var_count = *reinterpret_cast<const uint32_t*>(payload);
                payload += sizeof(uint32_t);
                
                // 각 변수 파싱
                for (uint32_t i = 0; i < var_count; i++) {
                    std::string name(reinterpret_cast<const char*>(payload));
                    payload += name.size() + 1;
                    
                    bool read_only = *reinterpret_cast<const bool*>(payload);
                    payload += sizeof(bool);
                    
                    uint8_t value_type = *payload;
                    payload += sizeof(uint8_t);
                    
                    Value value;
                    
                    if (value_type == VAL_TYPE_NUMERIC) {
                        value = *reinterpret_cast<const double*>(payload);
                        payload += sizeof(double);
                    } else if (value_type == VAL_TYPE_BOOL) {
                        value = *reinterpret_cast<const bool*>(payload);
                        payload += sizeof(bool);
                    } else if (value_type == VAL_TYPE_STRING) {
                        uint32_t str_len = *reinterpret_cast<const uint32_t*>(payload);
                        payload += sizeof(uint32_t);
                        value = std::string(reinterpret_cast<const char*>(payload), str_len);
                        payload += str_len;
                    }
                    
                    response.variables.push_back({name, value});
                }
            }
            // GetAllTriggers 응답 처리
            else if (reply.size() > sizeof(MessageHeader) + sizeof(uint32_t)) {
                uint32_t trigger_count = *reinterpret_cast<const uint32_t*>(payload);
                payload += sizeof(uint32_t);
                
                // 각 트리거 파싱
                for (uint32_t i = 0; i < trigger_count; i++) {
                    std::string name(reinterpret_cast<const char*>(payload));
                    payload += name.size() + 1;
                    response.triggers.push_back(name);
                }
            }
            // GetVariable 응답 처리
            else if (reply.size() > sizeof(MessageHeader)) {
                std::string name(reinterpret_cast<const char*>(payload));
                payload += name.size() + 1;
                
                bool read_only = *reinterpret_cast<const bool*>(payload);
                payload += sizeof(bool);
                
                uint8_t value_type = *payload;
                payload += sizeof(uint8_t);
                
                if (value_type == VAL_TYPE_NUMERIC) {
                    response.value = *reinterpret_cast<const double*>(payload);
                } else if (value_type == VAL_TYPE_BOOL) {
                    response.value = *reinterpret_cast<const bool*>(payload);
                } else if (value_type == VAL_TYPE_STRING) {
                    uint32_t str_len = *reinterpret_cast<const uint32_t*>(payload);
                    payload += sizeof(uint32_t);
                    response.value = std::string(reinterpret_cast<const char*>(payload), str_len);
                }
            }
        }
    }
    
    return response;
}

// 각 명령어에 대한 Zero-Copy 메시지 생성 및 전송 메서드
Response Client::__SendGetVariableCommand(uint32_t cmd_id, const std::string& name) {
    // 메시지 크기 계산: 헤더 + 변수 이름 + 널 문자
    size_t msg_size = sizeof(MessageHeader) + name.size() + 1;
    
    // 버퍼 생성
    ZmqBuffer* buffer = new ZmqBuffer(msg_size);
    
    // 헤더 설정
    MessageHeader* header = reinterpret_cast<MessageHeader*>(buffer->data());
    header->msg_type = MSG_GET_VARIABLE;
    header->msg_id = cmd_id;
    header->payload_size = name.size() + 1;
    
    // 변수 이름 복사
    char* name_ptr = reinterpret_cast<char*>(buffer->data() + sizeof(MessageHeader));
    strcpy(name_ptr, name.c_str());
    
    // 메시지 생성 및 전송
    zmq::message_t request(buffer->data(), buffer->size(), ZmqBuffer::FreeBuffer, buffer);
    return __SendAndReceive(request);
}

Response Client::__SendSetVariableCommand(uint32_t cmd_id, const std::string& name, const Value& value) {
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
    
    // 전체 메시지 크기 계산: 헤더 + 변수 이름 + 널 문자 + 값 크기
    size_t msg_size = sizeof(MessageHeader) + name.size() + 1 + value_size;
    
    // 버퍼 생성
    ZmqBuffer* buffer = new ZmqBuffer(msg_size);
    
    // 헤더 설정
    MessageHeader* header = reinterpret_cast<MessageHeader*>(buffer->data());
    header->msg_type = MSG_SET_VARIABLE;
    header->msg_id = cmd_id;
    header->payload_size = name.size() + 1 + value_size;
    
    // 변수 이름 복사
    uint8_t* data_ptr = buffer->data() + sizeof(MessageHeader);
    strcpy(reinterpret_cast<char*>(data_ptr), name.c_str());
    data_ptr += name.size() + 1;
    
    // 값 타입과 데이터 복사
    if (std::holds_alternative<double>(value)) {
        *data_ptr = VAL_TYPE_NUMERIC;
        data_ptr += sizeof(uint8_t);
        *reinterpret_cast<double*>(data_ptr) = std::get<double>(value);
    } else if (std::holds_alternative<bool>(value)) {
        *data_ptr = VAL_TYPE_BOOL;
        data_ptr += sizeof(uint8_t);
        *reinterpret_cast<bool*>(data_ptr) = std::get<bool>(value);
    } else {
        *data_ptr = VAL_TYPE_STRING;
        data_ptr += sizeof(uint8_t);
        
        const std::string& str_val = std::get<std::string>(value);
        uint32_t str_len = str_val.size();
        *reinterpret_cast<uint32_t*>(data_ptr) = str_len;
        data_ptr += sizeof(uint32_t);
        
        memcpy(data_ptr, str_val.c_str(), str_len);
    }
    
    // 메시지 생성 및 전송
    zmq::message_t request(buffer->data(), buffer->size(), ZmqBuffer::FreeBuffer, buffer);
    return __SendAndReceive(request);
}

Response Client::__SendGetAllVariablesCommand(uint32_t cmd_id) {
    // 버퍼 생성 (단순 헤더만 포함)
    ZmqBuffer* buffer = new ZmqBuffer(sizeof(MessageHeader));
    
    // 헤더 설정
    MessageHeader* header = reinterpret_cast<MessageHeader*>(buffer->data());
    header->msg_type = MSG_GET_ALL_VARIABLES;
    header->msg_id = cmd_id;
    header->payload_size = 0;
    
    // 메시지 생성 및 전송
    zmq::message_t request(buffer->data(), buffer->size(), ZmqBuffer::FreeBuffer, buffer);
    return __SendAndReceive(request);
}

Response Client::__SendGetAllTriggersCommand(uint32_t cmd_id) {
    // 버퍼 생성 (단순 헤더만 포함)
    ZmqBuffer* buffer = new ZmqBuffer(sizeof(MessageHeader));
    
    // 헤더 설정
    MessageHeader* header = reinterpret_cast<MessageHeader*>(buffer->data());
    header->msg_type = MSG_GET_ALL_TRIGGERS;
    header->msg_id = cmd_id;
    header->payload_size = 0;
    
    // 메시지 생성 및 전송
    zmq::message_t request(buffer->data(), buffer->size(), ZmqBuffer::FreeBuffer, buffer);
    return __SendAndReceive(request);
}

Response Client::__SendExecuteTriggerCommand(uint32_t cmd_id, const std::string& trigger_name) {
    // 메시지 크기 계산: 헤더 + 트리거 이름 + 널 문자
    size_t msg_size = sizeof(MessageHeader) + trigger_name.size() + 1;
    
    // 버퍼 생성
    ZmqBuffer* buffer = new ZmqBuffer(msg_size);
    
    // 헤더 설정
    MessageHeader* header = reinterpret_cast<MessageHeader*>(buffer->data());
    header->msg_type = MSG_EXECUTE_TRIGGER;
    header->msg_id = cmd_id;
    header->payload_size = trigger_name.size() + 1;
    
    // 트리거 이름 복사
    char* name_ptr = reinterpret_cast<char*>(buffer->data() + sizeof(MessageHeader));
    strcpy(name_ptr, trigger_name.c_str());
    
    // 메시지 생성 및 전송
    zmq::message_t request(buffer->data(), buffer->size(), ZmqBuffer::FreeBuffer, buffer);
    return __SendAndReceive(request);
}

}  // namespace proplink