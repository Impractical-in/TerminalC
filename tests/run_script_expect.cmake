if(NOT TERMINALC_EXE OR NOT TERMINALC_SCRIPT OR NOT DEFINED TERMINALC_EXPECT)
  message(FATAL_ERROR "TERMINALC_EXE, TERMINALC_SCRIPT, and TERMINALC_EXPECT are required")
endif()
execute_process(
  COMMAND "${TERMINALC_EXE}" --no-color --script "${TERMINALC_SCRIPT}"
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
  RESULT_VARIABLE rc
  TIMEOUT 15
)
if(DEFINED TERMINALC_EXIT AND NOT rc EQUAL TERMINALC_EXIT)
  message(FATAL_ERROR "Exit code ${rc}, expected ${TERMINALC_EXIT}\nstdout:\n${out}\nstderr:\n${err}")
endif()
if(NOT DEFINED TERMINALC_EXIT AND NOT rc EQUAL 0)
  message(FATAL_ERROR "Unexpected exit code ${rc}\nstdout:\n${out}\nstderr:\n${err}")
endif()
string(REPLACE "\r\n" "\n" out "${out}")
set(haystack "${out}")
if(DEFINED TERMINALC_MATCH_ERR AND TERMINALC_MATCH_ERR)
  set(haystack "${err}")
endif()
if(NOT haystack MATCHES "${TERMINALC_EXPECT}")
  message(FATAL_ERROR "Expected script output to match '${TERMINALC_EXPECT}'\nstdout:\n${out}\nstderr:\n${err}")
endif()
