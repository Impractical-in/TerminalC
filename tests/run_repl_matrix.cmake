if(NOT TERMINALC_EXE OR NOT TERMINALC_INPUT)
  message(FATAL_ERROR "TERMINALC_EXE and TERMINALC_INPUT are required")
endif()

execute_process(
  COMMAND "${TERMINALC_EXE}" --no-color
  INPUT_FILE "${TERMINALC_INPUT}"
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
  RESULT_VARIABLE rc
)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "command matrix exited ${rc}\nstdout:\n${out}\nstderr:\n${err}")
endif()

set(expected
  "TerminalC interactive commands"
  "TerminalC function catalog"
  "Precision:"
  "Precision set to 7"
  "Angle mode set to degrees."
  "0.9998477"
  "Angle mode set to radians."
  "a ="
  "Variables cleared."
  "(no variables)"
  "Advanced mode:"
  "Advanced mode on"
  "Advanced mode off"
  "Color disabled."
  "1  help"
  "(history is empty)"
  "Hi there! Welcome to TerminalC."
  "Bye! Thanks for using TerminalC."
)
foreach(item IN LISTS expected)
  string(FIND "${out}" "${item}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "command matrix missing '${item}'\nstdout:\n${out}\nstderr:\n${err}")
  endif()
endforeach()
