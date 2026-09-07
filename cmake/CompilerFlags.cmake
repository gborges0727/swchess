# Picks the warning flags for whichever compiler is building the project.
#
# GCC and Clang read -Wall -Wextra. MSVC reads /W4, and /utf-8 tells it that
# the sources are UTF-8 rather than the machine's code page. Every target sets
# these through the SWCHESS_WARNINGS variable, so one edit here changes them
# all.
#
# Each module CMakeLists includes this file. A module built on its own is not
# a subdirectory of the top level project, so it cannot inherit the variable.

if(MSVC)
  set(SWCHESS_WARNINGS /W4 /utf-8)
else()
  set(SWCHESS_WARNINGS -Wall -Wextra)
endif()
