# SPDX-License-Identifier: GPL-3.0-only
# Verifies the development sco-core "core-source" kit, extracts it under the build tree and adds it
# as a subproject, for the relay server (server/). It does for the kit what ScoSdk.cmake does for the SDK archive,
# and it reads the SDK version and the download folder from there, so include ScoSdk.cmake first.
#
#   SCO_CORE_SOURCE_SHA256   SHA-256 of the locally generated development core-source archive.
#   SCO_SDK_ARCHIVE_DIR      directory containing both development archives. Each is checked before extraction.
#
# Sets SCO_CORE_SOURCE_ROOT (the extracted kit) and makes two targets available:
#   sco_net            the kit's network library, built by the kit's own CMake file (EXCLUDE_FROM_ALL: only what
#                      is linked gets built; the game pack and the kit's own tests are off)
#   sco_relay_sockets  the kit's two socket files, src/net/udp_posix.cpp and src/net/scope.cpp, compiled from the
#                      extracted kit as they are. The kit's service library, which they belong to in the kit's own
#                      build, needs the game plugin host; the relay never links it.
# No file of the kit is copied into this repository, and no sibling sco-core checkout is read.

if(NOT DEFINED SCO_SDK_VERSION)
  message(FATAL_ERROR "cmake/ScoCoreSource.cmake needs the SDK version that cmake/ScoSdk.cmake sets: "
                      "configure with SC_MP_FETCH_SDK=ON, or leave SC_MP_BUILD_RELAY off")
endif()

# Locally generated from the same exact development source as the plugin SDK.
set(SCO_CORE_SOURCE_SHA256 "b7677073137867314a505711c24da97b9b3bf6d3a5e56650f071dd80a84ae1aa"
    CACHE STRING "SHA-256 of sco-sdk-core-source-<version>.zip")

set(_sco_cs_name "sco-sdk-core-source-${SCO_SDK_VERSION}")
set(_sco_cs_zip_name "${_sco_cs_name}.zip")
set(_sco_cs_dl_dir "${CMAKE_BINARY_DIR}/_sdk")
set(_sco_cs_zip "${_sco_cs_dl_dir}/${_sco_cs_zip_name}")
set(_sco_cs_src_parent "${_sco_cs_dl_dir}/core-source-${SCO_CORE_SOURCE_SHA256}")
set(SCO_CORE_SOURCE_ROOT "${_sco_cs_src_parent}/${_sco_cs_name}")

function(_sco_cs_hash_ok file out)
  set(${out} FALSE PARENT_SCOPE)
  if(EXISTS "${file}")
    file(SHA256 "${file}" _h)
    if(_h STREQUAL SCO_CORE_SOURCE_SHA256)
      set(${out} TRUE PARENT_SCOPE)
    endif()
  endif()
endfunction()

file(MAKE_DIRECTORY "${_sco_cs_dl_dir}")
_sco_cs_hash_ok("${_sco_cs_zip}" _have)
if(NOT _have)
  if(SCO_SDK_ARCHIVE_DIR)
    set(_local "${SCO_SDK_ARCHIVE_DIR}/${_sco_cs_zip_name}")
    if(NOT EXISTS "${_local}")
      message(FATAL_ERROR "SCO_SDK_ARCHIVE_DIR is set but ${_local} does not exist")
    endif()
    message(STATUS "sco-core source kit: using local archive ${_local}")
    file(COPY_FILE "${_local}" "${_sco_cs_zip}")
  else()
    message(FATAL_ERROR "Development core-source archive is required: set SCO_SDK_ARCHIVE_DIR as described in README.md")
  endif()
  # Whichever way the file arrived, it is checked here, before anything is extracted.
  _sco_cs_hash_ok("${_sco_cs_zip}" _have)
  if(NOT _have)
    file(SHA256 "${_sco_cs_zip}" _got)
    file(REMOVE "${_sco_cs_zip}")
    message(FATAL_ERROR "sco-core source kit ${_sco_cs_zip_name} has SHA-256 ${_got}, expected "
                        "${SCO_CORE_SOURCE_SHA256} (hash mismatch); refusing to build it")
  endif()
endif()
message(STATUS "sco-core source kit: ${_sco_cs_zip_name} SHA-256 verified")

# The extraction folder is keyed by the verified hash, so a different archive never reuses it.
if(NOT EXISTS "${SCO_CORE_SOURCE_ROOT}/SHA256SUMS")
  file(REMOVE_RECURSE "${_sco_cs_src_parent}")
  file(ARCHIVE_EXTRACT INPUT "${_sco_cs_zip}" DESTINATION "${_sco_cs_src_parent}")
  if(NOT EXISTS "${SCO_CORE_SOURCE_ROOT}/SHA256SUMS")
    message(FATAL_ERROR "sco-core source kit archive did not contain ${_sco_cs_name}/SHA256SUMS")
  endif()
endif()

# The kernel only: no Star Citizen game pack, and none of the kit's own tests (the kit's build is the one the
# kit's authors run; here it is a dependency). SCO_WERROR keeps the kit's own setting: warnings in its code are
# errors, as in its own build.
set(SCO_GAME_SC OFF CACHE BOOL "the relay never builds the game pack" FORCE)
set(SCO_BUILD_TESTS OFF CACHE BOOL "the kit's own tests are not part of this build" FORCE)
add_subdirectory("${SCO_CORE_SOURCE_ROOT}" sco-core-source EXCLUDE_FROM_ALL)

# These two files only need the kit's headers, so one compiled copy serves the server, its tests and the program.
add_library(sco_relay_sockets STATIC
  "${SCO_CORE_SOURCE_ROOT}/src/net/udp_$<IF:$<BOOL:${WIN32}>,win,posix>.cpp"
  "${SCO_CORE_SOURCE_ROOT}/src/net/scope.cpp")
target_include_directories(sco_relay_sockets PUBLIC "${SCO_CORE_SOURCE_ROOT}/include")
if(MSVC)
  target_compile_options(sco_relay_sockets PRIVATE /W4 $<$<BOOL:${SCO_WERROR}>:/WX>)
  target_link_libraries(sco_relay_sockets PUBLIC ws2_32)
else()
  target_compile_options(sco_relay_sockets PRIVATE -Wall -Wextra $<$<BOOL:${SCO_WERROR}>:-Werror>)
endif()
set_target_properties(sco_relay_sockets PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
