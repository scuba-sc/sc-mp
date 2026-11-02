# SPDX-License-Identifier: GPL-3.0-only
# Exercise configure-time and build-time compiler environment checks with synthetic directories only.
if(NOT DEFINED SCO_RELAY_CMAKE_COMMAND OR NOT DEFINED SCO_RELAY_SOURCE_ROOT OR NOT DEFINED SCO_RELAY_TEST_BINARY_DIR)
  message(FATAL_ERROR "test_compiler_environment.cmake requires the CMake command, source root, and test binary directory")
endif()

file(REMOVE_RECURSE "${SCO_RELAY_TEST_BINARY_DIR}")
set(_good_include "${SCO_RELAY_TEST_BINARY_DIR}/MSVC/VC/Tools/MSVC/14.44.35207/include")
set(_bad_include "${SCO_RELAY_TEST_BINARY_DIR}/external/include")
set(_good_library "${SCO_RELAY_TEST_BINARY_DIR}/MSVC/VC/Tools/MSVC/14.44.35207/lib/x64")
set(_good_library_alt "${SCO_RELAY_TEST_BINARY_DIR}/MSVC/VC/Tools/MSVC/14.44.35207/lib/x86")
set(_good_auxiliary_library "${SCO_RELAY_TEST_BINARY_DIR}/MSVC/VC/Auxiliary/VS/lib/x64")
set(_good_unittest_library "${SCO_RELAY_TEST_BINARY_DIR}/MSVC/VC/Auxiliary/VS/UnitTest/lib/x64")
set(_bad_library "${SCO_RELAY_TEST_BINARY_DIR}/external/lib")
set(_compiler "${SCO_RELAY_TEST_BINARY_DIR}/MSVC/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe")
set(_spoof_program_files "${SCO_RELAY_TEST_BINARY_DIR}/spoofed-program-files")
set(_spoof_sdk_include "${_spoof_program_files}/Windows Kits/10/Include/10.0.26100.0/ucrt")
set(_script "${SCO_RELAY_SOURCE_ROOT}/server/tools/check-compiler-environment.cmake")
set(_snapshot "${SCO_RELAY_TEST_BINARY_DIR}/environment.txt")
set(_cache "${SCO_RELAY_TEST_BINARY_DIR}/CMakeCache.txt")
set(_cmake_flag_cli_args
  -DCMAKE_C_FLAGS= -DCMAKE_C_FLAGS_INIT= -DCMAKE_CXX_FLAGS= -DCMAKE_CXX_FLAGS_INIT=
  -DCMAKE_EXE_LINKER_FLAGS= -DCMAKE_EXE_LINKER_FLAGS_INIT=
  -DCMAKE_SHARED_LINKER_FLAGS= -DCMAKE_SHARED_LINKER_FLAGS_INIT=
  -DCMAKE_MODULE_LINKER_FLAGS= -DCMAKE_MODULE_LINKER_FLAGS_INIT=
  -DCMAKE_STATIC_LINKER_FLAGS= -DCMAKE_STATIC_LINKER_FLAGS_INIT=
  "-DCMAKE_C_FLAGS_DEBUG=/Zi /Ob0 /Od /RTC1" "-DCMAKE_C_FLAGS_DEBUG_INIT=/Zi /Ob0 /Od /RTC1"
  "-DCMAKE_CXX_FLAGS_DEBUG=/Zi /Ob0 /Od /RTC1" "-DCMAKE_CXX_FLAGS_DEBUG_INIT=/Zi /Ob0 /Od /RTC1"
  "-DCMAKE_C_FLAGS_MINSIZEREL=/O1 /Ob1 /DNDEBUG" "-DCMAKE_C_FLAGS_MINSIZEREL_INIT=/O1 /Ob1 /DNDEBUG"
  "-DCMAKE_CXX_FLAGS_MINSIZEREL=/O1 /Ob1 /DNDEBUG" "-DCMAKE_CXX_FLAGS_MINSIZEREL_INIT=/O1 /Ob1 /DNDEBUG"
  "-DCMAKE_C_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG" "-DCMAKE_C_FLAGS_RELEASE_INIT=/O2 /Ob2 /DNDEBUG"
  "-DCMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG" "-DCMAKE_CXX_FLAGS_RELEASE_INIT=/O2 /Ob2 /DNDEBUG"
  "-DCMAKE_C_FLAGS_RELWITHDEBINFO=/Zi /O2 /Ob1 /DNDEBUG"
  "-DCMAKE_C_FLAGS_RELWITHDEBINFO_INIT=/Zi /O2 /Ob1 /DNDEBUG"
  "-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=/Zi /O2 /Ob1 /DNDEBUG"
  "-DCMAKE_CXX_FLAGS_RELWITHDEBINFO_INIT=/Zi /O2 /Ob1 /DNDEBUG"
  -DCMAKE_EXE_LINKER_FLAGS_DEBUG=/debug -DCMAKE_EXE_LINKER_FLAGS_DEBUG_INIT=/debug
  -DCMAKE_SHARED_LINKER_FLAGS_DEBUG=/debug -DCMAKE_SHARED_LINKER_FLAGS_DEBUG_INIT=/debug
  -DCMAKE_MODULE_LINKER_FLAGS_DEBUG=/debug -DCMAKE_MODULE_LINKER_FLAGS_DEBUG_INIT=/debug
  -DCMAKE_EXE_LINKER_FLAGS_MINSIZEREL= -DCMAKE_EXE_LINKER_FLAGS_MINSIZEREL_INIT=
  -DCMAKE_SHARED_LINKER_FLAGS_MINSIZEREL= -DCMAKE_SHARED_LINKER_FLAGS_MINSIZEREL_INIT=
  -DCMAKE_MODULE_LINKER_FLAGS_MINSIZEREL= -DCMAKE_MODULE_LINKER_FLAGS_MINSIZEREL_INIT=
  -DCMAKE_EXE_LINKER_FLAGS_RELEASE= -DCMAKE_EXE_LINKER_FLAGS_RELEASE_INIT=
  -DCMAKE_SHARED_LINKER_FLAGS_RELEASE= -DCMAKE_SHARED_LINKER_FLAGS_RELEASE_INIT=
  -DCMAKE_MODULE_LINKER_FLAGS_RELEASE= -DCMAKE_MODULE_LINKER_FLAGS_RELEASE_INIT=
  -DCMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO=/debug -DCMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO_INIT=/debug
  -DCMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO=/debug -DCMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO_INIT=/debug
  -DCMAKE_MODULE_LINKER_FLAGS_RELWITHDEBINFO=/debug -DCMAKE_MODULE_LINKER_FLAGS_RELWITHDEBINFO_INIT=/debug
  -DCMAKE_STATIC_LINKER_FLAGS_DEBUG= -DCMAKE_STATIC_LINKER_FLAGS_DEBUG_INIT=
  -DCMAKE_STATIC_LINKER_FLAGS_MINSIZEREL= -DCMAKE_STATIC_LINKER_FLAGS_MINSIZEREL_INIT=
  -DCMAKE_STATIC_LINKER_FLAGS_RELEASE= -DCMAKE_STATIC_LINKER_FLAGS_RELEASE_INIT=
  -DCMAKE_STATIC_LINKER_FLAGS_RELWITHDEBINFO= -DCMAKE_STATIC_LINKER_FLAGS_RELWITHDEBINFO_INIT=)
