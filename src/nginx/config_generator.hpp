#pragma once

#include <string>
#include <vector>

#include "model/route.hpp"

namespace kp {

class ConfigGenerator {
 public:
  // `output_path` is the file included from the baseline nginx.conf's
  // server block (see nginx/nginx.baseline.conf). It must live on a volume
  // shared with the nginx container.
  explicit ConfigGenerator(std::string output_path);

  // Pure rendering step, exposed separately so it's trivially unit-testable
  // without touching the filesystem.
  static std::string render(const std::vector<Route>& routes);

  // Writes the rendered config to output_path. Writes to a temp file in the
  // same directory and renames it into place so nginx (running in another
  // container, watching the same file via a shared volume) never observes a
  // partially-written config.
  void write(const std::vector<Route>& routes) const;

 private:
  std::string output_path_;
};

}  // namespace kp
