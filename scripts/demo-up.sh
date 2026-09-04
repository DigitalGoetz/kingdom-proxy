#!/usr/bin/env bash
# Builds and starts kingdom-proxy plus the example to-do app (see
# examples/todo-app), then registers a route for it at /demo. Run from
# the repo root. Safe to re-run: an already-existing route is left alone.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

wait_healthy() {
  local container="$1"
  for _ in $(seq 1 30); do
    if [[ "$(docker inspect -f '{{.State.Health.Status}}' "$container" 2>/dev/null)" == "healthy" ]]; then
      return 0
    fi
    sleep 2
  done
  echo "warning: $container did not report healthy in time -- continuing anyway" >&2
}

docker compose up -d --build
echo "waiting for nginx and the api to become healthy..."
wait_healthy kingdom-proxy-nginx
wait_healthy kingdom-proxy-api

docker compose --profile demo up -d --build demo
echo "waiting for the demo app to become healthy..."
wait_healthy kingdom-demo-todo-app

echo "registering a route at /demo..."
response=$(curl -s -o /dev/null -w '%{http_code}' -X POST http://localhost:4800/_proxy/routes \
  -H 'content-type: application/json' \
  -d '{"path_prefix":"/demo","container_name":"kingdom-demo-todo-app","container_port":8080}')
case "$response" in
  201) echo "route created." ;;
  409) echo "route already exists -- leaving it as-is." ;;
  *) echo "warning: unexpected response ($response) registering the route -- check 'docker compose logs api'" >&2 ;;
esac

cat <<'EOF'

The demo app is up: http://localhost:4800/demo/

Try it in a browser, or:
  curl http://localhost:4800/demo/api/todos

See its route (and everything else) in the dashboard: http://localhost:4800/
Tear it down again with: docker compose --profile demo down
EOF
