# SPDX-License-Identifier: GPL-3.0-only
cmake_minimum_required(VERSION 3.20)

# Reject compiler-option injection before project() runs its compiler identification probes. The relay defaults on
# for Windows/Linux builds using the published kit; explicit plugin-only builds keep their normal toolchain options.
if(DEFINED SC_MP_BUILD_RELAY)
  if(SC_MP_BUILD_RELAY)
    include("${CMAKE_CURRENT_LIST_DIR}/server/tools/check-compiler-bootstrap.cmake")
  endif()
elseif((NOT DEFINED SC_MP_FETCH_SDK OR SC_MP_FETCH_SDK) AND
       (CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows" OR
        CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Windows"))
  include("${CMAKE_CURRENT_LIST_DIR}/server/tools/check-compiler-bootstrap.cmake")
endif()

# The MSVC LIB environment variable can select linker inputs before the post-project guard can identify the selected
# compiler and SDK roots. For relay builds, keep it out of CMake's compiler/ABI probes; those probes only need to
# compile objects into a static library. Restore the developer environment immediately after project() so the
# guard can validate and snapshot its legitimate paths before any product target is built.
set(_sc_mp_relay_requested_before_project FALSE)
if(DEFINED SC_MP_BUILD_RELAY)
  if(SC_MP_BUILD_RELAY)
    set(_sc_mp_relay_requested_before_project TRUE)
  endif()
elseif((NOT DEFINED SC_MP_FETCH_SDK OR SC_MP_FETCH_SDK) AND
       CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
  set(_sc_mp_relay_requested_before_project TRUE)
endif()
if(_sc_mp_relay_requested_before_project AND CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows" AND
   NOT "$ENV{LIB}" STREQUAL "")
  set(_sc_mp_relay_preproject_lib "$ENV{LIB}")
  unset(ENV{LIB})
  if(DEFINED CMAKE_TRY_COMPILE_TARGET_TYPE)
    set(_sc_mp_preproject_try_compile_type_was_set TRUE)
    set(_sc_mp_preproject_try_compile_type "${CMAKE_TRY_COMPILE_TARGET_TYPE}")
  else()
    set(_sc_mp_preproject_try_compile_type_was_set FALSE)
  endif()
  set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
  set(_sc_mp_restore_preproject_lib TRUE)
endif()

project(sc-mp LANGUAGES C CXX)

if(_sc_mp_restore_preproject_lib)
  set(ENV{LIB} "${_sc_mp_relay_preproject_lib}")
  if(_sc_mp_preproject_try_compile_type_was_set)
    set(CMAKE_TRY_COMPILE_TARGET_TYPE "${_sc_mp_preproject_try_compile_type}")
  else()
    unset(CMAKE_TRY_COMPILE_TARGET_TYPE)
  endif()
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Clang may load configuration files from machine-specific locations, and those files may add include or link inputs.
# Disable default configuration loading for every Clang compile and link command in this build.
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
  add_compile_options("$<$<COMPILE_LANGUAGE:C>:--no-default-config>")
  add_link_options("$<$<LINK_LANGUAGE:C>:--no-default-config>")
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:--no-default-config>")
  add_link_options("$<$<LINK_LANGUAGE:CXX>:--no-default-config>")
endif()

# Standalone build of sc-mp against an extracted, hash-verified, PUBLISHED sco-core SDK archive
# (cmake/ScoSdk.cmake). To use an SDK you unpacked yourself, pass -DSCO_SDK_INCLUDE_DIR=<dir>/include
# and -DSC_MP_FETCH_SDK=OFF; the directory must sit next to the SDK's SHA256SUMS.
option(SC_MP_FETCH_SDK "Download and verify the published SDK archive" ON)
option(SC_MP_BUILD_TESTS "Build the workflow test and the sco-plugin-check gate" ON)
# The relay server (server/) is built from the published SDK's core-source kit (cmake/ScoCoreSource.cmake), which is
# downloaded and hash-checked like the SDK archive. It uses the kit's Windows/POSIX sockets: elsewhere the
# option is off, the kit is not downloaded, and the plugin builds and tests as before.
if((WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux") AND SC_MP_FETCH_SDK)
  set(_sc_mp_relay_default ON)
else()
  set(_sc_mp_relay_default OFF)
endif()
option(SC_MP_BUILD_RELAY "Build the relay server from the published core-source kit" ${_sc_mp_relay_default})
if(SC_MP_BUILD_RELAY)
  set(SCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT "${CMAKE_BINARY_DIR}/sco-relay-compiler-environment.txt")
  include("${CMAKE_CURRENT_LIST_DIR}/server/tools/check-compiler-environment.cmake")
endif()
if(SC_MP_BUILD_RELAY AND SC_MP_BUILD_TESTS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # The relay's boundary check audits the compiler inputs CMake actually generated, including source-specific flags.
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()
if(SC_MP_BUILD_RELAY AND NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "SC_MP_BUILD_RELAY requires Windows or Linux")
endif()
if(SC_MP_BUILD_RELAY AND NOT SC_MP_FETCH_SDK)
  message(FATAL_ERROR "SC_MP_BUILD_RELAY needs SC_MP_FETCH_SDK=ON: the relay's kit is fetched like the SDK archive")
endif()

if(SC_MP_FETCH_SDK)
  include("${CMAKE_CURRENT_LIST_DIR}/cmake/ScoSdk.cmake")
else()
  set(SCO_SDK_INCLUDE_DIR "" CACHE PATH "Extracted public SDK include directory")
  get_filename_component(SCO_SDK_ROOT "${SCO_SDK_INCLUDE_DIR}/.." ABSOLUTE)
endif()

if(SC_MP_BUILD_TESTS)
  enable_testing()
  set(SCO_WORKFLOW_TESTS ON)
endif()

add_subdirectory(plugins/multiplayer)

if(SC_MP_BUILD_RELAY)
  include("${CMAKE_CURRENT_LIST_DIR}/cmake/ScoCoreSource.cmake")
  add_subdirectory(server)
  add_custom_target(sco_relay_link_directive_check
    COMMAND "${CMAKE_COMMAND}"
            "-DSCO_RELAY_ROOT=${CMAKE_CURRENT_LIST_DIR}"
            -P "${CMAKE_CURRENT_LIST_DIR}/server/tools/check-no-implicit-link-directives.cmake"
    VERBATIM)
  add_dependencies(sco_net sco_relay_link_directive_check)
  add_dependencies(sco_relay_sockets sco_relay_link_directive_check)
  add_dependencies(sco_relay_server sco_relay_link_directive_check)
  add_dependencies(sco-relay sco_relay_link_directive_check)
  add_custom_target(sco_relay_compiler_environment_check
    COMMAND "${CMAKE_COMMAND}"
            "-DSCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT=${SCO_RELAY_COMPILER_ENVIRONMENT_SNAPSHOT}"
            "-DSCO_RELAY_COMPILER_ID=${CMAKE_CXX_COMPILER_ID}"
            "-DSCO_RELAY_CMAKE_GENERATOR=${CMAKE_GENERATOR}"
            "-DSCO_RELAY_MSVC_IDE_BUILD=${SCO_RELAY_MSVC_IDE_BUILD}"
            "-DSCO_RELAY_MSVC_EMPTY_LIB_PROBE=${SCO_RELAY_MSVC_EMPTY_LIB_PROBE}"
            "-DSCO_RELAY_CMAKE_CACHE=${CMAKE_BINARY_DIR}/CMakeCache.txt"
            -P "${CMAKE_CURRENT_LIST_DIR}/server/tools/check-compiler-environment.cmake"
    VERBATIM)
  add_dependencies(sco_net sco_relay_compiler_environment_check)
  add_dependencies(sco_relay_sockets sco_relay_compiler_environment_check)
  add_dependencies(sco_relay_server sco_relay_compiler_environment_check)
  add_dependencies(sco-relay sco_relay_compiler_environment_check)
elseif(NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(STATUS "sc-mp: relay unsupported on ${CMAKE_SYSTEM_NAME}")
else()
  message(STATUS "sc-mp: the relay server (server/) is not built (SC_MP_BUILD_RELAY is off)")
endif()

if(SC_MP_BUILD_TESTS)

  # sco-plugin-check is built from the SDK's own source, so the gate is the published one.
  set(_check_src "${SCO_SDK_ROOT}/tools/sco-plugin-check.c")
  if(NOT EXISTS "${_check_src}")
    message(FATAL_ERROR "SDK has no tools/sco-plugin-check.c: ${_check_src}")
  endif()
  add_executable(sco-plugin-check "${_check_src}")
  target_include_directories(sco-plugin-check PRIVATE "${SCO_SDK_INCLUDE_DIR}")
  set_target_properties(sco-plugin-check PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
  if(MSVC)
    # C5105: older Windows SDK headers trip the C11 preprocessor (winbase.h); not our code.
    target_compile_options(sco-plugin-check PRIVATE /W4 /WX /wd5105 /D_CRT_SECURE_NO_WARNINGS)
  else()
    target_compile_options(sco-plugin-check PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(sco-plugin-check PRIVATE ${CMAKE_DL_LIBS})
  endif()

  # Check the staged plugin folder (plugin.ini + built module), as the host reads it. The host is Windows,
  # so plugin.ini names multiplayer.dll: the gate runs on Windows only.
  if(WIN32)
    add_test(NAME multiplayer-plugin-check
      COMMAND sco-plugin-check "${CMAKE_BINARY_DIR}/plugins/multiplayer")
    set_tests_properties(multiplayer-plugin-check PROPERTIES TIMEOUT 60)
  endif()
endif()
