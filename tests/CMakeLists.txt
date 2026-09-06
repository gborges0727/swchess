# The oracle file holds one checksum line per distinct ANX record and per BMP,
# written by the Python reference decoder. The C++ test rebuilds those lines
# from src/assets and diffs them.
set(SWCHESS_ORACLE "${CMAKE_BINARY_DIR}/anx_oracle.txt")

file(GLOB SWCHESS_ANX_FILES RELATIVE "${SWCHESS_CD_DIR}" "${SWCHESS_CD_DIR}/*.ANX")
file(GLOB SWCHESS_BMP_FILES RELATIVE "${SWCHESS_CD_DIR}" "${SWCHESS_CD_DIR}/*.BMP")
list(SORT SWCHESS_ANX_FILES)
list(SORT SWCHESS_BMP_FILES)

add_custom_command(
  OUTPUT "${SWCHESS_ORACLE}"
  COMMAND ${Python3_EXECUTABLE} -B "${CMAKE_SOURCE_DIR}/tests/anx_dump.py"
          --cd "${SWCHESS_CD_DIR}" --out "${SWCHESS_ORACLE}"
  DEPENDS "${CMAKE_SOURCE_DIR}/tests/anx_dump.py"
          "${CMAKE_SOURCE_DIR}/tools/reference/anx.py"
  COMMENT "Decoding the CD with the Python reference decoder"
  VERBATIM
)
add_custom_target(anx_oracle ALL DEPENDS "${SWCHESS_ORACLE}")

add_executable(anx_oracle_test anx_oracle_test.cpp)
target_link_libraries(anx_oracle_test PRIVATE swchess_assets)
target_compile_options(anx_oracle_test PRIVATE -Wall -Wextra)
add_dependencies(anx_oracle_test anx_oracle)

foreach(anx IN LISTS SWCHESS_ANX_FILES)
  add_test(NAME anx.${anx}
           COMMAND anx_oracle_test "${SWCHESS_ORACLE}" "${SWCHESS_CD_DIR}" anx "${anx}")
endforeach()

foreach(bmp IN LISTS SWCHESS_BMP_FILES)
  add_test(NAME bmp.${bmp}
           COMMAND anx_oracle_test "${SWCHESS_ORACLE}" "${SWCHESS_CD_DIR}" bmp "${bmp}")
endforeach()

# The plan counts 4799 distinct records and 5423 timeline entries on the CD.
add_test(NAME anx.corpus_totals
         COMMAND anx_oracle_test "${SWCHESS_ORACLE}" "${SWCHESS_CD_DIR}" totals 4799 5423)

# The headless dump path must work on a machine with no display.
add_test(NAME viewer.dump_frame
         COMMAND swchess-viewer --cd "${SWCHESS_CD_DIR}" --capture BBWB --bg SPACE256
                 --dump-frame 39 "${CMAKE_BINARY_DIR}/bbwb_39.ppm")
