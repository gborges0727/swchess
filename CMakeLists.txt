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

add_executable(swchess-viewer src/app/viewer.cpp src/app/review.cpp)
target_link_libraries(swchess-viewer PRIVATE swchess_render swchess_anim swchess_audio swchess_text
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

# The playable game. It needs the chess, board, anim, audio and text modules,
# so it comes after all of them.
add_subdirectory(src/game)

# The title sequence, the menu button row and the settings file.
add_subdirectory(src/ui)
add_subdirectory(src/save)

# Two humans playing on the animated board.
add_executable(swchess src/app/game_main.cpp)
target_link_libraries(swchess PRIVATE swchess_game SDL3::SDL3)
target_compile_options(swchess PRIVATE -Wall -Wextra)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

# The macOS application bundle. It builds the same sources as swchess and
# wraps them in Star Wars Chess.app. The bundle carries no original game
# files, so the launcher inside it asks for the CD folder on first run.
if(APPLE)
  set(SWCHESS_BUNDLE_NAME "Star Wars Chess")
  set(SWCHESS_BUNDLE_ID "com.fairline.swchess")
  set(SWCHESS_BUNDLE_VERSION "0.6.0")
  set(SWCHESS_LAUNCHER_NAME "swchess-launcher")
  set(SWCHESS_GAME_BINARY "${SWCHESS_BUNDLE_NAME}")
  set(SWCHESS_ICON_NAME "StarWarsChess")
  set(SWCHESS_SOURCE_DIR "${CMAKE_SOURCE_DIR}")

  set(SWCHESS_ICNS "${CMAKE_BINARY_DIR}/${SWCHESS_ICON_NAME}.icns")
  add_custom_command(
    OUTPUT "${SWCHESS_ICNS}"
    COMMAND "${CMAKE_SOURCE_DIR}/packaging/make-icns.sh"
            "${CMAKE_SOURCE_DIR}/assets/ui/STLGO16.png"
            "${SWCHESS_ICNS}"
            "${CMAKE_BINARY_DIR}/icon-work"
    DEPENDS "${CMAKE_SOURCE_DIR}/packaging/make-icns.sh"
    COMMENT "Building the application icon"
    VERBATIM)
  set_source_files_properties("${SWCHESS_ICNS}" PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources
    GENERATED TRUE)

  configure_file("${CMAKE_SOURCE_DIR}/packaging/Info.plist.in"
                 "${CMAKE_BINARY_DIR}/Info.plist" @ONLY)
  configure_file("${CMAKE_SOURCE_DIR}/packaging/launcher.sh.in"
                 "${CMAKE_BINARY_DIR}/${SWCHESS_LAUNCHER_NAME}" @ONLY)

  add_executable(StarWarsChess MACOSX_BUNDLE src/app/game_main.cpp "${SWCHESS_ICNS}")
  target_link_libraries(StarWarsChess PRIVATE swchess_game SDL3::SDL3)
  target_compile_options(StarWarsChess PRIVATE -Wall -Wextra)
  set_target_properties(StarWarsChess PROPERTIES
    OUTPUT_NAME "${SWCHESS_BUNDLE_NAME}"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_BINARY_DIR}/Info.plist"
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "@executable_path/../Frameworks")

  set(SWCHESS_APP "$<TARGET_BUNDLE_DIR:StarWarsChess>")
  add_custom_command(TARGET StarWarsChess POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy
            "${CMAKE_BINARY_DIR}/${SWCHESS_LAUNCHER_NAME}"
            "${SWCHESS_APP}/Contents/MacOS/${SWCHESS_LAUNCHER_NAME}"
    COMMAND chmod +x "${SWCHESS_APP}/Contents/MacOS/${SWCHESS_LAUNCHER_NAME}"
    COMMAND "${CMAKE_COMMAND}"
            "-DAPP_BINARY=${SWCHESS_APP}/Contents/MacOS/${SWCHESS_GAME_BINARY}"
            "-DFRAMEWORKS_DIR=${SWCHESS_APP}/Contents/Frameworks"
            -P "${CMAKE_SOURCE_DIR}/cmake/BundleDylibs.cmake"
    COMMENT "Copying SDL3 into the bundle and fixing its load paths"
    VERBATIM)
endif()
