cmake_minimum_required(VERSION 3.16)
project(ripple LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Socketify must already be installed (apt or `cmake --install`).
# Ripple requires:
#   - Socketify >= 0.2.2  (pulse_easy retain + composable close / media-safe)
#   - built with SOCKETIFY_WITH_TLS=ON  (HTTPS / WSS — needed for mic/camera)
# ---------------------------------------------------------------------------
find_package(Socketify 0.2.2 REQUIRED)
message(STATUS "Socketify ${Socketify_VERSION} (${Socketify_DIR})")

get_target_property(_ripple_sock_defs Socketify::socketify INTERFACE_COMPILE_DEFINITIONS)
set(_ripple_has_tls FALSE)
if(_ripple_sock_defs)
  foreach(_def IN LISTS _ripple_sock_defs)
    if(_def MATCHES "SOCKETIFY_HAS_TLS=1")
      set(_ripple_has_tls TRUE)
    endif()
  endforeach()
endif()

if(NOT _ripple_has_tls)
  message(FATAL_ERROR
    "Socketify ${Socketify_VERSION} was found at:\n"
    "    ${Socketify_DIR}\n"
    "but it was built WITHOUT TLS (SOCKETIFY_HAS_TLS is not 1).\n"
    "\n"
    "Ripple is HTTPS-only. Voice/video calls need a secure context\n"
    "(browsers block getUserMedia on plain http:// except localhost).\n"
    "\n"
    "Rebuild and reinstall Socketify with TLS enabled (default):\n"
    "\n"
    "    cd /path/to/Socketify\n"
    "    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSOCKETIFY_WITH_TLS=ON\n"
    "    cmake --build build -j\$(nproc)\n"
    "    sudo cmake --install build\n"
    "\n"
    "Then reconfigure Ripple:\n"
    "    rm -rf build && ./run.sh\n"
  )
endif()

message(STATUS "Socketify TLS: enabled (SOCKETIFY_HAS_TLS=1)")

add_executable(ripple backend/main.cpp)
target_link_libraries(ripple PRIVATE Socketify::socketify)

# Stage frontend/dist → $<TARGET_FILE_DIR>/web after each build
set(_ripple_stage "${CMAKE_CURRENT_BINARY_DIR}/stage_web.sh")
file(WRITE "${_ripple_stage}" [=[
#!/usr/bin/env sh
set -eu
OUT="${1:?}"
DIST="${2:?}"
mkdir -p "${OUT}"
if [ -f "${DIST}/index.html" ] && ! grep -q 'Build the frontend' "${DIST}/index.html"; then
  cp -a "${DIST}/." "${OUT}/"
else
  echo "ripple: frontend/dist missing — run ./run.sh (or: cd frontend && npm i && npm run build)" >&2
  printf '%s\n' '<!doctype html><meta charset=utf-8><title>Ripple</title>' \
    '<p>Frontend not staged. Run <code>./run.sh</code> from the Ripple repo.</p>' > "${OUT}/index.html"
fi
]=])
file(CHMOD "${_ripple_stage}" PERMISSIONS
  OWNER_READ OWNER_WRITE OWNER_EXECUTE
  GROUP_READ GROUP_EXECUTE
  WORLD_READ WORLD_EXECUTE
)

add_custom_command(TARGET ripple POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:ripple>/data
  COMMAND ${CMAKE_COMMAND} -E rm -rf $<TARGET_FILE_DIR:ripple>/web
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:ripple>/web
  COMMAND "${_ripple_stage}"
          $<TARGET_FILE_DIR:ripple>/web
          ${CMAKE_CURRENT_SOURCE_DIR}/frontend/dist
  COMMENT "Staging Ripple web UI and data/ next to ripple"
)
