#pragma once

#include <chrono>
#include <format>
#include <iostream>
#include <mutex>
#include <string_view>

namespace kp {

// Minimal thread-safe logger. Not meant to be a general-purpose logging
// framework -- just enough structure (level, timestamp, component tag) to
// make the proxy's own operational logs readable.
class Logger {
 public:
  enum class Level { Debug, Info, Warn, Error };

  static Logger& instance() {
    static Logger logger;
    return logger;
  }

  template <typename... Args>
  void log(Level level, std::string_view component, std::format_string<Args...> fmt, Args&&... args) {
    if (level < min_level_) return;
    const auto message = std::format(fmt, std::forward<Args>(args)...);
    const auto now = std::chrono::system_clock::now();

    std::scoped_lock lock(mutex_);
    std::cout << std::format("{:%Y-%m-%dT%H:%M:%S}Z [{}] [{}] {}\n",
                              std::chrono::floor<std::chrono::seconds>(now), level_name(level),
                              component, message);
    // Explicit flush: stdout is fully (not line-) buffered when it isn't a
    // tty, which is always true under `docker logs` -- without this, log
    // lines sit in the buffer and never show up.
    std::cout.flush();
  }

  void set_min_level(Level level) { min_level_ = level; }

 private:
  Logger() = default;

  static constexpr std::string_view level_name(Level level) {
    switch (level) {
      case Level::Debug: return "DEBUG";
      case Level::Info: return "INFO";
      case Level::Warn: return "WARN";
      case Level::Error: return "ERROR";
    }
    return "?";
  }

  std::mutex mutex_;
  Level min_level_ = Level::Info;
};

}  // namespace kp

#define KP_LOG_DEBUG(component, ...) ::kp::Logger::instance().log(::kp::Logger::Level::Debug, component, __VA_ARGS__)
#define KP_LOG_INFO(component, ...) ::kp::Logger::instance().log(::kp::Logger::Level::Info, component, __VA_ARGS__)
#define KP_LOG_WARN(component, ...) ::kp::Logger::instance().log(::kp::Logger::Level::Warn, component, __VA_ARGS__)
#define KP_LOG_ERROR(component, ...) ::kp::Logger::instance().log(::kp::Logger::Level::Error, component, __VA_ARGS__)
