cmake_minimum_required(VERSION 3.20)
if(WIN32)
  project(TerminalC VERSION 1.0.0 LANGUAGES C RC)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  project(TerminalC VERSION 1.0.0 LANGUAGES C OBJC)
else()
  project(TerminalC VERSION 1.0.0 LANGUAGES C)
endif()

# CMake requires a numeric project version. Keep the prerelease identifier in
# one place for the binary banner, CPack artifact, and release-facing tests.
set(TERMINALC_RELEASE_VERSION "1.0.0-beta.1")

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Keep the binary lean: LTO when available, no unused symbols retained on link.
include(CheckIPOSupported)
check_ipo_supported(RESULT terminalc_ipo_ok OUTPUT terminalc_ipo_msg)
option(TERMINALC_ENABLE_LTO "Enable link-time optimization when supported" ON)
option(TERMINALC_PYTHON_WHEEL
  "Place the native executable in a private wheel directory to avoid pip script collisions"
  OFF)

if(TERMINALC_PYTHON_WHEEL)
  set(TERMINALC_NATIVE_INSTALL_DIR terminalc_native)
else()
  set(TERMINALC_NATIVE_INSTALL_DIR bin)
endif()

find_path(GMP_INCLUDE_DIR gmp.h REQUIRED)
find_library(GMP_LIBRARY NAMES gmp libgmp REQUIRED)
find_path(MPFR_INCLUDE_DIR mpfr.h REQUIRED)
find_library(MPFR_LIBRARY NAMES mpfr libmpfr REQUIRED)

