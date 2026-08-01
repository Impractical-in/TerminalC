# Register catalog tests from tests/eval_catalog.tsv
# Columns (TAB-separated):
#   name  expr  expect_regex  [options]
# options (comma-separated): p=N  fail  err
#   p=N   --precision N
#   fail  expect exit code 1
#   err   match regex on stderr
#
# Lines starting with # or empty are skipped.

set(_catalog "${CMAKE_CURRENT_SOURCE_DIR}/tests/eval_catalog.tsv")
if(NOT EXISTS "${_catalog}")
  message(FATAL_ERROR "Missing ${_catalog}")
endif()

file(STRINGS "${_catalog}" _lines)
set(_count 0)
foreach(_line IN LISTS _lines)
  # A UTF-8 BOM on the catalog's first comment can precede '#'. Skip the
  # descriptive header explicitly so configuration remains warning-free.
  if(_line MATCHES "TerminalC eval catalog" OR _line MATCHES "TAB-separated:")
    continue()
  endif()
  if(_line MATCHES "^[ \t]*#" OR _line MATCHES "^[ \t]*$")
    continue()
  endif()

  # Split on tabs into list
  string(REPLACE "\t" ";" _cols "${_line}")
  list(LENGTH _cols _n)
  if(_n LESS 3)
    message(WARNING "Bad catalog line (need 3+ tab fields): ${_line}")
    continue()
  endif()

  list(GET _cols 0 _name)
  list(GET _cols 1 _expr)
  list(GET _cols 2 _expect)
  set(_opts "")
  if(_n GREATER 3)
    list(GET _cols 3 _opts)
  endif()

  string(STRIP "${_name}" _name)
  string(STRIP "${_expr}" _expr)
  string(STRIP "${_expect}" _expect)
  string(STRIP "${_opts}" _opts)

  if(_name STREQUAL "" OR _expr STREQUAL "")
    continue()
  endif()

  set(_prec "")
  set(_exit 0)
  set(_match_err 0)
  if(_opts MATCHES "p=([0-9]+)")
    set(_prec "${CMAKE_MATCH_1}")
  endif()
  if(_opts MATCHES "fail")
    set(_exit 1)
  endif()
  if(_opts MATCHES "err")
    set(_match_err 1)
  endif()

  set(_test_name "cat_${_name}")
  # Quote -D values: expressions may contain >, <, *, (, ), etc.
  add_test(
    NAME ${_test_name}
    COMMAND ${CMAKE_COMMAND}
      "-DTERMINALC_EXE=$<TARGET_FILE:terminalc>"
      "-DTERMINALC_EXPR=${_expr}"
      "-DTERMINALC_EXPECT=${_expect}"
      "-DTERMINALC_PRECISION=${_prec}"
      "-DTERMINALC_EXIT=${_exit}"
      "-DTERMINALC_MATCH_ERR=${_match_err}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_eval_case.cmake"
  )
  math(EXPR _count "${_count}+1")
endforeach()

message(STATUS "Registered ${_count} catalog eval tests from eval_catalog.tsv")
