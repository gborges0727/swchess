# The user interface module. It draws the title sequence, the row of six menu
# buttons and the status bar beside them, and it reads and writes SWC.INI. It
# builds inside the top-level project and on its own:
#
#   cmake -S src/ui -B build-ui -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-ui
#   ctest --test-dir build-ui --output-on-failure
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.28)
  project(swchess_ui LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
  endif()
  enable_testing()
endif()

# The repo root, two directories above this one.
get_filename_component(SWCHESS_UI_REPO_ROOT
                       "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

if(NOT SWCHESS_CD_DIR)
  set(SWCHESS_CD_DIR "${SWCHESS_UI_REPO_ROOT}/original/win3x/cd")
endif()

# A standalone build has no swchess_text target. Its own CMakeLists builds the
# decoders it needs, so adding it here covers the assets as well.
if(NOT TARGET swchess_text)
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../text" "${CMAKE_CURRENT_BINARY_DIR}/text")
endif()

add_library(swchess_ui STATIC
  buttons.cpp
  draw.cpp
  res_bitmap.cpp
  settings.cpp
  title.cpp
)
# Headers include as "ui/buttons.h", so the include root is src.
target_include_directories(swchess_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(swchess_ui PUBLIC swchess_text)
target_compile_options(swchess_ui PRIVATE -Wall -Wextra)

add_executable(ui_test ui_test.cpp)
target_link_libraries(ui_test PRIVATE swchess_ui)
target_compile_options(ui_test PRIVATE -Wall -Wextra)

# The test writes its PPM files here.
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/ui")

add_test(NAME ui_test COMMAND ui_test "${SWCHESS_CD_DIR}" "${CMAKE_BINARY_DIR}/ui")
set_tests_properties(ui_test PROPERTIES TIMEOUT 300)
