# Run terminalc with a stdin fixture and require TERMINALC_EXPECT in stdout.
if(NOT TERMINALC_EXE OR NOT TERMINALC_INPUT OR NOT TERMINALC_EXPECT)
  message(FATAL_ERROR "TERMINALC_EXE, TERMINALC_INPUT, and TERMINALC_EXPECT are required")
endif()

execute_process(
  COMMAND "${TERMINALC_EXE}" --no-color
  INPUT_FILE "${TERMINALC_INPUT}"
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
  RESULT_VARIABLE rc
)

if(NOT rc EQUAL 0)
  message(FATAL_ERROR "terminalc exited ${rc}\nstdout:\n${out}\nstderr:\n${err}")
endif()

string(FIND "${out}" "${TERMINALC_EXPECT}" pos)
if(pos EQUAL -1)
  message(FATAL_ERROR "Expected substring not found: ${TERMINALC_EXPECT}\nOutput:\n${out}")
endif()
