# SPDX-License-Identifier: GPL-3.0-only
# The relay tests are registered with CTest alongside sc-mp's module tests:
#   test_server          the unit tests (config, address rule, wire bytes, roster, nearby-only forwarding, session,
#                        run loop, real UDP on loopback)
#   sco_relay_program    runs the real program and checks its exit status
#   net_boundary_checks  proves the sco-core boundary checks fire when they should: it compiles a header on its own
#                        and alters copies of the kit's files
#   relay_no_game_code   proves the server and shared multiplayer/relay_wire.h hold no game code and that the server
#                        touches the kit's network library in one file; it reads the program's real link line from
#                        link.txt, which only the Makefile generators write, so with Ninja or any generator that writes
#                        none it fails instead of skipping that part
#   relay_no_game_code_checks proves the gate has no way round it: it accepts the guarded Windows SDK headers and
#                           rejects other Windows headers, paths that climb out of the server folder, headers from a
#                           folder below server/ (server/tests), project shadows of allowed system headers, linker
#                           script/plugin/remap inputs, library search paths, raw .obj and unrecognized link
#                           options/positional inputs, a substituted source, a substituted or duplicate archive,
#                           compiler include paths/forced inputs/environment overrides, includes behind comments,
#                           line splices or the %: digraph, libraries
#                           hidden in a -Wl, list or an @response file, and a missing link.txt; an include inside a
#                           comment and ordinary GCC/Clang link options are also checked as positive controls
#   relay_no_implicit_link_directives rejects MSVC source-level linker pragmas on all platforms, including Windows
#   relay_install_guard_checks exercises installer identity and config-path guards using mocked account commands
#                                 and temporary files; it never executes the installer or changes system accounts
# The full binary/link-boundary checks are Linux-only; test_server and the source-level directive check run on both.

add_executable(test_server
  test_main.cpp
  test_address_rule.cpp
  test_config.cpp
  test_nearby.cpp
  test_nearby_session.cpp
  test_relay_wire.cpp
  test_roster.cpp
  test_session.cpp
  test_run_loop.cpp
  test_udp.cpp)
target_link_libraries(test_server PRIVATE sco_relay_server)
# Tests use repository-root include spellings; the production server deliberately does not, so project paths cannot
# shadow transitive system headers in its compiler search path.
target_include_directories(test_server PRIVATE "${SCO_RELAY_ROOT}")
# net_mem.h, the kit's in-memory network for tests, used from the extracted kit, unchanged. Only this test program
# includes it; the server does not.
target_include_directories(test_server PRIVATE "${SCO_CORE_SOURCE_ROOT}/tests")
target_compile_definitions(test_server PRIVATE
  SCO_RELAY_EXAMPLE_CONFIG="${SCO_RELAY_ROOT}/server/sco-relay.example.conf")
sco_relay_target_setup(test_server)
add_test(NAME test_server COMMAND test_server)
set_tests_properties(test_server PROPERTIES TIMEOUT 600)