string(CONCAT _msvc_config_cache_text
  "CMAKE_C_FLAGS_DEBUG:STRING=/Zi /Ob0 /Od /RTC1\nCMAKE_CXX_FLAGS_DEBUG:STRING=/Zi /Ob0 /Od /RTC1\n"
  "CMAKE_C_FLAGS_MINSIZEREL:STRING=/O1 /Ob1 /DNDEBUG\nCMAKE_CXX_FLAGS_MINSIZEREL:STRING=/O1 /Ob1 /DNDEBUG\n"
  "CMAKE_C_FLAGS_RELEASE:STRING=/O2 /Ob2 /DNDEBUG\nCMAKE_CXX_FLAGS_RELEASE:STRING=/O2 /Ob2 /DNDEBUG\n"
  "CMAKE_C_FLAGS_RELWITHDEBINFO:STRING=/Zi /O2 /Ob1 /DNDEBUG\n"
  "CMAKE_CXX_FLAGS_RELWITHDEBINFO:STRING=/Zi /O2 /Ob1 /DNDEBUG\n"
  "CMAKE_EXE_LINKER_FLAGS_DEBUG:STRING=/debug\nCMAKE_SHARED_LINKER_FLAGS_DEBUG:STRING=/debug\n"
  "CMAKE_MODULE_LINKER_FLAGS_DEBUG:STRING=/debug\n"
  "CMAKE_EXE_LINKER_FLAGS_MINSIZEREL:STRING=\nCMAKE_SHARED_LINKER_FLAGS_MINSIZEREL:STRING=\n"
  "CMAKE_MODULE_LINKER_FLAGS_MINSIZEREL:STRING=\n"
  "CMAKE_EXE_LINKER_FLAGS_RELEASE:STRING=\nCMAKE_SHARED_LINKER_FLAGS_RELEASE:STRING=\n"
  "CMAKE_MODULE_LINKER_FLAGS_RELEASE:STRING=\n"
  "CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO:STRING=/debug\n"
  "CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO:STRING=/debug\n"
  "CMAKE_MODULE_LINKER_FLAGS_RELWITHDEBINFO:STRING=/debug\n"
  "CMAKE_STATIC_LINKER_FLAGS_DEBUG:STRING=\nCMAKE_STATIC_LINKER_FLAGS_MINSIZEREL:STRING=\n"
  "CMAKE_STATIC_LINKER_FLAGS_RELEASE:STRING=\nCMAKE_STATIC_LINKER_FLAGS_RELWITHDEBINFO:STRING=\n")
file(MAKE_DIRECTORY "${_good_include}" "${_bad_include}" "${_good_library}" "${_good_library_alt}"
  "${_good_auxiliary_library}" "${_good_unittest_library}" "${_bad_library}" "${_spoof_sdk_include}"
  "${SCO_RELAY_TEST_BINARY_DIR}/MSVC/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64")
file(WRITE "${_cache}"
  "CMAKE_C_FLAGS:STRING=\nCMAKE_CXX_FLAGS:STRING=\nCMAKE_EXE_LINKER_FLAGS:STRING=\n"
  "CMAKE_SHARED_LINKER_FLAGS:STRING=\nCMAKE_MODULE_LINKER_FLAGS:STRING=\nCMAKE_STATIC_LINKER_FLAGS:STRING=\n"
  "${_msvc_config_cache_text}")

