# Run one --eval case for TerminalC.
# Required: TERMINALC_EXE, TERMINALC_EXPR, TERMINALC_EXPECT
# Optional: TERMINALC_PRECISION, TERMINALC_EXIT (default 0), TERMINALC_MATCH_ERR=1
#
# TERMINALC_EXPECT matching rules:
#   1) Exact match against stripped stdout/stderr (preferred for integers/bools)
#   2) Else CMake regex match (substring / patterns)
# Avoid relying on a trailing $ anchor alone — CMake/CLI can mangle it.
# Use EXACT:value to force exact-only, or RE:pattern to force regex-only.
if(NOT TERMINALC_EXE OR NOT DEFINED TERMINALC_EXPR OR NOT DEFINED TERMINALC_EXPECT)
  message(FATAL_ERROR "TERMINALC_EXE, TERMINALC_EXPR, TERMINALC_EXPECT required")
endif()

if(NOT DEFINED TERMINALC_EXIT OR TERMINALC_EXIT STREQUAL "")
  set(TERMINALC_EXIT 0)
endif()

set(_cmd "${TERMINALC_EXE}" --no-color)
if(TERMINALC_PRECISION AND NOT TERMINALC_PRECISION STREQUAL "")
  list(APPEND _cmd --precision "${TERMINALC_PRECISION}")
endif()
list(APPEND _cmd --eval "${TERMINALC_EXPR}")

execute_process(
  COMMAND ${_cmd}
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
  RESULT_VARIABLE rc
)

if(NOT rc EQUAL TERMINALC_EXIT)
  message(FATAL_ERROR
    "Exit code ${rc}, expected ${TERMINALC_EXIT}\n"
    "expr: ${TERMINALC_EXPR}\nstdout:\n${out}\nstderr:\n${err}")
endif()

if(TERMINALC_MATCH_ERR AND NOT TERMINALC_MATCH_ERR STREQUAL "0")
  set(haystack "${err}")
  set(which "stderr")
else()
  set(haystack "${out}")
  set(which "stdout")
endif()

# Normalize newlines for stable matching
string(REPLACE "\r\n" "\n" haystack "${haystack}")
string(REPLACE "\r" "\n" haystack "${haystack}")
string(STRIP "${haystack}" hay)
set(pat "${TERMINALC_EXPECT}")
string(STRIP "${pat}" pat)

set(_mode "auto") # auto | exact | re
if(pat MATCHES "^EXACT:(.*)$")
  set(_mode "exact")
  set(pat "${CMAKE_MATCH_1}")
elseif(pat MATCHES "^RE:(.*)$")
  set(_mode "re")
  set(pat "${CMAKE_MATCH_1}")
endif()

set(_ok FALSE)
if(_mode STREQUAL "exact" OR _mode STREQUAL "auto")
  if(hay STREQUAL pat)
    set(_ok TRUE)
  endif()
endif()

if(NOT _ok AND (_mode STREQUAL "re" OR _mode STREQUAL "auto"))
  # Drop a single trailing $ — unreliable through cmake -D / shells.
  if(pat MATCHES "\\$$")
    string(REGEX REPLACE "\\$$" "" pat "${pat}")
  endif()
  if(hay MATCHES "${pat}")
    set(_ok TRUE)
  endif()
endif()

# First line exact (common: multi-line Exact/Decimal dumps)
if(NOT _ok)
  string(REGEX REPLACE "\n.*" "" first_line "${hay}")
  if(first_line STREQUAL pat)
    set(_ok TRUE)
  elseif(first_line MATCHES "${pat}")
    set(_ok TRUE)
  endif()
endif()

if(NOT _ok)
  message(FATAL_ERROR
    "Expectation not matched in ${which}: ${TERMINALC_EXPECT}\n"
    "expr: ${TERMINALC_EXPR}\n"
    "normalized ${which}:\n[[${hay}]]\n"
    "raw stdout:\n${out}\nstderr:\n${err}")
endif()
