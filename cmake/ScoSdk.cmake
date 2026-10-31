# Fetches the published sco-core SDK archive, verifies its SHA-256 and extracts it under the build tree.
#
#   SCO_SDK_VERSION       SDK version consumed by sc-mp (the archive is sco-sdk-<version>.zip)
#   SCO_SDK_RELEASE_TAG   the sco-core release that holds the archive. SDK 2.4.0 is published as release
#                         candidate 2, tag sdk-v2.4.0-rc.2; the final sdk-v2.4.0 has not been published yet
#   SCO_SDK_ARCHIVE_DIR   directory holding a local copy of the release assets (sco-sdk-<version>.zip);
#                         when set, the archive is taken from there instead of downloaded. Either way
#                         the archive must match SCO_SDK_SHA256 or configure fails.
#
# Sets SCO_SDK_ROOT (the extracted kit) and SCO_SDK_INCLUDE_DIR (its include/). The kit sources are
# never copied into this repository, and no sibling sco-core checkout is read.

set(SCO_SDK_VERSION "2.4.0" CACHE STRING "sco-core SDK version consumed by sc-mp")
set(SCO_SDK_RELEASE_TAG "sdk-v2.4.0-rc.2" CACHE STRING "sco-core release tag that holds the SDK archives")
set(SCO_SDK_ARCHIVE_DIR "" CACHE PATH "Local directory with the release assets of SCO_SDK_RELEASE_TAG (optional)")
# From the release SHA256SUMS, line for sco-sdk-<version>.zip (sdk-v2.4.0-rc.2 release). Bump together with
# SCO_SDK_VERSION and SCO_SDK_RELEASE_TAG.
set(SCO_SDK_SHA256 "51a7ce266d087bcbeac39c92f26a6f905b4473883dd7ed6f0f5ca823fa88c88e"
    CACHE STRING "SHA-256 of sco-sdk-<version>.zip")

# A relay build uses one reviewed pair. Validate the SDK side before unpacking
# it or using its headers; the source-kit side is checked in ScoCoreSource.cmake.
if(SC_MP_BUILD_RELAY)
  set(SC_MP_NET_BOUNDARY_PROFILE "rc2" CACHE STRING "Reviewed relay network boundary profile (rc2 or dev-571)")
  set_property(CACHE SC_MP_NET_BOUNDARY_PROFILE PROPERTY STRINGS rc2 dev-571)
  if(SC_MP_NET_BOUNDARY_PROFILE STREQUAL "rc2")
    set(_relay_sdk_sha "51a7ce266d087bcbeac39c92f26a6f905b4473883dd7ed6f0f5ca823fa88c88e")
    set(_relay_sdk_sums "6c30f314fe7e1016801eeb333851243fc705d47500c2ce6034f20c34835bdc8c")
  elseif(SC_MP_NET_BOUNDARY_PROFILE STREQUAL "dev-571")
    set(_relay_sdk_sha "67f4f837879867ac6d953a2953c448ce7913a9da924d6e04931a48988f23b9c1")
    set(_relay_sdk_sums "21da1d9b9ca3648bcc9690667d41a20310e13e558d6c3451cd80456756ca03dc")
  else()
    message(FATAL_ERROR "Unknown SC_MP_NET_BOUNDARY_PROFILE: ${SC_MP_NET_BOUNDARY_PROFILE}")
  endif()
  if(NOT SCO_SDK_VERSION STREQUAL "2.4.0" OR NOT SCO_SDK_SHA256 STREQUAL _relay_sdk_sha)
    message(FATAL_ERROR "SDK archive pair does not match the reviewed ${SC_MP_NET_BOUNDARY_PROFILE} network boundary profile")
  endif()
endif()

set(_sco_sdk_name "sco-sdk-${SCO_SDK_VERSION}")
set(_sco_sdk_zip_name "${_sco_sdk_name}.zip")
set(_sco_sdk_url
    "https://github.com/scuba-sc/sco-core/releases/download/${SCO_SDK_RELEASE_TAG}/${_sco_sdk_zip_name}")
