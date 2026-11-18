#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Developer tool: audit storage through Clang's AST, not a C-text parser.

The resulting source-hashed manifest is shipped; normal builds need only Python
and a C compiler. Regenerate when updating Woof or the native platform adapters.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("build", type=Path)
parser.add_argument("output", type=Path)
parser.add_argument("--platform", default="linux")
parser.add_argument("--files", nargs="*")
args = parser.parse_args()
commands = json.loads((args.build / "compile_commands.json").read_text(encoding="utf-8"))
records = {}

def mutable(node):
    kind = node["type"]["qualType"]
    base = kind.split("[")[0].strip()
    return not (base.endswith("const") or (base.startswith("const ") and "*" not in base))

def location(node, key):
    loc = node.get(key, {})
    return loc.get("expansionLoc", loc)

for command in commands:
    source = Path(command["file"])
    source = (Path(command["directory"]) / source).resolve()
    # Standalone test fixtures compile the same upstream files with different
    # flags; only the embedded translation units belong to the reset table.
    if not source.name.endswith("-embedded.c"):
        continue
    if source.name.endswith("-embedded.c"):
        name = source.name.removesuffix("-embedded.c") + ".c"
        root = Path(__file__).resolve().parent.parent
        candidate = root / "src/woof" / name
        source = candidate if candidate.exists() else next(next((root / "subprojects").glob("woof-*/src")).parent.rglob(name))
    if not ("subprojects/woof-" in str(source) or source.name in ("engine_video.c", "engine_sound.c", "engine_exit.c", "engine_input.c", "engine_network.c")):
        continue
    if args.files and source.name not in args.files:
        continue
    invocation = command.get("arguments") or shlex.split(command["command"])
    flags = invocation[1:]
    if flags[0] in ("cc", "gcc", "clang"):
        flags.pop(0)
    output_index = flags.index("-o")
    del flags[output_index:output_index + 2]
    for flag in ("-c", "-fdiagnostics-color=always"):
        if flag in flags:
            flags.remove(flag)
    for flag in ("-MQ", "-MF"):
        if flag in flags:
            index = flags.index(flag)
            del flags[index:index + 2]
    if "-MD" in flags:
        flags.remove("-MD")
    flags = [str(source) if flag == command["file"] else flag for flag in flags]
    target = ["--target=x86_64-w64-windows-gnu"] if args.platform == "windows" else []
    tree = json.loads(subprocess.check_output(["clang", *target, *flags,
        "-include", "stdio.h", "-include", "stdlib.h", "-include", "string.h",
        "-Xclang", "-ast-dump=json", "-fsyntax-only"], cwd=command["directory"]))
    globals_ = []
    locals_ = []
    source_bytes = source.read_bytes()
    def walk(node, function=False, statement=None, current_file=None):
        loc = location(node, "loc")
        current_file = loc.get("file", current_file)
        if node.get("kind") == "FunctionDecl":
            function = True
        if node.get("kind") == "DeclStmt":
            statement = node
        if node.get("kind") == "VarDecl" and mutable(node):
            loc = location(node, "loc")
            # Included header storage is not translation-unit-owned. Its
            # mutable tables are either compile-time lookup tables or covered
            # separately by the engine initialization routines.
            is_source = (current_file and Path(current_file).resolve() == source) or (
                "offset" in loc and not loc.get("includedFrom") and not loc.get("file"))
            if is_source:
                if function and node.get("storageClass") == "static" and statement:
                    end = location(statement.get("range", {}), "end")
                    offset = end["offset"] + end["tokLen"]
                    if offset <= len(source_bytes):
                        # Clang reports byte offsets before newline normalization.
                        locals_.append((offset - source_bytes.count(b"\r\n", 0, offset), node["name"]))
                elif not function and node.get("storageClass") != "extern":
                    globals_.append(node["name"])
        for child in node.get("inner", []):
            walk(child, function, statement, current_file)
    current = None
    for node in tree.get("inner", []):
        loc = location(node, "loc")
        current = loc.get("file", current)
        walk(node, current_file=current)
    grouped = {}
    for offset, name in locals_:
        grouped.setdefault(offset, []).append(name)
    records[source.name] = {"sha256": hashlib.sha256(source_bytes.replace(b"\r\n", b"\n")).hexdigest(),
        "globals": sorted(set(globals_)), "locals": sorted(grouped.items())}
    print(source.name, len(globals_), len(locals_), flush=True)
existing = json.loads(args.output.read_text()) if args.output.exists() else {}
posix = {**existing.get("common", {}), **existing.get("posix", existing.get("linux", {}))}
windows = {**existing.get("common", {}), **existing.get("windows", {})}
(windows if args.platform == "windows" else posix).update(records)
common = {name: entry for name, entry in posix.items() if windows.get(name) == entry}
result = {"common": common,
    "posix": {name: entry for name, entry in posix.items() if name not in common},
    "windows": {name: entry for name, entry in windows.items() if name not in common}}
args.output.write_text(json.dumps(result, indent=2) + "\n")
