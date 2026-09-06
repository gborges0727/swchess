# The audio module. It builds inside the top-level project and on its own:
#
#   cmake -S src/audio -B build-audio -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-audio
#   ctest --test-dir build-audio --output-on-failure
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.28)
  project(swchess_audio LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
  endif()
  enable_testing()
endif()

if(NOT TARGET SDL3::SDL3)
  find_package(SDL3 CONFIG REQUIRED)
endif()

# The repo root, two directories above this one. The test reads WAV files from
# assets/audio underneath it.
get_filename_component(SWCHESS_AUDIO_REPO_ROOT
                       "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

add_library(swchess_audio STATIC
  audio.cpp
  wav_load.cpp
)
# Headers include as "audio/audio.h", so the include root is src.
target_include_directories(swchess_audio PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(swchess_audio PUBLIC SDL3::SDL3)
target_compile_options(swchess_audio PRIVATE -Wall -Wextra)

add_executable(audio_test audio_test.cpp)
target_link_libraries(audio_test PRIVATE swchess_audio)
target_compile_definitions(audio_test
  PRIVATE SWCHESS_REPO_ROOT="${SWCHESS_AUDIO_REPO_ROOT}")
target_compile_options(audio_test PRIVATE -Wall -Wextra)

add_test(NAME audio_test COMMAND audio_test "${SWCHESS_AUDIO_REPO_ROOT}")
# Nothing here needs a sound card. Ask SDL for the dummy driver when the
# environment says there is no audio, and keep the test off the speakers on a
# build machine.
set_tests_properties(audio_test PROPERTIES TIMEOUT 60)

# The same checks again with SDL forced onto the dummy driver. This is the
# path a build machine and an SSH session take, and it must pass there too.
add_test(NAME audio_test_dummy COMMAND audio_test "${SWCHESS_AUDIO_REPO_ROOT}")
set_tests_properties(audio_test_dummy PROPERTIES
  ENVIRONMENT "SDL_AUDIO_DRIVER=dummy"
  TIMEOUT 60)
