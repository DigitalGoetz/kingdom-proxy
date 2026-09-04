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

TEST_CASE("strip_prefix=true rewrites the URI before proxy_pass", "[config_generator]") {
  // Regression test: nginx never strips a location prefix from proxy_pass
  // when the target is a variable (needed here for per-request DNS
  // resolution) -- it always forwards the original request URI verbatim
  // regardless of what follows the variable, including a trailing "/".
  // Prefix stripping has to happen via `rewrite ... break` instead, and
  // proxy_pass must NOT have a trailing "/" (that would silently do
  // nothing and mislead a reader into thinking it strips the prefix).
  const auto rendered = ConfigGenerator::render({make_route(1, "/widgets", "widgets-app", 8080)});

  CHECK(rendered.find("location /widgets/ {") != std::string::npos);
  const auto set_pos = rendered.find("set $kp_upstream_1 \"http://widgets-app:8080\";");
  const auto rewrite_pos = rendered.find("rewrite ^/widgets/(.*)$ /$1 break;");
  const auto proxy_pass_pos = rendered.find("proxy_pass $kp_upstream_1;\n");
  REQUIRE(set_pos != std::string::npos);
  REQUIRE(rewrite_pos != std::string::npos);
  REQUIRE(proxy_pass_pos != std::string::npos);
  // Order matters: `break` halts the rewrite module's own remaining
  // directives in this location, so `set` must come first or the variable
  // is left uninitialized and every request 500s.
  CHECK(set_pos < rewrite_pos);
  CHECK(rewrite_pos < proxy_pass_pos);
  CHECK(rendered.find("proxy_pass $kp_upstream_1/;") == std::string::npos);
}

TEST_CASE("strip_prefix=false forwards the original request URI with no rewrite",
          "[config_generator]") {
  const auto rendered =
      ConfigGenerator::render({make_route(2, "/api", "api-app", 3000, /*strip_prefix=*/false)});

  CHECK(rendered.find("rewrite") == std::string::npos);
  CHECK(rendered.find("proxy_pass $kp_upstream_2;\n") != std::string::npos);
}

TEST_CASE("a '.' in path_prefix is escaped in the rewrite regex", "[config_generator]") {
  // Otherwise an unescaped '.' (a regex metacharacter, but a literal
  // character in path_prefix's own validated charset) would match any
  // character in a live request, not just a literal '.'.
  const auto rendered = ConfigGenerator::render({make_route(1, "/v1.2", "app", 80)});

  CHECK(rendered.find("rewrite ^/v1\\.2/(.*)$ /$1 break;") != std::string::npos);
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
