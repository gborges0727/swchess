cmake_minimum_required(VERSION 3.24)

# The saved game module. The top level build adds this directory. Building it
# on its own needs a project of its own, so declare one only in that case:
#
#   cmake -S src/save -B build-save -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-save
#   ctest --test-dir build-save --output-on-failure
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(swchess_save LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  enable_testing()
endif()

get_filename_component(SWCHESS_SAVE_REPO_ROOT
                       "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

if(NOT SWCHESS_CD_DIR)
  set(SWCHESS_CD_DIR "${SWCHESS_SAVE_REPO_ROOT}/original/win3x/cd")
endif()

find_package(nlohmann_json CONFIG REQUIRED)

# A standalone build has no rules module target, so build it from the same
# source the top level project uses.
if(NOT TARGET swchess_chess)
  add_library(swchess_chess STATIC "${CMAKE_CURRENT_SOURCE_DIR}/../chess/chess.cpp")
  target_compile_features(swchess_chess PUBLIC cxx_std_20)
  target_include_directories(swchess_chess PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../chess")
endif()

add_library(swchess_save STATIC cmg.cpp native.cpp)
# Headers include as "save/cmg.h", so the include root is src.
target_include_directories(swchess_save PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(swchess_save PUBLIC swchess_chess nlohmann_json::nlohmann_json)
target_compile_options(swchess_save PRIVATE -Wall -Wextra)

add_executable(save_test save_test.cpp)
target_link_libraries(save_test PRIVATE swchess_save)
target_compile_options(save_test PRIVATE -Wall -Wextra)

add_test(NAME save_test COMMAND save_test "${SWCHESS_CD_DIR}")
set_tests_properties(save_test PROPERTIES TIMEOUT 120)
