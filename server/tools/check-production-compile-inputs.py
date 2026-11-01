#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the compiler inputs for the relay product targets from CMake's generated command database."""

import json
import os
import shlex
import shutil
import sys


def reject(message: str) -> None:
    print(f"check-production-compile-inputs: {message}", file=sys.stderr)
    raise SystemExit(1)


def read_facts(path: str) -> dict[str, str]:
    facts: dict[str, str] = {}
    try:
        with open(path, encoding="utf-8") as stream:
            for line in stream:
                key, separator, value = line.rstrip("\n").partition("=")
                if separator:
                    facts[key] = value
    except OSError as exc:
        reject(f"cannot read generated build facts: {exc}")
    return facts


def read_cache(path: str) -> dict[str, str]:
    values: dict[str, str] = {}
    try:
        with open(path, encoding="utf-8") as stream:
            for line in stream:
                key_and_type, separator, value = line.rstrip("\n").partition("=")
                key, type_separator, _value_type = key_and_type.partition(":")
                if separator and type_separator:
                    values[key] = value
    except OSError as exc:
        reject(f"cannot read CMakeCache.txt: {exc}")
    return values


def absolute(path: str, directory: str) -> str:
    return os.path.realpath(path if os.path.isabs(path) else os.path.join(directory, path))


def command_arguments(entry: dict[str, object], source: str) -> list[str]:
    raw_arguments = entry.get("arguments")
    if isinstance(raw_arguments, list) and all(isinstance(arg, str) for arg in raw_arguments):
        return raw_arguments
    command = entry.get("command")
    if isinstance(command, str):
        return shlex.split(command)
    reject(f"{source}: compile command has neither arguments nor command text")


def include_argument(argv: list[str], index: int, source: str, directory: str, kit_include: str) -> int:
    """Validate a compiler include path option and return the last consumed argument index."""
    token = argv[index]
    flags = (
        "--include-directory-after",
        "--include-with-prefix-before",
        "--include-with-prefix-after",
        "--include-with-prefix",
        "--include-directory",
        "--include-prefix",
        "-stdlib++-isystem",
        "-iframeworkwithsysroot",
        "-isystem-after",
        "-cxx-isystem",
        "-iwithsysroot",
        "-iwithprefixbefore",
        "-iwithprefix",
        "-idirafter",
        "-iframework",
        "-iquote",
        "-isystem",
        "-imsvc",
        "-I",
        "/external:I",
        "/I",
    )
    for flag in flags:
        if token == flag:
            if index + 1 >= len(argv):
                reject(f"{source}: {flag} has no path")
            path = argv[index + 1]
            end = index + 1
        elif token.startswith(flag) and len(token) > len(flag):
            path = token[len(flag) :]
            if path.startswith("="):
                path = path[1:]
            end = index
        else:
            continue

        resolved = absolute(path, directory)
        if resolved != kit_include:
            reject(f"{source}: compiler include path is outside the pinned kit directory")
        return end
    return index


