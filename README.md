# kingdom-proxy

A programmable reverse proxy for routing to locally running containers by
path — runs under Docker or Podman (see
[Running under Podman](#running-under-podman)), plain HTTP by default or
TLS-terminated on 443 in production (see [HTTPS / TLS](#https--tls)) —
built as three cooperating pieces:

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
a cached IP. nginx is configured to use the container runtime's embedded
DNS resolver (auto-detected at startup -- see
[Running under Podman](#running-under-podman)) and to resolve the upstream
through an nginx variable, which forces re-resolution on every request —
so a route keeps working across a restart of its target container without
needing a new sync.

Visiting a route at its bare prefix with no trailing slash (e.g.
`/myapp` rather than `/myapp/` — the natural way to type a URL) redirects
to the slash-terminated form automatically, with the query string and the
actual port you connected on both preserved.

## Quick start

```sh
./scripts/dev-up.sh
```

(or `./scripts/demo-up.sh`, which also brings up and routes the
[example app](#example-app) — the fastest way to see the whole thing
working end to end.)

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

## HTTPS / TLS

The default stack (`docker-compose.yml`, `nginx.baseline.conf`) serves
plain HTTP on host port 4800, as described above — no certs, no setup,
good for local dev/demo. `docker-compose.prod.yml` is a separate,
TLS-terminating alternative for actually deploying this somewhere:
nginx listens on host `:443` for real traffic and `:80` only to redirect
to it (plus the health check nginx's own container relies on, see
`nginx/nginx.tls.conf`). Run it *instead of* `docker-compose.yml`, not
layered on top of it (see the comment at the top of the file for why):

```sh
docker compose -f docker-compose.prod.yml up -d --build
# or: podman compose -f docker-compose.prod.yml up -d --build
```

It doesn't generate or obtain a certificate itself — bring your own.
Before first start, put `fullchain.pem` and `privkey.pem` in `./certs/`
(or set `TLS_CERT_DIR` to point elsewhere, the same way
`CONTAINER_ENGINE_SOCKET` works for the Docker/Podman socket path — see
[Running under Podman](#running-under-podman)); nginx refuses to start if
either file is missing. To smoke-test the TLS wiring itself before a real
cert is in hand:

```sh
./scripts/gen-dev-cert.sh   # writes a throwaway self-signed pair to ./certs/
curl -k https://localhost/healthz   # -k: this test cert isn't signed by a real CA
```

(drop `-k` once real certs are in place — dropping it against the
self-signed dev pair will fail certificate verification, as expected.)

**Binding host ports below 1024:** both `:80` and `:443` are privileged
ports. A rootful Docker/Podman daemon can bind them freely; **rootless**
Podman (or rootless Docker) typically can't by default, and `compose up`
will fail with something like `rootlessport listen tcp 0.0.0.0:443: bind:
permission denied`. Either run rootful (`sudo podman compose -f
docker-compose.prod.yml up -d --build`, with `CONTAINER_ENGINE_SOCKET`
pointed at the rootful socket per the Podman section above), or allow your
regular user to bind low ports on this host:

```sh
sudo sysctl -w net.ipv4.ip_unprivileged_port_start=80
```

(add it to `/etc/sysctl.d/` to survive a reboot).

Everything else — registering routes, the dashboard, the admin API —
works identically to the plain-HTTP stack; only the scheme and port
change. `$scheme` in nginx's generated redirects (bare route prefixes,
`/_proxy` → `/_proxy/`) resolves to `https` automatically once traffic is
actually arriving over `:443`, so no route/API behavior differs.

## Web dashboard

Open `http://localhost:4800/` — nginx serves the built React app from its
document root. It lists current routes (enabled and disabled), lets you
create a new one, edit a route's target/port/strip-prefix in place, toggle
it enabled/disabled, or delete it, and shows the background sync worker's
status. Each route also has an **Open ↗** link to it in a new tab. It
talks to the admin API at `/_proxy/*` on the same origin, so there's no
separate host/port to configure and nothing extra to expose.

Below that, a **containers panel** lists every currently-running
container on the host (not just ones already on `kingdom-net`) with the
ports each one listens on, to help you find what to route to — click a
port to fill it into the create-route form above. It's read-only except
for one thing: a container not yet on `kingdom-net` is flagged as such
with an **Attach** button, doing the equivalent of
`docker network connect kingdom-net <container>` (which any container
needs before a route to it will work — see
[How a route gets applied](#how-a-route-gets-applied)) without leaving
the dashboard or touching a terminal. It's idempotent (safe to click on
something already attached) and doesn't itself create a route.

This works for *any* running container regardless of what created it —
another `docker-compose.yml`, a bare `docker run`, Podman, anything on
the same engine — not just containers already in kingdom-proxy's own
stack. The one caveat: `docker network connect` isn't persistent config,
so a container that gets recreated later (an image update via its own
compose file, say) will need Attach again unless you also add
`kingdom-net` as an `external: true` network in *that* container's own
compose file.

For local UI development against a running stack:

```sh
cd web
npm install
npm run dev   # proxies /_proxy/* to http://localhost:4800, see vite.config.ts
```

## Example app

`examples/todo-app/` is a minimal service to try routing with: a to-do
list, API and UI in one container, using nothing but Node's built-in
`http` module (no `npm install` needed — see its own `server.js`). It's
not part of the core stack; bring it up with:

```sh
./scripts/demo-up.sh
```

which starts kingdom-proxy (if it isn't already up), builds and starts
the demo app on `kingdom-net`, and registers a route for it at `/demo` —
then open `http://localhost:4800/demo/`. Tear it down again with
`docker compose --profile demo down`.

Doing this by hand instead, the same way you'd route any of your own
containers, looks like:

```sh
docker compose --profile demo up -d --build demo
curl -X POST http://localhost:4800/_proxy/routes \
  -H 'content-type: application/json' \
  -d '{"path_prefix":"/demo","container_name":"kingdom-demo-todo-app","container_port":8080}'
```

The one thing worth noting if you're adapting this for your own app: its
UI fetches its API with a *relative* path (`api/todos`, not `/api/todos`).
A leading-slash path resolves against the origin root regardless of the
page's own URL, ignoring whatever prefix kingdom-proxy mounted it at; a
relative one resolves against the current page, so it keeps working no
matter what prefix a route uses. See the comment at the top of
`examples/todo-app/server.js` for more.

## Admin API

Reachable two ways — through the proxy at `<nginx-host>/_proxy/<path>`
(e.g. `http://localhost:4800/_proxy/routes`), or directly on its own
loopback-only port (e.g. `http://localhost:4810/routes`). Paths below are
the API's own, with the `/_proxy` prefix already stripped.

| Method | Path           | Description                                   |
|--------|----------------|------------------------------------------------|
| GET    | `/healthz`     | Liveness check.                                |
| GET    | `/status`      | Last sync result, timestamp, route count.      |
| GET    | `/config`      | Everything the web dashboard needs in one call: all routes, `reserved_path_prefix`, `network_name`, and sync status. |
| GET    | `/containers`  | Every running container on the host, with `id`, `name`, `image`, `status`, `ports`, and `on_network` (whether it's attached to `network_name`). |
| POST   | `/containers/{name}/attach-network` | Attaches that container to `network_name` (`docker network connect` equivalent). Idempotent; doesn't create a route. |
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
examples/    Example app(s) to try routing with -- see todo-app/
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
| `KP_NETWORK_NAME`            | `kingdom-net`                              |
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
  route-creators with. `GET /containers` (and the dashboard's containers
  panel) makes this tradeoff concretely visible rather than just
  theoretical: it lists every running container on the *host*, including
  ones with nothing to do with this stack — container and image names,
  status, and listening ports. `POST /containers/{name}/attach-network`
  goes a step further and actually acts on that reach: anyone who can
  reach the admin API can attach any host container to `kingdom-net`,
  the same as they could with `docker network connect` directly.
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
