#!/usr/bin/env bash
# Ripple — build frontend + server, stage web/, run on :8080
#
# Usage:
#   ./run.sh                         # standalone (expects ../Socketify or installed Socketify)
#   ./run.sh --socketify-root PATH --build-dir PATH   # called from Socketify run_examples.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOCKETIFY_ROOT=""
BUILD_DIR=""
SKIP_NPM=0
PORT=8080

while [[ $# -gt 0 ]]; do
  case "$1" in
    --socketify-root) SOCKETIFY_ROOT="${2:?}"; shift 2 ;;
    --build-dir)      BUILD_DIR="${2:?}"; shift 2 ;;
    --skip-npm)       SKIP_NPM=1; shift ;;
    --port)           PORT="${2:?}"; shift 2 ;;
    -h|--help)
      sed -n '2,8p' "$0"
      exit 0
      ;;
    *)
      echo "unknown arg: $1" >&2
      exit 1
      ;;
  esac
done

# Detect Socketify when running as examples/ripple submodule.
if [[ -z "${SOCKETIFY_ROOT}" ]]; then
  if [[ -f "${ROOT}/../../CMakeLists.txt" ]] && grep -q 'project(Socketify' "${ROOT}/../../CMakeLists.txt" 2>/dev/null; then
    SOCKETIFY_ROOT="$(cd "${ROOT}/../.." && pwd)"
  elif [[ -f "${ROOT}/../Socketify/CMakeLists.txt" ]]; then
    SOCKETIFY_ROOT="$(cd "${ROOT}/../Socketify" && pwd)"
  fi
fi

if [[ -z "${BUILD_DIR}" ]]; then
  if [[ -n "${SOCKETIFY_ROOT}" ]]; then
    BUILD_DIR="${SOCKETIFY_ROOT}/build-release"
  else
    BUILD_DIR="${ROOT}/build"
  fi
fi

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

# ---- C++ binary ----
echo "==> Building server → ${BUILD_DIR}"
if [[ -n "${SOCKETIFY_ROOT}" ]]; then
  cmake -S "${SOCKETIFY_ROOT}" -B "${BUILD_DIR}" \
      -DCMAKE_BUILD_TYPE=Release \
      -DSOCKETIFY_BUILD_EXAMPLES=ON >/dev/null
  cmake --build "${BUILD_DIR}" -j"$(nproc)" --target example_ripple
  BIN_DIR="${BUILD_DIR}/examples/ripple"
else
  cmake -S "${ROOT}" -B "${BUILD_DIR}" \
      -DCMAKE_BUILD_TYPE=Release \
      -DSOCKETIFY_BUILD_EXAMPLES=OFF \
      -DSOCKETIFY_BUILD_TESTS=OFF >/dev/null
  cmake --build "${BUILD_DIR}" -j"$(nproc)" --target ripple
  BIN_DIR="${BUILD_DIR}"
fi

BIN=""
for cand in "${BIN_DIR}/ripple" "${BIN_DIR}/example_ripple"; do
  if [[ -x "${cand}" ]]; then
    BIN="${cand}"
    break
  fi
done
if [[ -z "${BIN}" ]]; then
  echo "error: ripple binary not found under ${BIN_DIR}" >&2
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