set(_configure_probe_source "${SCO_RELAY_TEST_BINARY_DIR}/configure-probe")
set(_configure_probe_build "${SCO_RELAY_TEST_BINARY_DIR}/configure-probe-build")
file(MAKE_DIRECTORY "${_configure_probe_source}")
file(WRITE "${_configure_probe_source}/CMakeLists.txt"
  "cmake_minimum_required(VERSION 3.20)\n"
  "include(\"${SCO_RELAY_SOURCE_ROOT}/server/tools/check-compiler-bootstrap.cmake\")\n"
  "project(relay_environment_probe LANGUAGES C CXX)\n"
  "set(SCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT \"\${CMAKE_BINARY_DIR}/environment.txt\")\n"
  "include(\"${_script}\")\n")
execute_process(
  COMMAND "${SCO_RELAY_CMAKE_COMMAND}" -S "${_configure_probe_source}" -B "${_configure_probe_build}"
  RESULT_VARIABLE _real_configure_result
  OUTPUT_VARIABLE _real_configure_stdout
  ERROR_VARIABLE _real_configure_stderr)
if(NOT _real_configure_result EQUAL 0)
  message(FATAL_ERROR "a normal compiler environment should configure: ${_real_configure_stderr}${_real_configure_stdout}")
endif()
execute_process(
  COMMAND "${SCO_RELAY_CMAKE_COMMAND}"
    "-DSCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT=${_configure_probe_build}/environment.txt"
    "-DSCO_RELAY_CMAKE_CACHE=${_configure_probe_build}/CMakeCache.txt"
    -P "${_script}"
  RESULT_VARIABLE _real_build_check_result
  OUTPUT_VARIABLE _real_build_check_stdout
  ERROR_VARIABLE _real_build_check_stderr)
if(NOT _real_build_check_result EQUAL 0)
  message(FATAL_ERROR "the real CMake cache should pass the build-time guard: ${_real_build_check_stderr}${_real_build_check_stdout}")
endif()
execute_process(
  COMMAND "${SCO_RELAY_CMAKE_COMMAND}" -E env "CXXFLAGS=-DRELAY_ENVIRONMENT_POISON=1"
    "${SCO_RELAY_CMAKE_COMMAND}" -S "${_configure_probe_source}"
    -B "${SCO_RELAY_TEST_BINARY_DIR}/configure-probe-poisoned"
  RESULT_VARIABLE _real_poisoned_configure_result
  OUTPUT_VARIABLE _real_poisoned_configure_stdout
  ERROR_VARIABLE _real_poisoned_configure_stderr)
if(_real_poisoned_configure_result EQUAL 0)
  message(FATAL_ERROR "CXXFLAGS imported by a real CMake project() should fail configuration")
endif()
execute_process(
  COMMAND "${SCO_RELAY_CMAKE_COMMAND}"
    "-DCMAKE_CXX_FLAGS_INIT=-include ${_bad_include}/injected.h"
    -S "${_configure_probe_source}" -B "${SCO_RELAY_TEST_BINARY_DIR}/configure-probe-poisoned-init"
  RESULT_VARIABLE _real_poisoned_init_result
  OUTPUT_VARIABLE _real_poisoned_init_stdout
  ERROR_VARIABLE _real_poisoned_init_stderr)
if(_real_poisoned_init_result EQUAL 0)
  message(FATAL_ERROR "caller-supplied CMAKE_CXX_FLAGS_INIT should fail before project()")
endif()
execute_process(
  COMMAND "${SCO_RELAY_CMAKE_COMMAND}"
    "-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=${_script}"
    -S "${_configure_probe_source}" -B "${SCO_RELAY_TEST_BINARY_DIR}/configure-probe-hook"
  RESULT_VARIABLE _real_hook_result
  OUTPUT_VARIABLE _real_hook_stdout
  ERROR_VARIABLE _real_hook_stderr)
if(_real_hook_result EQUAL 0)
  message(FATAL_ERROR "a caller-supplied CMake project hook should fail before project()")
endif()

# Model the standard compiler library search list. All MSVC fixture subprocesses inherit it unless a negative test
# replaces it with an external path.
set(ENV{LIB} "${_good_library};")

execute_process(
  COMMAND "${SCO_RELAY_CMAKE_COMMAND}" -E env
    "INCLUDE=${_good_include}"
    "LIB=${_good_library}"
    "${SCO_RELAY_CMAKE_COMMAND}"
    "-DCMAKE_CXX_COMPILER_ID=MSVC"
    "-DCMAKE_CXX_COMPILER=${_compiler}"
    "-DCMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION=10.0.26100.0"
    ${_cmake_flag_cli_args}
    "-DCMAKE_BINARY_DIR=${SCO_RELAY_TEST_BINARY_DIR}"
    "-DSCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT=${_snapshot}"
    -P "${_script}"
  RESULT_VARIABLE _configure_result
  OUTPUT_VARIABLE _configure_stdout
  ERROR_VARIABLE _configure_stderr)
if(NOT _configure_result EQUAL 0)
  message(FATAL_ERROR "selected MSVC INCLUDE root should pass configuration: ${_configure_stderr}${_configure_stdout}")
endif()

execute_process(
  COMMAND "${SCO_RELAY_CMAKE_COMMAND}" -E env "INCLUDE=${_good_include}"
    "${SCO_RELAY_CMAKE_COMMAND}"
    -DCMAKE_CXX_COMPILER_ID=MSVC
    ${_cmake_flag_cli_args}
    "-DCMAKE_CXX_FLAGS=/FI${_bad_include}/injected.h"
    "-DCMAKE_BINARY_DIR=${SCO_RELAY_TEST_BINARY_DIR}"
    "-DSCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT=${SCO_RELAY_TEST_BINARY_DIR}/poisoned-global.txt"
    -P "${_script}"
  RESULT_VARIABLE _configure_global_flag_result
  OUTPUT_VARIABLE _configure_global_flag_stdout
  ERROR_VARIABLE _configure_global_flag_stderr)
