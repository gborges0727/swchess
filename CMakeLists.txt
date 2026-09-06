cmake_minimum_required(VERSION 3.28)
project(swchess LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Where the read-only Windows release lives. Tests read from here and never
# write into it.
set(SWCHESS_CD_DIR "${CMAKE_SOURCE_DIR}/original/win3x/cd"
    CACHE PATH "directory holding the original CD files")

find_package(SDL3 CONFIG REQUIRED)
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)

# Decoders for the original artwork. No SDL and no windowing.
add_library(swchess_assets STATIC
  src/assets/anx.cpp
  src/assets/bmp.cpp
  src/assets/ne.cpp
  src/assets/ini.cpp
  src/assets/piece_dll.cpp
  src/assets/sheet.cpp
  src/assets/wav.cpp
)
target_include_directories(swchess_assets PUBLIC src)
target_compile_options(swchess_assets PRIVATE -Wall -Wextra)

# Placement and software compositing shared by the window and the PPM dump.
add_library(swchess_render STATIC
  src/render/compositor.cpp
)
target_include_directories(swchess_render PUBLIC src)
target_link_libraries(swchess_render PUBLIC swchess_assets)
target_compile_options(swchess_render PRIVATE -Wall -Wextra)

add_executable(swchess-viewer src/app/viewer.cpp)
target_link_libraries(swchess-viewer PRIVATE swchess_render swchess_anim swchess_audio
                      SDL3::SDL3)
target_compile_options(swchess-viewer PRIVATE -Wall -Wextra)

# The chess rules module arrives separately. Build it when it is present.
if(EXISTS ${CMAKE_SOURCE_DIR}/src/chess/CMakeLists.txt)
  add_subdirectory(src/chess)
endif()

# The audio module arrives separately too. Build it when it is present.
if(EXISTS ${CMAKE_SOURCE_DIR}/src/audio/CMakeLists.txt)
  add_subdirectory(src/audio)
endif()

# The capture animation module resolves timelines and plays them. Its own
# test registers with CTest, so testing turns on before the subdirectory.
include(CTest)
add_subdirectory(src/anim)
add_subdirectory(src/board)

# The text module reads the language string tables and the two bitmap fonts.
add_subdirectory(src/text)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
