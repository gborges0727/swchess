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
target_link_libraries(anx_oracle_test PRIVATE swchess_assets swchess_export)
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

# The second oracle covers the piece DLLs, the piece sheets and the sound
# resources. tests/assets_dump.py writes it with the Python extractor, and
# assets_oracle_test rebuilds the same lines from src/assets.
set(SWCHESS_ASSET_ORACLE "${CMAKE_BINARY_DIR}/assets_oracle.txt")

add_custom_command(
  OUTPUT "${SWCHESS_ASSET_ORACLE}"
  COMMAND ${Python3_EXECUTABLE} -B "${CMAKE_SOURCE_DIR}/tests/assets_dump.py"
          --cd "${SWCHESS_CD_DIR}" --out "${SWCHESS_ASSET_ORACLE}"
  DEPENDS "${CMAKE_SOURCE_DIR}/tests/assets_dump.py"
          "${CMAKE_SOURCE_DIR}/tools/extract/ne.py"
          "${CMAKE_SOURCE_DIR}/tools/extract/dib.py"
          "${CMAKE_SOURCE_DIR}/tools/extract/ini.py"
          "${CMAKE_SOURCE_DIR}/tools/extract/audio.py"
          "${CMAKE_SOURCE_DIR}/tools/extract/sheets.py"
          "${CMAKE_SOURCE_DIR}/tools/extract/pieces.py"
          "${CMAKE_SOURCE_DIR}/tools/reference/anx.py"
  COMMENT "Decoding the piece DLLs, sheets and sounds with the Python extractor"
  VERBATIM
)
add_custom_target(assets_oracle ALL DEPENDS "${SWCHESS_ASSET_ORACLE}")

add_executable(assets_oracle_test assets_oracle_test.cpp)
target_link_libraries(assets_oracle_test PRIVATE swchess_assets swchess_export)
target_compile_options(assets_oracle_test PRIVATE -Wall -Wextra)
add_dependencies(assets_oracle_test assets_oracle)

set(SWCHESS_PIECES AT BF C3 CB DV EM LO LS R2 SP ST YO)
foreach(piece IN LISTS SWCHESS_PIECES)
  add_test(NAME piece.${piece}
           COMMAND assets_oracle_test "${SWCHESS_ASSET_ORACLE}" "${SWCHESS_CD_DIR}"
                   piece "${piece}")
endforeach()

set(SWCHESS_SETS 2DSET WHTBTM WHTTOP FACING)
foreach(set IN LISTS SWCHESS_SETS)
  add_test(NAME sheet.${set}
           COMMAND assets_oracle_test "${SWCHESS_ASSET_ORACLE}" "${SWCHESS_CD_DIR}"
                   cells "${set}")
endforeach()

# All 110 WAVE records in SWCAUDIO.DLL plus the four loose WAV files.
add_test(NAME audio.waves
         COMMAND assets_oracle_test "${SWCHESS_ASSET_ORACLE}" "${SWCHESS_CD_DIR}" waves)

# 1344 piece bitmaps, 48 sheet cells, and 110 WAVE records under 109 names.
add_test(NAME assets.totals
         COMMAND assets_oracle_test "${SWCHESS_ASSET_ORACLE}" "${SWCHESS_CD_DIR}"
                 totals 1344 48 110 109)

# Every assets/captures/*/resolved.json must match the C++ player's own
# --dump-timeline output exactly. Skips itself if assets/ has not been
# generated with `python3 -m tools.extract`.
add_test(NAME timeline.parity
         COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/timeline_parity.py"
                 $<TARGET_FILE:swchess-viewer> "${CMAKE_SOURCE_DIR}/original/win3x/cd"
                 "${CMAKE_SOURCE_DIR}/assets")

# The case-insensitive CD lookup. It builds its own temporary directory, so it
# needs no CD.
add_executable(cdfs_test "${CMAKE_SOURCE_DIR}/src/assets/cdfs_test.cpp")
target_include_directories(cdfs_test PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_options(cdfs_test PRIVATE ${SWCHESS_WARNINGS})
add_test(NAME assets.cdfs COMMAND cdfs_test)

# How startup picks the CD folder and the artwork folder. It builds its own
# temporary directories and opens no dialog, so it needs no CD and no display.
add_executable(startup_test "${CMAKE_SOURCE_DIR}/tests/startup_test.cpp"
               "${CMAKE_SOURCE_DIR}/src/app/startup.cpp")
target_include_directories(startup_test PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(startup_test PRIVATE swchess_platform SDL3::SDL3)
target_compile_options(startup_test PRIVATE ${SWCHESS_WARNINGS})
add_test(NAME app.startup COMMAND startup_test)

# A copy of the CD with every filename lowercased must render the same frame
# as the CD itself. The test copies the CD, so it only runs when SWCHESS_CD_DIR
# points at a directory that exists.
if(EXISTS "${SWCHESS_CD_DIR}")
  add_test(NAME assets.lowercase_cd
           COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tests/lowercase_cd.py"
                   $<TARGET_FILE:swchess> "${SWCHESS_CD_DIR}"
                   "${CMAKE_SOURCE_DIR}/assets")
endif()
