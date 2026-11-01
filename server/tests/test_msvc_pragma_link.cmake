# SPDX-License-Identifier: GPL-3.0-only
# Confirm with MSVC which source-level pragma forms emit linker library records.
if(NOT DEFINED SCO_RELAY_CMAKE_COMMAND OR NOT DEFINED SCO_RELAY_COMPILER OR NOT DEFINED SCO_RELAY_TEST_BINARY_DIR)
  message(FATAL_ERROR "test requires CMake, the MSVC compiler and a temporary binary directory")
endif()

file(REMOVE_RECURSE "${SCO_RELAY_TEST_BINARY_DIR}")
file(MAKE_DIRECTORY "${SCO_RELAY_TEST_BINARY_DIR}")
set(_missing_library "scmp_pragma_probe_missing_8236.lib")
set(_source "${SCO_RELAY_TEST_BINARY_DIR}/pragma_probe.cpp")
set(_program "${SCO_RELAY_TEST_BINARY_DIR}/pragma_probe.exe")
file(WRITE "${_source}" "_Pragma(\"comment(lib, \\\"${_missing_library}\\\")\")\nint main() { return 0; }\n")

execute_process(
  COMMAND "${SCO_RELAY_COMPILER}" /nologo "/Fe${_program}" "${_source}"
  RESULT_VARIABLE _result
  OUTPUT_VARIABLE _stdout
  ERROR_VARIABLE _stderr)
set(_output "${_stdout}${_stderr}")
if(_result EQUAL 0)
  message(FATAL_ERROR "MSVC did not fail while linking the _Pragma-injected library; linker injection was not demonstrated")
endif()
if(NOT _output MATCHES "LNK1104" OR NOT _output MATCHES "${_missing_library}")
  message(FATAL_ERROR "MSVC _Pragma probe failed for an unrelated reason: ${_output}")
endif()
message(STATUS "MSVC _Pragma comment(lib, ...) generated the expected unresolved library search record")

set(_macro_missing_library "scmp_macro_pragma_probe_missing_3147.lib")
set(_macro_source "${SCO_RELAY_TEST_BINARY_DIR}/macro_pragma_probe.cpp")
set(_macro_program "${SCO_RELAY_TEST_BINARY_DIR}/macro_pragma_probe.exe")
file(WRITE "${_macro_source}"
  "#define LINKER_COMMENT comment\n#pragma LINKER_COMMENT(lib, \"${_macro_missing_library}\")\nint main() { return 0; }\n")
execute_process(
  COMMAND "${SCO_RELAY_COMPILER}" /nologo "/Fe${_macro_program}" "${_macro_source}"
  RESULT_VARIABLE _macro_result
  OUTPUT_VARIABLE _macro_stdout
  ERROR_VARIABLE _macro_stderr)
set(_macro_output "${_macro_stdout}${_macro_stderr}")
if(_macro_result EQUAL 0)
  message(STATUS "MSVC did not emit a linker search record from a macro-expanded #pragma name")
elseif(_macro_output MATCHES "LNK1104" AND _macro_output MATCHES "${_macro_missing_library}")
  message(STATUS "MSVC macro-expanded #pragma name generated the expected unresolved library search record")
else()
  message(FATAL_ERROR "MSVC macro-expanded #pragma probe failed for an unrelated reason: ${_macro_output}")
endif()

# Microsoft documents that LINK searches its working directory before LIB. Verify that each empty LIB list form
# preserves that baseline. The safety claim that dropping empty entries adds no search directory follows from that
# documented order; this probe does not try to distinguish a repeated working-directory lookup.
get_filename_component(_tool_dir "${SCO_RELAY_COMPILER}" DIRECTORY)
set(_linker "${_tool_dir}/link.exe")
set(_librarian "${_tool_dir}/lib.exe")
if(NOT EXISTS "${_linker}" OR NOT EXISTS "${_librarian}")
  message(FATAL_ERROR "the selected MSVC toolset is missing link.exe or lib.exe")
endif()
set(_empty_lib_root "${SCO_RELAY_TEST_BINARY_DIR}/lib-search")
set(_empty_lib_work "${_empty_lib_root}/work")
set(_empty_lib_safe "${_empty_lib_root}/safe")
set(_empty_lib_other "${_empty_lib_root}/other")
file(MAKE_DIRECTORY "${_empty_lib_work}" "${_empty_lib_safe}" "${_empty_lib_other}")
file(WRITE "${_empty_lib_root}/implementation.cpp" "extern \"C\" int scmp_empty_lib_probe() { return 0; }\n")
file(WRITE "${_empty_lib_root}/entry.cpp"
  "#pragma comment(lib, \"scmp_empty_lib_probe.lib\")\nextern \"C\" int scmp_empty_lib_probe();\nint main() { return scmp_empty_lib_probe(); }\n")
