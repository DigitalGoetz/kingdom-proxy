#!/usr/bin/env bash
# Generates a throwaway self-signed cert/key for smoke-testing
# docker-compose.prod.yml's TLS config before wiring in a real
# certificate. Writes to ./certs/{fullchain,privkey}.pem, which is also
# where TLS_CERT_FILE/TLS_KEY_FILE point by default -- so plain
# `docker-compose.prod.yml` picks this up with no further setup. Browsers
# will show an untrusted-certificate warning for this pair -- that's
# expected, it's not signed by any real CA. Run from the repo root. See
# README.md's "HTTPS / TLS" section.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

out_dir="./certs"
days="${1:-365}"

if [[ -e "$out_dir/fullchain.pem" || -e "$out_dir/privkey.pem" ]]; then
  echo "$out_dir already has fullchain.pem/privkey.pem -- remove them first if you want new ones." >&2
  exit 1
fi

mkdir -p "$out_dir"
openssl req -x509 -nodes -newkey rsa:2048 \
  -days "$days" \
  -keyout "$out_dir/privkey.pem" \
  -out "$out_dir/fullchain.pem" \
  -subj "/CN=localhost" \
  -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"

chmod 600 "$out_dir/privkey.pem"

echo "wrote $out_dir/fullchain.pem and $out_dir/privkey.pem (self-signed, valid $days days)."
