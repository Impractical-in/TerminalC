if(NOT TERMINALC_EXE)
  message(FATAL_ERROR "TERMINALC_EXE is required")
endif()

set(script "${CMAKE_CURRENT_BINARY_DIR}/terminalc_graph_test.tc")
file(WRITE "${script}"
  "plot(sin(x), x=-pi..pi)\n"
  "plot(x^2, x=-2..2)\n")
set(ENV{TERMINALC_ASCII_PLOT} "1")
execute_process(
  COMMAND "${TERMINALC_EXE}" --no-color --script "${script}"
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
  RESULT_VARIABLE rc
  TIMEOUT 20
)
file(REMOVE "${script}")
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "ASCII graph test exited ${rc}\nstdout:\n${out}\nstderr:\n${err}")
endif()
foreach(item
    "+-- Plot: y = sin(x)"
    "x: -3.141593 .. 3.141593"
    "+-- Plot: y = x^2"
    "x: -2.000000 .. 2.000000")
  string(FIND "${out}" "${item}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "ASCII graph output missing '${item}'\nstdout:\n${out}")
  endif()
endforeach()
