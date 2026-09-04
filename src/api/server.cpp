#include "api/server.hpp"

#include <nlohmann/json.hpp>

#include <format>
#include <regex>
#include <stdexcept>

#include "util/logger.hpp"

namespace kp {
namespace {

constexpr std::string_view kComponent = "api";
using json = nlohmann::json;

// Route path prefixes and container names both end up interpolated directly
// into generated nginx config (see nginx/config_generator.cpp), so they're
// validated strictly at this boundary rather than escaped later.
bool is_valid_path_prefix(const std::string& value) {
  static const std::regex pattern(R"(^/[A-Za-z0-9._-]+(/[A-Za-z0-9._-]+)*$)");
  return std::regex_match(value, pattern);
}

bool is_valid_container_name(const std::string& value) {
  static const std::regex pattern(R"(^[A-Za-z0-9][A-Za-z0-9_.-]{0,254}$)");
  return std::regex_match(value, pattern);
}

bool is_valid_port(int port) { return port > 0 && port <= 65535; }

// "/_proxy" is wired up in nginx.baseline.conf as a static route straight
// to kingdom-proxy-api itself, so the admin API is reachable through the
// same front door as everything else. Routes can't be registered under it,
// so a dynamic route can never shadow the admin API.
constexpr std::string_view kReservedPathPrefix = "/_proxy";

bool is_reserved_path_prefix(const std::string& value) {
  return value == kReservedPathPrefix ||
         value.starts_with(std::string(kReservedPathPrefix) + "/");
}

json route_to_json(const Route& route) {
  return json{
      {"id", route.id},
      {"path_prefix", route.path_prefix},
      {"container_name", route.container_name},
      {"container_port", route.container_port},
      {"strip_prefix", route.strip_prefix},
      {"enabled", route.enabled},
      {"last_seen_ip", route.last_seen_ip},
      {"created_at", route.created_at},
      {"updated_at", route.updated_at},
  };
}

void send_error(httplib::Response& res, int status, const std::string& message) {
  res.status = status;
  res.set_content(json{{"error", message}}.dump(), "application/json");
}

}  // namespace

ApiServer::ApiServer(Database& database, const DockerClient& docker_client, SyncWorker& sync_worker)
    : database_(database), docker_client_(docker_client), sync_worker_(sync_worker) {
  setup_routes();
}

void ApiServer::setup_routes() {
  server_.set_default_headers({{"Server", "kingdom-proxy-api"}});

  server_.Get("/healthz", [](const httplib::Request&, httplib::Response& res) {
    res.set_content(json{{"status", "ok"}}.dump(), "application/json");
  });

  server_.Get("/status", [this](const httplib::Request&, httplib::Response& res) {
    const auto status = sync_worker_.status();
    res.set_content(json{{"last_sync_ok", status.last_sync_ok},
                          {"last_error", status.last_error},
                          {"last_sync_at", status.last_sync_at},
                          {"sync_count", status.sync_count},
                          {"route_count", database_.list_routes().size()}}
                         .dump(),
                     "application/json");
  });

  server_.Get("/routes", [this](const httplib::Request&, httplib::Response& res) {
    json routes = json::array();
    for (const auto& route : database_.list_routes()) routes.push_back(route_to_json(route));
    res.set_content(routes.dump(), "application/json");
  });

  server_.Get(R"(/routes/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
    const int64_t id = std::stoll(req.matches[1]);
    const auto route = database_.get_route(id);
    if (!route) {
      send_error(res, 404, std::format("no route with id {}", id));
      return;
    }
    res.set_content(route_to_json(*route).dump(), "application/json");
  });

  server_.Post("/routes", [this](const httplib::Request& req, httplib::Response& res) {
    json body;
    try {
      body = json::parse(req.body);
    } catch (const json::parse_error&) {
      send_error(res, 400, "request body must be valid JSON");
      return;
    }

    NewRoute new_route;
    new_route.path_prefix = body.value("path_prefix", "");
    new_route.container_name = body.value("container_name", "");
    new_route.container_port = body.value("container_port", 0);
    new_route.strip_prefix = body.value("strip_prefix", true);

    if (!is_valid_path_prefix(new_route.path_prefix)) {
      send_error(res, 422,
                 "path_prefix must start with '/' and contain only letters, digits, '.', '_', "
                 "'-' and '/' (no trailing slash, no repeated slashes)");
      return;
    }
    if (is_reserved_path_prefix(new_route.path_prefix)) {
      send_error(res, 422,
                 std::format("path_prefix '{}' is reserved for the admin API itself",
                             kReservedPathPrefix));
      return;
    }
    if (!is_valid_container_name(new_route.container_name)) {
      send_error(res, 422, "container_name is not a valid docker container name");
      return;
    }
    if (!is_valid_port(new_route.container_port)) {
      send_error(res, 422, "container_port must be between 1 and 65535");
      return;
    }

    std::optional<ContainerInfo> container;
    try {
      container = docker_client_.inspect_container(new_route.container_name);
    } catch (const std::exception& e) {
      send_error(res, 502, std::format("failed to reach docker: {}", e.what()));
      return;
    }
    if (!container) {
      send_error(res, 422,
                 std::format("no container named '{}' was found", new_route.container_name));
      return;
    }

    try {
      const Route created = database_.create_route(new_route, container->primary_ip);
      sync_worker_.trigger();

      json response = route_to_json(created);
      if (!container->running) {
        response["warning"] =
            "container is not currently running; the route will start working once it is";
      }
      res.status = 201;
      res.set_content(response.dump(), "application/json");
      KP_LOG_INFO(kComponent, "created route {} -> {}:{}", created.path_prefix,
                  created.container_name, created.container_port);
    } catch (const std::exception& e) {
      send_error(res, 409, e.what());
    }
  });

  server_.Patch(R"(/routes/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
    const int64_t id = std::stoll(req.matches[1]);
    if (!database_.get_route(id)) {
      send_error(res, 404, std::format("no route with id {}", id));
      return;
    }

    json body;
    try {
      body = json::parse(req.body);
    } catch (const json::parse_error&) {
      send_error(res, 400, "request body must be valid JSON");
      return;
    }
    if (!body.contains("enabled") || !body["enabled"].is_boolean()) {
      send_error(res, 422, "request body must contain a boolean 'enabled' field");
      return;
    }

    database_.set_enabled(id, body["enabled"].get<bool>());
    sync_worker_.trigger();
    res.set_content(route_to_json(*database_.get_route(id)).dump(), "application/json");
  });

  server_.Delete(R"(/routes/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
    const int64_t id = std::stoll(req.matches[1]);
    if (!database_.delete_route(id)) {
      send_error(res, 404, std::format("no route with id {}", id));
      return;
    }
    sync_worker_.trigger();
    res.status = 204;
    KP_LOG_INFO(kComponent, "deleted route {}", id);
  });

  server_.set_exception_handler([](const httplib::Request&, httplib::Response& res,
                                    const std::exception_ptr& ep) {
    std::string message = "internal error";
    try {
      if (ep) std::rethrow_exception(ep);
    } catch (const std::exception& e) {
      message = e.what();
    }
    send_error(res, 500, message);
  });
}

void ApiServer::listen(const std::string& host, int port) {
  KP_LOG_INFO(kComponent, "listening on {}:{}", host, port);
  if (!server_.listen(host, port)) {
    throw std::runtime_error(std::format("failed to bind {}:{}", host, port));
  }
}

void ApiServer::stop() { server_.stop(); }

}  // namespace kp
