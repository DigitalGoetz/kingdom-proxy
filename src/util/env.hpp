#pragma once

#include <cstdlib>
#include <string>

namespace kp::env {

inline std::string get(const char* name, std::string default_value) {
  const char* value = std::getenv(name);
  return value != nullptr ? std::string(value) : default_value;
}

inline int get_int(const char* name, int default_value) {
  const char* value = std::getenv(name);
  if (value == nullptr) return default_value;
  try {
    return std::stoi(value);
  } catch (...) {
    return default_value;
  }
}

}  // namespace kp::env
