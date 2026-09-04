# kingdom-proxy

A programmable reverse proxy for routing to locally running containers by
path — runs under Docker or Podman (see
[Running under Podman](#running-under-podman)) — built as three cooperating
pieces:

- **nginx** — does the actual proxying. Runs with a small static baseline
  config plus one generated file that holds all dynamic routes, and also
  serves the web dashboard's static files from its document root.
- **kingdom-proxy-api** (Modern C++20) — an orchestration/admin service
  sitting behind nginx. It exposes an HTTP API for registering routes,
  persists them in SQLite, and keeps nginx's generated config in sync with
  the database in the background.
- **web** (React + TypeScript) — a small dashboard for seeing what routes
  exist and creating/editing/deleting them, served at nginx's `/` and
  talking to the admin API at `/_proxy/*` on the same origin.

```
                     ┌─────────────────────────┐
   browser/curl ──▶  │  nginx  (:4800 → :80)    │ ──▶ any container on
                     │  baseline + generated    │     kingdom-net, by
                     │  config, path routing    │     path prefix
                     └────────────┬─────────────┘
                                  │ /            → the React dashboard
                                  │                (static files, this image)
                                  │ /_proxy/*    → stripped, proxied to the
                                  │                api container (baseline
                                  │                route -- see below)
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

## Running under Podman

Everything above works under Podman instead of Docker, with a few
adjustments. What makes this possible, and what doesn't need to change:

- **The Docker Engine API calls kingdom-proxy-api makes** (container
  inspect, `exec` create/start for nginx reload) go over
  `KP_DOCKER_SOCKET`, an ordinary Unix socket -- Podman's API server
  implements a Docker-compatible subset of that same API on its own
  socket, by design, specifically so tools built against Docker's API
  (this one included) work against it unmodified. No code differences
  between the two.
- **The embedded-DNS resolver nginx needs** (so a route survives its
  target container restarting -- see
  [How a route gets applied](#how-a-route-gets-applied)) is *not* the same
  fixed address under Podman as it is under Docker, and even varies by
  network under Podman. This image detects it at container start rather
  than hardcoding it -- see
  `nginx/docker-entrypoint.d/40-kingdom-proxy-resolver.sh` -- specifically
  so this works under either runtime.
- **`docker-compose.yml` itself** doesn't need to change -- `podman-compose`
  or Podman's own `podman compose` can run it directly. The one thing that
  does need to change is telling the `api` service where Podman's socket
  actually is (see below), since that path isn't `/var/run/docker.sock`.

Setup:

1. Enable Podman's API socket (skip if it's already running):
   ```sh
   systemctl --user enable --now podman.socket   # rootless (typical)
   # or, for a rootful setup:
   sudo systemctl enable --now podman.socket
   ```
2. Point the stack at it and bring it up -- `CONTAINER_ENGINE_SOCKET` is a
   plain environment variable `docker-compose.yml`'s `api` service reads
   for the *host* side of the socket bind-mount (the container-internal
   path is unaffected, see the compose file's comments there):
   ```sh
   export CONTAINER_ENGINE_SOCKET=$XDG_RUNTIME_DIR/podman/podman.sock   # rootless
   # or: export CONTAINER_ENGINE_SOCKET=/run/podman/podman.sock        # rootful
   podman-compose up -d --build
   # or: podman compose up -d --build
   ```
3. `docker network connect` in this README and `scripts/dev-up.sh` becomes
   `podman network connect` for attaching an existing container to
   `kingdom-net`.

To sanity-check the resolver detection worked, the same way this project's
own testing did:
```sh
podman logs kingdom-proxy-nginx | grep kingdom-proxy   # should show the detected resolver
podman exec kingdom-proxy-nginx nginx -t
```

**Caveat:** this was built and end-to-end tested against a real Docker
daemon; the Podman-specific pieces (resolver detection, the compat API
calls, the socket path) are implemented against Podman's documented
Docker-compatibility rather than verified against a live Podman daemon, so
give it that same smoke test (create a route, confirm traffic flows, then
restart the target container and confirm the route still works) before
relying on it.

## Web dashboard

Open `http://localhost:4800/` — nginx serves the built React app from its
document root. It lists current routes (enabled and disabled), lets you
create a new one, edit a route's target/port/strip-prefix in place, toggle
it enabled/disabled, or delete it, and shows the background sync worker's
status. It talks to the admin API at `/_proxy/*` on the same origin, so
there's no separate host/port to configure and nothing extra to expose.

For local UI development against a running stack:

```sh
cd web
npm install
npm run dev   # proxies /_proxy/* to http://localhost:4800, see vite.config.ts
```

## Admin API

Reachable two ways — through the proxy at `<nginx-host>/_proxy/<path>`
(e.g. `http://localhost:4800/_proxy/routes`), or directly on its own
loopback-only port (e.g. `http://localhost:4810/routes`). Paths below are
the API's own, with the `/_proxy` prefix already stripped.

| Method | Path           | Description                                   |
|--------|----------------|------------------------------------------------|
| GET    | `/healthz`     | Liveness check.                                |
| GET    | `/status`      | Last sync result, timestamp, route count.      |
| GET    | `/config`      | Everything the web dashboard needs in one call: all routes, `reserved_path_prefix`, and sync status. |
| GET    | `/routes`      | List all routes.                               |
| POST   | `/routes`      | Create a route (see body above).               |
| GET    | `/routes/{id}` | Fetch one route.                               |
| PATCH  | `/routes/{id}` | Partial update — any of `container_name`, `container_port`, `strip_prefix`, `enabled`. `path_prefix` can't be changed (`422`); delete and recreate instead. |
| DELETE | `/routes/{id}` | Remove a route.                                |

`path_prefix` values of `/_proxy` or anything under it are rejected
(`422`) when creating a route — that prefix is reserved for the admin API
itself so a route can never shadow it. A `PATCH` that changes
`container_name` re-validates the new container against Docker (same as
`POST`) and refreshes `last_seen_ip`; changing only `enabled` or
`strip_prefix` does not.

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
web/         React + TypeScript dashboard (built into the nginx image by
             Dockerfile.nginx's web-build stage; see web/src/App.tsx)
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

- The admin API has no authentication, and now neither does the web
  dashboard that drives it. Its own loopback-only port (`4810`) was
  already unauthenticated by design, but wiring it up at `/_proxy/*` on
  nginx's port means full route management (create/edit/delete) is also
  reachable from wherever nginx's port (`4800`) itself is reachable. Don't
  expose that port beyond a trusted network without adding auth in front
  of it (e.g. a sidecar, an authenticating reverse proxy, or a VPN).
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
