#pragma once

#include <optional>
#include <string>
#include <vector>

namespace kp {

struct ContainerInfo {
  std::string id;
  std::string name;
  bool running = false;
  std::string primary_ip;            // Best-effort: first attached network's IP.
  std::vector<std::string> networks;  // Names of docker networks the container is on.
};

// A lighter-weight summary for listing containers, e.g. to help someone
// pick a target for a new route -- see ApiServer's GET /containers.
struct ContainerSummary {
  std::string id;
  std::string name;
  std::string image;
  std::string status;         // Docker's own human-readable status, e.g. "Up 2 hours".
  std::vector<int> ports;     // Distinct ports the container listens on (published or not).
  std::vector<std::string> networks;
};

// Talks to the Docker Engine API over the local unix socket. Used for two
// things: (1) validating a route's target container exists, is running, and
// is reachable before we persist the route, and (2) triggering an in-place
// `nginx -s reload` inside the nginx container via `docker exec`, since the
// nginx process runs in a separate container from this one.
class DockerClient {
 public:
  explicit DockerClient(std::string socket_path = "/var/run/docker.sock");

  // Returns std::nullopt if no such container exists.
  std::optional<ContainerInfo> inspect_container(const std::string& name_or_id) const;

  // Lists currently-running containers on the host -- not just ones on
  // this stack's network -- so the admin API can help someone see what's
  // available to route to (see ApiServer's GET /containers). Read-only.
  std::vector<ContainerSummary> list_containers() const;

  // Runs `nginx -t` inside the target container to validate the freshly
  // generated config, and only reloads if that check passes. Returns true
  // if the config validated and the reload succeeded; on failure, `detail`
  // (if non-null) is filled with whatever nginx printed to help debugging.
  bool reload_nginx(const std::string& container_name, std::string* detail = nullptr) const;

  // Attaches a container to a docker network (`docker network connect
  // <network_name> <container_name_or_id>`) -- what a route needs before
  // it can resolve that container by name at all. Idempotent: attaching a
  // container that's already on the network is treated as success, not an
  // error. Returns false with `detail` filled in (if non-null) if the
  // network or container doesn't exist, or the daemon otherwise refuses.
  bool connect_network(const std::string& network_name, const std::string& container_name_or_id,
                        std::string* detail = nullptr) const;

 private:
  struct ExecResult {
    bool ok = false;
    int exit_code = -1;
    std::string output;
  };

  // Runs `cmd` inside `container_name` via the Docker exec API and waits
  // for it to finish, capturing combined stdout/stderr.
  ExecResult exec(const std::string& container_name, const std::vector<std::string>& cmd) const;

  std::string socket_path_;
};

}  // namespace kp
