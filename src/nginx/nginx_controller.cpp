#include "nginx/nginx_controller.hpp"

#include "util/logger.hpp"

namespace kp {
namespace {
constexpr std::string_view kComponent = "nginx_controller";
}  // namespace

NginxController::NginxController(ConfigGenerator config_generator, const DockerClient& docker_client,
                                   std::string nginx_container_name)
    : config_generator_(std::move(config_generator)),
      docker_client_(docker_client),
      nginx_container_name_(std::move(nginx_container_name)) {}

NginxController::ApplyResult NginxController::apply(const std::vector<Route>& routes) const {
  try {
    config_generator_.write(routes);
  } catch (const std::exception& e) {
    KP_LOG_ERROR(kComponent, "failed to write nginx config: {}", e.what());
    return {.ok = false, .detail = e.what()};
  }

  std::string detail;
  const bool reloaded = docker_client_.reload_nginx(nginx_container_name_, &detail);
  return {.ok = reloaded, .detail = std::move(detail)};
}

}  // namespace kp
