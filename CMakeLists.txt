cmake_minimum_required(VERSION 3.16)
project(ripple LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Socketify must already be installed (apt or `cmake --install`).
#   find_package(Socketify 0.2 REQUIRED)
#   target_link_libraries(ripple PRIVATE Socketify::socketify)
#
# Version alone is not enough — an old 0.2 install may lack ORM/Pulse.
# We require the headers Ripple actually uses.
# ---------------------------------------------------------------------------
find_package(Socketify 0.2 REQUIRED)

set(_ripple_required_headers
  socketify/db.h
  socketify/pulse.h
  socketify/pulse_easy.h
  socketify/pulse_media.h
  socketify/json.h
  socketify/validate.h
  socketify/config.h
)

get_target_property(_ripple_sock_incs Socketify::socketify INTERFACE_INCLUDE_DIRECTORIES)
if(NOT _ripple_sock_incs)
  set(_ripple_sock_incs "")
endif()

set(_ripple_missing_headers "")
foreach(_hdr IN LISTS _ripple_required_headers)
  set(_found FALSE)
  foreach(_inc IN LISTS _ripple_sock_incs)
    # Strip common generator expressions from installed targets.
    string(REGEX REPLACE "^\\$<[^:]+:([^>]+)>$" "\\1" _inc_clean "${_inc}")
    if(EXISTS "${_inc_clean}/${_hdr}")
      set(_found TRUE)
      break()
    endif()
  endforeach()
  if(NOT _found)
    list(APPEND _ripple_missing_headers "${_hdr}")
  endif()
endforeach()

# Prefer reading SOCKETIFY_HAS_SQLITE from the imported target when present.
get_target_property(_ripple_sock_defs Socketify::socketify INTERFACE_COMPILE_DEFINITIONS)
set(_ripple_has_sqlite FALSE)
if(_ripple_sock_defs)
  foreach(_def IN LISTS _ripple_sock_defs)
    if(_def MATCHES "SOCKETIFY_HAS_SQLITE=1")
      set(_ripple_has_sqlite TRUE)
    endif()
  endforeach()
endif()
# Header presence implies a SQLite-capable install for our purposes.
if(NOT "socketify/db.h" IN_LIST _ripple_missing_headers)
  set(_ripple_has_sqlite TRUE)
endif()

if(_ripple_missing_headers OR NOT _ripple_has_sqlite)
  string(REPLACE ";" "\n    - " _missing_fmt "${_ripple_missing_headers}")
  message(FATAL_ERROR
    "Socketify was found at:\n"
    "    ${Socketify_DIR}\n"
    "but this install is too old or incomplete for Ripple.\n"
    "Missing headers:\n    - ${_missing_fmt}\n"
    "\n"
    "Ripple needs Socketify >= 0.2 with SQLite ORM + Pulse (easy/media).\n"
    "Reinstall from a current Socketify tree:\n"
    "\n"
    "    cd /path/to/Socketify\n"
    "    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release\n"
    "    cmake --build build -j\$(nproc)\n"
    "    sudo cmake --install build\n"
    "\n"
    "Then reconfigure Ripple (delete build/ first if needed):\n"
    "    rm -rf build && ./run.sh\n"
  )
endif()

message(STATUS "Socketify: ${Socketify_DIR} (ok — ORM + Pulse present)")

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