if(_configure_global_flag_result EQUAL 0)
  message(FATAL_ERROR "an explicit CMAKE_CXX_FLAGS forced include should fail configuration")
endif()

execute_process(
  COMMAND "${SCO_RELAY_CMAKE_COMMAND}" -E env "INCLUDE=${_good_include}"
    "${SCO_RELAY_CMAKE_COMMAND}"
    -DCMAKE_CXX_COMPILER_ID=MSVC
    ${_cmake_flag_cli_args}
    "-DCMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG /FI${_bad_include}/injected.h"
    "-DCMAKE_BINARY_DIR=${SCO_RELAY_TEST_BINARY_DIR}"
    "-DSCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT=${SCO_RELAY_TEST_BINARY_DIR}/poisoned-config.txt"
    -P "${_script}"
  RESULT_VARIABLE _configure_config_flag_result
  OUTPUT_VARIABLE _configure_config_flag_stdout
  ERROR_VARIABLE _configure_config_flag_stderr)
if(_configure_config_flag_result EQUAL 0)
  message(FATAL_ERROR "an explicit CMAKE_CXX_FLAGS_RELEASE forced include should fail configuration")
endif()

execute_process(
  COMMAND "${SCO_RELAY_CMAKE_COMMAND}" -E env "INCLUDE=${_good_include}"
    "${SCO_RELAY_CMAKE_COMMAND}"
    "-DSCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT=${_snapshot}"
    "-DSCO_RELAY_CMAKE_CACHE=${_cache}"
    -DSCO_RELAY_COMPILER_ID=MSVC
    -P "${_script}"
  RESULT_VARIABLE _unchanged_result
  OUTPUT_VARIABLE _unchanged_stdout
  ERROR_VARIABLE _unchanged_stderr)
if(NOT _unchanged_result EQUAL 0)
  message(FATAL_ERROR "unchanged selected MSVC INCLUDE should pass the build check: ${_unchanged_stderr}${_unchanged_stdout}")
endif()

# The Visual Studio generator builds through MSBuild project properties, where Microsoft documents that INCLUDE is
# ignored; MSBuild can still add an empty component to the child environment. The build-time guard must use the
# already validated configure snapshot only for this exact generator/compiler pair.
file(APPEND "${_snapshot}" "msvc_empty_lib_probe=passed\n")
execute_process(
  COMMAND "${SCO_RELAY_CMAKE_COMMAND}" -E env "INCLUDE=;"
    "${SCO_RELAY_CMAKE_COMMAND}"
    "-DSCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT=${_snapshot}"
    "-DSCO_RELAY_CMAKE_CACHE=${_cache}"
    -DSCO_RELAY_COMPILER_ID=MSVC
    "-DSCO_RELAY_CMAKE_GENERATOR=Visual Studio 17 2022"
    -DSCO_RELAY_MSVC_IDE_BUILD=ON
    -DSCO_RELAY_MSVC_EMPTY_LIB_PROBE=ON
    -P "${_script}"
  RESULT_VARIABLE _msbuild_include_result
  OUTPUT_VARIABLE _msbuild_include_stdout
  ERROR_VARIABLE _msbuild_include_stderr)
if(NOT _msbuild_include_result EQUAL 0)
  message(FATAL_ERROR "the verified Visual Studio build may ignore its MSBuild INCLUDE environment: ${_msbuild_include_stderr}${_msbuild_include_stdout}")
endif()

execute_process(
  COMMAND "${SCO_RELAY_CMAKE_COMMAND}" -E env "INCLUDE=;" "LIB=;"
    "${SCO_RELAY_CMAKE_COMMAND}"
    "-DSCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT=${_snapshot}"
    "-DSCO_RELAY_CMAKE_CACHE=${_cache}"
    -DSCO_RELAY_COMPILER_ID=MSVC
    "-DSCO_RELAY_CMAKE_GENERATOR=Visual Studio 17 2022"
    -DSCO_RELAY_MSVC_IDE_BUILD=ON
    -DSCO_RELAY_MSVC_EMPTY_LIB_PROBE=ON
    -P "${_script}"
  RESULT_VARIABLE _msbuild_empty_library_result
  OUTPUT_VARIABLE _msbuild_empty_library_stdout
  ERROR_VARIABLE _msbuild_empty_library_stderr)
if(NOT _msbuild_empty_library_result EQUAL 0)
  message(FATAL_ERROR "a probed Visual Studio build with no usable LIB directories should not add search paths: ${_msbuild_empty_library_stderr}${_msbuild_empty_library_stdout}")
endif()

set(_empty_config_snapshot "${SCO_RELAY_TEST_BINARY_DIR}/empty-config-libraries.txt")
execute_process(
  COMMAND "${SCO_RELAY_CMAKE_COMMAND}" -E env "INCLUDE=" "LIB="
    "${SCO_RELAY_CMAKE_COMMAND}"
    "-DCMAKE_CXX_COMPILER_ID=MSVC"
    "-DCMAKE_CXX_COMPILER=${_compiler}"
    "-DCMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION=10.0.26100.0"
    ${_cmake_flag_cli_args}
    "-DCMAKE_BINARY_DIR=${SCO_RELAY_TEST_BINARY_DIR}"
    "-DSCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT=${_empty_config_snapshot}"
    -P "${_script}"
  RESULT_VARIABLE _empty_config_libraries_result
  OUTPUT_VARIABLE _empty_config_libraries_stdout
  ERROR_VARIABLE _empty_config_libraries_stderr)
