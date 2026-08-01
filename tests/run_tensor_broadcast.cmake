# Verify scalar broadcasting for tensor arithmetic, including operand order.
if(NOT TERMINALC_EXE)
  message(FATAL_ERROR "TERMINALC_EXE required")
endif()

function(check_case expr expected)
  execute_process(
    COMMAND "${TERMINALC_EXE}" --no-color --eval "${expr}"
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${expr} failed with ${rc}\nstdout:\n${out}\nstderr:\n${err}")
  endif()
  string(REPLACE "\r\n" "\n" out "${out}")
  string(REPLACE "\r" "\n" out "${out}")
  string(STRIP "${out}" out)
  if(NOT out STREQUAL expected)
    message(FATAL_ERROR "${expr} returned:\n[[${out}]]\nexpected:\n[[${expected}]]")
  endif()
endfunction()

check_case("[1,2]+1" [=[2-vector
[ 2 3 ]]=])
check_case("1+[1,2]" [=[2-vector
[ 2 3 ]]=])
check_case("[1,2]-1" [=[2-vector
[ 0 1 ]]=])
check_case("1-[1,2]" [=[2-vector
[ 0 -1 ]]=])
check_case("[1,2]*2" [=[2-vector
[ 2 4 ]]=])
check_case("2*[1,2]" [=[2-vector
[ 2 4 ]]=])
check_case("[1,2]/2" [=[2-vector
[ 1/2 1 ]]=])
check_case("2/[1,2]" [=[2-vector
[ 2 1 ]]=])
