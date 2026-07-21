#!/usr/bin/env bash
# Generate a self-signed TLS certificate for Ripple (HTTPS / WSS).
#
# Usage:
#   ./gen_certs.sh                 # → certs/server.crt + certs/server.key
#   ./gen_certs.sh /path/to/dir
#   ./gen_certs.sh --force         # regenerate (picks up current LAN IPs)
#
# Browsers will warn on self-signed certs — accept the exception for local/LAN
# testing. Voice/video need HTTPS (secure context) off-localhost.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FORCE=0
OUT_DIR="${ROOT}/certs"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --force|-f) FORCE=1; shift ;;
    -h|--help)
      sed -n '2,12p' "$0"
      exit 0
      ;;
    *)
      OUT_DIR="$1"
      shift
      ;;
  esac
done

mkdir -p "${OUT_DIR}"

CERT="${OUT_DIR}/server.crt"
KEY="${OUT_DIR}/server.key"

if [[ -f "${CERT}" && -f "${KEY}" && "${FORCE}" -eq 0 ]]; then
  echo "already exists: ${CERT} ${KEY}"
  echo "run: ./gen_certs.sh --force   to regenerate (include current LAN IPs)"
  exit 0
fi

if ! command -v openssl >/dev/null 2>&1; then
  echo "error: openssl not found — install openssl to generate certs" >&2
  exit 1
fi

# Collect hostnames / IPs for subjectAltName (needed for https://192.168.x.x).
SAN_ENTRIES=("DNS:localhost" "DNS:*.local" "IP:127.0.0.1" "IP:::1")
if command -v hostname >/dev/null 2>&1; then
  hn="$(hostname -f 2>/dev/null || hostname 2>/dev/null || true)"
  if [[ -n "${hn}" && "${hn}" != "localhost" ]]; then
    SAN_ENTRIES+=("DNS:${hn}")
  fi
fi
# Linux: all non-loopback IPv4 addresses
if command -v ip >/dev/null 2>&1; then
  while read -r ipaddr; do
    [[ -n "${ipaddr}" ]] && SAN_ENTRIES+=("IP:${ipaddr}")
  done < <(ip -4 -o addr show scope global 2>/dev/null | awk '{print $4}' | cut -d/ -f1 | sort -u)
elif command -v hostname >/dev/null 2>&1; then
  while read -r ipaddr; do
    [[ -n "${ipaddr}" && "${ipaddr}" != "127.0.0.1" ]] && SAN_ENTRIES+=("IP:${ipaddr}")
  done < <(hostname -I 2>/dev/null | tr ' ' '\n' | sort -u)
fi

# Deduplicate
SAN="$(printf '%s\n' "${SAN_ENTRIES[@]}" | awk '!seen[$0]++' | paste -sd, -)"

echo "==> subjectAltName=${SAN}"

openssl req -x509 -newkey rsa:2048 -nodes -days 365 \
  -keyout "${KEY}" \
  -out "${CERT}" \
  -subj "/CN=localhost" \
  -addext "subjectAltName=${SAN}"

chmod 600 "${KEY}"
echo "wrote ${CERT}"
echo "wrote ${KEY}"
echo
echo "Open Ripple as https://<this-machine-ip>:8443 (not http://)."
echo "Accept the certificate warning, then Allow microphone when prompted."
echo
echo "Run:  ./run.sh"
