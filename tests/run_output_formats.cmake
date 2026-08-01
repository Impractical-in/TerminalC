if(NOT TERMINALC_EXE)
  message(FATAL_ERROR "TERMINALC_EXE is required")
endif()

function(check_output format expr expected)
  execute_process(
    COMMAND "${TERMINALC_EXE}" --no-color --format "${format}" --eval "${expr}"
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${format} ${expr} failed with ${rc}\nstdout:\n${out}\nstderr:\n${err}")
  endif()
  string(REPLACE "\r\n" "\n" out "${out}")
  string(REPLACE "\r" "\n" out "${out}")
  string(STRIP "${out}" out)
  string(FIND "${out}" "${expected}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "${format} ${expr} returned:\n${out}\nmissing:\n${expected}")
  endif()
endfunction()

check_output(json "logb(8,2)" "{\"type\":\"real\",\"decimal\":\"3\"}")
check_output(json "[1,2]+1" "\"type\":\"tensor\",\"rank\":1,\"shape\":[2]")
check_output(csv "1/3" "type,exact,decimal,unit,shape,values\nrational,1/3,0.3333")
check_output(csv "[1,2]+1" "tensor,,,,2,\"2;3\"")
