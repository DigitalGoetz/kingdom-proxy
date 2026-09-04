#!/usr/bin/env bash
# Builds and starts kingdom-proxy locally with docker compose, then waits
# for both containers to report healthy. Run from the repo root.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

docker compose up -d --build

echo "waiting for nginx and the api to become healthy..."
for _ in $(seq 1 30); do
  nginx_status=$(docker inspect -f '{{.State.Health.Status}}' kingdom-proxy-nginx 2>/dev/null || echo "starting")
  api_status=$(docker inspect -f '{{.State.Health.Status}}' kingdom-proxy-api 2>/dev/null || echo "starting")
  if [[ "$nginx_status" == "healthy" && "$api_status" == "healthy" ]]; then
    echo "both containers healthy."
    break
  fi
  sleep 2
done

cat <<'EOF'

kingdom-proxy is up:
  proxy:     http://localhost:4800  (admin API also at /_proxy/* on this port)
  admin API: http://localhost:4810  (direct, loopback-only)

Try it out, entirely through the proxy:
  # Attach some container to the shared network so nginx and the api can
  # both reach it by name:
  docker network connect kingdom-net <your-container>

  # Register a route (path prefix -> container:port):
  curl -X POST http://localhost:4800/_proxy/routes \
    -H 'content-type: application/json' \
    -d '{"path_prefix":"/myapp","container_name":"<your-container>","container_port":8080}'

  # List routes:
  curl http://localhost:4800/_proxy/routes

  # Traffic now flows:
  curl http://localhost:4800/myapp/

  # Check sync status:
  curl http://localhost:4800/_proxy/status

(swap "http://localhost:4800/_proxy" for "http://localhost:4810" in any of
the above to hit the admin API directly instead, bypassing nginx.)
EOF
