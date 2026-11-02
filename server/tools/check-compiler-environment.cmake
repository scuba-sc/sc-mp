# SPDX-License-Identifier: GPL-3.0-only
# Compiler environment variables can add implicit headers after CMake has generated compile_commands.json. Keep the
# product build deterministic by refusing such overrides both during configuration and immediately before targets build.
set(_sco_relay_compiler_environment_variables
  CPATH CPLUS_INCLUDE_PATH C_INCLUDE_PATH OBJC_INCLUDE_PATH
  CFLAGS CXXFLAGS OBJCFLAGS LDFLAGS
  LIBRARY_PATH LINK _LINK_
  GCC_EXEC_PREFIX COMPILER_PATH CL _CL_ CCC_OVERRIDE_OPTIONS)
foreach(_sco_relay_environment_name IN LISTS _sco_relay_compiler_environment_variables)
  if(NOT "$ENV{${_sco_relay_environment_name}}" STREQUAL "")
    message(FATAL_ERROR
      "sc-mp relay: ${_sco_relay_environment_name} changes compiler or linker search/options; unset it and reconfigure")
  endif()
endforeach()

set(_sco_relay_cmake_flag_variables
  CMAKE_C_FLAGS CMAKE_CXX_FLAGS CMAKE_EXE_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS
  CMAKE_MODULE_LINKER_FLAGS CMAKE_STATIC_LINKER_FLAGS
  CMAKE_C_FLAGS_DEBUG CMAKE_CXX_FLAGS_DEBUG
  CMAKE_C_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_MINSIZEREL
  CMAKE_C_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELEASE
  CMAKE_C_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_RELWITHDEBINFO
  CMAKE_EXE_LINKER_FLAGS_DEBUG CMAKE_SHARED_LINKER_FLAGS_DEBUG CMAKE_MODULE_LINKER_FLAGS_DEBUG
  CMAKE_EXE_LINKER_FLAGS_MINSIZEREL CMAKE_SHARED_LINKER_FLAGS_MINSIZEREL CMAKE_MODULE_LINKER_FLAGS_MINSIZEREL
  CMAKE_EXE_LINKER_FLAGS_RELEASE CMAKE_SHARED_LINKER_FLAGS_RELEASE CMAKE_MODULE_LINKER_FLAGS_RELEASE
  CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO CMAKE_MODULE_LINKER_FLAGS_RELWITHDEBINFO
  CMAKE_STATIC_LINKER_FLAGS_DEBUG CMAKE_STATIC_LINKER_FLAGS_MINSIZEREL
  CMAKE_STATIC_LINKER_FLAGS_RELEASE CMAKE_STATIC_LINKER_FLAGS_RELWITHDEBINFO)