# MSVC accepts link inputs embedded in source-level pragma comments. Check these source directives on every platform,
# including Windows where the full archive/link-line gate below is not available.
add_test(NAME relay_no_implicit_link_directives
  COMMAND "${CMAKE_COMMAND}"
          "-DSCO_RELAY_ROOT=${SCO_RELAY_ROOT}"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/test_no_implicit_link_directives.cmake")
set_tests_properties(relay_no_implicit_link_directives PROPERTIES TIMEOUT 30)
add_test(NAME relay_compiler_environment_checks
  COMMAND "${CMAKE_COMMAND}"
          "-DSCO_RELAY_CMAKE_COMMAND=${CMAKE_COMMAND}"
          "-DSCO_RELAY_SOURCE_ROOT=${SCO_RELAY_ROOT}"
          "-DSCO_RELAY_TEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/compiler-environment-test"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/test_compiler_environment.cmake")
set_tests_properties(relay_compiler_environment_checks PROPERTIES TIMEOUT 30)

if(WIN32)
  add_executable(test_windows_process test_windows_process.cpp)
  target_include_directories(test_windows_process PRIVATE "${SCO_CORE_SOURCE_ROOT}/include")
  target_link_libraries(test_windows_process PRIVATE sco_net ws2_32)
  sco_relay_target_setup(test_windows_process)
  add_test(NAME sco_relay_windows_process COMMAND test_windows_process $<TARGET_FILE:sco-relay>)
  set_tests_properties(sco_relay_windows_process PROPERTIES TIMEOUT 60)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    add_test(NAME relay_msvc_pragma_link COMMAND "${CMAKE_COMMAND}"
      "-DSCO_RELAY_CMAKE_COMMAND=${CMAKE_COMMAND}"
      "-DSCO_RELAY_COMPILER=${CMAKE_CXX_COMPILER}"
      "-DSCO_RELAY_TEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/msvc-pragma-test"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/test_msvc_pragma_link.cmake")
    set_tests_properties(relay_msvc_pragma_link PROPERTIES TIMEOUT 30)
  endif()
endif()
if(NOT WIN32)
add_test(NAME sco_relay_program
  COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/test_program.sh" "$<TARGET_FILE:sco-relay>")
set_tests_properties(sco_relay_program PROPERTIES TIMEOUT 300)

# The compiler is the one this build uses, so the header test and the compile-time checks see what the build sees.
add_test(NAME net_boundary_checks
  COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/test_net_boundary_checks.sh"
          "${SCO_RELAY_ROOT}" "${SCO_CORE_SOURCE_ROOT}" "${CMAKE_CXX_COMPILER}")
set_tests_properties(net_boundary_checks PROPERTIES TIMEOUT 600)

# What the build looks like, written when CMake generates, for server/tools/check-no-game-code.sh: where the
# libraries, compiler commands and program are, which sources they compile, what each links and passes on to what
# links it (INTERFACE_LINK_LIBRARIES, which reaches the program's link line through the target that lists it), the
# files that hold the program's compile/link commands, and the kit's include path. The script reads these. The real
# link line is required: it is read from that file, which only
# the Makefile generators write, so a build without it (Ninja, for one) fails relay_no_game_code, it does not skip it.
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/relay-build-facts.txt" CONTENT
"cache=${CMAKE_BINARY_DIR}/CMakeCache.txt
c_compiler_id=${CMAKE_C_COMPILER_ID}
cxx_compiler_id=${CMAKE_CXX_COMPILER_ID}
kit=${SCO_CORE_SOURCE_ROOT}
kit_include=${SCO_CORE_SOURCE_ROOT}/include
compile_commands=${CMAKE_BINARY_DIR}/compile_commands.json
program=$<TARGET_FILE:sco-relay>
program_sources=$<TARGET_PROPERTY:sco-relay,SOURCES>
program_links=$<TARGET_PROPERTY:sco-relay,LINK_LIBRARIES>
program_interface_links=$<TARGET_PROPERTY:sco-relay,INTERFACE_LINK_LIBRARIES>
link_file=$<TARGET_PROPERTY:sco-relay,BINARY_DIR>/CMakeFiles/sco-relay.dir/link.txt
server_library=$<TARGET_FILE:sco_relay_server>
server_sources=$<TARGET_PROPERTY:sco_relay_server,SOURCES>
server_links=$<TARGET_PROPERTY:sco_relay_server,LINK_LIBRARIES>
server_interface_links=$<TARGET_PROPERTY:sco_relay_server,INTERFACE_LINK_LIBRARIES>
net_library=$<TARGET_FILE:sco_net>
net_sources=$<TARGET_PROPERTY:sco_net,SOURCES>
net_links=$<TARGET_PROPERTY:sco_net,LINK_LIBRARIES>
net_interface_links=$<TARGET_PROPERTY:sco_net,INTERFACE_LINK_LIBRARIES>
sockets_library=$<TARGET_FILE:sco_relay_sockets>
sockets_sources=$<TARGET_PROPERTY:sco_relay_sockets,SOURCES>
sockets_links=$<TARGET_PROPERTY:sco_relay_sockets,LINK_LIBRARIES>
sockets_interface_links=$<TARGET_PROPERTY:sco_relay_sockets,INTERFACE_LINK_LIBRARIES>
production_include_dirs=$<TARGET_PROPERTY:sco_relay_server,INCLUDE_DIRECTORIES>;$<TARGET_PROPERTY:sco_relay_server,INTERFACE_INCLUDE_DIRECTORIES>;$<TARGET_PROPERTY:sco-relay,INCLUDE_DIRECTORIES>;$<TARGET_PROPERTY:sco-relay,INTERFACE_INCLUDE_DIRECTORIES>;$<TARGET_PROPERTY:sco_net,INCLUDE_DIRECTORIES>;$<TARGET_PROPERTY:sco_net,INTERFACE_INCLUDE_DIRECTORIES>;$<TARGET_PROPERTY:sco_relay_sockets,INCLUDE_DIRECTORIES>;$<TARGET_PROPERTY:sco_relay_sockets,INTERFACE_INCLUDE_DIRECTORIES>
")
add_test(NAME relay_no_game_code
  COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/../tools/check-no-game-code.sh"
          "${SCO_RELAY_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/relay-build-facts.txt")
set_tests_properties(relay_no_game_code PROPERTIES TIMEOUT 120)
add_test(NAME relay_no_game_code_checks
  COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/test_no_game_code.sh"
          "${SCO_RELAY_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/relay-build-facts.txt")
set_tests_properties(relay_no_game_code_checks PROPERTIES TIMEOUT 240)
# This is a hermetic shell test: account commands are mocked and files live under a temporary directory.
add_test(NAME relay_install_guard_checks
  COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/test_install_guard.sh"
          "${SCO_RELAY_ROOT}/server/deploy/install_guard.sh"
          "${SCO_RELAY_ROOT}/server/deploy/install.sh")
set_tests_properties(relay_install_guard_checks PROPERTIES TIMEOUT 30)
endif()
