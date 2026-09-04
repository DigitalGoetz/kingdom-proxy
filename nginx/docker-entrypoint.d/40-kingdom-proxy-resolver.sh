#!/bin/sh
# Runs as part of the official nginx image's startup sequence (it executes
# every *.sh file under /docker-entrypoint.d/, sorted by name, before
# starting nginx -- ours runs last, after the stock scripts).
#
# Detects this container's embedded DNS resolver from /etc/resolv.conf and
# writes it into conf.d/resolver.conf, which nginx.conf includes as a
# `resolver` directive. This can't be a fixed IP: Docker's embedded DNS is
# always 127.0.0.11, but under Podman (aardvark-dns, the netavark network
# backend) it's the *network's own gateway address*, which differs per
# network -- so it has to be read from the environment at container start
# rather than hardcoded, for this image to work under either runtime.
#
# This is what makes routes resolve their target by container *name* on
# every request (see ConfigGenerator::render in
# src/nginx/config_generator.cpp) rather than a cached IP -- the mechanism
# that lets a route survive a `docker restart`/`podman restart` of its
# target container.
set -eu

out=/etc/nginx/conf.d/resolver.conf
resolvers=$(awk '$1 == "nameserver" { if ($2 ~ ":") print "["$2"]"; else print $2 }' /etc/resolv.conf | tr '\n' ' ')

if [ -z "$resolvers" ]; then
  echo "kingdom-proxy: no nameserver found in /etc/resolv.conf -- dynamic routes and /_proxy/ will fail to resolve their upstream and every request to them will 502. Falling back to Docker's default embedded-DNS address (127.0.0.11), which is almost certainly wrong if this container is running under Podman." >&2
  resolvers="127.0.0.11"
fi

echo "resolver $resolvers valid=10s;" > "$out"
echo "kingdom-proxy: detected local resolver(s) from /etc/resolv.conf, wrote $out: resolver $resolvers valid=10s;"
