# The text module. It reads the four language string tables and the two bitmap
# fonts, and draws a stored string to RGBA. It builds inside the top-level
# project and on its own:
#
#   cmake -S src/text -B build-text -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-text
#   ctest --test-dir build-text --output-on-failure
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.28)
  project(swchess_text LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
  endif()
  enable_testing()
endif()

# The repo root, two directories above this one.
get_filename_component(SWCHESS_TEXT_REPO_ROOT
                       "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

if(NOT SWCHESS_CD_DIR)
  set(SWCHESS_CD_DIR "${SWCHESS_TEXT_REPO_ROOT}/original/win3x/cd")
endif()

# A standalone build has no swchess_assets target, so build the two decoders
# this module needs from the same sources the top-level project uses.
if(NOT TARGET swchess_assets)
  add_library(swchess_text_assets STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/bmp.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/ne.cpp"
  )
  target_include_directories(swchess_text_assets PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
  target_compile_options(swchess_text_assets PRIVATE -Wall -Wextra)
  set(SWCHESS_TEXT_ASSETS swchess_text_assets)
else()
  set(SWCHESS_TEXT_ASSETS swchess_assets)
endif()

add_library(swchess_text STATIC
  font.cpp
  strings.cpp
)
# Headers include as "text/font.h", so the include root is src.
target_include_directories(swchess_text PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(swchess_text PUBLIC ${SWCHESS_TEXT_ASSETS})
target_compile_options(swchess_text PRIVATE -Wall -Wextra)

add_executable(text_test text_test.cpp)
target_link_libraries(text_test PRIVATE swchess_text)
target_compile_options(text_test PRIVATE -Wall -Wextra)

# The test checks its own layout against tools/fonts/legfont.py through
# src/text/width_oracle.py. Without a Python interpreter it skips that one
# check and runs the rest.
if(NOT Python3_EXECUTABLE)
  find_package(Python3 3.10 COMPONENTS Interpreter)
endif()

add_test(NAME text_test COMMAND text_test "${SWCHESS_CD_DIR}" "${CMAKE_BINARY_DIR}"
         "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/width_oracle.py")
set_tests_properties(text_test PROPERTIES TIMEOUT 300)
