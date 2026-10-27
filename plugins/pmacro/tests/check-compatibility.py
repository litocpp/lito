#!/usr/bin/env python3
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

parser = argparse.ArgumentParser(description="Check the existing pmacro provider and consumer")
parser.add_argument("--lito", required=True)
parser.add_argument("--build-dir", type=Path, required=True)
parser.add_argument("--cxx", required=True)
args = parser.parse_args()
project = Path(__file__).resolve().parents[3]
support = Path(__file__).resolve().parents[1]
work = args.build_dir.resolve()
(work / "src").mkdir(parents=True, exist_ok=True)
for name in ["consumer.cppm", "generated.cppm", "local.hpp"]:
    shutil.copyfile(project / "tests/src/pmacro" / name, work / "src" / name)
(work / "lito.toml").write_text(f'''[package]
name = "pmacro-compatibility"
version = "0.0.0"
standard = "c++23"
[[bin]]
name = "pmacro-compatibility"
sources = ["src/main.cpp", "src/consumer.cppm", "src/generated.cppm"]
link-stdlib = false
[dependencies]
pmacro-basic-provider = {{ path = "{os.path.relpath(project / 'tests/proc-macro/basic', work)}" }}
[profile.base]
exceptions = false
rtti = false
''')
main = work / "src/main.cpp"
main.write_text('''import lito.test.pmacro.consumer;
static_assert(scoped_identity_valid && scoped_derive_valid && equal_has_marker);
static_assert(prefix_value == 7 && Equal{7} == Equal{7} && !(Equal{7} == Equal{8}));
static_assert(sizeof(ReplaceSeed) == sizeof(int) && sizeof(RecursiveSeed) == sizeof(int));
int main() { return Value{7}.value == 7 && TokenModel{}.value == 42 && Arguments{9}.value == 9 ? 0 : 1; }
''')
report = work / "outputs.json"
command = [args.lito, "build", "--no-config", "--profile", "release", "--build-dir", str(work / "build"),
           "--outputs-json", str(report), "-c", f'builtin.packages.pmacro.path="{support}"',
           "-c", f'toolchain.cxx="{args.cxx}"', "-c", 'toolchain.stdlib="libstdc++"', "-j4"]
result = subprocess.run(command, cwd=work, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
(work / "basic.log").write_text(result.stdout)
if result.returncode:
    raise RuntimeError(f"compatibility build/diagnostic failed; see {work / 'basic.log'}")
artifact = next(item for item in json.loads(report.read_text())["artifacts"] if item["name"] == "pmacro-compatibility")
executable = next(item["path"] for item in artifact["files"] if item["role"] == "runtime" and item["publish"])
subprocess.run([executable], check=True)
main.write_text('''import lito.test.pmacro.consumer;
struct [[pmacro::attr("pmacro-basic-provider::recursive_limit")]] RecursiveLimitSeed {};
int main() {}
''')
result = subprocess.run(command, cwd=work, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
(work / "recursive.log").write_text(result.stdout)
if result.returncode == 0 or "recursion limit" not in result.stdout:
    raise RuntimeError(f"missing recursion failure; see {work / 'recursive.log'}")
print("existing provider: attr, derive, helper, import, diagnostic, recursion passed")
for name, source, diagnostic in [
    ("rename", 'struct [[pmacro::attr("pmacro-basic-provider::replace")]] Other {};', "must retain the same declaration head"),
    ("remove", 'struct [[pmacro::attr("pmacro-basic-provider::remove")]] RemoveSeed {};', "must retain the same declaration"),
    ("import", 'struct [[pmacro::attr("pmacro-basic-provider::emit_import")]] ImportSeed {};', "must not change module dependencies"),
]:
    main.write_text("import lito.test.pmacro.consumer;\n" + source + "\nint main() {}\n")
    result = subprocess.run(command, cwd=work, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (work / f"{name}.log").write_text(result.stdout)
    if result.returncode == 0 or diagnostic not in result.stdout:
        raise RuntimeError(f"{name}: missing failure; see {work / (name + '.log')}")
    print(f"{name}: expected failure")
