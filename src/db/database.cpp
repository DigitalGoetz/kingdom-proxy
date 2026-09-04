#include "db/database.hpp"

#include <sqlite3.h>

#include <format>
#include <stdexcept>

#include "util/logger.hpp"

namespace kp {
namespace {

constexpr std::string_view kComponent = "database";

// RAII wrapper around a prepared statement so every code path -- including
// early returns from thrown exceptions -- finalizes it exactly once.
class Stmt {
 public:
  Stmt(sqlite3* db, const char* sql) : db_(db) {
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
      throw std::runtime_error(std::format("failed to prepare statement: {}", sqlite3_errmsg(db_)));
    }
  }

  ~Stmt() { sqlite3_finalize(stmt_); }

  Stmt(const Stmt&) = delete;
  Stmt& operator=(const Stmt&) = delete;

  void bind(int index, const std::string& value) {
    sqlite3_bind_text(stmt_, index, value.c_str(), -1, SQLITE_TRANSIENT);
  }
  void bind(int index, int64_t value) { sqlite3_bind_int64(stmt_, index, value); }
  void bind(int index, int value) { sqlite3_bind_int(stmt_, index, value); }

  // Steps once. Returns true if a row is available (SQLITE_ROW).
  bool step() {
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;
    throw std::runtime_error(std::format("failed to execute statement: {}", sqlite3_errmsg(db_)));
  }

  std::string column_text(int index) const {
    const auto* text = sqlite3_column_text(stmt_, index);
    return text != nullptr ? reinterpret_cast<const char*>(text) : std::string();
  }
  int64_t column_int64(int index) const { return sqlite3_column_int64(stmt_, index); }
  int column_int(int index) const { return sqlite3_column_int(stmt_, index); }

  sqlite3_stmt* raw() { return stmt_; }

 private:
  sqlite3* db_;
  sqlite3_stmt* stmt_ = nullptr;
};

Route row_to_route(Stmt& stmt) {
  Route route;
  route.id = stmt.column_int64(0);
  route.path_prefix = stmt.column_text(1);
  route.container_name = stmt.column_text(2);
  route.container_port = stmt.column_int(3);
  route.strip_prefix = stmt.column_int(4) != 0;
  route.enabled = stmt.column_int(5) != 0;
  route.last_seen_ip = stmt.column_text(6);
  route.created_at = stmt.column_text(7);
  route.updated_at = stmt.column_text(8);
  return route;
}

constexpr const char* kSelectColumns =
    "id, path_prefix, container_name, container_port, strip_prefix, enabled, last_seen_ip, "
    "created_at, updated_at";

}  // namespace

Database::Database(const std::string& path) {
  const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
  if (sqlite3_open_v2(path.c_str(), &db_, flags, nullptr) != SQLITE_OK) {
    const std::string message = db_ != nullptr ? sqlite3_errmsg(db_) : "unknown error";
    sqlite3_close(db_);
    throw std::runtime_error(std::format("failed to open database at '{}': {}", path, message));
  }

  // WAL keeps the background sync worker's reads from blocking on API
  // writes (and vice versa); busy_timeout avoids SQLITE_BUSY under the
  // light contention this single-writer workload can see.
  sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
  sqlite3_exec(db_, "PRAGMA busy_timeout=5000;", nullptr, nullptr, nullptr);
  sqlite3_exec(db_, "PRAGMA foreign_keys=ON;", nullptr, nullptr, nullptr);

  init_schema();
  KP_LOG_INFO(kComponent, "opened database at '{}'", path);
}

Database::~Database() {
  if (db_ != nullptr) sqlite3_close(db_);
}

void Database::init_schema() {
  static constexpr const char* kSchema = R"sql(
    CREATE TABLE IF NOT EXISTS routes (
      id              INTEGER PRIMARY KEY AUTOINCREMENT,
      path_prefix     TEXT NOT NULL UNIQUE,
      container_name  TEXT NOT NULL,
      container_port  INTEGER NOT NULL,
      strip_prefix    INTEGER NOT NULL DEFAULT 1,
      enabled         INTEGER NOT NULL DEFAULT 1,
      last_seen_ip    TEXT NOT NULL DEFAULT '',
      created_at      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now')),
      updated_at      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
    );
  )sql";

  char* error = nullptr;
  if (sqlite3_exec(db_, kSchema, nullptr, nullptr, &error) != SQLITE_OK) {
    std::string message = error != nullptr ? error : "unknown error";
    sqlite3_free(error);
    throw std::runtime_error(std::format("failed to initialize schema: {}", message));
  }
}