execute_process(
  COMMAND "${SCO_RELAY_COMPILER}" /nologo /c /GS- /Zl
          "/Fo${_empty_lib_work}/implementation.obj" "${_empty_lib_root}/implementation.cpp"
  WORKING_DIRECTORY "${_empty_lib_work}"
  RESULT_VARIABLE _implementation_result
  OUTPUT_VARIABLE _implementation_stdout
  ERROR_VARIABLE _implementation_stderr)
if(NOT _implementation_result EQUAL 0)
  message(FATAL_ERROR "MSVC empty-LIB search fixture could not compile its implementation: ${_implementation_stdout}${_implementation_stderr}")
endif()
execute_process(
  COMMAND "${_librarian}" /nologo "/OUT:${_empty_lib_work}/scmp_empty_lib_probe.lib"
          "${_empty_lib_work}/implementation.obj"
  WORKING_DIRECTORY "${_empty_lib_work}"
  RESULT_VARIABLE _librarian_result
  OUTPUT_VARIABLE _librarian_stdout
  ERROR_VARIABLE _librarian_stderr)
if(NOT _librarian_result EQUAL 0)
  message(FATAL_ERROR "MSVC empty-LIB search fixture could not create its library: ${_librarian_stdout}${_librarian_stderr}")
endif()
execute_process(
  COMMAND "${SCO_RELAY_COMPILER}" /nologo /c /GS- /Zl
          "/Fo${_empty_lib_work}/entry.obj" "${_empty_lib_root}/entry.cpp"
  WORKING_DIRECTORY "${_empty_lib_work}"
  RESULT_VARIABLE _entry_result
  OUTPUT_VARIABLE _entry_stdout
  ERROR_VARIABLE _entry_stderr)
if(NOT _entry_result EQUAL 0)
  message(FATAL_ERROR "MSVC empty-LIB search fixture could not compile its entry point: ${_entry_stdout}${_entry_stderr}")
endif()

foreach(_case IN ITEMS plain leading_empty interior_empty trailing_empty)
  if(_case STREQUAL "plain")
    set(_library_environment "${_empty_lib_safe}")
  elseif(_case STREQUAL "leading_empty")
    set(_library_environment ";${_empty_lib_safe}")
  elseif(_case STREQUAL "interior_empty")
    set(_library_environment "${_empty_lib_safe};;${_empty_lib_other}")
  else()
    set(_library_environment "${_empty_lib_safe};")
  endif()
  execute_process(
    COMMAND "${SCO_RELAY_CMAKE_COMMAND}" -E env "INCLUDE=" "LIB=${_library_environment}"
            "LINK=" "_LINK_=" "CL=" "_CL_="
            "${_linker}" /NOLOGO /ENTRY:main /SUBSYSTEM:CONSOLE
            "/OUT:${_empty_lib_work}/${_case}.exe" "${_empty_lib_work}/entry.obj"
    WORKING_DIRECTORY "${_empty_lib_work}"
    RESULT_VARIABLE _link_result
    OUTPUT_VARIABLE _link_stdout
    ERROR_VARIABLE _link_stderr)
  set(_link_output "${_link_stdout}${_link_stderr}")
  if(_link_result EQUAL 0)
    set(${_case}_link_result success)
  elseif(_link_output MATCHES "LNK1104" AND _link_output MATCHES "scmp_empty_lib_probe\\.lib")
    set(${_case}_link_result missing_probe_library)
  else()
    message(FATAL_ERROR "MSVC empty-LIB search probe failed for an unrelated reason: ${_link_output}")
  endif()
endforeach()
if(NOT plain_link_result STREQUAL "success")
  message(FATAL_ERROR "MSVC did not resolve a default library from LINK's documented working-directory search")
endif()
foreach(_case IN ITEMS leading_empty interior_empty trailing_empty)
  if(NOT "${${_case}_link_result}" STREQUAL "success")
    message(FATAL_ERROR "MSVC empty LIB component changed current-directory library resolution (${_case}: ${${_case}_link_result})")
  endif()
endforeach()
message(STATUS "MSVC preserved working-directory default-library resolution with plain and empty-component LIB forms")
