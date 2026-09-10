#!/usr/bin/env bash
# One-time step (re-run after every renewal/rotation) for
# docker-compose.prod.yml on hosts where the real cert/key are root-owned
# system files (e.g. under /etc/pki/tls/) and this stack runs rootless
# Podman: copies them into a directory *your own user* owns.
#
# Why: rootless Podman's SELinux relabeling of a bind mount (the ":z" flag
# docker-compose.prod.yml uses on the cert/key mounts) works by rewriting
# the host file's security context -- an operation your unprivileged user
# has no DAC permission to perform on a file it doesn't own, root-owned
# key files included, regardless of SELinux policy. Copying to a
# user-owned location sidesteps that; see README.md's "HTTPS / TLS"
# section for the rootful alternative if that's viable on this host
# instead (real root can read/relabel the originals directly, no copy
# needed).
#
# Usage: scripts/sync-tls-cert.sh <source-cert> <source-key> [dest-dir]
#   dest-dir defaults to ~/kingdom-proxy-tls
set -euo pipefail

src_cert="${1:?usage: $0 <source-cert> <source-key> [dest-dir]}"
src_key="${2:?usage: $0 <source-cert> <source-key> [dest-dir]}"
dest_dir="${3:-$HOME/kingdom-proxy-tls}"

mkdir -p "$dest_dir"
# -m sets the copy's permissions directly; sudo is only needed to read the
# root-owned sources, not to widen them in place.
sudo install -o "$(id -un)" -g "$(id -gn)" -m 0444 "$src_cert" "$dest_dir/fullchain.pem"
sudo install -o "$(id -un)" -g "$(id -gn)" -m 0400 "$src_key" "$dest_dir/privkey.pem"

cat <<EOF

Copied to $dest_dir. Point the stack at it:
  export TLS_CERT_FILE=$dest_dir/fullchain.pem
  export TLS_KEY_FILE=$dest_dir/privkey.pem
(or set both in .env instead of exporting -- see .env.example)

This is a point-in-time copy, not a link -- re-run this script after the
source cert/key are ever renewed or rotated.
EOF
