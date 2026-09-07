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
  include(CTest)
endif()

# The warning flags for this compiler. A build of this module on its own does
# not inherit them from the top level project.
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/CompilerFlags.cmake")

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
  target_compile_options(swchess_assets PRIVATE ${SWCHESS_WARNINGS})
endif()
if(NOT TARGET swchess_render)
  add_library(swchess_render STATIC "${CMAKE_CURRENT_SOURCE_DIR}/../render/compositor.cpp")
  target_include_directories(swchess_render PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
  target_link_libraries(swchess_render PUBLIC swchess_assets)
  target_compile_options(swchess_render PRIVATE ${SWCHESS_WARNINGS})
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
target_compile_options(swchess_board PRIVATE ${SWCHESS_WARNINGS})

# The test draws the real board, so it reads the original CD.
if(BUILD_TESTING AND EXISTS "${SWCHESS_CD_DIR}")
  add_executable(board_test board_test.cpp)
  target_link_libraries(board_test PRIVATE swchess_board)
  target_compile_options(board_test PRIVATE ${SWCHESS_WARNINGS})

  add_test(NAME board_test COMMAND board_test "${SWCHESS_CD_DIR}")
  set_tests_properties(board_test PROPERTIES TIMEOUT 300)
endif()

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
