#pragma once

#include <string>
#include <vector>

#include "docker/docker_client.hpp"
#include "model/route.hpp"
#include "nginx/config_generator.hpp"

namespace kp {

// Applies a set of routes to the live nginx instance: render + write the
// generated config to the shared volume, then validate and reload nginx
// in-place via `docker exec` (nginx runs in a separate container).
class NginxController {
 public:
  NginxController(ConfigGenerator config_generator, const DockerClient& docker_client,
                   std::string nginx_container_name);

  struct ApplyResult {
    bool ok = false;
    std::string detail;  // nginx's own output on failure.
  };

  ApplyResult apply(const std::vector<Route>& routes) const;

 private:
  ConfigGenerator config_generator_;
  const DockerClient& docker_client_;
  std::string nginx_container_name_;
};

}  // namespace kp
