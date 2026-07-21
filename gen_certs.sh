#!/usr/bin/env bash
# Generate TLS certificates for Ripple (HTTPS / WSS).
#
# Interactive (default):
#   ./gen_certs.sh
#     → "Do you have a domain?"
#        yes  → Let's Encrypt (trusted; no browser warning)
#        no   → self-signed random/local cert (browsers warn once)
#
# Non-interactive:
#   ./gen_certs.sh --domain chat.example.com          # Let's Encrypt
#   ./gen_certs.sh --self-signed                      # local self-signed
#   ./gen_certs.sh --self-signed --domain lan.test    # self-signed with name
#   ./gen_certs.sh --force …
#   ./gen_certs.sh --out /path/to/dir …
#
# Env: RIPPLE_DOMAIN=chat.example.com  (implies Let's Encrypt when interactive
#      would ask, or when --domain is omitted but env is set with no --self-signed)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FORCE=0
OUT_DIR="${ROOT}/certs"
DOMAIN="${RIPPLE_DOMAIN:-}"
MODE=""   # letsencrypt | self-signed | "" (ask)
EXTRA_DOMAINS=()

usage() {
  sed -n '2,20p' "$0"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --force|-f) FORCE=1; shift ;;
    --letsencrypt|--acme)
      MODE=letsencrypt
      shift
      ;;
    --self-signed|--local)
      MODE=self-signed
      shift
      ;;
    --domain|-d)
      DOMAIN="${2:?--domain needs a value}"
      shift 2
      ;;
    --also)
      EXTRA_DOMAINS+=("${2:?}")
      shift 2
      ;;
    --out|-o)
      OUT_DIR="${2:?}"
      shift 2
      ;;
    -h|--help)
      usage
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
META="${OUT_DIR}/domain.txt"

if [[ -f "${CERT}" && -f "${KEY}" && "${FORCE}" -eq 0 ]]; then
  echo "already exists: ${CERT} ${KEY}"
  [[ -f "${META}" ]] && echo "domain: $(cat "${META}")"
  echo "run: ./gen_certs.sh --force   to regenerate"
  exit 0
fi

validate_domain() {
  local d="$1"
  [[ -z "${d}" ]] && return 0
  if [[ ! "${d}" =~ ^[A-Za-z0-9]([A-Za-z0-9.-]*[A-Za-z0-9])?$ ]]; then
    echo "error: invalid domain '${d}'" >&2
    exit 1
  fi
  if [[ "${d}" == *://* || "${d}" == */* || "${d}" == *:* ]]; then
    echo "error: pass only the hostname (e.g. chat.example.com), not a URL" >&2
    exit 1
  fi
}

ask_yes_no() {
  local prompt="$1"
  local reply
  while true; do
    read -r -p "${prompt} [y/N] " reply
    reply="$(echo "${reply}" | tr '[:upper:]' '[:lower:]' | tr -d '[:space:]')"
    case "${reply}" in
      y|yes) return 0 ;;
      n|no|"") return 1 ;;
      *) echo "Please answer y or n." ;;
    esac
  done
}

# Decide mode + domain
if [[ -z "${MODE}" ]]; then
  if [[ -n "${DOMAIN}" ]]; then
    # Domain already provided via env/flag → Let's Encrypt unless --self-signed
    MODE=letsencrypt
  elif [[ -t 0 ]]; then
    echo
    echo "Ripple needs HTTPS for voice/video (mic/camera)."
    echo
    if ask_yes_no "Do you have a public domain name (e.g. chat.example.com)?"; then
      MODE=letsencrypt
      read -r -p "Domain: " DOMAIN
      DOMAIN="$(echo "${DOMAIN}" | tr -d '[:space:]')"
      if [[ -z "${DOMAIN}" ]]; then
        echo "error: domain is required for Let's Encrypt" >&2
        exit 1
      fi
    else
      MODE=self-signed
      DOMAIN=""
      echo "==> No domain — creating a self-signed cert for local/LAN use."
    fi
  else
    # Non-interactive, no flags → local self-signed
    MODE=self-signed
    DOMAIN=""
  fi
fi

# --domain with default mode already set to letsencrypt above;
# if user passed --self-signed --domain X, keep self-signed with that CN.
if [[ "${MODE}" == "letsencrypt" && -z "${DOMAIN}" ]]; then
  if [[ -t 0 ]]; then
    read -r -p "Domain for Let's Encrypt: " DOMAIN
    DOMAIN="$(echo "${DOMAIN}" | tr -d '[:space:]')"
  fi
  if [[ -z "${DOMAIN}" ]]; then
    echo "error: Let's Encrypt needs --domain your.domain.com" >&2
    exit 1
  fi
fi

validate_domain "${DOMAIN}"
for d in "${EXTRA_DOMAINS[@]+"${EXTRA_DOMAINS[@]}"}"; do
  validate_domain "${d}"
