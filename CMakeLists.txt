cmake_minimum_required(VERSION 3.16)
project(ripple LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(RIPPLE_TARGET ripple)
else()
  set(RIPPLE_TARGET example_ripple)
endif()

if(NOT TARGET Socketify::socketify)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../Socketify/CMakeLists.txt")
    set(SOCKETIFY_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SOCKETIFY_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory(
      "${CMAKE_CURRENT_SOURCE_DIR}/../Socketify"
      "${CMAKE_CURRENT_BINARY_DIR}/socketify"
      EXCLUDE_FROM_ALL
    )
  else()
    find_package(Socketify REQUIRED)
  endif()
endif()

add_executable(${RIPPLE_TARGET} backend/main.cpp)
target_link_libraries(${RIPPLE_TARGET} PRIVATE Socketify::socketify)
set_target_properties(${RIPPLE_TARGET} PROPERTIES OUTPUT_NAME ripple)

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

add_custom_command(TARGET ${RIPPLE_TARGET} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${RIPPLE_TARGET}>/data
  COMMAND ${CMAKE_COMMAND} -E rm -rf $<TARGET_FILE_DIR:${RIPPLE_TARGET}>/web
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${RIPPLE_TARGET}>/web
  COMMAND "${_ripple_stage}"
          $<TARGET_FILE_DIR:${RIPPLE_TARGET}>/web
          ${CMAKE_CURRENT_SOURCE_DIR}/frontend/dist
  COMMENT "Staging Ripple web UI and data/ next to ripple"
)
