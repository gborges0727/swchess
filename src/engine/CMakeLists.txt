# The computer opponent. It builds inside the top-level project and on its
# own:
#
#   cmake -S src/engine -B build-engine -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-engine
#   ctest --test-dir build-engine --output-on-failure
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.24)
  project(swchess_engine LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
  endif()
  enable_testing()
endif()

# The top-level project calls include(CTest) after it adds this directory, so
# testing is still off here. Turn it on so add_test below registers.
enable_testing()

get_filename_component(SWCHESS_ENGINE_REPO_ROOT
                       "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

if(NOT SWCHESS_CD_DIR)
  set(SWCHESS_CD_DIR "${SWCHESS_ENGINE_REPO_ROOT}/original/win3x/cd")
endif()

# A standalone build has no swchess_chess target, so build the rules module
# from the same source the top-level project uses.
if(NOT TARGET swchess_chess)
  add_library(swchess_chess STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../chess/chess.cpp")
  target_compile_features(swchess_chess PUBLIC cxx_std_20)
  target_include_directories(swchess_chess PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../chess")
endif()

add_library(swchess_engine STATIC
  random_engine.cpp
  original/book.cpp
  original/cmp.cpp
  original/eval.cpp
  original/search.cpp
  original/original_engine.cpp
)
# Headers include as "engine/engine.h", so the include root is src.
target_include_directories(swchess_engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(swchess_engine PUBLIC swchess_chess)
target_compile_options(swchess_engine PRIVATE -Wall -Wextra)

find_package(Threads REQUIRED)
target_link_libraries(swchess_engine PUBLIC Threads::Threads)

add_executable(engine_test engine_test.cpp)
target_link_libraries(engine_test PRIVATE swchess_engine)
target_compile_options(engine_test PRIVATE -Wall -Wextra)

add_test(NAME engine_test COMMAND engine_test "${SWCHESS_CD_DIR}")
set_tests_properties(engine_test PROPERTIES TIMEOUT 300)