done

# ── Let's Encrypt ──────────────────────────────────────────────────────────
if [[ "${MODE}" == "letsencrypt" ]]; then
  if ! command -v certbot >/dev/null 2>&1; then
    echo "error: certbot not found. Install it, e.g.:" >&2
    echo "  sudo apt install certbot" >&2
    exit 1
  fi

  echo
  echo "==> Let's Encrypt for ${DOMAIN}"
  echo "    • DNS A/AAAA for ${DOMAIN} must point at this machine"
  echo "    • Port 80 must be free (certbot --standalone)"
  echo

  sudo certbot certonly --standalone \
    -d "${DOMAIN}" \
    --non-interactive --agree-tos \
    --register-unsafely-without-email \
    ${FORCE:+--force-renewal}

  LIVE="/etc/letsencrypt/live/${DOMAIN}"
  if [[ ! -f "${LIVE}/fullchain.pem" || ! -f "${LIVE}/privkey.pem" ]]; then
    echo "error: certbot finished but ${LIVE}/fullchain.pem not found" >&2
    exit 1
  fi

  sudo cp -f "${LIVE}/fullchain.pem" "${CERT}"
  sudo cp -f "${LIVE}/privkey.pem" "${KEY}"
  sudo chown "$(id -u):$(id -g)" "${CERT}" "${KEY}"
  chmod 644 "${CERT}"
  chmod 600 "${KEY}"
  printf '%s\n' "${DOMAIN}" > "${META}"

  echo
  echo "Trusted certificate ready — open https://${DOMAIN}"
  echo "  cert: ${CERT}"
  echo "  key:  ${KEY}"
  echo
  echo "Run:  ./run.sh --port 443"
  echo "  or: ./build/ripple --cert ${CERT} --key ${KEY} --port 443"
  exit 0
fi

# ── Self-signed (no domain / local) ────────────────────────────────────────
if ! command -v openssl >/dev/null 2>&1; then
  echo "error: openssl not found — install openssl to generate certs" >&2
  exit 1
fi

# Random-ish local CN when no domain (unique enough for local trust prompts)
if [[ -z "${DOMAIN}" ]]; then
  RAND="$(openssl rand -hex 4 2>/dev/null || date +%s)"
  CN="ripple-local-${RAND}"
else
  CN="${DOMAIN}"
fi

SAN_ENTRIES=("DNS:localhost" "DNS:${CN}" "DNS:*.local" "IP:127.0.0.1" "IP:::1")
if [[ -n "${DOMAIN}" ]]; then
  SAN_ENTRIES+=("DNS:${DOMAIN}")
  if [[ "${DOMAIN}" != www.* ]]; then
    SAN_ENTRIES+=("DNS:www.${DOMAIN}")
  fi
fi
for d in "${EXTRA_DOMAINS[@]+"${EXTRA_DOMAINS[@]}"}"; do
  SAN_ENTRIES+=("DNS:${d}")
done

if command -v hostname >/dev/null 2>&1; then
  hn="$(hostname -f 2>/dev/null || hostname 2>/dev/null || true)"
  if [[ -n "${hn}" && "${hn}" != "localhost" ]]; then
    SAN_ENTRIES+=("DNS:${hn}")
  fi
fi
if command -v ip >/dev/null 2>&1; then
  while read -r ipaddr; do
    [[ -n "${ipaddr}" ]] && SAN_ENTRIES+=("IP:${ipaddr}")
  done < <(ip -4 -o addr show scope global 2>/dev/null | awk '{print $4}' | cut -d/ -f1 | sort -u)
elif command -v hostname >/dev/null 2>&1; then
  while read -r ipaddr; do
    [[ -n "${ipaddr}" && "${ipaddr}" != "127.0.0.1" ]] && SAN_ENTRIES+=("IP:${ipaddr}")
  done < <(hostname -I 2>/dev/null | tr ' ' '\n' | sort -u)
fi

SAN="$(printf '%s\n' "${SAN_ENTRIES[@]}" | awk '!seen[$0]++' | paste -sd, -)"

echo "==> Self-signed CN=${CN}"
echo "==> subjectAltName=${SAN}"

openssl req -x509 -newkey rsa:2048 -nodes -days 365 \
  -keyout "${KEY}" \
  -out "${CERT}" \
  -subj "/CN=${CN}" \
  -addext "subjectAltName=${SAN}"

chmod 600 "${KEY}"
printf '%s\n' "${DOMAIN}" > "${META}"

echo "wrote ${CERT}"
echo "wrote ${KEY}"
echo
echo "Self-signed cert ready for local/LAN HTTPS."
echo "Browsers will warn once — accept to continue (needed for mic/camera)."
echo "Open https://<lan-ip>:8443  (not http://)."
echo
echo "Run:  ./run.sh"