# GMP/MPFR allocate memory through their C runtime. Mixing a MinGW compiler
# from one installation with DLL import libraries from another can corrupt
# the heap at shutdown (especially after tensor results such as primes()).
# Fail at configure time instead of producing a binary that may crash.
if(WIN32 AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
  get_filename_component(_terminalc_compiler_bin "${CMAKE_C_COMPILER}" DIRECTORY)
  get_filename_component(_terminalc_compiler_prefix "${_terminalc_compiler_bin}" DIRECTORY)
  get_filename_component(_terminalc_gmp_lib_dir "${GMP_LIBRARY}" DIRECTORY)
  get_filename_component(_terminalc_gmp_prefix "${_terminalc_gmp_lib_dir}" DIRECTORY)
  get_filename_component(_terminalc_mpfr_lib_dir "${MPFR_LIBRARY}" DIRECTORY)
  get_filename_component(_terminalc_mpfr_prefix "${_terminalc_mpfr_lib_dir}" DIRECTORY)
  file(TO_CMAKE_PATH "${_terminalc_compiler_prefix}" _terminalc_compiler_prefix)
  file(TO_CMAKE_PATH "${_terminalc_gmp_prefix}" _terminalc_gmp_prefix)
  file(TO_CMAKE_PATH "${_terminalc_mpfr_prefix}" _terminalc_mpfr_prefix)
  string(TOLOWER "${_terminalc_compiler_prefix}" _terminalc_compiler_prefix)
  string(TOLOWER "${_terminalc_gmp_prefix}" _terminalc_gmp_prefix)
  string(TOLOWER "${_terminalc_mpfr_prefix}" _terminalc_mpfr_prefix)
  if(NOT _terminalc_compiler_prefix STREQUAL _terminalc_gmp_prefix OR
     NOT _terminalc_compiler_prefix STREQUAL _terminalc_mpfr_prefix)
    message(FATAL_ERROR
      "GNU/MinGW compiler and GMP/MPFR must come from the same prefix. "
      "compiler=${_terminalc_compiler_prefix}, "
      "gmp=${_terminalc_gmp_prefix}, mpfr=${_terminalc_mpfr_prefix}. "
      "Do not mix MSYS2 and WinLibs/UCRT runtime families.")
  endif()
endif()

set(TERMINALC_SOURCES
  src/main.c
  src/meta.c
  src/help.c
  src/color.c
  src/value.c
  src/advanced.c
  src/calc.c
  src/parse.c
  src/functions.c
  src/tensor.c
  src/linalg.c
  src/domains.c
  src/series.c
  src/algebra.c
  src/plot.c
  src/plot_export.c
  src/repl.c
)

# Interactive plot window: native system UI on Windows and macOS; stub elsewhere.
if(WIN32)
  list(APPEND TERMINALC_SOURCES src/plot_win32.c)
  # PE VERSIONINFO — Company/Developer visible in file Properties, not in REPL.
  list(APPEND TERMINALC_SOURCES src/terminalc.rc)
elseif(APPLE)
  list(APPEND TERMINALC_SOURCES src/plot_macos.m)
else()
  list(APPEND TERMINALC_SOURCES src/plot_gui_stub.c)
endif()

add_executable(terminalc ${TERMINALC_SOURCES})
target_include_directories(terminalc
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${GMP_INCLUDE_DIR}
    ${MPFR_INCLUDE_DIR}
)
target_link_libraries(terminalc PRIVATE ${MPFR_LIBRARY} ${GMP_LIBRARY})
if(WIN32)
  target_link_libraries(terminalc PRIVATE gdi32 user32 psapi)
elseif(APPLE)
  target_link_libraries(terminalc PRIVATE "-framework Cocoa")
  # Cocoa headers are system code; keep project warnings fatal without making
  # SDK compatibility annotations fail the build on newer Apple Clang.
  set_source_files_properties(src/plot_macos.m PROPERTIES
    COMPILE_OPTIONS "-Wno-error=pedantic")
endif()
target_compile_definitions(terminalc PRIVATE TERMINALC_VERSION="${TERMINALC_RELEASE_VERSION}")

if(terminalc_ipo_ok AND TERMINALC_ENABLE_LTO)
  set_property(TARGET terminalc PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

if(MSVC)
  target_compile_options(terminalc PRIVATE /W4 /WX /O2)
else()
  target_compile_options(terminalc PRIVATE
    -Wall -Wextra -Wpedantic -Werror
    -O3
    -ffunction-sections -fdata-sections
  )
  if(WIN32 AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
    # MinGW's Windows SDK headers contain a few intentional C/C++ compatibility
    # constructs that trigger -Wpedantic on newer GCC releases. Keep project
    # warnings fatal, but do not make those external headers unbuildable.
    target_compile_options(terminalc PRIVATE -Wno-error=pedantic)
  endif()
  if(NOT APPLE)
    target_link_options(terminalc PRIVATE -Wl,--gc-sections)
  endif()
endif()

# Windows MinGW: ship runtime DLLs next to the executable.
if(WIN32 AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
  get_filename_component(GMP_RUNTIME_DIR "${GMP_LIBRARY}" DIRECTORY)
  get_filename_component(MINGW_PREFIX "${GMP_RUNTIME_DIR}" DIRECTORY)
  add_custom_command(TARGET terminalc POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${MINGW_PREFIX}/bin/libgmp-10.dll" "$<TARGET_FILE_DIR:terminalc>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${MINGW_PREFIX}/bin/libmpfr-6.dll" "$<TARGET_FILE_DIR:terminalc>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${MINGW_PREFIX}/bin/libgcc_s_seh-1.dll" "$<TARGET_FILE_DIR:terminalc>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${MINGW_PREFIX}/bin/libwinpthread-1.dll" "$<TARGET_FILE_DIR:terminalc>")
  install(FILES
    "${MINGW_PREFIX}/bin/libgmp-10.dll"
    "${MINGW_PREFIX}/bin/libmpfr-6.dll"
    "${MINGW_PREFIX}/bin/libgcc_s_seh-1.dll"
    "${MINGW_PREFIX}/bin/libwinpthread-1.dll"
    DESTINATION "${TERMINALC_NATIVE_INSTALL_DIR}")
endif()

install(TARGETS terminalc RUNTIME DESTINATION "${TERMINALC_NATIVE_INSTALL_DIR}")
install(FILES include/terminalc.h DESTINATION include)
install(FILES README.md LICENSE docs/USAGE.md docs/FUNCTIONS.md DESTINATION .)
install(DIRECTORY examples DESTINATION .)

# A platform-native ZIP is the convenient release artifact. Windows builds
# include the GMP/MPFR/runtime DLLs copied above; macOS/Linux use shared libs.
set(CPACK_PACKAGE_NAME "TerminalC")
set(CPACK_PACKAGE_VENDOR "Impractical.in")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Exact, arbitrary-precision terminal calculator")
set(CPACK_PACKAGE_VERSION "${TERMINALC_RELEASE_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_FILE_NAME "TerminalC-${TERMINALC_RELEASE_VERSION}-${CMAKE_SYSTEM_NAME}")
include(CPack)

include(CTest)
if(BUILD_TESTING)
  # ---- Bulk regression catalog (hundreds of --eval cases) ----
  include(${CMAKE_CURRENT_SOURCE_DIR}/tests/register_catalog.cmake)

  # ---- CLI banners ----
  add_test(NAME help_banner COMMAND terminalc --help)
  set_tests_properties(help_banner PROPERTIES
    PASS_REGULAR_EXPRESSION "TerminalC function catalog")

  add_test(NAME version_banner COMMAND terminalc --version)
  set_tests_properties(version_banner PROPERTIES
    PASS_REGULAR_EXPRESSION "TerminalC ${TERMINALC_RELEASE_VERSION}")

  # ---- REPL fixtures ----
  add_test(NAME help_hex_topic
    COMMAND ${CMAKE_COMMAND}
      "-DTERMINALC_EXE=$<TARGET_FILE:terminalc>"
      "-DTERMINALC_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/help_hex.input"
      "-DTERMINALC_EXPECT=hex(n)"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_repl_expect.cmake")

  add_test(NAME help_ascii_arguments
    COMMAND ${CMAKE_COMMAND}
      "-DTERMINALC_EXE=$<TARGET_FILE:terminalc>"
      "-DTERMINALC_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/help_cube.input"
      "-DTERMINALC_EXPECT=a - cube edge"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_repl_expect.cmake")

  add_test(NAME plot_bounds
    COMMAND ${CMAKE_COMMAND}
      "-DTERMINALC_EXE=$<TARGET_FILE:terminalc>"
      "-DTERMINALC_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/plot_bounds.input"
      "-DTERMINALC_EXPECT=x: 1.000000 .. 9.000000"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_repl_expect.cmake")

  add_test(NAME plot_svg_export
    COMMAND ${CMAKE_COMMAND}
      "-DTERMINALC_EXE=$<TARGET_FILE:terminalc>"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_plot_export.cmake")

  add_test(NAME history_session
    COMMAND ${CMAKE_COMMAND}
      "-DTERMINALC_EXE=$<TARGET_FILE:terminalc>"
      "-DTERMINALC_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/history.input"
      "-DTERMINALC_EXPECT=History cleared."
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_repl_expect.cmake")
  set_tests_properties(history_session PROPERTIES
    ENVIRONMENT "TERMINALC_HISTORY_FILE=")

  add_test(NAME help_topics_index
    COMMAND ${CMAKE_COMMAND}
      "-DTERMINALC_EXE=$<TARGET_FILE:terminalc>"
      "-DTERMINALC_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/help_topics.input"
      "-DTERMINALC_EXPECT=Help topics"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_repl_expect.cmake")

  add_test(NAME help_functions_catalog
    COMMAND ${CMAKE_COMMAND}
      "-DTERMINALC_EXE=$<TARGET_FILE:terminalc>"
      "-DTERMINALC_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/help_functions.input"
      "-DTERMINALC_EXPECT=TerminalC function catalog"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_repl_expect.cmake")

  add_test(NAME help_functions_complete
    COMMAND ${CMAKE_COMMAND}
      "-DTERMINALC_EXE=$<TARGET_FILE:terminalc>"
      "-DTERMINALC_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/help_functions.input"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_help_catalog.cmake")

  add_test(NAME help_ramanujan_pi_formula
    COMMAND ${CMAKE_COMMAND}
      "-DTERMINALC_EXE=$<TARGET_FILE:terminalc>"
      "-DTERMINALC_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/help_ramanujan_pi.input"
      "-DTERMINALC_EXPECT=1/pi ="
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_repl_expect.cmake")

  add_test(NAME help_ramanujan_pi_tribute
    COMMAND ${CMAKE_COMMAND}
      "-DTERMINALC_EXE=$<TARGET_FILE:terminalc>"
      "-DTERMINALC_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/help_ramanujan_pi.input"
      "-DTERMINALC_EXPECT=Srinivasa Ramanujan"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_repl_expect.cmake")

  add_test(NAME help_units_catalog
    COMMAND ${CMAKE_COMMAND}
      "-DTERMINALC_EXE=$<TARGET_FILE:terminalc>"
      "-DTERMINALC_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/help_units.input"
      "-DTERMINALC_EXPECT=Unit names"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_repl_expect.cmake")

  add_test(NAME quadratic_roots
    COMMAND ${CMAKE_COMMAND}
      "-DTERMINALC_EXE=$<TARGET_FILE:terminalc>"
      "-DTERMINALC_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/quadratic.input"
      "-DTERMINALC_EXPECT=x1 = 2"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_repl_expect.cmake")

  add_test(NAME precision_command
    COMMAND ${CMAKE_COMMAND}
      "-DTERMINALC_EXE=$<TARGET_FILE:terminalc>"
      "-DTERMINALC_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/precision.input"
      "-DTERMINALC_EXPECT=Precision set to 5"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_repl_expect.cmake")

  add_test(NAME bool_repl_session
    COMMAND ${CMAKE_COMMAND}
      "-DTERMINALC_EXE=$<TARGET_FILE:terminalc>"
      "-DTERMINALC_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/bool_session.input"
      "-DTERMINALC_EXPECT=true"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_repl_expect.cmake")

  add_test(NAME robustness_cli
    COMMAND ${CMAKE_COMMAND}
      "-DTERMINALC_EXE=$<TARGET_FILE:terminalc>"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_robustness.cmake")

  add_test(NAME malformed_inputs
    COMMAND ${CMAKE_COMMAND}
      "-DTERMINALC_EXE=$<TARGET_FILE:terminalc>"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_malformed.cmake")

  add_test(NAME tensor_scalar_broadcast
    COMMAND ${CMAKE_COMMAND}
      "-DTERMINALC_EXE=$<TARGET_FILE:terminalc>"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tensor_broadcast.cmake")

  add_test(NAME output_formats
    COMMAND ${CMAKE_COMMAND}
      "-DTERMINALC_EXE=$<TARGET_FILE:terminalc>"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_output_formats.cmake")

  add_test(NAME automation_script
    COMMAND ${CMAKE_COMMAND}
      "-DTERMINALC_EXE=$<TARGET_FILE:terminalc>"
      "-DTERMINALC_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/automation.tc"
      "-DTERMINALC_EXPECT=TerminalC function catalog"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_script_expect.cmake")

  add_test(NAME automation_script_error
    COMMAND ${CMAKE_COMMAND}
      "-DTERMINALC_EXE=$<TARGET_FILE:terminalc>"
      "-DTERMINALC_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/automation_error.tc"
      "-DTERMINALC_EXPECT=division by zero"
      "-DTERMINALC_MATCH_ERR=1"
      "-DTERMINALC_EXIT=1"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_script_expect.cmake")

  add_test(NAME automation_command_error
    COMMAND ${CMAKE_COMMAND}
      "-DTERMINALC_EXE=$<TARGET_FILE:terminalc>"
      "-DTERMINALC_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/automation_command_error.tc"
      "-DTERMINALC_EXPECT=Unknown help topic"
      "-DTERMINALC_MATCH_ERR=1"
      "-DTERMINALC_EXIT=1"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_script_expect.cmake")

  add_test(NAME automation_command_syntax
    COMMAND ${CMAKE_COMMAND}
      "-DTERMINALC_EXE=$<TARGET_FILE:terminalc>"
      "-DTERMINALC_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/automation_command_syntax.tc"
      "-DTERMINALC_EXPECT=Use degree"
      "-DTERMINALC_MATCH_ERR=1"
      "-DTERMINALC_EXIT=1"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_script_expect.cmake")
endif()
