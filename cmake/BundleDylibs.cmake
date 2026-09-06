# Copies every non-system dynamic library the game links against into the
# bundle and rewrites the load paths to point at Contents/Frameworks. After
# this runs the app starts on a Mac that has no Homebrew.
#
# Run it as a script:
#
#   cmake -DAPP_BINARY=... -DFRAMEWORKS_DIR=... -P cmake/BundleDylibs.cmake
#
# Anything under /usr/lib or /System belongs to macOS itself, so it stays
# where it is. zlib comes from the SDK that way and is never copied.

if(NOT APP_BINARY OR NOT FRAMEWORKS_DIR)
  message(FATAL_ERROR "BundleDylibs.cmake needs APP_BINARY and FRAMEWORKS_DIR")
endif()

file(MAKE_DIRECTORY "${FRAMEWORKS_DIR}")

# Reads the install names a Mach-O file loads and returns the ones macOS does
# not provide.
function(swchess_foreign_deps target out_var)
  execute_process(COMMAND otool -L "${target}"
                  OUTPUT_VARIABLE listing
                  ERROR_QUIET
                  RESULT_VARIABLE status)
  set(found "")
  if(NOT status EQUAL 0)
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()
  string(REPLACE "\n" ";" lines "${listing}")
  foreach(line IN LISTS lines)
    if(line MATCHES "^\t([^ ]+) \\(compatibility")
      set(dep "${CMAKE_MATCH_1}")
      if(dep MATCHES "^/usr/lib/" OR dep MATCHES "^/System/" OR dep MATCHES "^@")
        continue()
      endif()
      list(APPEND found "${dep}")
    endif()
  endforeach()
  set(${out_var} "${found}" PARENT_SCOPE)
endfunction()

set(pending "${APP_BINARY}")
set(copied "")

while(pending)
  list(POP_FRONT pending current)
  swchess_foreign_deps("${current}" deps)
  foreach(dep IN LISTS deps)
    get_filename_component(name "${dep}" NAME)
    set(destination "${FRAMEWORKS_DIR}/${name}")
    if(NOT EXISTS "${destination}")
      get_filename_component(real "${dep}" REALPATH)
      if(NOT EXISTS "${real}")
        message(FATAL_ERROR "the game links against ${dep}, which is missing")
      endif()
      file(COPY_FILE "${real}" "${destination}")
      execute_process(COMMAND chmod u+w "${destination}")
      execute_process(COMMAND install_name_tool -id "@rpath/${name}" "${destination}"
                      ERROR_QUIET)
      list(APPEND pending "${destination}")
      list(APPEND copied "${name}")
      message(STATUS "bundled ${name}")
    endif()
    execute_process(COMMAND install_name_tool -change "${dep}" "@rpath/${name}" "${current}"
                    ERROR_QUIET)
  endforeach()
endwhile()

# Rewriting load commands breaks the signature the linker wrote, so sign the
# binary and every library again without a certificate.
foreach(name IN LISTS copied)
  execute_process(COMMAND codesign --force --sign - "${FRAMEWORKS_DIR}/${name}"
                  OUTPUT_QUIET ERROR_QUIET)
endforeach()
execute_process(COMMAND codesign --force --sign - "${APP_BINARY}" OUTPUT_QUIET ERROR_QUIET)
