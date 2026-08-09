if(NOT TERMINALC_EXE)
  message(FATAL_ERROR "TERMINALC_EXE is required")
endif()

set(stem "${CMAKE_CURRENT_BINARY_DIR}/terminalc_plot_export_test")
set(script "${stem}.tc")
set(svg "${stem}.svg")
file(TO_CMAKE_PATH "${svg}" svg_script_path)
file(WRITE "${script}"
  "plot_save(sin(x);cos(x), x=0..6.283185307179586, \"${svg_script_path}\")\n")

execute_process(
  COMMAND "${TERMINALC_EXE}" --no-color --script "${script}"
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
  RESULT_VARIABLE rc
  TIMEOUT 15
)
if(NOT rc EQUAL 0)
  file(REMOVE "${script}" "${svg}")
  message(FATAL_ERROR "plot export exited ${rc}\nstdout:\n${out}\nstderr:\n${err}")
endif()
if(NOT EXISTS "${svg}")
  file(REMOVE "${script}")
  message(FATAL_ERROR "plot export did not create ${svg}\nstdout:\n${out}\nstderr:\n${err}")
endif()
file(READ "${svg}" contents)
if(NOT contents MATCHES "<svg")
  file(REMOVE "${script}" "${svg}")
  message(FATAL_ERROR "plot export is not SVG\n${contents}")
endif()
if(NOT contents MATCHES "#16a34a" OR NOT contents MATCHES "#2563eb")
  file(REMOVE "${script}" "${svg}")
  message(FATAL_ERROR "multi-curve SVG is missing expected curve colors")
endif()
file(REMOVE "${script}" "${svg}")
