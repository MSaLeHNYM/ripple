cmake_minimum_required(VERSION 3.16)
project(ripple LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Socketify must already be installed (apt or `cmake --install`).
#   find_package(Socketify 0.2 REQUIRED)
#   target_link_libraries(ripple PRIVATE Socketify::socketify)
# ---------------------------------------------------------------------------
find_package(Socketify 0.2 REQUIRED)
message(STATUS "Socketify: ${Socketify_DIR}")

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
