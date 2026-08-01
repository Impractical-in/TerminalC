if(NOT TERMINALC_EXE)
  message(FATAL_ERROR "TERMINALC_EXE is required")
endif()

function(expect_failure expected_exit expected_error)
  execute_process(
    COMMAND "${TERMINALC_EXE}" --no-color ${ARGN}
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE rc
    TIMEOUT 10
  )
  if(NOT rc EQUAL expected_exit)
    message(FATAL_ERROR
      "Exit code ${rc}, expected ${expected_exit}\nstdout:\n${out}\nstderr:\n${err}")
  endif()
  if(NOT err MATCHES "${expected_error}")
    message(FATAL_ERROR
      "Expected stderr to match '${expected_error}'\nstdout:\n${out}\nstderr:\n${err}")
  endif()
endfunction()

function(expect_success expected_output)
  execute_process(
    COMMAND "${TERMINALC_EXE}" --no-color ${ARGN}
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE rc
    TIMEOUT 10
  )
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR
      "Expected success, got exit code ${rc}\nstdout:\n${out}\nstderr:\n${err}")
  endif()
  if(NOT out MATCHES "${expected_output}")
    message(FATAL_ERROR
      "Expected stdout to match '${expected_output}'\nstdout:\n${out}\nstderr:\n${err}")
  endif()
endfunction()

expect_failure(2 "invalid precision value" --precision abc --eval "1+1")
expect_failure(2 "invalid precision value"
  --precision 999999999999999999999999 --eval "1+1")
expect_failure(2 "Precision must be between" --precision 0 --eval "1+1")
expect_failure(2 "--eval and --script cannot be used together"
  --eval "1+1" --script "${CMAKE_CURRENT_LIST_DIR}/automation.tc")

# Numeric domains cap intermediate precision so even the maximum display
# precision remains a bounded operation.
expect_success("1\\.41421356237309504880" --precision 100000 --eval "solve(x^2-2,0,2)")
expect_success("^2" --precision 100000 --eval "integral(sin(x),0,pi)")

set(deep "1")
foreach(_i RANGE 1 300)
  set(deep "(${deep})")
endforeach()
expect_failure(1 "expression nesting is too deep" --eval "${deep}")
