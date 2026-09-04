# kingdom-proxy

A programmable reverse proxy for routing to locally running Docker
containers by path, built as two cooperating pieces:

- **nginx** — does the actual proxying. Runs with a small static baseline
  config plus one generated file that holds all dynamic routes.
- **kingdom-proxy-api** (Modern C++20) — an orchestration/admin service
  sitting behind nginx. It exposes an HTTP API for registering routes,
  persists them in SQLite, and keeps nginx's generated config in sync with
  the database in the background.

```
                     ┌─────────────────────────┐
   client ────────▶  │  nginx  (:4800 → :80)    │ ──▶ any container on
                     │  baseline + generated    │     kingdom-net, by
                     │  config, path routing    │     path prefix
                     └────────────┬─────────────┘
                                  │ /_proxy/*  → stripped, proxied to the
                                  │              api container (baseline
                                  │              route -- see below)
                                  │ everything else → dynamic routes
                                  │
                                  │ docker exec (nginx -t / -s reload)
                                  │ shared volume (conf.d)
                     ┌────────────▼─────────────┐
   admin ─────────▶  │  kingdom-proxy-api (:4810)│  (also reachable directly,
   (curl/scripts)    │  HTTP API → SQLite        │   loopback-only, for local
                     │  background sync worker   │   debugging)
                     └────────────┬─────────────┘
                                  │ Docker Engine API (unix socket)
                                  ▼
                       validates target containers,
                       triggers nginx reload
```

The admin API is reachable two ways: through the proxy at `/_proxy/*` on
nginx's port (e.g. `http://localhost:4800/_proxy/routes`), which is how
everything else reaches it too, and directly on its own loopback-only port
(`http://localhost:4810/routes`) for local debugging. `/_proxy` is a
reserved path prefix — the API refuses to let a dynamic route register
under it, so a route can never shadow the admin API itself.

## How a route gets applied

1. `POST /routes` on the admin API with a path prefix and a target
   container name + port.
2. The API validates the input, confirms the container exists via the
   Docker Engine API (over `/var/run/docker.sock`), and inserts a row into
   a local SQLite database (`kingdom-proxy.db`).
3. The API wakes its background **sync worker**, which reads every enabled
   route from SQLite, renders an nginx config fragment
   (`conf.d/kingdom-routes.conf`) from it, and writes it atomically to a
   volume shared with the nginx container.
4. The sync worker runs `nginx -t` and then `nginx -s reload` *inside the
   nginx container* via `docker exec` (over the same Docker socket) so the
   new route takes effect with no dropped connections.
5. The sync worker also runs on a timer (default every 30s) as a safety
   net, and once at startup, so it self-heals from a missed trigger.

Routes are generated to reference their target by **container name**, not
a cached IP. nginx is configured to use Docker's embedded DNS resolver
(`127.0.0.11`) and to resolve the upstream through an nginx variable, which
forces re-resolution on every request — so a route keeps working across a
`docker restart` of its target container without needing a new sync.

## Quick start

```sh
./scripts/dev-up.sh
```

or manually:

```sh
docker compose up -d --build
```