def main() -> None:
    if len(sys.argv) != 3:
        reject("usage: check-production-compile-inputs.py <repository root> <build facts file>")

    root = os.path.realpath(sys.argv[1])
    facts = read_facts(sys.argv[2])
    cache = read_cache(facts.get("cache", ""))
    database = facts.get("compile_commands", "")
    kit_include = facts.get("kit_include", "")
    if not database or not os.path.isfile(database):
        reject("CMake compile_commands.json is missing; configure a supported Unix Makefiles build")
    if not kit_include or not os.path.isdir(kit_include):
        reject("the pinned SDK include directory is missing from build facts")
    kit_include = os.path.realpath(kit_include)

    kit_root = os.path.realpath(facts.get("kit", ""))
    server_sources = [absolute(path, root) for path in facts.get("server_sources", "").split(";") if path]
    net_sources = [absolute(path, kit_root) for path in facts.get("net_sources", "").split(";") if path]
    socket_sources = [absolute(path, kit_root) for path in facts.get("sockets_sources", "").split(";") if path]
    main_source = os.path.realpath(os.path.join(root, "server", "main.cpp"))

    expected_net = {
        os.path.join(kit_root, "src", "net", name)
        for name in ("core.cpp", "reliable.cpp", "sha2.c", "wire.cpp")
    }
    socket_udp = "udp_win.cpp" if os.name == "nt" else "udp_posix.cpp"
    expected_sockets = {
        os.path.join(kit_root, "src", "net", "scope.cpp"),
        os.path.join(kit_root, "src", "net", socket_udp),
    }
    if set(net_sources) != expected_net:
        reject("CMake network-library sources do not match the pinned kit's four reviewed network files")
    if set(socket_sources) != expected_sockets:
        reject("CMake socket-library sources do not match the pinned kit's reviewed platform socket files")

    source_targets: dict[str, str] = {}
    for source in server_sources:
        source_targets[source] = "sco_relay_server"
    source_targets[main_source] = "sco-relay"
    for source in net_sources:
        source_targets[source] = "sco_net"
    for source in socket_sources:
        source_targets[source] = "sco_relay_sockets"
    expected = list(source_targets)
    if len(set(expected)) != len(expected) or not expected:
        reject("the expected product source list is empty or contains duplicates")

    try:
        with open(database, encoding="utf-8") as stream:
            entries = json.load(stream)
    except (OSError, json.JSONDecodeError) as exc:
        reject(f"cannot read CMake compile_commands.json: {exc}")
    if not isinstance(entries, list):
        reject("compile_commands.json is not an array")
    configured_compilers = {
        ".c": cache.get("CMAKE_C_COMPILER", ""),
        ".cpp": cache.get("CMAKE_CXX_COMPILER", ""),
    }
    compiler_ids = {
        ".c": facts.get("c_compiler_id", ""),
        ".cpp": facts.get("cxx_compiler_id", ""),
    }
    for extension, compiler in configured_compilers.items():
        if not compiler:
            reject(f"CMakeCache.txt does not name the configured {extension} compiler")

    selected: dict[str, list[dict[str, object]]] = {path: [] for path in expected}
    for entry in entries:
        if not isinstance(entry, dict) or not isinstance(entry.get("file"), str):
            reject("compile_commands.json contains an invalid entry")
        directory = entry.get("directory")
        if not isinstance(directory, str):
            reject("compile_commands.json entry has no working directory")
        source = absolute(entry["file"], directory)
        output = entry.get("output")
        if not isinstance(output, str) or not output:
            argv = command_arguments(entry, source)
            output = ""
            for index, token in enumerate(argv):
                if token == "-o" and index + 1 < len(argv):
                    output = argv[index + 1]
                    break
                if token.startswith("-o") and len(token) > 2:
                    output = token[2:]
                    break
            if not output:
                reject(f"{source}: compile command has no output identity")
        output_path = absolute(output, directory)
        expected_target = source_targets.get(source)
        if expected_target and f"/CMakeFiles/{expected_target}.dir/" in output_path:
            selected[source].append(entry)

    for source, commands in selected.items():
        if len(commands) != 1:
            reject(f"{source}: expected exactly one compiler command, found {len(commands)}")

    forbidden_environment = (
        "CPATH",
        "CPLUS_INCLUDE_PATH",
        "C_INCLUDE_PATH",
        "OBJC_INCLUDE_PATH",
        "INCLUDE",
        "CFLAGS",
        "CXXFLAGS",
        "OBJCFLAGS",
        "LDFLAGS",
        "LIBRARY_PATH",
        "LIB",
        "LINK",
        "_LINK_",
        "GCC_EXEC_PREFIX",
        "COMPILER_PATH",
        "CL",
        "_CL_",
        "CCC_OVERRIDE_OPTIONS",
    )
    active_environment = [name for name in forbidden_environment if os.environ.get(name)]
    if active_environment:
        reject("compiler environment overrides include, linker or toolchain search: " + ", ".join(active_environment))

    forced_prefixes = (
        "-include",
        "-imacros",
        "-include-pch",
        "-ivfsoverlay",
        "-vfsoverlay",
        "-fplugin",
        "-fmodule-map-file",
        "-fmodule-file",
        "-fprebuilt-module-path",
        "-fmodules",
        "-stdlib=",
        "-fpass-plugin",
        "-specs",
        "-wrapper",
        "-cc1",
        "-internal-",
        "-mllvm",
        "/FI",
        "/Yu",
        "/Fp",
    )
    for source, commands in selected.items():
        entry = commands[0]
        directory = str(entry["directory"])
        argv = command_arguments(entry, source)
        if not argv:
            reject(f"{source}: compile command is empty")
        extension = os.path.splitext(source)[1]
        configured_compiler = configured_compilers.get(extension)
        actual_compiler = shutil.which(argv[0]) if not os.path.isabs(argv[0]) else argv[0]
        if not configured_compiler or not actual_compiler or os.path.realpath(actual_compiler) != os.path.realpath(configured_compiler):
            reject(f"{source}: command does not start with the compiler recorded by CMake")
        clang_command = "clang" in compiler_ids.get(extension, "").lower()
        if clang_command and "--no-default-config" not in argv:
            reject(f"{source}: Clang default configuration files are not disabled")
        if not clang_command and "--no-default-config" in argv:
            reject(f"{source}: --no-default-config appears under a non-Clang compiler identity")

        index = 0
        while index < len(argv):
            token = argv[index]
            if token.startswith("@"):
                reject(f"{source}: compiler response files are not inspected")
            if token.startswith(("-Xclang", "-Xpreprocessor", "-Wp,")):
                reject(f"{source}: opaque compiler/preprocessor forwarding is not inspected")
            if any(token.startswith(option) for option in
                   ("-iprefix", "-iwithprefix", "-imultilib", "-imultiarch",
                    "--include-prefix", "--include-with-prefix")):
                reject(f"{source}: compiler include prefixes or multiarch paths are not allowed")
            if token in ("--sysroot", "-isysroot", "-F", "-B", "--prefix", "--gcc-toolchain", "-gcc-toolchain", "--gcc-install-dir", "-resource-dir", "--target") or token.startswith(("--sysroot=", "-isysroot", "-F", "-B", "--prefix=", "--gcc-toolchain=", "-gcc-toolchain=", "--gcc-install-dir=", "-resource-dir=", "--target=")):
                reject(f"{source}: alternate compiler, toolchain or system-header roots are not allowed")
            if token in ("--include", "--imacros") or token.startswith("--imacros") or (
                token.startswith("--include")
                and not token.startswith(("--include-directory", "--include-with-prefix", "--include-prefix"))
            ):
                reject(f"{source}: forced include or macro input is not allowed")
            if token == "--config" or token.startswith("--config="):
                reject(f"{source}: compiler configuration files are not allowed")
            if token in ("--config-user-dir", "--config-system-dir") or token.startswith(
                ("--config-user-dir=", "--config-system-dir=")
            ):
                reject(f"{source}: compiler configuration search directories are not allowed")
            if token == "-mllvm" or token.startswith("-mllvm="):
                reject(f"{source}: LLVM backend option forwarding is not inspected")
            if token == "-fno-integrated-as":
                reject(f"{source}: selecting an external assembler is not allowed")
            if token in ("-nostdinc", "-nostdinc++"):
                reject(f"{source}: compiler options that replace standard-header lookup are not allowed")
            if any(token == prefix or token.startswith(prefix + "=") or token.startswith(prefix) for prefix in forced_prefixes):
                reject(f"{source}: forced include, PCH, VFS overlay, plugin, specs, wrapper or frontend input is not allowed")
            consumed = include_argument(argv, index, source, directory, kit_include)
            index = consumed + 1

    link_file = facts.get("link_file", "")
    try:
        with open(link_file, encoding="utf-8") as stream:
            link_argv = shlex.split(stream.read())
    except OSError as exc:
        reject(f"cannot read the generated program link command: {exc}")
    if not link_argv:
        reject("the generated program link command is empty")
    cxx_compiler = configured_compilers[".cpp"]
    actual_linker = shutil.which(link_argv[0]) if not os.path.isabs(link_argv[0]) else link_argv[0]
    if not actual_linker or os.path.realpath(actual_linker) != os.path.realpath(cxx_compiler):
        reject("the program link command does not start with the compiler recorded by CMake")
    clang_link = "clang" in compiler_ids[".cpp"].lower()
    if clang_link and "--no-default-config" not in link_argv:
        reject("Clang default configuration files are not disabled for the program link")
    if not clang_link and "--no-default-config" in link_argv:
        reject("--no-default-config appears under a non-Clang program linker identity")
    for token in link_argv:
        if token == "--config" or token.startswith(("--config=", "--config-user-dir", "--config-system-dir")):
            reject("Clang configuration files or search directories are not allowed in the program link command")

    print(
        "check-production-compile-inputs: "
        f"{len(expected)} product sources use only the pinned SDK include directory, with no forced headers or implicit include paths"
    )


if __name__ == "__main__":
    main()
