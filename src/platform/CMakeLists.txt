cmake_minimum_required(VERSION 3.24)

# The per-platform directories the game writes into. The top level build adds
# this directory. Building it on its own needs a project of its own, so
# declare one only in that case:
#
#   cmake -S src/platform -B build-platform -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-platform
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(swchess_platform LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
endif()

if(NOT TARGET SDL3::SDL3)
  find_package(SDL3 CONFIG REQUIRED)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/CompilerFlags.cmake")

add_library(swchess_platform STATIC paths.cpp)
# Headers include as "platform/paths.h", so the include root is src.
target_include_directories(swchess_platform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(swchess_platform PUBLIC SDL3::SDL3)
target_compile_options(swchess_platform PRIVATE ${SWCHESS_WARNINGS})
