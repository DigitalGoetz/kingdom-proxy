#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "model/route.hpp"

struct sqlite3;

namespace kp {

// Owns the SQLite connection backing the route table. All public methods are
// safe to call from multiple threads (guarded by an internal mutex) -- the
// admin API handlers and the background sync worker both touch this class.
class Database {
 public:
  explicit Database(const std::string& path);
  ~Database();

  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;

  // Inserts a new route. Throws std::runtime_error if path_prefix already
  // has a route (unique constraint) or on any other SQLite failure.
  Route create_route(const NewRoute& new_route, const std::string& last_seen_ip);

  std::vector<Route> list_routes() const;
  std::vector<Route> list_enabled_routes() const;
  std::optional<Route> get_route(int64_t id) const;
  std::optional<Route> find_by_path_prefix(const std::string& path_prefix) const;

  // Returns false if no route with that id exists.
  bool set_enabled(int64_t id, bool enabled);
  bool delete_route(int64_t id);

 private:
  void init_schema();

  sqlite3* db_ = nullptr;
  mutable std::mutex mutex_;
};

}  // namespace kp
