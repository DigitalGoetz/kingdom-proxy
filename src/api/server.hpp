#pragma once

#include <httplib.h>

#include <string>

#include "db/database.hpp"
#include "docker/docker_client.hpp"
#include "worker/sync_worker.hpp"

namespace kp {

// The admin HTTP API: create/list/toggle/delete routes. This is the
// "programmable" surface of the proxy -- everything it does is a thin layer
// over Database + SyncWorker.
//
// This API is not authenticated. It's meant to be reachable only from a
// trusted network (e.g. bound to loopback, or an internal docker network) --
// see docker-compose.yml and the README.
class ApiServer {
 public:
  ApiServer(Database& database, const DockerClient& docker_client, SyncWorker& sync_worker);

  // Blocks until stop() is called from another thread.
  void listen(const std::string& host, int port);
  void stop();

 private:
  void setup_routes();

  httplib::Server server_;
  Database& database_;
  const DockerClient& docker_client_;
  SyncWorker& sync_worker_;
};

}  // namespace kp
