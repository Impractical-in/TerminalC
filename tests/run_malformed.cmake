if(NOT TERMINALC_EXE)
  message(FATAL_ERROR "TERMINALC_EXE is required")
endif()

set(cases
  "1+"
  "(1+2"
  "[1,2"
  "1/(2-2)"
  "1e"
  "0x"
  "2^^3"
  "sin("
  "zeros(-1)"
  "ones(100000000,100000000)"
  "mat(2,2,1)"
)

foreach(expr IN LISTS cases)
  execute_process(
    COMMAND "${TERMINALC_EXE}" --no-color --eval "${expr}"
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE rc
    TIMEOUT 10
  )
  if(NOT rc EQUAL 1 AND NOT rc EQUAL 2)
    message(FATAL_ERROR
      "Malformed expression did not return a normal calculator error: '${expr}'\n"
      "rc=${rc}\nstdout:\n${out}\nstderr:\n${err}")
  endif()
endforeach()