if(NOT _empty_config_libraries_result EQUAL 0)
  message(FATAL_ERROR "an MSVC configure with no exported INCLUDE or LIB should still record selected roots: ${_empty_config_libraries_stderr}${_empty_config_libraries_stdout}")
endif()
file(APPEND "${_empty_config_snapshot}" "msvc_empty_lib_probe=passed\n")
execute_process(
  COMMAND "${SCO_RELAY_CMAKE_COMMAND}" -E env "INCLUDE=;" "LIB=${_good_library}"
    "${SCO_RELAY_CMAKE_COMMAND}"
    "-DSCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT=${_empty_config_snapshot}"
    "-DSCO_RELAY_CMAKE_CACHE=${_cache}"
    -DSCO_RELAY_COMPILER_ID=MSVC
    "-DSCO_RELAY_CMAKE_GENERATOR=Visual Studio 17 2022"
    -DSCO_RELAY_MSVC_IDE_BUILD=ON
    -DSCO_RELAY_MSVC_EMPTY_LIB_PROBE=ON
    -P "${_script}"
  RESULT_VARIABLE _msbuild_selected_root_library_result
  OUTPUT_VARIABLE _msbuild_selected_root_library_stdout
  ERROR_VARIABLE _msbuild_selected_root_library_stderr)
if(NOT _msbuild_selected_root_library_result EQUAL 0)
  message(FATAL_ERROR "a Visual Studio build may populate LIB from the configure-selected roots: ${_msbuild_selected_root_library_stderr}${_msbuild_selected_root_library_stdout}")
endif()

execute_process(
  COMMAND "${SCO_RELAY_CMAKE_COMMAND}" -E env "INCLUDE=;" "LIB=${_good_auxiliary_library}"
    "${SCO_RELAY_CMAKE_COMMAND}"
    "-DSCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT=${_snapshot}"
    "-DSCO_RELAY_CMAKE_CACHE=${_cache}"
    -DSCO_RELAY_COMPILER_ID=MSVC
    "-DSCO_RELAY_CMAKE_GENERATOR=Visual Studio 17 2022"
    -DSCO_RELAY_MSVC_IDE_BUILD=ON
    -DSCO_RELAY_MSVC_EMPTY_LIB_PROBE=ON
    -P "${_script}"
  RESULT_VARIABLE _msbuild_auxiliary_library_result
  OUTPUT_VARIABLE _msbuild_auxiliary_library_stdout
  ERROR_VARIABLE _msbuild_auxiliary_library_stderr)
if(NOT _msbuild_auxiliary_library_result EQUAL 0)
  message(FATAL_ERROR "the selected Visual Studio auxiliary library directory should pass the recorded-root guard: ${_msbuild_auxiliary_library_stderr}${_msbuild_auxiliary_library_stdout}")
endif()

execute_process(
  COMMAND "${SCO_RELAY_CMAKE_COMMAND}" -E env "INCLUDE=;" "LIB=${_good_unittest_library}"
    "${SCO_RELAY_CMAKE_COMMAND}"
    "-DSCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT=${_snapshot}"
    "-DSCO_RELAY_CMAKE_CACHE=${_cache}"
    -DSCO_RELAY_COMPILER_ID=MSVC
    "-DSCO_RELAY_CMAKE_GENERATOR=Visual Studio 17 2022"
    -DSCO_RELAY_MSVC_IDE_BUILD=ON
    -DSCO_RELAY_MSVC_EMPTY_LIB_PROBE=ON
    -P "${_script}"
  RESULT_VARIABLE _msbuild_unittest_library_result
  OUTPUT_VARIABLE _msbuild_unittest_library_stdout
  ERROR_VARIABLE _msbuild_unittest_library_stderr)
if(NOT _msbuild_unittest_library_result EQUAL 0)
  message(FATAL_ERROR "the selected Visual Studio unit-test library directory should pass the recorded-root guard: ${_msbuild_unittest_library_stderr}${_msbuild_unittest_library_stdout}")
endif()

