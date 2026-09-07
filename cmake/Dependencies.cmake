# Finds SDL3, nlohmann-json and zlib for every target in the project.
#
# SWCHESS_VENDOR_DEPS decides where they come from. When it is ON the build
# downloads pinned source archives and compiles them, so a checkout builds on
# a machine that has none of the three installed, and a released binary loads
# nothing from Homebrew or from a distribution package. When it is OFF the
# build calls find_package and uses whatever the machine already has, which
# is faster and is what a developer wants day to day.
#
# Either way the targets come out named SDL3::SDL3,
# nlohmann_json::nlohmann_json and ZLIB::ZLIB, so nothing else in the project
# has to know which path ran.

option(SWCHESS_VENDOR_DEPS "build SDL3, nlohmann-json and zlib from pinned source" ON)

if(NOT SWCHESS_VENDOR_DEPS)
  find_package(SDL3 CONFIG REQUIRED)
  find_package(nlohmann_json CONFIG REQUIRED)
  find_package(ZLIB REQUIRED)
  return()
endif()

include(FetchContent)

# SDL 3.4.16, the release tagged release-3.4.16. The archive below is the
# source tarball GitHub publishes for that tag.
#
# The game links SDL statically. One static SDL removes the whole business of
# copying a dylib into the application, rewriting its load paths and signing
# it again, and it removes the matching DLL and shared object work on the
# other two systems.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
# SDL reaches gamepads over libusb when it finds one at configure time. On this
# machine that is Homebrew's libusb, which a released binary must not depend on.
# SDL opens it by name at run time rather than linking it, so the load commands
# stay clean either way, but turning it off keeps the release honest.
set(SDL_HIDAPI_LIBUSB OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
  URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz
  URL_HASH SHA256=7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# nlohmann-json 3.11.3. src/anim reads manifest.json with it and src/save
# writes the native saved game format with it.
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(SDL3 nlohmann_json)

# SDL names the alias itself when either variant is built, but say so out loud
# in case a future release stops doing that.
if(NOT TARGET SDL3::SDL3)
  add_library(SDL3::SDL3 ALIAS SDL3-static)
endif()

# macOS and every Linux distribution ship zlib and its headers, and the copy
# they ship is the one the rest of the system already loads. Use it. Windows
# ships no zlib, so build the pinned source there, and do the same anywhere
# else the header is missing.
if(NOT WIN32)
  find_package(ZLIB QUIET)
endif()

if(NOT TARGET ZLIB::ZLIB)
  set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(zlib
    URL https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
    URL_HASH SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(zlib)
  # zlib's own CMake names no target in a namespace, and it writes zconf.h
  # into the build directory rather than beside zlib.h.
  target_include_directories(zlibstatic PUBLIC
    "${zlib_SOURCE_DIR}" "${zlib_BINARY_DIR}")
  add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif()
