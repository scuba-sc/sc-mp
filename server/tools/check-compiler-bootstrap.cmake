# SPDX-License-Identifier: GPL-3.0-only
# Run before project() so caller-supplied compiler options cannot affect CMake's compiler-identification probes.
set(_sco_relay_bootstrap_environment_variables
  CPATH CPLUS_INCLUDE_PATH C_INCLUDE_PATH OBJC_INCLUDE_PATH
  CFLAGS CXXFLAGS OBJCFLAGS LDFLAGS
  LIBRARY_PATH LINK _LINK_
  GCC_EXEC_PREFIX COMPILER_PATH CL _CL_ CCC_OVERRIDE_OPTIONS)
foreach(_sco_relay_environment_name IN LISTS _sco_relay_bootstrap_environment_variables)
  if(NOT "$ENV{${_sco_relay_environment_name}}" STREQUAL "")
    message(FATAL_ERROR
      "sc-mp relay: ${_sco_relay_environment_name} changes compiler or linker search/options; unset it before configuring")
  endif()
endforeach()

if(CMAKE_TOOLCHAIN_FILE OR CMAKE_USER_MAKE_RULES_OVERRIDE OR CMAKE_USER_MAKE_RULES_OVERRIDE_C OR
   CMAKE_USER_MAKE_RULES_OVERRIDE_CXX OR CMAKE_PROJECT_INCLUDE_BEFORE OR CMAKE_PROJECT_INCLUDE OR
   CMAKE_PROJECT_sc-mp_INCLUDE_BEFORE OR CMAKE_PROJECT_sc-mp_INCLUDE OR
   CMAKE_PROJECT_TOP_LEVEL_INCLUDES OR
   CMAKE_C_COMPILER_LAUNCHER OR CMAKE_CXX_COMPILER_LAUNCHER OR
   CMAKE_C_COMPILER_ARG1 OR CMAKE_CXX_COMPILER_ARG1 OR
   CMAKE_C_COMPILER_EXTERNAL_TOOLCHAIN OR CMAKE_CXX_COMPILER_EXTERNAL_TOOLCHAIN)
  message(FATAL_ERROR "sc-mp relay: custom CMake toolchains, rules hooks and compiler launchers are not allowed")
endif()

set(_sco_relay_bootstrap_flag_variables
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

set(_sco_relay_bootstrap_snapshot "")
if(DEFINED CMAKE_BINARY_DIR AND NOT CMAKE_BINARY_DIR STREQUAL "")
  set(_sco_relay_bootstrap_snapshot "${CMAKE_BINARY_DIR}/sco-relay-compiler-environment.txt")
endif()
if(_sco_relay_bootstrap_snapshot AND EXISTS "${_sco_relay_bootstrap_snapshot}")
  file(STRINGS "${_sco_relay_bootstrap_snapshot}" _sco_relay_bootstrap_snapshot_lines)
  foreach(_sco_relay_flag_variable IN LISTS _sco_relay_bootstrap_flag_variables)
    if(DEFINED ${_sco_relay_flag_variable})
      set(_sco_relay_snapshot_value "")
      set(_sco_relay_snapshot_seen FALSE)
      foreach(_sco_relay_snapshot_line IN LISTS _sco_relay_bootstrap_snapshot_lines)
        if(_sco_relay_snapshot_line MATCHES "^flag_${_sco_relay_flag_variable}=(.*)$")
          set(_sco_relay_snapshot_value "${CMAKE_MATCH_1}")
          set(_sco_relay_snapshot_seen TRUE)
          break()
        endif()
      endforeach()
      if(NOT _sco_relay_snapshot_seen OR NOT "${${_sco_relay_flag_variable}}" STREQUAL "${_sco_relay_snapshot_value}")
        message(FATAL_ERROR "sc-mp relay: ${_sco_relay_flag_variable} changed before configuration; restore the reviewed cache")
      endif()
    endif()
  endforeach()
else()
  # Before the first project(), platform defaults have not been initialized. Any nonempty caller value is an override.
  foreach(_sco_relay_flag_variable IN LISTS _sco_relay_bootstrap_flag_variables)
    if(DEFINED ${_sco_relay_flag_variable} AND NOT "${${_sco_relay_flag_variable}}" STREQUAL "")
      message(FATAL_ERROR "sc-mp relay: ${_sco_relay_flag_variable} must not be set before project()")
    endif()
  endforeach()
endif()

foreach(_sco_relay_init_variable IN LISTS _sco_relay_bootstrap_flag_variables)
  string(APPEND _sco_relay_init_variable "_INIT")
  if(DEFINED ${_sco_relay_init_variable} AND NOT "${${_sco_relay_init_variable}}" STREQUAL "")
    message(FATAL_ERROR "sc-mp relay: caller-supplied ${_sco_relay_init_variable} is not allowed")
  endif()
endforeach()
