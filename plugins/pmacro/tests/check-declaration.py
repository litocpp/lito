#!/usr/bin/env python3
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description="Check declaration ownership against the public Clang SDK")
parser.add_argument("--cxx", required=True)
parser.add_argument("--sdk", type=Path, required=True)
parser.add_argument("--std-module-source", type=Path, required=True)
parser.add_argument("--build-dir", type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
build = args.build_dir.resolve()
build.mkdir(parents=True, exist_ok=True)
flags = [args.cxx, "-std=c++23", "-stdlib=libstdc++", "-fno-exceptions", "-fno-rtti",
         "-Wno-reserved-module-identifier", f"-fprebuilt-module-path={build}"]


def run(arguments):
    subprocess.run(arguments, check=True)


for source, name in [(args.std_module_source, "std"), (root / "src/lib.cppm", "pmacro")]:
    run(flags + ["-x", "c++-module", "--precompile", str(source), "-o", str(build / f"{name}.pcm")])
    run(flags + ["-c", str(build / f"{name}.pcm"), "-o", str(build / f"{name}.o")])
for source, name in [(root / "src/clang/declarations.cpp", "declarations"),
                     (root / "tests/declaration.cpp", "test")]:
    run(flags + ["-isystem", str(args.sdk / "include"), "-c", str(source), "-o", str(build / f"{name}.o")])
run(flags + [str(build / f"{name}.o") for name in ["std", "pmacro", "declarations", "test"]] +
    [str(args.sdk / "lib/libclang-cpp.so"), f"-Wl,-rpath,{args.sdk / 'lib'}", "-o", str(build / "declaration-test")])
run([str(build / "declaration-test")])
