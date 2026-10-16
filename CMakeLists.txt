# SPDX-License-Identifier: GPL-3.0-only
cmake_minimum_required(VERSION 3.20)
project(sc-mp LANGUAGES C CXX)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Standalone build of sc-mp against an extracted, hash-verified, PUBLISHED sco-core SDK archive
# (cmake/ScoSdk.cmake). To use an SDK you unpacked yourself, pass -DSCO_SDK_INCLUDE_DIR=<dir>/include
# and -DSC_MP_FETCH_SDK=OFF; the directory must sit next to the SDK's SHA256SUMS.
option(SC_MP_FETCH_SDK "Download and verify the published SDK archive" ON)
option(SC_MP_BUILD_TESTS "Build the workflow test and the sco-plugin-check gate" ON)

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
