#!/usr/bin/env bash
# Ripple — build frontend + server, stage web/, run on :8080
#
# Requires Socketify already installed (apt or cmake --install).
# Ripple only links Socketify::socketify via find_package — it does not build it.
#
# Usage:
#   ./run.sh
#   ./run.sh --port 8080
#   ./run.sh --skip-npm
#   ./run.sh --build-dir /tmp/ripple-build
#   ./run.sh --prefix /usr/local          # where Socketify was installed
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT}/build"
SKIP_NPM=0
PORT=8080
PREFIX=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir) BUILD_DIR="${2:?}"; shift 2 ;;
    --skip-npm)  SKIP_NPM=1; shift ;;
    --port)      PORT="${2:?}"; shift 2 ;;
    --prefix)    PREFIX="${2:?}"; shift 2 ;;
    --socketify-root) shift 2 ;;  # ignored (compat)
    -h|--help)
      sed -n '2,14p' "$0"
      exit 0
      ;;
    *)
      echo "unknown arg: $1" >&2
      exit 1
      ;;
  esac
done

echo "==> Ripple root: ${ROOT}"

# ---- Frontend ----
if [[ "${SKIP_NPM}" -eq 0 ]]; then
  echo "==> Building frontend"
  if [[ ! -d "${ROOT}/frontend/node_modules" ]]; then
    (cd "${ROOT}/frontend" && npm install)
  fi
  (cd "${ROOT}/frontend" && npm run build)
fi

if [[ ! -f "${ROOT}/frontend/dist/index.html" ]]; then
  echo "error: frontend/dist/index.html missing after build" >&2
  exit 1
fi
if grep -q 'Build the frontend' "${ROOT}/frontend/dist/index.html"; then
  echo "error: frontend/dist still looks like the CMake placeholder — rebuild failed" >&2
  exit 1
fi

# ---- C++ binary (find_package(Socketify) only) ----
echo "==> Building server → ${BUILD_DIR}"
CMAKE_ARGS=(
  -S "${ROOT}"
  -B "${BUILD_DIR}"
  -DCMAKE_BUILD_TYPE=Release
)
if [[ -n "${PREFIX}" ]]; then
  CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=${PREFIX}")
fi

if ! cmake "${CMAKE_ARGS[@]}"; then
  cat >&2 <<'EOF'

error: CMake could not find Socketify 0.2.

Install it first, then re-run ./run.sh:

  cd ../Socketify   # or your Socketify clone
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build -j"$(nproc)"
  sudo cmake --install build

Or: ./run.sh --prefix /usr/local
EOF
  exit 1
fi

cmake --build "${BUILD_DIR}" -j"$(nproc)" --target ripple

BIN_DIR="${BUILD_DIR}"
BIN="${BIN_DIR}/ripple"
if [[ ! -x "${BIN}" ]]; then
  echo "error: ripple binary not found at ${BIN}" >&2
  exit 1
fi

# Always re-stage UI next to the binary (CMake POST_BUILD is skipped on no-op rebuilds).
echo "==> Staging web/ next to binary"
rm -rf "${BIN_DIR}/web"
mkdir -p "${BIN_DIR}/web" "${BIN_DIR}/data"
cp -a "${ROOT}/frontend/dist/." "${BIN_DIR}/web/"
if grep -q 'Build the frontend' "${BIN_DIR}/web/index.html"; then
  echo "error: staged web/index.html is still the placeholder" >&2
  exit 1
fi

# Socketify uses SO_REUSEPORT — a stale ./ripple can keep serving old HTML on :8080.
free_port() {
  local port="$1"
  local pids=""
  if command -v fuser >/dev/null 2>&1; then
    fuser -k "${port}/tcp" >/dev/null 2>&1 || true
  fi
  if command -v lsof >/dev/null 2>&1; then
    pids="$(lsof -t -iTCP:"${port}" -sTCP:LISTEN 2>/dev/null || true)"
  fi
  if [[ -z "${pids}" ]] && command -v ss >/dev/null 2>&1; then
    pids="$(ss -tlnp "sport = :${port}" 2>/dev/null | sed -n 's/.*pid=\([0-9]\+\).*/\1/p' | sort -u)"
  fi
  if [[ -n "${pids}" ]]; then
    echo "==> Freeing :${port} (pids: ${pids})"
    # shellcheck disable=SC2086
    kill ${pids} 2>/dev/null || true
    sleep 0.4
    # shellcheck disable=SC2086
    kill -9 ${pids} 2>/dev/null || true
  fi
}
echo "==> Ensuring port ${PORT} is free"
free_port "${PORT}"

echo "==> Running ${BIN} (Ctrl-C to stop)"
echo "    http://localhost:${PORT}"
cd "${BIN_DIR}"
exec "${BIN}"