foreach(_empty_lib_case IN ITEMS leading interior trailing)
  if(_empty_lib_case STREQUAL "interior")
    set(_case_snapshot "${SCO_RELAY_TEST_BINARY_DIR}/multiple-libraries.txt")
    execute_process(
      COMMAND "${SCO_RELAY_CMAKE_COMMAND}" -E env
        "INCLUDE=${_good_include}" "LIB=${_good_library};${_good_library_alt}"
        "${SCO_RELAY_CMAKE_COMMAND}"
        "-DCMAKE_CXX_COMPILER_ID=MSVC"
        "-DCMAKE_CXX_COMPILER=${_compiler}"
        "-DCMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION=10.0.26100.0"
        ${_cmake_flag_cli_args}
        "-DCMAKE_BINARY_DIR=${SCO_RELAY_TEST_BINARY_DIR}"
        "-DSCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT=${_case_snapshot}"
        -P "${_script}"
      RESULT_VARIABLE _multi_library_configure_result
      OUTPUT_VARIABLE _multi_library_configure_stdout
      ERROR_VARIABLE _multi_library_configure_stderr)
    if(NOT _multi_library_configure_result EQUAL 0)
      message(FATAL_ERROR "trusted multi-directory LIB should configure: ${_multi_library_configure_stderr}${_multi_library_configure_stdout}")
    endif()
    # This fixture models the configure-time MSVC probe; its separate Windows test executes the real compiler probe.
    file(APPEND "${_case_snapshot}" "msvc_empty_lib_probe=passed\n")
    set(_case_libraries "${_good_library};;${_good_library_alt}")
  elseif(_empty_lib_case STREQUAL "leading")
    set(_case_snapshot "${_snapshot}")
    set(_case_libraries ";${_good_library}")
  else()
    set(_case_snapshot "${_snapshot}")
    set(_case_libraries "${_good_library};")
  endif()
  execute_process(
    COMMAND "${SCO_RELAY_CMAKE_COMMAND}" -E env "INCLUDE=;" "LIB=${_case_libraries}"
      "${SCO_RELAY_CMAKE_COMMAND}"
      "-DSCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT=${_case_snapshot}"
      "-DSCO_RELAY_CMAKE_CACHE=${_cache}"
      -DSCO_RELAY_COMPILER_ID=MSVC
      "-DSCO_RELAY_CMAKE_GENERATOR=Visual Studio 17 2022"
      -DSCO_RELAY_MSVC_IDE_BUILD=ON
      -DSCO_RELAY_MSVC_EMPTY_LIB_PROBE=ON
      -P "${_script}"
    RESULT_VARIABLE _empty_lib_build_result
    OUTPUT_VARIABLE _empty_lib_build_stdout
    ERROR_VARIABLE _empty_lib_build_stderr)
  if(NOT _empty_lib_build_result EQUAL 0)
    message(FATAL_ERROR "a probed Visual Studio build should accept ${_empty_lib_case} empty LIB components without adding paths: ${_empty_lib_build_stderr}${_empty_lib_build_stdout}")
  endif()
endforeach()

execute_process(
  COMMAND "${SCO_RELAY_CMAKE_COMMAND}" -E env "INCLUDE=;" "LIB=${_bad_library}"
    "${SCO_RELAY_CMAKE_COMMAND}"
    "-DSCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT=${_snapshot}"
    "-DSCO_RELAY_CMAKE_CACHE=${_cache}"
    -DSCO_RELAY_COMPILER_ID=MSVC
    "-DSCO_RELAY_CMAKE_GENERATOR=Visual Studio 17 2022"
    -DSCO_RELAY_MSVC_IDE_BUILD=ON
    -DSCO_RELAY_MSVC_EMPTY_LIB_PROBE=ON
    -P "${_script}"
  RESULT_VARIABLE _msbuild_untrusted_library_result
  OUTPUT_VARIABLE _msbuild_untrusted_library_stdout
  ERROR_VARIABLE _msbuild_untrusted_library_stderr)
if(_msbuild_untrusted_library_result EQUAL 0)
  message(FATAL_ERROR "the Visual Studio probe must not authorize a new LIB directory")
endif()

execute_process(
  COMMAND "${SCO_RELAY_CMAKE_COMMAND}" -E env "INCLUDE=;" "LIB=${_good_library};;${_bad_library}"
    "${SCO_RELAY_CMAKE_COMMAND}"
    "-DSCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT=${_snapshot}"
    "-DSCO_RELAY_CMAKE_CACHE=${_cache}"
    -DSCO_RELAY_COMPILER_ID=MSVC
    "-DSCO_RELAY_CMAKE_GENERATOR=Visual Studio 17 2022"
    -DSCO_RELAY_MSVC_IDE_BUILD=ON
    -DSCO_RELAY_MSVC_EMPTY_LIB_PROBE=ON
    -P "${_script}"
  RESULT_VARIABLE _msbuild_mixed_untrusted_library_result
  OUTPUT_VARIABLE _msbuild_mixed_untrusted_library_stdout
  ERROR_VARIABLE _msbuild_mixed_untrusted_library_stderr)
if(_msbuild_mixed_untrusted_library_result EQUAL 0)
  message(FATAL_ERROR "empty Visual Studio LIB separators must not authorize a new external directory")
endif()

execute_process(
  COMMAND "${SCO_RELAY_CMAKE_COMMAND}" -E env "INCLUDE=;"
    "${SCO_RELAY_CMAKE_COMMAND}"
    "-DSCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT=${_snapshot}"
    "-DSCO_RELAY_CMAKE_CACHE=${_cache}"
    -DSCO_RELAY_COMPILER_ID=MSVC
    -DSCO_RELAY_CMAKE_GENERATOR=Ninja
    -DSCO_RELAY_MSVC_IDE_BUILD=ON
    -DSCO_RELAY_MSVC_EMPTY_LIB_PROBE=ON
    -P "${_script}"
  RESULT_VARIABLE _invalid_msbuild_include_result
  OUTPUT_VARIABLE _invalid_msbuild_include_stdout
  ERROR_VARIABLE _invalid_msbuild_include_stderr)
if(_invalid_msbuild_include_result EQUAL 0)
  message(FATAL_ERROR "the Visual Studio INCLUDE exception must reject a non-Visual-Studio generator")
endif()

execute_process(
  COMMAND "${SCO_RELAY_CMAKE_COMMAND}" -E env "INCLUDE=${_good_include};${_bad_include}"
    "${SCO_RELAY_CMAKE_COMMAND}"
    "-DSCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT=${_snapshot}"
    "-DSCO_RELAY_CMAKE_CACHE=${_cache}"
    -DSCO_RELAY_COMPILER_ID=MSVC
    -P "${_script}"
  RESULT_VARIABLE _changed_result
  OUTPUT_VARIABLE _changed_stdout
  ERROR_VARIABLE _changed_stderr)
