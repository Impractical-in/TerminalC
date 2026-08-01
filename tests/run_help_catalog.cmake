if(NOT TERMINALC_EXE OR NOT TERMINALC_INPUT)
  message(FATAL_ERROR "TERMINALC_EXE and TERMINALC_INPUT are required")
endif()

execute_process(
  COMMAND "${TERMINALC_EXE}" --no-color
  INPUT_FILE "${TERMINALC_INPUT}"
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
  RESULT_VARIABLE rc
  TIMEOUT 10
)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "terminalc exited ${rc}\nstdout:\n${out}\nstderr:\n${err}")
endif()

set(required
  "TerminalC function catalog"
  "[Core and logic]"
  "[Trigonometry and logarithms]"
  "[Number theory and combinatorics]"
  "[Bases and integer display]"
  "[Sequences and summation]"
  "[Matrices and tensors]"
  "[Numerical solvers and calculus]"
  "[Statistics and machine learning]"
  "[Units and measurements]"
  "[Geometry and mensuration]"
  "[Algebra and plotting]"
  "factorial(n)"
  "logb(x, base)"
  "erf(x)"
  "softplus(x)"
  "ramanujan_pi()"
  "solve(expr, guess)"
  "integral(expr, lo, hi)"
  "nthroot(x, n)"
  "[REPL/script]"
  "Constants: pi, e, phi, sqrt2, ln2, ans, _, x"
)
foreach(item IN LISTS required)
  string(FIND "${out}" "${item}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "Help catalog is missing '${item}'\nOutput:\n${out}")
  endif()
endforeach()