Route Database::create_route(const NewRoute& new_route, const std::string& last_seen_ip) {
  std::scoped_lock lock(mutex_);

  Stmt insert(db_,
              "INSERT INTO routes (path_prefix, container_name, container_port, strip_prefix, "
              "last_seen_ip) VALUES (?, ?, ?, ?, ?);");
  insert.bind(1, new_route.path_prefix);
  insert.bind(2, new_route.container_name);
  insert.bind(3, new_route.container_port);
  insert.bind(4, new_route.strip_prefix ? 1 : 0);
  insert.bind(5, last_seen_ip);

  try {
    insert.step();
  } catch (const std::runtime_error&) {
    if (sqlite3_extended_errcode(db_) == SQLITE_CONSTRAINT_UNIQUE) {
      throw std::runtime_error(
          std::format("a route for path prefix '{}' already exists", new_route.path_prefix));
    }
    throw;
  }

  const int64_t id = sqlite3_last_insert_rowid(db_);

  Stmt select(db_, std::format("SELECT {} FROM routes WHERE id = ?;", kSelectColumns).c_str());
  select.bind(1, id);
  if (!select.step()) {
    throw std::runtime_error("failed to read back newly created route");
  }
  return row_to_route(select);
}

std::vector<Route> Database::list_routes() const {
  std::scoped_lock lock(mutex_);
  Stmt select(db_, std::format("SELECT {} FROM routes ORDER BY id ASC;", kSelectColumns).c_str());

  std::vector<Route> routes;
  while (select.step()) {
    routes.push_back(row_to_route(select));
  }
  return routes;
}

std::vector<Route> Database::list_enabled_routes() const {
  std::scoped_lock lock(mutex_);
  Stmt select(
      db_, std::format("SELECT {} FROM routes WHERE enabled = 1 ORDER BY id ASC;", kSelectColumns)
               .c_str());

  std::vector<Route> routes;
  while (select.step()) {
    routes.push_back(row_to_route(select));
  }
  return routes;
}

std::optional<Route> Database::get_route(int64_t id) const {
  std::scoped_lock lock(mutex_);
  Stmt select(db_, std::format("SELECT {} FROM routes WHERE id = ?;", kSelectColumns).c_str());
  select.bind(1, id);
  if (!select.step()) return std::nullopt;
  return row_to_route(select);
}

std::optional<Route> Database::find_by_path_prefix(const std::string& path_prefix) const {
  std::scoped_lock lock(mutex_);
  Stmt select(
      db_, std::format("SELECT {} FROM routes WHERE path_prefix = ?;", kSelectColumns).c_str());
  select.bind(1, path_prefix);
  if (!select.step()) return std::nullopt;
  return row_to_route(select);
}

std::optional<Route> Database::update_route(int64_t id, const RouteUpdate& patch) {
  std::scoped_lock lock(mutex_);

  // Read-modify-write under a single lock acquisition: reuses raw Stmt
  // objects rather than the public get_route()/etc. accessors, since those
  // take this same (non-recursive) mutex themselves and would deadlock.
  Stmt select(db_, std::format("SELECT {} FROM routes WHERE id = ?;", kSelectColumns).c_str());
  select.bind(1, id);
  if (!select.step()) return std::nullopt;
  const Route current = row_to_route(select);

  Stmt update(db_,
              "UPDATE routes SET container_name = ?, container_port = ?, strip_prefix = ?, "
              "enabled = ?, last_seen_ip = ?, updated_at = strftime('%Y-%m-%dT%H:%M:%SZ', 'now') "
              "WHERE id = ?;");
  update.bind(1, patch.container_name.value_or(current.container_name));
  update.bind(2, patch.container_port.value_or(current.container_port));
  update.bind(3, patch.strip_prefix.value_or(current.strip_prefix) ? 1 : 0);
  update.bind(4, patch.enabled.value_or(current.enabled) ? 1 : 0);
  update.bind(5, patch.last_seen_ip.value_or(current.last_seen_ip));
  update.bind(6, id);
  update.step();

  Stmt reselect(db_, std::format("SELECT {} FROM routes WHERE id = ?;", kSelectColumns).c_str());
  reselect.bind(1, id);
  reselect.step();
  return row_to_route(reselect);
}

bool Database::set_enabled(int64_t id, bool enabled) {
  std::scoped_lock lock(mutex_);
  Stmt update(db_,
              "UPDATE routes SET enabled = ?, updated_at = strftime('%Y-%m-%dT%H:%M:%SZ', 'now') "
              "WHERE id = ?;");
  update.bind(1, enabled ? 1 : 0);
  update.bind(2, id);
  update.step();
  return sqlite3_changes(db_) > 0;
}

bool Database::delete_route(int64_t id) {
  std::scoped_lock lock(mutex_);
  Stmt del(db_, "DELETE FROM routes WHERE id = ?;");
  del.bind(1, id);
  del.step();
  return sqlite3_changes(db_) > 0;
}

}  // namespace kp
