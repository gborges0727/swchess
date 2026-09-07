# The native frame interpolation helper. It builds inside the top level
# project and on its own:
#
#   cmake -S src/interp -B build-interp -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-interp
#   ctest --test-dir build-interp --output-on-failure
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.28)
  project(swchess_interp LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
  endif()
  include(CTest)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/CompilerFlags.cmake")

# The repo root, two directories above this one.
get_filename_component(SWCHESS_INTERP_REPO_ROOT
                       "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

if(NOT SWCHESS_ASSETS_DIR)
  set(SWCHESS_ASSETS_DIR "${SWCHESS_INTERP_REPO_ROOT}/assets")
endif()

# Where the frame interpolation backend and its weights live. tools/rife
# builds both into .cache/rife/bin. The packaging lane reads these two
# variables to decide what to copy into the application.
set(SWCHESS_RIFE_BINARY "${SWCHESS_INTERP_REPO_ROOT}/.cache/rife/bin/rife-ncnn-vulkan"
    CACHE FILEPATH "the rife-ncnn-vulkan executable the helper runs")
set(SWCHESS_RIFE_MODEL_DIR "${SWCHESS_INTERP_REPO_ROOT}/.cache/rife/bin/rife-v4.6"
    CACHE PATH "the directory holding flownet.bin and flownet.param")

# png_io.cpp inflates what RIFE wrote, and the manifests go through
# nlohmann-json.
if(NOT TARGET nlohmann_json::nlohmann_json)
  find_package(nlohmann_json CONFIG REQUIRED)
endif()
if(NOT TARGET ZLIB::ZLIB)
  find_package(ZLIB REQUIRED)
endif()
find_package(Threads REQUIRED)

# The helper shares the PNG writer, the JSON writer and the hash with the
# exporter. A standalone build has no swchess_export target yet.
if(NOT TARGET swchess_export)
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../export"
                   "${CMAKE_CURRENT_BINARY_DIR}/export")
endif()

add_library(swchess_interp STATIC
  check.cpp
  images.cpp
  pipeline.cpp
  png_io.cpp
  process.cpp
  timeline.cpp
)
# Headers include as "interp/pipeline.h", so the include root is src.
target_include_directories(swchess_interp PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(swchess_interp PUBLIC swchess_export nlohmann_json::nlohmann_json
                      ZLIB::ZLIB Threads::Threads)
target_compile_options(swchess_interp PRIVATE ${SWCHESS_WARNINGS})

add_executable(swchess-interpolate interpolate_main.cpp)
target_link_libraries(swchess-interpolate PRIVATE swchess_interp)
target_compile_definitions(swchess-interpolate PRIVATE
  "SWCHESS_RIFE_BINARY=\"${SWCHESS_RIFE_BINARY}\""
  "SWCHESS_RIFE_MODEL_DIR=\"${SWCHESS_RIFE_MODEL_DIR}\"")
target_compile_options(swchess-interpolate PRIVATE ${SWCHESS_WARNINGS})

# The oracle test interpolates two captures again and compares them with what
# the Python pipeline produced. It needs the private asset cache and a built
# RIFE backend, so it only registers when both are here.
if(BUILD_TESTING AND EXISTS "${SWCHESS_RIFE_BINARY}"
   AND EXISTS "${SWCHESS_ASSETS_DIR}/captures/BBWB/interp60/manifest.json")
  add_test(NAME interp.oracle
           COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/interp_oracle_test.sh"
                   "$<TARGET_FILE:swchess-interpolate>" "${SWCHESS_ASSETS_DIR}"
                   "${CMAKE_CURRENT_BINARY_DIR}/oracle"
                   "${SWCHESS_RIFE_BINARY}" "${SWCHESS_RIFE_MODEL_DIR}")
  set_tests_properties(interp.oracle PROPERTIES TIMEOUT 1800)
endif()
