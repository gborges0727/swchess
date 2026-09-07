# The native asset exporter. It builds inside the top level project and on its
# own:
#
#   cmake -S src/export -B build-export -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-export
#   ctest --test-dir build-export --output-on-failure
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.28)
  project(swchess_export LANGUAGES CXX)
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
get_filename_component(SWCHESS_EXPORT_REPO_ROOT
                       "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

if(NOT SWCHESS_CD_DIR)
  set(SWCHESS_CD_DIR "${SWCHESS_EXPORT_REPO_ROOT}/original/win3x/cd")
endif()
if(NOT SWCHESS_ASSETS_DIR)
  set(SWCHESS_ASSETS_DIR "${SWCHESS_EXPORT_REPO_ROOT}/assets")
endif()

# json_write.cpp builds the manifests with nlohmann-json, and png_write.cpp
# deflates the pixel rows with zlib.
if(NOT TARGET nlohmann_json::nlohmann_json)
  find_package(nlohmann_json CONFIG REQUIRED)
endif()
if(NOT TARGET ZLIB::ZLIB)
  find_package(ZLIB REQUIRED)
endif()

# A standalone build has no swchess_assets target, so build the decoders the
# exporter needs from the same sources the top level project uses.
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

add_library(swchess_export STATIC
  extract.cpp
  json_write.cpp
  png_write.cpp
  sha256.cpp
  text.cpp
)
# Headers include as "export/extract.h", so the include root is src.
target_include_directories(swchess_export PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(swchess_export PUBLIC swchess_assets nlohmann_json::nlohmann_json
                      ZLIB::ZLIB)
target_compile_options(swchess_export PRIVATE ${SWCHESS_WARNINGS})

add_executable(swchess-extract extract_main.cpp)
target_link_libraries(swchess-extract PRIVATE swchess_export)
target_compile_options(swchess-extract PRIVATE ${SWCHESS_WARNINGS})

# The oracle test runs the helper against the real CD and diffs its output
# against the cache the Python tool wrote. Both of those are private files that
# a checkout does not carry, so the test only registers when they are here.
if(BUILD_TESTING AND EXISTS "${SWCHESS_CD_DIR}" AND EXISTS "${SWCHESS_ASSETS_DIR}/catalog.json")
  add_test(NAME export.oracle
           COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/export_oracle_test.sh"
                   "$<TARGET_FILE:swchess-extract>" "${SWCHESS_CD_DIR}"
                   "${SWCHESS_ASSETS_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/oracle")
  set_tests_properties(export.oracle PROPERTIES TIMEOUT 1800)
endif()
