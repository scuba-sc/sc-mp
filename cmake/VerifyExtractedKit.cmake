# SPDX-License-Identifier: GPL-3.0-only
# Validate the complete extracted tree before any SDK CMake executes.
function(sc_mp_verify_extracted_kit kit_root expected_sums label)
  find_program(_kit_python NAMES python3 python REQUIRED)
  execute_process(
    COMMAND "${_kit_python}" -c "import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)"
    RESULT_VARIABLE _python_result
    TIMEOUT 10)
  if(NOT _python_result STREQUAL "0")
    message(FATAL_ERROR "${label}: Python 3.8 or newer is required for kit verification")
  endif()
  execute_process(
    COMMAND "${_kit_python}" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/verify_host_context_kit.py" "${kit_root}" "${expected_sums}"
    RESULT_VARIABLE _kit_result
    OUTPUT_VARIABLE _kit_output
    ERROR_VARIABLE _kit_error
    TIMEOUT 60)
  if(NOT _kit_result STREQUAL "0")
    message(FATAL_ERROR "${label}: ${_kit_error} ${_kit_output}")
  endif()
  message(STATUS "${label}: ${_kit_output}")
endfunction()
