# Cross-compiles a 64-bit Windows build on macOS or Linux with MinGW-w64.
#
#   cmake --preset windows-mingw-release -B build-win
#
# Homebrew's mingw-w64 formula installs the three programs below into
# /opt/homebrew/bin, so a Mac needs nothing else. A Linux machine gets them
# from its own mingw-w64 package.
#
# The link flags make the exe self-contained. Without -static the program
# loads libgcc_s_seh-1.dll, libstdc++-6.dll and libwinpthread-1.dll from the
# compiler's own directory, and none of those exist on a player's Windows.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(SWCHESS_MINGW_PREFIX x86_64-w64-mingw32 CACHE STRING "MinGW-w64 program prefix")

set(CMAKE_C_COMPILER ${SWCHESS_MINGW_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${SWCHESS_MINGW_PREFIX}-g++)
set(CMAKE_RC_COMPILER ${SWCHESS_MINGW_PREFIX}-windres)

# Look for programs on the host, and for headers and libraries only inside
# the MinGW sysroot. Without this CMake finds the Mac's own zlib and its
# headers and the link fails on mismatched object formats.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-static")
