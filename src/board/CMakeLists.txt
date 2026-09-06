cmake_minimum_required(VERSION 3.24)

# The board module. The top level build adds this directory. Building it on
# its own needs a project of its own, so declare one only in that case:
#
#   cmake -S src/board -B build-board -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-board
#   ctest --test-dir build-board --output-on-failure
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(swchess_board LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  enable_testing()
endif()

get_filename_component(SWCHESS_BOARD_REPO_ROOT
                       "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

if(NOT SWCHESS_CD_DIR)
  set(SWCHESS_CD_DIR "${SWCHESS_BOARD_REPO_ROOT}/original/win3x/cd")
endif()

# A standalone build has none of the sibling targets, so build the ones this
# module links against from the same sources the top level project uses.
if(NOT TARGET swchess_assets)
  add_library(swchess_assets STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/anx.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/bmp.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/ne.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/ini.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/piece_dll.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/sheet.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/wav.cpp"
  )
  target_include_directories(swchess_assets PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
  target_compile_options(swchess_assets PRIVATE -Wall -Wextra)
endif()
if(NOT TARGET swchess_render)
  add_library(swchess_render STATIC "${CMAKE_CURRENT_SOURCE_DIR}/../render/compositor.cpp")
  target_include_directories(swchess_render PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
  target_link_libraries(swchess_render PUBLIC swchess_assets)
  target_compile_options(swchess_render PRIVATE -Wall -Wextra)
endif()
if(NOT TARGET swchess_chess)
  add_library(swchess_chess STATIC "${CMAKE_CURRENT_SOURCE_DIR}/../chess/chess.cpp")
  target_include_directories(swchess_chess PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../chess")
endif()

add_library(swchess_board STATIC
  geometry.cpp
  placement.cpp
  board_view.cpp
)
# Headers include as "board/geometry.h", so the include root is src.
target_include_directories(swchess_board PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(swchess_board PUBLIC swchess_render swchess_chess)
target_compile_options(swchess_board PRIVATE -Wall -Wextra)

add_executable(board_test board_test.cpp)
target_link_libraries(board_test PRIVATE swchess_board)
target_compile_options(board_test PRIVATE -Wall -Wextra)

add_test(NAME board_test COMMAND board_test "${SWCHESS_CD_DIR}")
set_tests_properties(board_test PROPERTIES TIMEOUT 300)

# The viewer draws a board, and this directory is added after that target
# exists. The viewer already names swchess_render, and swchess_board brings
# the same archive along, so drop the direct name to keep the linker from
# warning about the repeat.
if(TARGET swchess-viewer)
  get_target_property(SWCHESS_VIEWER_LIBS swchess-viewer LINK_LIBRARIES)
  list(REMOVE_ITEM SWCHESS_VIEWER_LIBS swchess_render)
  set_target_properties(swchess-viewer PROPERTIES LINK_LIBRARIES "${SWCHESS_VIEWER_LIBS}")
  target_link_libraries(swchess-viewer PRIVATE swchess_board)
endif()