if(DEFINED CMAKE_CXX_COMPILER_ID)
  # The audited targets do not use caller-supplied global flags. Compare all generic and per-configuration compiler/
  # linker flags with CMake's selected platform defaults; this catches environment and cache injections while
  # retaining the stock MSVC flags and architecture options required by the Visual Studio generator.
  if(CMAKE_TOOLCHAIN_FILE OR CMAKE_USER_MAKE_RULES_OVERRIDE OR CMAKE_USER_MAKE_RULES_OVERRIDE_C OR
     CMAKE_USER_MAKE_RULES_OVERRIDE_CXX OR CMAKE_PROJECT_INCLUDE_BEFORE OR CMAKE_PROJECT_INCLUDE OR
     CMAKE_PROJECT_sc-mp_INCLUDE_BEFORE OR CMAKE_PROJECT_sc-mp_INCLUDE OR
     CMAKE_PROJECT_TOP_LEVEL_INCLUDES OR
     CMAKE_C_COMPILER_LAUNCHER OR CMAKE_CXX_COMPILER_LAUNCHER OR
     CMAKE_C_COMPILER_ARG1 OR CMAKE_CXX_COMPILER_ARG1 OR
     CMAKE_C_COMPILER_EXTERNAL_TOOLCHAIN OR CMAKE_CXX_COMPILER_EXTERNAL_TOOLCHAIN)
    message(FATAL_ERROR "sc-mp relay: custom CMake toolchains, rules hooks and compiler launchers are not allowed")
  endif()
  foreach(_sco_relay_flag_variable IN LISTS _sco_relay_cmake_flag_variables)
    if(NOT DEFINED ${_sco_relay_flag_variable})
      message(FATAL_ERROR "sc-mp relay: ${_sco_relay_flag_variable} is missing after project()")
    endif()
    set(_sco_relay_init_variable "${_sco_relay_flag_variable}_INIT")
    if(DEFINED ${_sco_relay_init_variable})
      set(_sco_relay_expected_flag_value "${${_sco_relay_init_variable}}")
    else()
      set(_sco_relay_expected_flag_value "")
    endif()
    string(STRIP "${${_sco_relay_flag_variable}}" _sco_relay_actual_flag_value)
    string(STRIP "${_sco_relay_expected_flag_value}" _sco_relay_expected_flag_value)
    if(NOT "${_sco_relay_actual_flag_value}" STREQUAL "${_sco_relay_expected_flag_value}")
      message(FATAL_ERROR "sc-mp relay: ${_sco_relay_flag_variable} differs from the selected platform default")
    endif()
  endforeach()
endif()

if(NOT DEFINED SCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT OR SCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT STREQUAL "")
  if(NOT DEFINED CMAKE_BINARY_DIR OR CMAKE_BINARY_DIR STREQUAL "")
    message(FATAL_ERROR "sc-mp relay: compiler environment snapshot path is required")
  endif()
  set(SCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT "${CMAKE_BINARY_DIR}/sco-relay-compiler-environment.txt")
endif()

if(NOT DEFINED SCO_RELAY_MSVC_IDE_BUILD)
  set(SCO_RELAY_MSVC_IDE_BUILD OFF)
endif()
if(NOT DEFINED SCO_RELAY_MSVC_EMPTY_LIB_PROBE)
  set(SCO_RELAY_MSVC_EMPTY_LIB_PROBE OFF)
endif()
set(_sco_relay_msvc_ide_build_check FALSE)
if(DEFINED CMAKE_CXX_COMPILER_ID AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND
   CMAKE_GENERATOR MATCHES "^Visual Studio")
  set(SCO_RELAY_MSVC_IDE_BUILD ON)
elseif(SCO_RELAY_MSVC_IDE_BUILD AND NOT DEFINED CMAKE_CXX_COMPILER_ID)
  set(_sco_relay_msvc_ide_build_check TRUE)
  if(NOT DEFINED SCO_RELAY_COMPILER_ID OR NOT SCO_RELAY_COMPILER_ID STREQUAL "MSVC" OR
     NOT DEFINED SCO_RELAY_CMAKE_GENERATOR OR NOT SCO_RELAY_CMAKE_GENERATOR MATCHES "^Visual Studio" OR
     NOT SCO_RELAY_MSVC_EMPTY_LIB_PROBE)
    message(FATAL_ERROR "sc-mp relay: the Visual Studio environment exception requires its configure-time MSVC probe")
  endif()
elseif(SCO_RELAY_MSVC_IDE_BUILD)
  message(FATAL_ERROR "sc-mp relay: the Visual Studio environment exception requires native MSVC")
endif()

set(_sco_relay_include_dirs "")
if(NOT _sco_relay_msvc_ide_build_check AND NOT "$ENV{INCLUDE}" STREQUAL "")
  set(_sco_relay_raw_include_dirs "$ENV{INCLUDE}")
  foreach(_sco_relay_include_dir IN LISTS _sco_relay_raw_include_dirs)
    if(_sco_relay_include_dir STREQUAL "" OR NOT IS_DIRECTORY "${_sco_relay_include_dir}")
      message(FATAL_ERROR "sc-mp relay: INCLUDE contains an empty or unavailable directory: '${_sco_relay_include_dir}'")
    endif()
    file(REAL_PATH "${_sco_relay_include_dir}" _sco_relay_include_real)
    string(TOLOWER "${_sco_relay_include_real}" _sco_relay_include_key)
    list(APPEND _sco_relay_include_dirs "${_sco_relay_include_key}")
  endforeach()
