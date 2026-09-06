# The playable game: the rules, the board, the walk and the capture film tied
# together in one session. The top level build adds this directory. Building
# it on its own needs a project of its own:
#
#   cmake -S src/game -B build-game -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-game
#   ctest --test-dir build-game --output-on-failure
cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(swchess_game LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
  endif()
  enable_testing()
  get_filename_component(SWCHESS_GAME_REPO_ROOT
                         "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
  if(NOT SWCHESS_CD_DIR)
    set(SWCHESS_CD_DIR "${SWCHESS_GAME_REPO_ROOT}/original/win3x/cd")
  endif()
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../anim" anim)
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../board" board)
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../audio" audio)
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../text" text)
endif()

add_library(swchess_game STATIC
  session.cpp
  script.cpp
)
# Headers include as "game/session.h", so the include root is src.
target_include_directories(swchess_game PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(swchess_game PUBLIC swchess_anim swchess_board swchess_audio swchess_text)
target_compile_options(swchess_game PRIVATE -Wall -Wextra)

add_executable(game_test game_test.cpp)
target_link_libraries(game_test PRIVATE swchess_game)
target_compile_options(game_test PRIVATE -Wall -Wextra)

# Where tools/interp writes its 60 frames per second capture frames. The test
# skips its enhanced cadence checks when BBWB has no manifest there yet.
if(NOT SWCHESS_ASSETS_DIR)
  if(DEFINED SWCHESS_GAME_REPO_ROOT)
    set(SWCHESS_ASSETS_DIR "${SWCHESS_GAME_REPO_ROOT}/assets")
  else()
    set(SWCHESS_ASSETS_DIR "${CMAKE_SOURCE_DIR}/assets")
  endif()
endif()

add_test(NAME game_test COMMAND game_test "${SWCHESS_CD_DIR}" "${CMAKE_BINARY_DIR}"
         "${SWCHESS_ASSETS_DIR}")
# Nothing here needs a sound card. The mixer falls back to SDL's dummy driver
# on its own, and asking for it outright keeps a build machine quiet.
set_tests_properties(game_test PROPERTIES
  ENVIRONMENT "SDL_AUDIO_DRIVER=dummy"
  TIMEOUT 600)
