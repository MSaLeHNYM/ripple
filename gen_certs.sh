#!/usr/bin/env bash
# Generate a self-signed TLS certificate for Ripple (HTTPS / WSS).
#
# Usage:
#   ./gen_certs.sh              # → certs/server.crt + certs/server.key
#   ./gen_certs.sh /path/to/dir
#
# Browsers will warn on self-signed certs — accept the exception for local/LAN
# testing. Voice/video need HTTPS (secure context) off-localhost.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="${1:-${ROOT}/certs}"
mkdir -p "${OUT_DIR}"

CERT="${OUT_DIR}/server.crt"
KEY="${OUT_DIR}/server.key"

if [[ -f "${CERT}" && -f "${KEY}" ]]; then
  echo "already exists: ${CERT} ${KEY}"
  echo "remove them to regenerate"
  exit 0
fi

if ! command -v openssl >/dev/null 2>&1; then
  echo "error: openssl not found — install openssl to generate certs" >&2
  exit 1
fi

# SAN covers localhost + loopback; add LAN IPs manually if needed for phones.
openssl req -x509 -newkey rsa:2048 -nodes -days 365 \
  -keyout "${KEY}" \
  -out "${CERT}" \
  -subj "/CN=localhost" \
  -addext "subjectAltName=DNS:localhost,DNS:*.local,IP:127.0.0.1"

chmod 600 "${KEY}"
echo "wrote ${CERT}"
echo "wrote ${KEY}"
echo
echo "Run Ripple with:"
echo "  ./run.sh"
echo "  # or: ./build/ripple --cert ${CERT} --key ${KEY}"
