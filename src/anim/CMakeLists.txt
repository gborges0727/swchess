# The capture animation module. It builds inside the top-level project and on
# its own:
#
#   cmake -S src/anim -B build-anim -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-anim
#   ctest --test-dir build-anim --output-on-failure
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.28)
  project(swchess_anim LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
  endif()
  include(CTest)
endif()

# The warning flags for this compiler. A build of this module on its own does
# not inherit them from the top level project.
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/CompilerFlags.cmake")

# The repo root, two directories above this one.
get_filename_component(SWCHESS_ANIM_REPO_ROOT
                       "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

if(NOT SWCHESS_CD_DIR)
  set(SWCHESS_CD_DIR "${SWCHESS_ANIM_REPO_ROOT}/original/win3x/cd")
endif()

# Where tools/extract writes the decoded artwork and tools/interp writes the
# 60 frames per second sequences.
if(NOT SWCHESS_ASSETS_DIR)
  set(SWCHESS_ASSETS_DIR "${SWCHESS_ANIM_REPO_ROOT}/assets")
endif()

# interp.cpp reads manifest.json, and png_read.cpp inflates the frame PNGs.
# The top level build already made both targets in cmake/Dependencies.cmake.
if(NOT TARGET nlohmann_json::nlohmann_json)
  find_package(nlohmann_json CONFIG REQUIRED)
endif()
if(NOT TARGET ZLIB::ZLIB)
  find_package(ZLIB REQUIRED)
endif()

# interp.cpp runs the frame work on several std::thread workers.
find_package(Threads REQUIRED)

# A standalone build has no swchess_assets target, so build the decoders it
# needs from the same sources the top-level project uses.
if(NOT TARGET swchess_assets)
  add_library(swchess_assets STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/anx.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/bmp.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/ne.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/ini.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/piece_dll.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/sheet.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/wav.cpp"
  )
  target_include_directories(swchess_assets PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
  target_compile_options(swchess_assets PRIVATE ${SWCHESS_WARNINGS})
endif()

add_library(swchess_anim STATIC
  capture.cpp
  interp.cpp
  player.cpp
  png_read.cpp
  walk.cpp
)
# Headers include as "anim/capture.h", so the include root is src.
target_include_directories(swchess_anim PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(swchess_anim PUBLIC swchess_assets nlohmann_json::nlohmann_json
                      ZLIB::ZLIB Threads::Threads)
target_compile_options(swchess_anim PRIVATE ${SWCHESS_WARNINGS})

# Both tests read the original CD, which a checkout does not carry.
if(BUILD_TESTING AND EXISTS "${SWCHESS_CD_DIR}")
  add_executable(anim_test anim_test.cpp)
  target_link_libraries(anim_test PRIVATE swchess_anim)
  target_compile_options(anim_test PRIVATE ${SWCHESS_WARNINGS})

  add_test(NAME anim_test COMMAND anim_test "${SWCHESS_CD_DIR}" "${SWCHESS_ASSETS_DIR}")
  set_tests_properties(anim_test PROPERTIES TIMEOUT 300)

  add_executable(walk_test walk_test.cpp)
  target_link_libraries(walk_test PRIVATE swchess_anim)
  target_compile_options(walk_test PRIVATE ${SWCHESS_WARNINGS})

  add_test(NAME walk_test COMMAND walk_test "${SWCHESS_CD_DIR}" "${SWCHESS_ASSETS_DIR}")
  set_tests_properties(walk_test PROPERTIES TIMEOUT 300)
endif()