if(_changed_result EQUAL 0)
  message(FATAL_ERROR "a post-configure external INCLUDE path should fail")
endif()

execute_process(
  COMMAND "${SCO_RELAY_CMAKE_COMMAND}" -E env "INCLUDE=${_good_include}" "LIB=${_bad_library}"
    "${SCO_RELAY_CMAKE_COMMAND}"
    "-DSCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT=${_snapshot}"
    "-DSCO_RELAY_CMAKE_CACHE=${_cache}"
    -DSCO_RELAY_COMPILER_ID=MSVC
    -P "${_script}"
  RESULT_VARIABLE _changed_library_result
  OUTPUT_VARIABLE _changed_library_stdout
  ERROR_VARIABLE _changed_library_stderr)
if(_changed_library_result EQUAL 0)
  message(FATAL_ERROR "a post-configure external LIB path should fail")
endif()

execute_process(
  COMMAND "${SCO_RELAY_CMAKE_COMMAND}" -E env "INCLUDE=${_good_include}" "LIB=${_bad_library}"
    "${SCO_RELAY_CMAKE_COMMAND}"
    "-DCMAKE_CXX_COMPILER_ID=MSVC"
    "-DCMAKE_CXX_COMPILER=${_compiler}"
    "-DCMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION=10.0.26100.0"
    ${_cmake_flag_cli_args}
    "-DCMAKE_BINARY_DIR=${SCO_RELAY_TEST_BINARY_DIR}"
    "-DSCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT=${SCO_RELAY_TEST_BINARY_DIR}/external-library-config.txt"
    -P "${_script}"
  RESULT_VARIABLE _external_library_result
  OUTPUT_VARIABLE _external_library_stdout
  ERROR_VARIABLE _external_library_stderr)
if(_external_library_result EQUAL 0)
  message(FATAL_ERROR "an external LIB directory should fail configuration")
endif()

execute_process(
  COMMAND "${SCO_RELAY_CMAKE_COMMAND}" -E env "INCLUDE=${_good_include}"
    "LIB=${_good_library};;${_good_library}"
    "${SCO_RELAY_CMAKE_COMMAND}"
    "-DCMAKE_CXX_COMPILER_ID=MSVC"
    "-DCMAKE_CXX_COMPILER=${_compiler}"
    "-DCMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION=10.0.26100.0"
    ${_cmake_flag_cli_args}
    "-DCMAKE_BINARY_DIR=${SCO_RELAY_TEST_BINARY_DIR}"
    "-DSCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT=${SCO_RELAY_TEST_BINARY_DIR}/empty-library-entry.txt"
    -P "${_script}"
  RESULT_VARIABLE _empty_library_entry_result
  OUTPUT_VARIABLE _empty_library_entry_stdout
  ERROR_VARIABLE _empty_library_entry_stderr)
if(_empty_library_entry_result EQUAL 0)
  message(FATAL_ERROR "an interior empty LIB search entry should fail configuration")
endif()

execute_process(
  COMMAND "${SCO_RELAY_CMAKE_COMMAND}" -E env "INCLUDE=${_bad_include}"
    "${SCO_RELAY_CMAKE_COMMAND}"
    "-DCMAKE_CXX_COMPILER_ID=MSVC"
    "-DCMAKE_CXX_COMPILER=${_compiler}"
    "-DCMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION=10.0.26100.0"
    ${_cmake_flag_cli_args}
    "-DCMAKE_BINARY_DIR=${SCO_RELAY_TEST_BINARY_DIR}"
    "-DSCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT=${SCO_RELAY_TEST_BINARY_DIR}/rejected.txt"
    -P "${_script}"
  RESULT_VARIABLE _foreign_result
  OUTPUT_VARIABLE _foreign_stdout
  ERROR_VARIABLE _foreign_stderr)
if(_foreign_result EQUAL 0)
  message(FATAL_ERROR "an external INCLUDE directory should fail configuration")
endif()

execute_process(
  COMMAND "${SCO_RELAY_CMAKE_COMMAND}" -E env
    "ProgramFiles(x86)=${_spoof_program_files}"
    "INCLUDE=${_spoof_sdk_include}"
    "${SCO_RELAY_CMAKE_COMMAND}"
    -DCMAKE_CXX_COMPILER_ID=MSVC
    "-DCMAKE_CXX_COMPILER=${_compiler}"
    "-DCMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION=10.0.26100.0"
    ${_cmake_flag_cli_args}
    "-DCMAKE_BINARY_DIR=${SCO_RELAY_TEST_BINARY_DIR}"
    "-DSCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT=${SCO_RELAY_TEST_BINARY_DIR}/spoofed.txt"
    -P "${_script}"
  RESULT_VARIABLE _spoofed_sdk_result
  OUTPUT_VARIABLE _spoofed_sdk_stdout
  ERROR_VARIABLE _spoofed_sdk_stderr)
if(_spoofed_sdk_result EQUAL 0)
  message(FATAL_ERROR "a caller-supplied ProgramFiles(x86) root should not authorize INCLUDE")
endif()

execute_process(
  COMMAND "${SCO_RELAY_CMAKE_COMMAND}" -E env "INCLUDE=${_good_include}"
    "${SCO_RELAY_CMAKE_COMMAND}"
    -DCMAKE_CXX_COMPILER_ID=Clang
    "-DCMAKE_BINARY_DIR=${SCO_RELAY_TEST_BINARY_DIR}"
    "-DSCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT=${SCO_RELAY_TEST_BINARY_DIR}/clang.txt"
    -P "${_script}"
  RESULT_VARIABLE _clang_include_result
  OUTPUT_VARIABLE _clang_include_stdout
  ERROR_VARIABLE _clang_include_stderr)
