#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include "db/database.hpp"
#include "nginx/nginx_controller.hpp"

namespace kp {

// Background thread that keeps the live nginx config in sync with the
// routes table. Runs on two triggers: an explicit trigger() call (made by
// the API right after a route is created/updated/deleted) for low latency,
// and a periodic interval as a safety net in case a trigger is ever missed.
class SyncWorker {
 public:
  struct Status {
    bool last_sync_ok = false;
    std::string last_error;
    std::string last_sync_at;  // ISO-8601 UTC, empty if never run.
    uint64_t sync_count = 0;
  };

  SyncWorker(Database& database, NginxController nginx_controller,
             std::chrono::seconds interval);
  ~SyncWorker();

  SyncWorker(const SyncWorker&) = delete;
  SyncWorker& operator=(const SyncWorker&) = delete;

  void start();
  void stop();

  // Wakes the worker to run a sync immediately rather than waiting for the
  // next periodic tick. Non-blocking.
  void trigger();

  Status status() const;

 private:
  void run();
  void sync_once();

  Database& database_;
  NginxController nginx_controller_;
  std::chrono::seconds interval_;

  std::thread thread_;
  std::mutex cv_mutex_;
  std::condition_variable cv_;
  std::atomic<bool> running_{false};
  bool wake_requested_ = false;

  mutable std::mutex status_mutex_;
  Status status_;
};

}  // namespace kp
