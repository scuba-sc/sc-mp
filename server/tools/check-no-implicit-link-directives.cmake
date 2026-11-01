# SPDX-License-Identifier: GPL-3.0-only
# Reject MSVC source directives that can add libraries or raw linker options outside the checked CMake link graph.
if(NOT DEFINED SCO_RELAY_ROOT OR NOT IS_DIRECTORY "${SCO_RELAY_ROOT}/server")
  message(FATAL_ERROR "usage: cmake -DSCO_RELAY_ROOT=<repository> -P check-no-implicit-link-directives.cmake")
endif()

file(GLOB _sco_relay_source_files LIST_DIRECTORIES false
  "${SCO_RELAY_ROOT}/server/*.cpp"
  "${SCO_RELAY_ROOT}/server/*.h")
list(APPEND _sco_relay_source_files "${SCO_RELAY_ROOT}/plugins/multiplayer/relay_wire.h")

foreach(_sco_relay_source IN LISTS _sco_relay_source_files)
  if(NOT EXISTS "${_sco_relay_source}")
    message(FATAL_ERROR "sc-mp relay: production source is missing: ${_sco_relay_source}")
  endif()
  file(READ "${_sco_relay_source}" _sco_relay_source_text)
  string(TOLOWER "${_sco_relay_source_text}" _sco_relay_source_lower)
  string(REPLACE "\\\r\n" "" _sco_relay_source_spliced "${_sco_relay_source_lower}")
  string(REPLACE "\\\n" "" _sco_relay_source_spliced "${_sco_relay_source_spliced}")
  # Remove ordinary string/character literals before comments so literal text cannot masquerade as a directive.
  string(REGEX REPLACE "\"([^\"\\\\\n]|\\\\.)*\"" "\"\"" _sco_relay_source_checked
    "${_sco_relay_source_spliced}")
  string(REGEX REPLACE "'([^'\\\\\n]|\\\\.)*'" "''" _sco_relay_source_checked
    "${_sco_relay_source_checked}")
  string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" " " _sco_relay_source_checked
    "${_sco_relay_source_checked}")
  string(REGEX REPLACE "//[^\n]*" " " _sco_relay_source_checked "${_sco_relay_source_checked}")
  string(ASCII 9 _sco_relay_tab)
  string(ASCII 11 _sco_relay_vertical_tab)
  string(ASCII 12 _sco_relay_form_feed)
  string(ASCII 13 _sco_relay_carriage_return)
  foreach(_sco_relay_horizontal_space IN ITEMS
      "${_sco_relay_tab}" "${_sco_relay_vertical_tab}" "${_sco_relay_form_feed}" "${_sco_relay_carriage_return}")
    string(REPLACE "${_sco_relay_horizontal_space}" " " _sco_relay_source_checked
      "${_sco_relay_source_checked}")
  endforeach()
  # _Pragma is an operator in C99/C++11 that can create the same pragma records. Newlines are whitespace in an
  # operator invocation, so scan a newline-normalized copy too. Token pasting can synthesize hidden directive tokens;
  # production source has no need for it, so fail closed when it appears.
  string(REPLACE "\n" " " _sco_relay_source_pragma_scan "${_sco_relay_source_checked}")
  # The pragma name can itself be a macro (for example, #define M comment followed by #pragma M(lib, ...)).
  # Production has no need for source pragmas except #pragma once, so remove only that exact directive and fail
  # closed on the rest rather than trying to model every compiler's macro-expansion rules.
  string(REGEX REPLACE "(^|\n)[ ]*(#|%:) *pragma +once[ ]*(\n|$)" "\\1 " _sco_relay_source_without_safe_pragma
    "${_sco_relay_source_checked}")
  if(_sco_relay_source_checked MATCHES "(^|\n)[ ]*(#|%:) *define[^\n]*(##|%:%:)" OR
     _sco_relay_source_without_safe_pragma MATCHES "(^|\n)[ ]*(#|%:) *pragma([^a-z0-9_]|$)" OR
     _sco_relay_source_pragma_scan MATCHES "(^|[^a-z0-9_])__pragma([^a-z0-9_]|$)" OR
     _sco_relay_source_pragma_scan MATCHES "(^|[^a-z0-9_])_pragma([^a-z0-9_]|$)")
    message(FATAL_ERROR
      "sc-mp relay: source-level pragma or token-paste can inject linker inputs: ${_sco_relay_source}")
  endif()
endforeach()

message(STATUS "sc-mp relay: no source-level MSVC link directives or token-pasted pragma operators")
