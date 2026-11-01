# SPDX-License-Identifier: GPL-3.0-only
cmake_minimum_required(VERSION 3.20)
project(host_context LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(SCO_GAME_SC OFF CACHE BOOL "" FORCE)
set(SCO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
if(NOT SDK_ROOT OR NOT EXISTS "${SDK_ROOT}/SHA256SUMS")
  message(FATAL_ERROR "SDK_ROOT must point to the checked core-source kit")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/VerifyExtractedKit.cmake")
sc_mp_verify_extracted_kit("${SDK_ROOT}" "cd9208fa2b122ea353ef8f2316f9e77cd87b28047b4e01617285adee9d7d54ba" "host-context core kit")
add_subdirectory("${SDK_ROOT}" sdk EXCLUDE_FROM_ALL)
# Test-only observation of the real core retirement wait. It changes no
# admission, provenance, or retirement behavior and supplies no callback.
target_compile_definitions(sco_authority PRIVATE SCO_RESERVED_DRAIN_TESTING=1)
# The single runner contains the adapter and a test-only typed barrier. The
# authority is the real executable-linked target, with its provenance guard on.
add_executable(host-context main.cpp context.cpp)
target_compile_definitions(host-context PRIVATE HOST_CONTEXT_TESTING=1 SCO_RESERVED_DRAIN_TESTING=1)
target_compile_options(host-context PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(host-context PRIVATE sco_authority)
enable_testing()
foreach(scenario basic overflow drain drain-now output-contract output-overflow coherence replace-during-copy success-drain leave-during-copy)
  add_test(NAME host-context-${scenario} COMMAND host-context ${scenario})
  set_tests_properties(host-context-${scenario} PROPERTIES TIMEOUT 15)
endforeach()
