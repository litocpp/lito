#!/usr/bin/env python3
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description="Check in-compiler expansion through public Clang entry points")
parser.add_argument("--cxx", required=True)
parser.add_argument("--sdk", type=Path, required=True)
parser.add_argument("--std-module-source", type=Path, required=True)
parser.add_argument("--build-dir", type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
build = args.build_dir.resolve()
build.mkdir(parents=True, exist_ok=True)
flags = [args.cxx, "-std=c++23", "-stdlib=libstdc++", "-fno-exceptions", "-fno-rtti", "-fPIC",
         "-Wno-reserved-module-identifier", f"-fprebuilt-module-path={build}"]


def run(name, arguments, expected=None, absent=None):
    result = subprocess.run(arguments, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (build / f"{name}.log").write_text(result.stdout)
    valid = result.returncode == 0 if expected is None else result.returncode != 0 and expected in result.stdout
    if absent is not None:
        valid = valid and absent not in result.stdout
    if not valid:
        raise RuntimeError(f"{name} failed; see {build / (name + '.log')}")
    print(f"{name}: passed", flush=True)


for source, name in [(args.std_module_source, "std"), (root / "src/lib.cppm", "pmacro")]:
    run(name, flags + ["-x", "c++-module", "--precompile", str(source), "-o", str(build / f"{name}.pcm")])
    run(f"{name}-object", flags + ["-c", str(build / f"{name}.pcm"), "-o", str(build / f"{name}.o")])
objects = ["std", "pmacro", "declarations", "fragment", "plugin"]
for name in objects[2:]:
    run(name, flags + ["-isystem", str(args.sdk / "include"), "-c", str(root / f"src/clang/{name}.cpp"),
                      "-o", str(build / f"{name}.o")])
run("plugin-link", flags + ["-shared"] + [str(build / f"{name}.o") for name in objects] +
    ["-o", str(build / "pmacro.so")])
run("provider", flags + [f"-fplugin={build / 'pmacro.so'}", "-Xclang", "-plugin-arg-pmacro", "-Xclang",
                        "provider=compiler-test-provider", "-c", str(root / "tests/compiler/provider.cppm"),
                        f"-fmodule-output={build / 'provider.pcm'}", "-o", str(build / "provider.o")])
run("aggregate", flags + ["-shared"] + [str(build / f"{name}.o") for name in objects + ["provider"]] +
    ["-o", str(build / "aggregate.so")])
plugin = [f"-fplugin={build / 'aggregate.so'}"]
run("consumer", flags + plugin + [str(root / "tests/compiler/consumer.cpp"), "-nostdlib++", "-o", str(build / "consumer")])
run("consumer-run", [str(build / "consumer")])

interface = build / "interface.cppm"
dependency = build / "dependency.cppm"
dependency.write_text("export module available; export namespace foreign { using Count = long; struct Value {}; }\n")
run("dependency-module", flags + ["-c", str(dependency), f"-fmodule-output={build / 'available.pcm'}",
                                 "-o", str(build / "available.o")])
interface.write_text('''export module generated;
import available;
export namespace exported {
using Choice = long;
struct [[pmacro::attr("compiler-test::warning"), pmacro::derive("compiler-test::after")]] Seed {};
struct [[pmacro::derive("compiler-test::at_end")]] End {};
struct Later { long value; };
struct [[pmacro::attr("compiler-test::inspect")]] Snapshot { foreign::Count value; Snapshot* next; };
static_assert(__is_same(inspected_type, foreign::Count));
}
export {
namespace block {
using Choice = short;
struct [[pmacro::derive("compiler-test::after")]] Seed {};
}
}
''')
run("interface", flags + plugin + ["-c", str(interface), f"-fmodule-output={build / 'generated.pcm'}",
                                  "-o", str(build / "generated.o")])
importer = build / "importer.cpp"
importer.write_text('''import generated;
static_assert(exported::generated() == 42 && block::generated() == 42);
static_assert(__is_same(exported::generated_choice, long));
static_assert(__is_same(block::generated_choice, short));
static_assert(__is_same(exported::later_type, exported::Later));
int main() { return exported::deferred() != sizeof(exported::Later); }
''')
run("independent-importer", flags + [str(importer), str(build / "generated.o"), str(build / "available.o"),
                                    "-nostdlib++", "-o", str(build / "importer")])
run("independent-run", [str(build / "importer")])

cases = {
    "deleted-function": ('[[pmacro::attr("compiler-test::function")]] void deleted() = delete;',
                         "requires an unconsumed declaration or body"),
    "deleted-followed-by-semicolon": ('[[pmacro::attr("compiler-test::function")]] void deleted() = delete; ;',
                                     "requires an unconsumed declaration or body"),
    "rename": ('struct [[pmacro::attr("compiler-test::rewrite", "struct Other {};")]] Original {};',
               "must retain the same declaration head"),
    "remove": ('struct [[pmacro::attr("compiler-test::rewrite", "")]] Original {};',
               "must retain the same declaration"),
    "dependency": ('struct [[pmacro::attr("compiler-test::rewrite", "import generated; struct Original {};")]] Original {};',
                   "must not change module dependencies"),
    "side-dependency": ('namespace scope { struct [[pmacro::derive("compiler-test::import_at_end")]] Original {}; }',
                        "must not change module dependencies"),
    "forward": ('struct [[pmacro::derive("compiler-test::after")]] Forward;', "requires a complete record"),
    "helper": ('struct Orphan { [[pmacro::helper("compiler-test::after")]] int value; };',
               "is not owned by an active derive invocation"),
    "failed-transaction": ('struct [[pmacro::attr("compiler-test::fail_after")]] Original {};',
                           "provider returned an expansion error"),
    "output-budget": ('struct [[pmacro::attr("compiler-test::budget")]] Original {};', "output budget exceeded"),
}
for name, (source, diagnostic) in cases.items():
    path = build / f"{name}.cpp"
    path.write_text(source + "\n")
    run(name, flags + plugin + ["-fsyntax-only", str(path)], diagnostic,
        "uncommitted-output" if name == "failed-transaction" else None)

for name, source, diagnostic in [
    ("define-signature", '[[pmacro::define("compiler-test")]] int invalid(int value) { return value; }',
     "must take AttributeInput or DeriveInput"),
    ("define-overload", '''[[pmacro::define("compiler-test")]]
auto invalid(pmacro::AttributeInput input, pmacro::Context&) noexcept -> std::expected<pmacro::TokenStream, pmacro::Error> { return input.item; }
void invalid(int) {}''', "must not be overloaded"),
]:
    path = build / f"{name}.cppm"
    path.write_text("export module invalid_provider;\nimport std;\nimport pmacro;\n" + source + "\n")
    run(name, flags + [f"-fplugin={build / 'pmacro.so'}", "-Xclang", "-plugin-arg-pmacro", "-Xclang",
                      "provider=invalid-provider", "-fsyntax-only", str(path)], diagnostic)

print("in-compiler expansion: scope, identity, snapshots, positions, PCM and failures passed")
