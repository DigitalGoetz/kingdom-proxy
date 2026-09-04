#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace kp {

// A single path-based route: requests under `path_prefix` on the shared
// nginx listener are forwarded to `container_name:container_port`.
//
// The target is resolved by *name*, not by a cached IP address: nginx is
// configured with Docker's embedded DNS resolver (127.0.0.11) and re-resolves
// the container name on each request (see nginx/config_generator.cpp), so a
// route keeps working across container restarts as long as the container
// keeps its name and stays attached to the shared docker network.
struct Route {
  int64_t id = 0;
  std::string path_prefix;      // e.g. "/widgets" -- must start with '/'.
  std::string container_name;   // Docker container name or ID.
  int container_port = 0;       // Port the container listens on.
  bool strip_prefix = true;     // Strip path_prefix before forwarding upstream.
  bool enabled = true;
  std::string last_seen_ip;     // Informational snapshot from route creation.
  std::string created_at;       // ISO-8601 UTC.
  std::string updated_at;       // ISO-8601 UTC.
};

// Fields accepted from clients when creating a route. Kept separate from
// Route so server-assigned fields (id, timestamps) can't be spoofed by a
// request body.
struct NewRoute {
  std::string path_prefix;
  std::string container_name;
  int container_port = 0;
  bool strip_prefix = true;
};

// A partial update to an existing route. path_prefix is deliberately not
// editable here -- it's the route's identity; changing a target's path is
// modeled as delete + create instead of an in-place rename. Any field left
// as std::nullopt keeps the route's current value.
struct RouteUpdate {
  std::optional<std::string> container_name;
  std::optional<int> container_port;
  std::optional<bool> strip_prefix;
  std::optional<bool> enabled;
  std::optional<std::string> last_seen_ip;  // Refreshed by the API when container_name changes.
};

}  // namespace kp
