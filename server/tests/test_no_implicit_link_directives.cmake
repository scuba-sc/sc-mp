# SPDX-License-Identifier: GPL-3.0-only
# Exercise the portable source-level linker-directive check with clean and hostile fixtures.
if(NOT DEFINED SCO_RELAY_ROOT OR NOT IS_DIRECTORY "${SCO_RELAY_ROOT}/server")
  message(FATAL_ERROR "test needs -DSCO_RELAY_ROOT=<repository>")
endif()

set(_sco_relay_checker "${SCO_RELAY_ROOT}/server/tools/check-no-implicit-link-directives.cmake")
set(_sco_relay_fixture "${CMAKE_CURRENT_BINARY_DIR}/relay-link-directive-fixture")
file(REMOVE_RECURSE "${_sco_relay_fixture}")
file(MAKE_DIRECTORY "${_sco_relay_fixture}/server" "${_sco_relay_fixture}/plugins/multiplayer")
file(WRITE "${_sco_relay_fixture}/server/example.cpp"
  "const char* pragma_text = \"_Pragma(comment(lib)) #pragma comment(lib)\";\nint relay_example() { return 0; }\n")
file(WRITE "${_sco_relay_fixture}/plugins/multiplayer/relay_wire.h" "#pragma once\n")

execute_process(
  COMMAND "${CMAKE_COMMAND}" "-DSCO_RELAY_ROOT=${_sco_relay_fixture}" -P "${_sco_relay_checker}"
  RESULT_VARIABLE _sco_relay_clean_result
  OUTPUT_VARIABLE _sco_relay_output
  ERROR_VARIABLE _sco_relay_error)
if(NOT _sco_relay_clean_result EQUAL 0)
  file(REMOVE_RECURSE "${_sco_relay_fixture}")
  message(FATAL_ERROR "clean source fixture was rejected: ${_sco_relay_output}${_sco_relay_error}")
endif()

function(_sco_relay_expect_refused description source_text)
  file(WRITE "${_sco_relay_fixture}/server/example.cpp" "${source_text}\n")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DSCO_RELAY_ROOT=${_sco_relay_fixture}" -P "${_sco_relay_checker}"
    RESULT_VARIABLE _sco_relay_hostile_result
    OUTPUT_VARIABLE _sco_relay_output
    ERROR_VARIABLE _sco_relay_error)
  if(_sco_relay_hostile_result EQUAL 0)
    message(FATAL_ERROR "${description}: source-level link directive was accepted")
  endif()
  if(NOT "${_sco_relay_output}${_sco_relay_error}" MATCHES "source-level pragma or token-paste can inject linker inputs")
    message(FATAL_ERROR "${description}: source was refused for the wrong reason: ${_sco_relay_output}${_sco_relay_error}")
  endif()
endfunction()

_sco_relay_expect_refused("plain pragma" "#pragma comment(lib, \"game.lib\")")
_sco_relay_expect_refused("pragma after safe once directive"
  "int relay_value;\n#pragma once\n#pragma comment(lib, \"game.lib\")")
_sco_relay_expect_refused("digraph pragma after safe once directive"
  "int relay_value;\n%:pragma once\n%:pragma comment(lib, \"game.lib\")")
_sco_relay_expect_refused("pragma after quote in line comment"
  "// \"\n#pragma comment(lib, \"game.lib\")")
_sco_relay_expect_refused("pragma after apostrophe in line comment"
  "// '\n#pragma comment(lib, \"game.lib\") // '")
_sco_relay_expect_refused("macro-expanded pragma name"
  "#define LINKER_COMMENT comment\n#pragma LINKER_COMMENT(lib, \"game.lib\")")
_sco_relay_expect_refused("plain __pragma" "__pragma(comment(linker, \"/DEFAULTLIB:game.lib\"))")
_sco_relay_expect_refused("__pragma macro alias" "#define ADD_LINKER_PRAGMA __pragma\nADD_LINKER_PRAGMA(comment(lib, \"game.lib\"))")
_sco_relay_expect_refused("__pragma newline call" "__pragma\n(comment(lib, \"game.lib\"))")
_sco_relay_expect_refused("block-comment-separated pragma tokens" "#/**/pragma/**/comment(lib, \"game.lib\")")
_sco_relay_expect_refused("digraph pragma" "%:pragma comment(lib, \"game.lib\")")
_sco_relay_expect_refused("block-comment-separated digraph pragma" "%:/**/pragma/**/comment(lib, \"game.lib\")")
_sco_relay_expect_refused("block-comment-separated __pragma tokens"
  "__pragma/**/(comment(linker, \"/DEFAULTLIB:game.lib\"))")
_sco_relay_expect_refused("line-spliced pragma token" "#\\\npragma comment(lib, \"game.lib\")")
_sco_relay_expect_refused("_Pragma operator" "_Pragma(\"comment(lib, \\\"game.lib\\\")\")")
_sco_relay_expect_refused("_Pragma macro alias" "#define ADD_LINKER_PRAGMA _Pragma\nADD_LINKER_PRAGMA(\"comment(lib, \\\"game.lib\\\")\")")
string(ASCII 11 _sco_relay_vt)
string(ASCII 12 _sco_relay_ff)
_sco_relay_expect_refused("vertical-tab and form-feed pragma separators"
  "#${_sco_relay_vt}pragma${_sco_relay_ff}comment(lib, \"game.lib\")")
_sco_relay_expect_refused("newline in _Pragma invocation" "_Pragma\n(\"comment(lib, \\\"game.lib\\\")\")")
_sco_relay_expect_refused("token-pasted pragma operator"
  "#define JOIN(a, b) a ## b\n#define ADD_LINKER_PRAGMA JOIN(_Prag, ma)\nADD_LINKER_PRAGMA(\"comment(lib, \\\"game.lib\\\")\")")
_sco_relay_expect_refused("digraph token-pasted __pragma operator"
  "#define JOIN(a, b) a %:%: b\n#define ADD_LINKER_PRAGMA JOIN(__pra, gma)\nADD_LINKER_PRAGMA(comment(lib, \"game.lib\"))")
file(REMOVE_RECURSE "${_sco_relay_fixture}")
message(STATUS "sc-mp relay: clean source accepted and direct, obfuscated, _Pragma and token-paste link directives refused")
