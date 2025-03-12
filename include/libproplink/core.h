#ifndef PROPLINK_CORE_H_
#define PROPLINK_CORE_H_

#include "libproplink/property.pb.h"
#include <variant>

#define PROPLINK_SOCK_POOL_SIZE 1

namespace proplink {

using Value = std::variant<bool, double, int, std::string>;
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

}

#endif