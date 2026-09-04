#include "worker/sync_worker.hpp"

#include <chrono>
#include <format>

#include "util/logger.hpp"

namespace kp {
namespace {
constexpr std::string_view kComponent = "sync_worker";

std::string now_iso8601() {
  const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
  return std::format("{:%Y-%m-%dT%H:%M:%S}Z", now);
}

}  // namespace

SyncWorker::SyncWorker(Database& database, NginxController nginx_controller,
                        std::chrono::seconds interval)
    : database_(database), nginx_controller_(std::move(nginx_controller)), interval_(interval) {}

SyncWorker::~SyncWorker() { stop(); }

void SyncWorker::start() {
  if (running_.exchange(true)) return;
  thread_ = std::thread(&SyncWorker::run, this);
  KP_LOG_INFO(kComponent, "started (interval={}s)", interval_.count());
}

void SyncWorker::stop() {
  if (!running_.exchange(false)) return;
  {
    std::scoped_lock lock(cv_mutex_);
    wake_requested_ = true;
  }
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
  KP_LOG_INFO(kComponent, "stopped");
}

void SyncWorker::trigger() {
  {
    std::scoped_lock lock(cv_mutex_);
    wake_requested_ = true;
  }
  cv_.notify_all();
}

void SyncWorker::run() {
  // Sync once at startup so a restarted API immediately reconciles nginx
  // with whatever routes are already in the database.
  sync_once();

  while (running_.load()) {
    std::unique_lock lock(cv_mutex_);
    cv_.wait_for(lock, interval_, [this] { return wake_requested_ || !running_.load(); });
    wake_requested_ = false;
    lock.unlock();

    if (!running_.load()) break;
    sync_once();
  }
}

void SyncWorker::sync_once() {
  try {
    const auto routes = database_.list_enabled_routes();
    const auto result = nginx_controller_.apply(routes);

    std::scoped_lock lock(status_mutex_);
    status_.last_sync_ok = result.ok;
    status_.last_error = result.ok ? "" : result.detail;
    status_.last_sync_at = now_iso8601();
    status_.sync_count++;

    if (result.ok) {
      KP_LOG_INFO(kComponent, "sync #{} applied {} route(s)", status_.sync_count, routes.size());
    } else {
      KP_LOG_ERROR(kComponent, "sync #{} failed: {}", status_.sync_count, result.detail);
    }
  } catch (const std::exception& e) {
    std::scoped_lock lock(status_mutex_);
    status_.last_sync_ok = false;
    status_.last_error = e.what();
    status_.last_sync_at = now_iso8601();
    status_.sync_count++;
    KP_LOG_ERROR(kComponent, "sync #{} threw: {}", status_.sync_count, e.what());
  }
}

SyncWorker::Status SyncWorker::status() const {
  std::scoped_lock lock(status_mutex_);
  return status_;
}

}  // namespace kp
