#include "docker/docker_client.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <format>

#include "util/logger.hpp"

namespace kp {
namespace {

constexpr std::string_view kComponent = "docker_client";
using json = nlohmann::json;

httplib::Client make_client(const std::string& socket_path) {
  // cpp-httplib treats the "host" as the unix socket path once the address
  // family is forced to AF_UNIX; the actual HTTP request paths below (e.g.
  // "/containers/json") are what the Docker Engine API expects.
  httplib::Client client(socket_path);
  client.set_address_family(AF_UNIX);
  client.set_connection_timeout(5, 0);
  client.set_read_timeout(30, 0);
  // Over a unix socket, cpp-httplib derives a Host header from the socket
  // path itself (e.g. "Host: /var/run/docker.sock"), which the Docker
  // daemon's Go HTTP server rejects with "400 Bad Request: malformed Host
  // header". The actual value doesn't matter here since there's no virtual
  // hosting involved -- just needs to be syntactically valid.
  client.set_default_headers({{"Host", "localhost"}});
  return client;
}

// Docker multiplexes exec output as a series of frames when the exec was
// started without a TTY: [1 byte stream type][3 bytes padding][4 bytes
// big-endian length][payload]. This strips the framing and concatenates
// stdout/stderr payloads into plain text.
std::string demux_stream(const std::string& raw) {
  std::string text;
  size_t offset = 0;
  while (offset + 8 <= raw.size()) {
    const uint32_t length = (static_cast<uint8_t>(raw[offset + 4]) << 24) |
                             (static_cast<uint8_t>(raw[offset + 5]) << 16) |
                             (static_cast<uint8_t>(raw[offset + 6]) << 8) |
                             (static_cast<uint8_t>(raw[offset + 7]));
    offset += 8;
    if (offset + length > raw.size()) break;
    text.append(raw, offset, length);
    offset += length;
  }
  return text;
}

}  // namespace

DockerClient::DockerClient(std::string socket_path) : socket_path_(std::move(socket_path)) {}

std::optional<ContainerInfo> DockerClient::inspect_container(const std::string& name_or_id) const {
  auto client = make_client(socket_path_);
  const std::string path = std::format("/containers/{}/json", name_or_id);
  auto res = client.Get(path);

  if (!res) {
    KP_LOG_ERROR(kComponent, "failed to reach docker socket at '{}': {}", socket_path_,
                 httplib::to_string(res.error()));
    throw std::runtime_error(std::format("cannot reach docker socket at '{}'", socket_path_));
  }
  if (res->status == 404) return std::nullopt;
  if (res->status != 200) {
    throw std::runtime_error(
        std::format("docker inspect of '{}' failed: HTTP {}", name_or_id, res->status));
  }

  const json body = json::parse(res->body);
  ContainerInfo info;
  info.id = body.value("Id", "");
  info.name = body.value("Name", "");
  if (!info.name.empty() && info.name.front() == '/') info.name.erase(0, 1);
  info.running = body.value("/State/Running"_json_pointer, false);

  if (body.contains("/NetworkSettings/Networks"_json_pointer)) {
    for (const auto& [network_name, network] : body["NetworkSettings"]["Networks"].items()) {
      info.networks.push_back(network_name);
      const std::string ip = network.value("IPAddress", "");
      if (info.primary_ip.empty() && !ip.empty()) info.primary_ip = ip;
    }
  }

  return info;
}

DockerClient::ExecResult DockerClient::exec(const std::string& container_name,
                                             const std::vector<std::string>& cmd) const {
  auto client = make_client(socket_path_);

  json create_body = {
      {"Cmd", cmd},
      {"AttachStdout", true},
      {"AttachStderr", true},
  };
  auto create_res =
      client.Post(std::format("/containers/{}/exec", container_name), create_body.dump(),
                  "application/json");
  if (!create_res || create_res->status != 201) {
    const int status = create_res ? create_res->status : -1;
    const std::string body = create_res ? create_res->body : "";
    throw std::runtime_error(std::format("failed to create exec in container '{}': HTTP {}: {}",
                                          container_name, status, body));
  }
  const std::string exec_id = json::parse(create_res->body).value("Id", "");
  if (exec_id.empty()) {
    throw std::runtime_error("docker exec create returned no Id");
  }

  // Detach=false makes this call block until the command finishes and
  // stream back its multiplexed stdout/stderr in the response body.
  json start_body = {{"Detach", false}, {"Tty", false}};
  auto start_res =
      client.Post(std::format("/exec/{}/start", exec_id), start_body.dump(), "application/json");
  if (!start_res || start_res->status != 200) {
    const int status = start_res ? start_res->status : -1;
    throw std::runtime_error(std::format("failed to start exec '{}': HTTP {}", exec_id, status));
  }
  const std::string output = demux_stream(start_res->body);

  auto inspect_res = client.Get(std::format("/exec/{}/json", exec_id));
  int exit_code = -1;
  if (inspect_res && inspect_res->status == 200) {
    exit_code = json::parse(inspect_res->body).value("ExitCode", -1);
  }

  return ExecResult{.ok = exit_code == 0, .exit_code = exit_code, .output = output};
}

bool DockerClient::reload_nginx(const std::string& container_name, std::string* detail) const {
  const auto test = exec(container_name, {"nginx", "-t"});
  if (!test.ok) {
    KP_LOG_ERROR(kComponent, "nginx -t failed in '{}' (exit {}): {}", container_name,
                 test.exit_code, test.output);
    if (detail != nullptr) *detail = test.output;
    return false;
  }

  const auto reload = exec(container_name, {"nginx", "-s", "reload"});
  if (!reload.ok) {
    KP_LOG_ERROR(kComponent, "nginx -s reload failed in '{}' (exit {}): {}", container_name,
                 reload.exit_code, reload.output);
    if (detail != nullptr) *detail = reload.output;
    return false;
  }

  KP_LOG_INFO(kComponent, "reloaded nginx in container '{}'", container_name);
  return true;
}

}  // namespace kp