set(_sco_sdk_dl_dir "${CMAKE_BINARY_DIR}/_sdk")
set(_sco_sdk_zip "${_sco_sdk_dl_dir}/${_sco_sdk_zip_name}")
set(_sco_sdk_src_parent "${_sco_sdk_dl_dir}/src-${SCO_SDK_SHA256}")
set(SCO_SDK_ROOT "${_sco_sdk_src_parent}/${_sco_sdk_name}")

function(_sco_sdk_hash_ok file out)
  set(${out} FALSE PARENT_SCOPE)
  if(EXISTS "${file}")
    file(SHA256 "${file}" _h)
    if(_h STREQUAL SCO_SDK_SHA256)
      set(${out} TRUE PARENT_SCOPE)
    endif()
  endif()
endfunction()

file(MAKE_DIRECTORY "${_sco_sdk_dl_dir}")
_sco_sdk_hash_ok("${_sco_sdk_zip}" _have)
if(NOT _have)
  if(SCO_SDK_ARCHIVE_DIR)
    set(_local "${SCO_SDK_ARCHIVE_DIR}/${_sco_sdk_zip_name}")
    if(NOT EXISTS "${_local}")
      message(FATAL_ERROR "SCO_SDK_ARCHIVE_DIR is set but ${_local} does not exist")
    endif()
    message(STATUS "sco-core SDK: using local archive ${_local}")
    file(COPY_FILE "${_local}" "${_sco_sdk_zip}")
  else()
    message(STATUS "sco-core SDK: downloading ${_sco_sdk_url}")
    file(DOWNLOAD "${_sco_sdk_url}" "${_sco_sdk_zip}"
         EXPECTED_HASH SHA256=${SCO_SDK_SHA256}
         TLS_VERIFY ON
         STATUS _dl_status)
    list(GET _dl_status 0 _dl_code)
    if(NOT _dl_code EQUAL 0)
      file(REMOVE "${_sco_sdk_zip}")
      message(FATAL_ERROR "sco-core SDK download failed: ${_dl_status}")
    endif()
  endif()
  _sco_sdk_hash_ok("${_sco_sdk_zip}" _have)
  if(NOT _have)
    file(SHA256 "${_sco_sdk_zip}" _got)
    file(REMOVE "${_sco_sdk_zip}")
    message(FATAL_ERROR "sco-core SDK archive ${_sco_sdk_zip_name} has SHA-256 ${_got}, "
                        "expected ${SCO_SDK_SHA256}; refusing to build it")
  endif()
endif()
message(STATUS "sco-core SDK: ${_sco_sdk_zip_name} SHA-256 verified")

# The extraction directory is keyed by the verified hash, so a different archive never reuses it.
if(NOT EXISTS "${SCO_SDK_ROOT}/SHA256SUMS")
  file(REMOVE_RECURSE "${_sco_sdk_src_parent}")
  file(ARCHIVE_EXTRACT INPUT "${_sco_sdk_zip}" DESTINATION "${_sco_sdk_src_parent}")
  if(NOT EXISTS "${SCO_SDK_ROOT}/SHA256SUMS")
    message(FATAL_ERROR "sco-core SDK archive did not contain ${_sco_sdk_name}/SHA256SUMS")
  endif()
endif()

if(SC_MP_BUILD_RELAY)
  include("${CMAKE_CURRENT_LIST_DIR}/VerifyExtractedKit.cmake")
  sc_mp_verify_extracted_kit("${SCO_SDK_ROOT}" "${_relay_sdk_sums}" "SDK")
endif()

set(SCO_SDK_INCLUDE_DIR "${SCO_SDK_ROOT}/include" CACHE PATH "Extracted public SDK include directory" FORCE)
