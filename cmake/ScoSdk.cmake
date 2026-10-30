# Verifies the explicitly supplied development SDK archive and extracts it under the build tree.
#
#   SCO_SDK_VERSION       SDK version consumed by sc-mp (the archive is sco-sdk-<version>.zip)
#   SCO_SDK_RELEASE_TAG   a descriptive label for the development source, not a release tag
#   SCO_SDK_ARCHIVE_DIR   directory holding the local development SDK archive; its bytes must
#                         match SCO_SDK_SHA256 or configure fails.
#
# Sets SCO_SDK_ROOT (the extracted kit) and SCO_SDK_INCLUDE_DIR (its include/). The kit sources are
# never copied into this repository, and no sibling sco-core checkout is read.

set(SCO_SDK_VERSION "2.4.0" CACHE STRING "sco-core SDK version consumed by sc-mp")
set(SCO_SDK_RELEASE_TAG "development-e2ad885" CACHE STRING "Label for the development SDK inputs")
set(SCO_SDK_ARCHIVE_DIR "" CACHE PATH "Directory containing the development SDK archives")
# Locally generated from exact development source e2ad885aa3027ed1162793d30292ca103c2f5b28; not a release asset.
set(SCO_SDK_SHA256 "ff4fb3295fc6c0740f813e325c2882913be309e123deaf5fca3c9af095f2b789"
    CACHE STRING "SHA-256 of sco-sdk-<version>.zip")

set(_sco_sdk_name "sco-sdk-${SCO_SDK_VERSION}")
set(_sco_sdk_zip_name "${_sco_sdk_name}.zip")
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
    message(FATAL_ERROR "Development SDK archive is required: set SCO_SDK_ARCHIVE_DIR as described in README.md")
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

set(SCO_SDK_INCLUDE_DIR "${SCO_SDK_ROOT}/include" CACHE PATH "Extracted public SDK include directory" FORCE)