endif()

# Visual Studio's developer environment sets LIB to the selected compiler and SDK libraries. It is an implicit
# linker search path, so admit it only after checking every entry against the selected toolchain and snapshot it
# across configuration and build. Other compilers must not inherit this Windows-specific input.
set(_sco_relay_library_dirs "")
set(_sco_relay_trusted_library_roots_normalized "")
if(NOT "$ENV{LIB}" STREQUAL "")
  # LINK searches its working directory before LIB. At configure time, require real entries only. A generated
  # Visual Studio build may add empty components to LIB; those are discarded only after the configure-time MSVC
  # link probe passes, and every remaining nonempty path must still match the validated snapshot.
  set(_sco_relay_library_path_for_validation "$ENV{LIB}")
  string(REGEX REPLACE ";+$" "" _sco_relay_library_path_for_validation
    "${_sco_relay_library_path_for_validation}")
  if(_sco_relay_library_path_for_validation STREQUAL "")
    if(NOT _sco_relay_msvc_ide_build_check OR NOT SCO_RELAY_MSVC_EMPTY_LIB_PROBE)
      message(FATAL_ERROR "sc-mp relay: LIB contains only empty search entries")
    endif()
  endif()
  set(_sco_relay_raw_library_dirs "${_sco_relay_library_path_for_validation}")
  foreach(_sco_relay_library_dir IN LISTS _sco_relay_raw_library_dirs)
    if(_sco_relay_library_dir STREQUAL "")
      if(_sco_relay_msvc_ide_build_check AND SCO_RELAY_MSVC_EMPTY_LIB_PROBE)
        continue()
      endif()
      message(FATAL_ERROR "sc-mp relay: LIB contains an empty search path")
    endif()
    if(NOT IS_DIRECTORY "${_sco_relay_library_dir}")
      message(FATAL_ERROR "sc-mp relay: LIB contains an empty or unavailable directory")
    endif()
    file(REAL_PATH "${_sco_relay_library_dir}" _sco_relay_library_real)
    string(TOLOWER "${_sco_relay_library_real}" _sco_relay_library_key)
    list(APPEND _sco_relay_library_dirs "${_sco_relay_library_key}")
  endforeach()
endif()

