#include <catch2/catch_test_macros.hpp>

#include "model/route.hpp"
#include "nginx/config_generator.hpp"

using kp::ConfigGenerator;
using kp::Route;

namespace {

Route make_route(int64_t id, std::string path_prefix, std::string container_name, int port,
                  bool strip_prefix = true, bool enabled = true) {
  Route route;
  route.id = id;
  route.path_prefix = std::move(path_prefix);
  route.container_name = std::move(container_name);
  route.container_port = port;
  route.strip_prefix = strip_prefix;
  route.enabled = enabled;
  return route;
}

}  // namespace

TEST_CASE("renders a location block with a resolvable upstream variable", "[config_generator]") {
  const auto rendered = ConfigGenerator::render({make_route(1, "/widgets", "widgets-app", 8080)});

  CHECK(rendered.find("location /widgets/ {") != std::string::npos);
  CHECK(rendered.find("set $kp_upstream_1 \"http://widgets-app:8080\";") != std::string::npos);
  CHECK(rendered.find("proxy_pass $kp_upstream_1/;") != std::string::npos);
}

TEST_CASE("strip_prefix=false forwards the original request URI", "[config_generator]") {
  const auto rendered =
      ConfigGenerator::render({make_route(2, "/api", "api-app", 3000, /*strip_prefix=*/false)});

  CHECK(rendered.find("proxy_pass $kp_upstream_2;\n") != std::string::npos);
  CHECK(rendered.find("proxy_pass $kp_upstream_2/;") == std::string::npos);
}

TEST_CASE("disabled routes are omitted", "[config_generator]") {
  const auto rendered = ConfigGenerator::render(
      {make_route(3, "/off", "off-app", 80, /*strip_prefix=*/true, /*enabled=*/false)});

  CHECK(rendered.find("location") == std::string::npos);
}

TEST_CASE("multiple routes each get distinct upstream variables", "[config_generator]") {
  const auto rendered = ConfigGenerator::render({
      make_route(1, "/a", "app-a", 80),
      make_route(2, "/b", "app-b", 81),
  });

  CHECK(rendered.find("$kp_upstream_1") != std::string::npos);
  CHECK(rendered.find("$kp_upstream_2") != std::string::npos);
}