This starts two containers on a shared `kingdom-net` docker network:
nginx on `localhost:4800` (the actual proxy) and the admin API on
`127.0.0.1:4810` (loopback-only by default — it's unauthenticated).

Any container you want to route to must be attached to `kingdom-net` too:

```sh
docker network connect kingdom-net <your-container>
```

Then register a route, through the proxy itself:

```sh
curl -X POST http://localhost:4800/_proxy/routes \
  -H 'content-type: application/json' \
  -d '{
        "path_prefix": "/myapp",
        "container_name": "<your-container>",
        "container_port": 8080,
        "strip_prefix": true
      }'
```

(or the same call against `http://localhost:4810/routes` directly, bypassing
nginx — see [Admin API](#admin-api).)

`strip_prefix: true` (the default) means a request to
`http://localhost:4800/myapp/foo` reaches the container as `/foo`; set it
to `false` to forward `/myapp/foo` unchanged.

Traffic now flows: `curl http://localhost:4800/myapp/`

## Admin API

Reachable two ways — through the proxy at `<nginx-host>/_proxy/<path>`
(e.g. `http://localhost:4800/_proxy/routes`), or directly on its own
loopback-only port (e.g. `http://localhost:4810/routes`). Paths below are
the API's own, with the `/_proxy` prefix already stripped.

| Method | Path           | Description                                   |
|--------|----------------|------------------------------------------------|
| GET    | `/healthz`     | Liveness check.                                |
| GET    | `/status`      | Last sync result, timestamp, route count.      |
| GET    | `/routes`      | List all routes.                               |
| POST   | `/routes`      | Create a route (see body above).               |
| GET    | `/routes/{id}` | Fetch one route.                               |
| PATCH  | `/routes/{id}` | `{"enabled": false}` — disable without deleting.|
| DELETE | `/routes/{id}` | Remove a route.                                |

`path_prefix` values of `/_proxy` or anything under it are rejected
(`422`) when creating a route — that prefix is reserved for the admin API
itself so a route can never shadow it.

Every write triggers an immediate background sync (`nginx -t` + reload);
`GET /status` reports whether the last sync succeeded and, if not, what
nginx said.

## Project layout

```
src/
  api/       admin HTTP API (routes/handlers)
  db/        SQLite-backed route store
  docker/    Docker Engine API client (unix socket) — container inspection
             + `docker exec` for nginx reload
  model/     Route data model
  nginx/     nginx config rendering + reload orchestration
  worker/    background sync worker (DB -> nginx config -> reload)
  main.cpp   wiring + config from environment variables
nginx/       static baseline nginx.conf
tests/       Catch2 unit tests (config rendering, database)
```

## Configuration (environment variables, all optional)

| Variable                   | Default                                   |
|-----------------------------|--------------------------------------------|
| `KP_HTTP_HOST`               | `0.0.0.0`                                  |
| `KP_HTTP_PORT`               | `9000`                                     |
| `KP_DB_PATH`                 | `/data/kingdom-proxy.db`                   |
| `KP_DOCKER_SOCKET`           | `/var/run/docker.sock`                     |
| `KP_NGINX_CONF_PATH`         | `/etc/nginx/conf.d/kingdom-routes.conf`    |
| `KP_NGINX_CONTAINER_NAME`    | `kingdom-proxy-nginx`                      |
| `KP_SYNC_INTERVAL_SECONDS`   | `30`                                       |

## Building locally (without Docker)

Requires a C++20 compiler, CMake ≥ 3.20, and git/network access for
`FetchContent` (cpp-httplib, nlohmann/json, the SQLite amalgamation, and
Catch2 for tests — all fetched and pinned to exact versions at configure
time, no system packages needed).

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Run outside Docker for local iteration (needs a real nginx container to
talk to for reload testing, otherwise `NginxController::apply` will just
fail to `docker exec` and the sync worker will log that and retry):

```sh
KP_DB_PATH=/tmp/kingdom-proxy.db \
KP_NGINX_CONF_PATH=/tmp/kingdom-routes.conf \
  ./build/kingdom-proxy-api
```

## Security notes

- The admin API has no authentication — it is bound to `127.0.0.1` by
  `docker-compose.yml` on purpose. Don't expose it without adding auth in
  front of it (e.g. a sidecar, an authenticating reverse proxy, or a VPN).
- The api container is mounted the host's Docker socket read-only and runs
  as root, which is effectively equivalent to root on the host — the same
  trust tradeoff as any tool that talks to `docker.sock` (Traefik's docker
  provider, watchtower, etc.). Don't run this on a host you don't trust
  route-creators with.
- `path_prefix` and `container_name` are validated against a strict
  allow-list of characters before being interpolated into generated nginx
  config, since there's no templating engine doing escaping for us.

## What's intentionally out of scope (v1)

- TLS termination (put a TLS-terminating LB in front of nginx if you need
  it publicly reachable).
- Auto-discovery of containers via Docker labels (routes are registered
  explicitly through the API; label-based auto-registration would be a
  natural follow-up).
- Multiple nginx replicas / HA (single nginx instance, single SQLite file).