if(DEFINED CMAKE_CXX_COMPILER_ID)
  # Visual Studio's developer environment sets INCLUDE to compiler/SDK system headers. Admit those only when they
  # are inside the selected compiler installation or selected Windows SDK version.
  if(_sco_relay_include_dirs OR _sco_relay_library_dirs)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
      message(FATAL_ERROR "sc-mp relay: INCLUDE and LIB are accepted only for the native MSVC toolchain")
    endif()
    get_filename_component(_sco_relay_compiler "${CMAKE_CXX_COMPILER}" REALPATH)
    get_filename_component(_sco_relay_msvc_x64_dir "${_sco_relay_compiler}" DIRECTORY)
    get_filename_component(_sco_relay_msvc_host_dir "${_sco_relay_msvc_x64_dir}" DIRECTORY)
    get_filename_component(_sco_relay_msvc_bin_dir "${_sco_relay_msvc_host_dir}" DIRECTORY)
    get_filename_component(_sco_relay_msvc_root "${_sco_relay_msvc_bin_dir}" DIRECTORY)
    set(_sco_relay_trusted_include_roots "${_sco_relay_msvc_root}")

    if(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
      list(APPEND _sco_relay_trusted_include_roots
        "C:/Program Files (x86)/Windows Kits/10/Include/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}")
    endif()
    list(APPEND _sco_relay_trusted_include_roots
      "C:/Program Files (x86)/Windows Kits/NETFXSDK")

    set(_sco_relay_trusted_include_roots_normalized "")
    foreach(_sco_relay_root IN LISTS _sco_relay_trusted_include_roots)
      if(IS_DIRECTORY "${_sco_relay_root}")
        file(REAL_PATH "${_sco_relay_root}" _sco_relay_root_real)
        string(TOLOWER "${_sco_relay_root_real}" _sco_relay_root_key)
        list(APPEND _sco_relay_trusted_include_roots_normalized "${_sco_relay_root_key}")
      endif()
    endforeach()
    if(NOT _sco_relay_trusted_include_roots_normalized)
      message(FATAL_ERROR "sc-mp relay: could not identify the selected MSVC and Windows SDK include roots")
    endif()

    foreach(_sco_relay_include_key IN LISTS _sco_relay_include_dirs)
      set(_sco_relay_include_is_trusted FALSE)
      foreach(_sco_relay_root_key IN LISTS _sco_relay_trusted_include_roots_normalized)
        string(FIND "${_sco_relay_include_key}/" "${_sco_relay_root_key}/" _sco_relay_root_match)
        if(_sco_relay_root_match EQUAL 0)
          set(_sco_relay_include_is_trusted TRUE)
          break()
        endif()
      endforeach()
      if(NOT _sco_relay_include_is_trusted)
        message(FATAL_ERROR "sc-mp relay: INCLUDE contains a path outside the selected MSVC/Windows SDK roots")
      endif()
    endforeach()
  endif()

  if(_sco_relay_library_dirs)
    # The selected toolset root includes the standard C++ and optional ATL/MFC library directories. Visual Studio
    # also adds its auxiliary and unit-test libraries; keep those exact subtrees rooted in the selected install.
    # The NETFX SDK is installed beside the versioned Windows Kits SDK and is part of a normal VS environment.
    set(_sco_relay_trusted_library_roots "${_sco_relay_msvc_root}")
    get_filename_component(_sco_relay_msvc_version_parent "${_sco_relay_msvc_root}" DIRECTORY)
    get_filename_component(_sco_relay_vc_tools_dir "${_sco_relay_msvc_version_parent}" DIRECTORY)
    get_filename_component(_sco_relay_vc_dir "${_sco_relay_vc_tools_dir}" DIRECTORY)
    get_filename_component(_sco_relay_vs_root "${_sco_relay_vc_dir}" DIRECTORY)
    list(APPEND _sco_relay_trusted_library_roots
      "${_sco_relay_vs_root}/VC/Auxiliary/VS/lib"
      "${_sco_relay_vs_root}/VC/Auxiliary/VS/UnitTest/lib")
    if(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
      list(APPEND _sco_relay_trusted_library_roots
        "C:/Program Files (x86)/Windows Kits/10/Lib/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}")
    endif()
    list(APPEND _sco_relay_trusted_library_roots
      "C:/Program Files (x86)/Windows Kits/NETFXSDK")
    set(_sco_relay_trusted_library_roots_normalized "")
    foreach(_sco_relay_root IN LISTS _sco_relay_trusted_library_roots)
      if(IS_DIRECTORY "${_sco_relay_root}")
        file(REAL_PATH "${_sco_relay_root}" _sco_relay_root_real)
        string(TOLOWER "${_sco_relay_root_real}" _sco_relay_root_key)
        list(APPEND _sco_relay_trusted_library_roots_normalized "${_sco_relay_root_key}")
      endif()
    endforeach()
    if(NOT _sco_relay_trusted_library_roots_normalized)
      message(FATAL_ERROR "sc-mp relay: could not identify the selected MSVC and Windows SDK library roots")
    endif()

    foreach(_sco_relay_library_key IN LISTS _sco_relay_library_dirs)
      set(_sco_relay_library_is_trusted FALSE)
      foreach(_sco_relay_root_key IN LISTS _sco_relay_trusted_library_roots_normalized)
        string(FIND "${_sco_relay_library_key}/" "${_sco_relay_root_key}/" _sco_relay_root_match)
        if(_sco_relay_root_match EQUAL 0)
          set(_sco_relay_library_is_trusted TRUE)
          break()
        endif()
      endforeach()
      if(NOT _sco_relay_library_is_trusted)
        message(FATAL_ERROR "sc-mp relay: LIB contains a path outside the selected MSVC/Windows SDK roots")
      endif()
    endforeach()
  endif()

  # Visual Studio may not export LIB during CMake configuration; MSBuild can populate it later from the selected
  # compiler and target SDK. Record those roots even when there were no configure-time LIB entries to validate.
  if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND NOT _sco_relay_trusted_library_roots_normalized)
    get_filename_component(_sco_relay_compiler "${CMAKE_CXX_COMPILER}" REALPATH)
    get_filename_component(_sco_relay_msvc_x64_dir "${_sco_relay_compiler}" DIRECTORY)
    get_filename_component(_sco_relay_msvc_host_dir "${_sco_relay_msvc_x64_dir}" DIRECTORY)
    get_filename_component(_sco_relay_msvc_bin_dir "${_sco_relay_msvc_host_dir}" DIRECTORY)
    get_filename_component(_sco_relay_msvc_root "${_sco_relay_msvc_bin_dir}" DIRECTORY)
    set(_sco_relay_trusted_library_roots "${_sco_relay_msvc_root}")
    get_filename_component(_sco_relay_msvc_version_parent "${_sco_relay_msvc_root}" DIRECTORY)
    get_filename_component(_sco_relay_vc_tools_dir "${_sco_relay_msvc_version_parent}" DIRECTORY)
    get_filename_component(_sco_relay_vc_dir "${_sco_relay_vc_tools_dir}" DIRECTORY)
    get_filename_component(_sco_relay_vs_root "${_sco_relay_vc_dir}" DIRECTORY)
    list(APPEND _sco_relay_trusted_library_roots
      "${_sco_relay_vs_root}/VC/Auxiliary/VS/lib"
      "${_sco_relay_vs_root}/VC/Auxiliary/VS/UnitTest/lib")
    if(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
      list(APPEND _sco_relay_trusted_library_roots
        "C:/Program Files (x86)/Windows Kits/10/Lib/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}")
    endif()
    list(APPEND _sco_relay_trusted_library_roots
      "C:/Program Files (x86)/Windows Kits/NETFXSDK")
    foreach(_sco_relay_root IN LISTS _sco_relay_trusted_library_roots)
      if(IS_DIRECTORY "${_sco_relay_root}")
        file(REAL_PATH "${_sco_relay_root}" _sco_relay_root_real)
        string(TOLOWER "${_sco_relay_root_real}" _sco_relay_root_key)
        list(APPEND _sco_relay_trusted_library_roots_normalized "${_sco_relay_root_key}")
      endif()
    endforeach()
    if(NOT _sco_relay_trusted_library_roots_normalized)
      message(FATAL_ERROR "sc-mp relay: could not identify the selected MSVC and Windows SDK library roots")
    endif()
  endif()

  if(SCO_RELAY_MSVC_IDE_BUILD)
    execute_process(
      COMMAND "${CMAKE_COMMAND}"
              "-DSCO_RELAY_CMAKE_COMMAND=${CMAKE_COMMAND}"
              "-DSCO_RELAY_COMPILER=${CMAKE_CXX_COMPILER}"
              "-DSCO_RELAY_TEST_BINARY_DIR=${CMAKE_BINARY_DIR}/relay-msvc-environment-probe"
              -P "${CMAKE_CURRENT_LIST_DIR}/../tests/test_msvc_pragma_link.cmake"
      RESULT_VARIABLE _sco_relay_msvc_probe_result
      OUTPUT_VARIABLE _sco_relay_msvc_probe_stdout
      ERROR_VARIABLE _sco_relay_msvc_probe_stderr)
    if(NOT _sco_relay_msvc_probe_result EQUAL 0)
      message(FATAL_ERROR
        "sc-mp relay: the selected Visual Studio compiler failed its source/linker environment probe: ${_sco_relay_msvc_probe_stdout}${_sco_relay_msvc_probe_stderr}")
    endif()
    set(SCO_RELAY_MSVC_EMPTY_LIB_PROBE ON)
    message(STATUS "sc-mp relay: Visual Studio environment probe passed: ${_sco_relay_msvc_probe_stdout}${_sco_relay_msvc_probe_stderr}")
  endif()

  file(WRITE "${SCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT}"
    "compiler_id=${CMAKE_CXX_COMPILER_ID}\n")
  if(SCO_RELAY_MSVC_EMPTY_LIB_PROBE)
    file(APPEND "${SCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT}" "msvc_empty_lib_probe=passed\n")
  endif()
  foreach(_sco_relay_include_key IN LISTS _sco_relay_include_dirs)
    file(APPEND "${SCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT}" "include=${_sco_relay_include_key}\n")
  endforeach()
  foreach(_sco_relay_library_key IN LISTS _sco_relay_library_dirs)
    file(APPEND "${SCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT}" "library=${_sco_relay_library_key}\n")
  endforeach()
  foreach(_sco_relay_library_root IN LISTS _sco_relay_trusted_library_roots_normalized)
    file(APPEND "${SCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT}" "library_root=${_sco_relay_library_root}\n")
  endforeach()
  foreach(_sco_relay_flag_variable IN LISTS _sco_relay_cmake_flag_variables)
    file(APPEND "${SCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT}"
      "flag_${_sco_relay_flag_variable}=${${_sco_relay_flag_variable}}\n")
  endforeach()
else()
  # The build-time check must see the exact compiler and INCLUDE list that passed configuration.
  if(NOT EXISTS "${SCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT}")
    message(FATAL_ERROR "sc-mp relay: compiler environment snapshot is missing; reconfigure")
  endif()
  if(NOT DEFINED SCO_RELAY_CMAKE_CACHE OR NOT EXISTS "${SCO_RELAY_CMAKE_CACHE}")
    message(FATAL_ERROR "sc-mp relay: CMake cache path is missing; reconfigure")
  endif()
  file(STRINGS "${SCO_RELAY_CMAKE_CACHE}" _sco_relay_cache_lines)
  file(STRINGS "${SCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT}" _sco_relay_snapshot_lines)
  set(_sco_relay_snapshot_msvc_probe_passed FALSE)
  foreach(_sco_relay_snapshot_line IN LISTS _sco_relay_snapshot_lines)
    if(_sco_relay_snapshot_line STREQUAL "msvc_empty_lib_probe=passed")
      set(_sco_relay_snapshot_msvc_probe_passed TRUE)
    endif()
  endforeach()
  if(_sco_relay_msvc_ide_build_check AND
     (NOT SCO_RELAY_MSVC_EMPTY_LIB_PROBE OR NOT _sco_relay_snapshot_msvc_probe_passed))
    message(FATAL_ERROR "sc-mp relay: the Visual Studio build is missing its recorded MSVC environment probe")
  endif()
  foreach(_sco_relay_flag_variable IN LISTS _sco_relay_cmake_flag_variables)
    set(_sco_relay_cache_value "")
    set(_sco_relay_cache_key_seen FALSE)
    set(_sco_relay_snapshot_value "")
    set(_sco_relay_snapshot_key_seen FALSE)
    foreach(_sco_relay_cache_line IN LISTS _sco_relay_cache_lines)
      if(_sco_relay_cache_line MATCHES "^${_sco_relay_flag_variable}:[^=]*=(.*)$")
        set(_sco_relay_cache_value "${CMAKE_MATCH_1}")
        set(_sco_relay_cache_key_seen TRUE)
        break()
      endif()
    endforeach()
    foreach(_sco_relay_snapshot_line IN LISTS _sco_relay_snapshot_lines)
      if(_sco_relay_snapshot_line MATCHES "^flag_${_sco_relay_flag_variable}=(.*)$")
        set(_sco_relay_snapshot_value "${CMAKE_MATCH_1}")
        set(_sco_relay_snapshot_key_seen TRUE)
        break()
      endif()
    endforeach()
    if(NOT _sco_relay_cache_key_seen OR NOT _sco_relay_snapshot_key_seen OR
       NOT "${_sco_relay_cache_value}" STREQUAL "${_sco_relay_snapshot_value}")
      message(FATAL_ERROR "sc-mp relay: ${_sco_relay_flag_variable} changed since configuration; reconfigure")
    endif()
  endforeach()
  set(_sco_relay_snapshot_compiler_id "")
  set(_sco_relay_snapshot_include_dirs "")
  set(_sco_relay_snapshot_library_dirs "")
  set(_sco_relay_snapshot_library_roots "")
  foreach(_sco_relay_snapshot_line IN LISTS _sco_relay_snapshot_lines)
    if(_sco_relay_snapshot_line MATCHES "^compiler_id=(.*)$")
      set(_sco_relay_snapshot_compiler_id "${CMAKE_MATCH_1}")
    elseif(_sco_relay_snapshot_line MATCHES "^include=(.*)$")
      list(APPEND _sco_relay_snapshot_include_dirs "${CMAKE_MATCH_1}")
    elseif(_sco_relay_snapshot_line MATCHES "^library=(.*)$")
      list(APPEND _sco_relay_snapshot_library_dirs "${CMAKE_MATCH_1}")
    elseif(_sco_relay_snapshot_line MATCHES "^library_root=(.*)$")
      list(APPEND _sco_relay_snapshot_library_roots "${CMAKE_MATCH_1}")
    endif()
  endforeach()
  if(DEFINED SCO_RELAY_COMPILER_ID AND NOT "${SCO_RELAY_COMPILER_ID}" STREQUAL "${_sco_relay_snapshot_compiler_id}")
    message(FATAL_ERROR "sc-mp relay: selected compiler changed after configuration; reconfigure")
  endif()
  # MSVC ignores INCLUDE while building through Visual Studio project files and uses their explicit include
  # properties instead. MSBuild may alter INCLUDE in child processes, so use the configure-time validated snapshot.
  if(NOT _sco_relay_msvc_ide_build_check AND
     NOT "${_sco_relay_include_dirs}" STREQUAL "${_sco_relay_snapshot_include_dirs}")
    message(FATAL_ERROR "sc-mp relay: INCLUDE changed after configuration; reconfigure")
  endif()
  if(_sco_relay_msvc_ide_build_check AND SCO_RELAY_MSVC_EMPTY_LIB_PROBE)
    foreach(_sco_relay_library_key IN LISTS _sco_relay_library_dirs)
      set(_sco_relay_library_is_trusted FALSE)
      foreach(_sco_relay_root_key IN LISTS _sco_relay_snapshot_library_roots)
        string(FIND "${_sco_relay_library_key}/" "${_sco_relay_root_key}/" _sco_relay_root_match)
        if(_sco_relay_root_match EQUAL 0)
          set(_sco_relay_library_is_trusted TRUE)
          break()
        endif()
      endforeach()
      if(NOT _sco_relay_library_is_trusted)
        message(FATAL_ERROR
          "sc-mp relay: Visual Studio LIB contains a path outside the compiler and SDK roots validated at configuration: ${_sco_relay_library_key}")
      endif()
    endforeach()
  elseif(NOT "${_sco_relay_library_dirs}" STREQUAL "${_sco_relay_snapshot_library_dirs}")
    message(FATAL_ERROR "sc-mp relay: LIB changed after configuration; reconfigure")
  endif()
endif()
