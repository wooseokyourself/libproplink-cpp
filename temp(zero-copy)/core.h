#ifndef PROPLINK_CORE_H_
#define PROPLINK_CORE_H_

#include "property.pb.h"
#include <variant>

namespace proplink {

using Value = std::variant<bool, double, std::string>;
struct Variable {
  Variable(const std::string& name, const Value& value, const bool& read_only = false) 
    : name(name), value(value), read_only(read_only) {}
  std::string name;
  Value value;
  const bool read_only; // If true, only Server can change the value.
};
using Trigger = std::string;
using VariableChangedCallback = std::function<void(const Value& value)>;
using TriggerCallback = std::function<void()>;
enum ConnectionOptions {
  SyncConnection = 0,
  AsyncConnection = 1
};

// Zero-Copy 메시지 형식 상수 정의 (서버와 동일해야 함)
constexpr uint8_t MSG_GET_VARIABLE = 1;
constexpr uint8_t MSG_SET_VARIABLE = 2;
constexpr uint8_t MSG_GET_ALL_VARIABLES = 3;
constexpr uint8_t MSG_GET_ALL_TRIGGERS = 4;
constexpr uint8_t MSG_EXECUTE_TRIGGER = 5;
constexpr uint8_t MSG_VARIABLE_UPDATE = 6;

constexpr uint8_t VAL_TYPE_NUMERIC = 1;
constexpr uint8_t VAL_TYPE_BOOL = 2;
constexpr uint8_t VAL_TYPE_STRING = 3;

constexpr uint8_t RESP_SUCCESS = 1;
constexpr uint8_t RESP_ERROR = 0;

// 메시지 헤더 구조체 (서버와 동일해야 함)
struct MessageHeader {
    uint8_t msg_type;
    uint32_t msg_id;
    uint32_t payload_size;
};

// Zero-Copy 메모리 관리를 위한 헬퍼 클래스
class ZmqBuffer {
public:
    ZmqBuffer(size_t size) : size_(size) {
        data_ = new uint8_t[size];
    }
    
    ~ZmqBuffer() {
        delete[] data_;
    }
    
    uint8_t* data() { return data_; }
    size_t size() const { return size_; }
    
    // ZMQ 메시지 해제 콜백
    static void FreeBuffer(void* data, void* hint) {
        ZmqBuffer* buffer = static_cast<ZmqBuffer*>(hint);
        delete buffer;
    }

private:
    uint8_t* data_;
    size_t size_;
};

}

#endif