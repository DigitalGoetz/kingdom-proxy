#include <csignal>
#include <cstdlib>
#include <memory>

#include "api/server.hpp"
#include "db/database.hpp"
#include "docker/docker_client.hpp"
#include "nginx/config_generator.hpp"
#include "nginx/nginx_controller.hpp"
#include "util/env.hpp"
#include "util/logger.hpp"
#include "worker/sync_worker.hpp"

namespace {

constexpr std::string_view kComponent = "main";

kp::ApiServer* g_api_server = nullptr;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

void handle_shutdown_signal(int signal) {
  KP_LOG_INFO(kComponent, "received signal {}, shutting down", signal);
  if (g_api_server != nullptr) g_api_server->stop();
}

}  // namespace

int main() {
  using kp::env::get;
  using kp::env::get_int;

  const std::string http_host = get("KP_HTTP_HOST", "0.0.0.0");
  const int http_port = get_int("KP_HTTP_PORT", 9000);
  const std::string db_path = get("KP_DB_PATH", "/data/kingdom-proxy.db");
  const std::string docker_socket = get("KP_DOCKER_SOCKET", "/var/run/docker.sock");
  const std::string nginx_conf_path =
      get("KP_NGINX_CONF_PATH", "/etc/nginx/conf.d/kingdom-routes.conf");
  const std::string nginx_container_name = get("KP_NGINX_CONTAINER_NAME", "kingdom-proxy-nginx");
  const int sync_interval_seconds = get_int("KP_SYNC_INTERVAL_SECONDS", 30);

  KP_LOG_INFO(kComponent, "starting kingdom-proxy-api");

  try {
    kp::Database database(db_path);
    kp::DockerClient docker_client(docker_socket);

    kp::ConfigGenerator config_generator(nginx_conf_path);
    kp::NginxController nginx_controller(std::move(config_generator), docker_client,
                                          nginx_container_name);

    kp::SyncWorker sync_worker(database, std::move(nginx_controller),
                                std::chrono::seconds(sync_interval_seconds));
    sync_worker.start();

    kp::ApiServer api_server(database, docker_client, sync_worker);
    g_api_server = &api_server;
    std::signal(SIGINT, handle_shutdown_signal);
    std::signal(SIGTERM, handle_shutdown_signal);

    api_server.listen(http_host, http_port);

    sync_worker.stop();
    KP_LOG_INFO(kComponent, "stopped cleanly");
    return EXIT_SUCCESS;
  } catch (const std::exception& e) {
    KP_LOG_ERROR(kComponent, "fatal error: {}", e.what());
    return EXIT_FAILURE;
  }
}
