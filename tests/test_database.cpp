#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <format>
#include <random>

#include "db/database.hpp"

using kp::Database;
using kp::NewRoute;

namespace {

// Each test gets its own throwaway sqlite file so tests can run in parallel
// and don't leave state behind between runs.
std::string temp_db_path() {
  static std::mt19937_64 rng{std::random_device{}()};
  const auto dir = std::filesystem::temp_directory_path();
  return (dir / std::format("kingdom-proxy-test-{}.db", rng())).string();
}

NewRoute make_new_route(std::string path_prefix, std::string container_name = "app",
                         int port = 8080) {
  NewRoute route;
  route.path_prefix = std::move(path_prefix);
  route.container_name = std::move(container_name);
  route.container_port = port;
  route.strip_prefix = true;
  return route;
}

}  // namespace

TEST_CASE("create_route persists and round-trips all fields", "[database]") {
  Database db(temp_db_path());

  const auto created = db.create_route(make_new_route("/widgets", "widgets-app", 8080), "172.18.0.5");

  CHECK(created.id > 0);
  CHECK(created.path_prefix == "/widgets");
  CHECK(created.container_name == "widgets-app");
  CHECK(created.container_port == 8080);
  CHECK(created.strip_prefix);
  CHECK(created.enabled);
  CHECK(created.last_seen_ip == "172.18.0.5");
  CHECK_FALSE(created.created_at.empty());
}

TEST_CASE("duplicate path_prefix is rejected", "[database]") {
  Database db(temp_db_path());
  db.create_route(make_new_route("/dup"), "");

  CHECK_THROWS_AS(db.create_route(make_new_route("/dup"), ""), std::runtime_error);
}

TEST_CASE("list_enabled_routes excludes disabled routes", "[database]") {
  Database db(temp_db_path());
  const auto a = db.create_route(make_new_route("/a"), "");
  db.create_route(make_new_route("/b"), "");
  db.set_enabled(a.id, false);

  const auto enabled = db.list_enabled_routes();
  REQUIRE(enabled.size() == 1);
  CHECK(enabled.front().path_prefix == "/b");

  const auto all = db.list_routes();
  CHECK(all.size() == 2);
}

TEST_CASE("find_by_path_prefix and get_route", "[database]") {
  Database db(temp_db_path());
  const auto created = db.create_route(make_new_route("/lookup"), "");

  const auto by_id = db.get_route(created.id);
  REQUIRE(by_id.has_value());
  CHECK(by_id->path_prefix == "/lookup");

  const auto by_prefix = db.find_by_path_prefix("/lookup");
  REQUIRE(by_prefix.has_value());
  CHECK(by_prefix->id == created.id);

  CHECK_FALSE(db.get_route(999999).has_value());
  CHECK_FALSE(db.find_by_path_prefix("/nope").has_value());
}

TEST_CASE("delete_route removes the row and reports whether it existed", "[database]") {
  Database db(temp_db_path());
  const auto created = db.create_route(make_new_route("/gone"), "");

  CHECK(db.delete_route(created.id));
  CHECK_FALSE(db.get_route(created.id).has_value());
  CHECK_FALSE(db.delete_route(created.id));
}