if(_clang_include_result EQUAL 0)
  message(FATAL_ERROR "INCLUDE on non-MSVC should fail configuration")
endif()

execute_process(
  COMMAND "${SCO_RELAY_CMAKE_COMMAND}" -E env "INCLUDE=" "LIB=${_good_library}"
    "${SCO_RELAY_CMAKE_COMMAND}"
    "-DCMAKE_CXX_COMPILER_ID=Clang"
    "-DCMAKE_BINARY_DIR=${SCO_RELAY_TEST_BINARY_DIR}"
    "-DSCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT=${SCO_RELAY_TEST_BINARY_DIR}/clang-lib.txt"
    -P "${_script}"
  RESULT_VARIABLE _clang_library_result
  OUTPUT_VARIABLE _clang_library_stdout
  ERROR_VARIABLE _clang_library_stderr)
if(_clang_library_result EQUAL 0)
  message(FATAL_ERROR "LIB on non-MSVC should fail configuration")
endif()

execute_process(
  COMMAND "${SCO_RELAY_CMAKE_COMMAND}" -E env "CPATH=${_bad_include}"
    "${SCO_RELAY_CMAKE_COMMAND}"
    "-DSCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT=${_snapshot}"
    "-DSCO_RELAY_CMAKE_CACHE=${_cache}"
    -DSCO_RELAY_COMPILER_ID=MSVC
    -P "${_script}"
  RESULT_VARIABLE _cpath_result
  OUTPUT_VARIABLE _cpath_stdout
  ERROR_VARIABLE _cpath_stderr)
if(_cpath_result EQUAL 0)
  message(FATAL_ERROR "a CPATH override should fail even with a valid MSVC INCLUDE snapshot")
endif()

foreach(_compiler_flags_variable IN ITEMS CFLAGS CXXFLAGS OBJCFLAGS LDFLAGS)
  execute_process(
    COMMAND "${SCO_RELAY_CMAKE_COMMAND}" -E env "${_compiler_flags_variable}=/FI${_bad_include}/injected.h"
      "${SCO_RELAY_CMAKE_COMMAND}"
      "-DSCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT=${_snapshot}"
    "-DSCO_RELAY_CMAKE_CACHE=${_cache}"
      -DSCO_RELAY_COMPILER_ID=MSVC
      -P "${_script}"
    RESULT_VARIABLE _flags_result
    OUTPUT_VARIABLE _flags_stdout
    ERROR_VARIABLE _flags_stderr)
  if(_flags_result EQUAL 0)
    message(FATAL_ERROR "${_compiler_flags_variable} should fail even after configure")
  endif()
endforeach()

execute_process(
  COMMAND "${SCO_RELAY_CMAKE_COMMAND}" -E env "INCLUDE=${_good_include}"
    "${SCO_RELAY_CMAKE_COMMAND}"
    "-DSCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT=${_snapshot}"
    "-DSCO_RELAY_CMAKE_CACHE=${_cache}"
    -DSCO_RELAY_COMPILER_ID=MSVC
    -P "${_script}"
  RESULT_VARIABLE _safe_cache_result
  OUTPUT_VARIABLE _safe_cache_stdout
  ERROR_VARIABLE _safe_cache_stderr)
if(NOT _safe_cache_result EQUAL 0)
  message(FATAL_ERROR "empty CMake global flags should pass: ${_safe_cache_stderr}${_safe_cache_stdout}")
endif()

string(REPLACE "CMAKE_CXX_FLAGS_RELEASE:STRING=/O2 /Ob2 /DNDEBUG"
  "CMAKE_CXX_FLAGS_RELEASE:STRING=/O2 /Ob2 /DNDEBUG /FI${_bad_include}/injected.h"
  _poisoned_config_cache_text "${_msvc_config_cache_text}")
file(WRITE "${SCO_RELAY_TEST_BINARY_DIR}/poisoned-cache.txt"
  "CMAKE_C_FLAGS:STRING=\nCMAKE_CXX_FLAGS:STRING=/FI${_bad_include}/injected.h\n"
  "CMAKE_EXE_LINKER_FLAGS:STRING=/DEFAULTLIB:probe_missing.lib\n"
  "CMAKE_SHARED_LINKER_FLAGS:STRING=\nCMAKE_MODULE_LINKER_FLAGS:STRING=\nCMAKE_STATIC_LINKER_FLAGS:STRING=\n"
  "${_poisoned_config_cache_text}")
execute_process(
  COMMAND "${SCO_RELAY_CMAKE_COMMAND}" -E env "INCLUDE=${_good_include}"
    "${SCO_RELAY_CMAKE_COMMAND}"
    "-DSCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT=${_snapshot}"
    "-DSCO_RELAY_CMAKE_CACHE=${SCO_RELAY_TEST_BINARY_DIR}/poisoned-cache.txt"
    -DSCO_RELAY_COMPILER_ID=MSVC
    -P "${_script}"
  RESULT_VARIABLE _poisoned_cache_result
  OUTPUT_VARIABLE _poisoned_cache_stdout
  ERROR_VARIABLE _poisoned_cache_stderr)
if(_poisoned_cache_result EQUAL 0)
  message(FATAL_ERROR "cached forced-include and default-library flags should fail at build time")
endif()
