cmake_minimum_required(VERSION 3.24)

# The top level build adds this directory. Building it on its own needs a
# project of its own, so declare one only in that case.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(swchess_chess LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  include(CTest)
endif()

add_library(swchess_chess STATIC chess.cpp)
target_compile_features(swchess_chess PUBLIC cxx_std_20)
target_include_directories(swchess_chess PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# The rules test counts its own positions and reads nothing from the CD, so
# it runs on any checkout.
if(BUILD_TESTING)
  add_executable(chess_perft_test perft_test.cpp)
  target_link_libraries(chess_perft_test PRIVATE swchess_chess)

  add_test(NAME chess_perft_test COMMAND chess_perft_test)
  set_tests_properties(chess_perft_test PROPERTIES TIMEOUT 300)
endif()
