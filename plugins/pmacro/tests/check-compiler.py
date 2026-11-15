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


def run(name, arguments, expected=None, absent=None, failures=None):
    result = subprocess.run(arguments, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=120)
    (build / f"{name}.log").write_text(result.stdout)
    valid = result.returncode == 0 if expected is None else result.returncode != 0 and expected in result.stdout
    if absent is not None:
        valid = valid and absent not in result.stdout
    if not valid:
        message = f"{name} failed; see {build / (name + '.log')}"
        if failures is None:
            raise RuntimeError(message)
        failures.append(message)
        print(message, flush=True)
        return
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

helper_source = root / "tests/compiler/helper-codegen.cppm"
helper_flags = flags + ["-fmodules-reduced-bmi"]
run("helper-codegen", helper_flags + plugin + ["-g2", "-c", str(helper_source),
                                               f"-fmodule-output={build / 'helper_codegen.pcm'}",
                                               "-o", str(build / "helper-codegen.o")])
run("helper-codegen-ir", helper_flags + plugin + ["-g2", "-S", "-emit-llvm", str(helper_source),
                                                  f"-fmodule-output={build / 'helper-codegen-ir.pcm'}",
                                                  "-o", str(build / "helper-codegen.ll")])
helper_ir = (build / "helper-codegen.ll").read_text()
if "pmacro.helper" in helper_ir or "pmacro::helper" in helper_ir:
    raise RuntimeError("helper metadata escaped into generated IR")
if "user.field" not in helper_ir or "llvm.ptr.annotation" not in helper_ir:
    raise RuntimeError("ordinary field annotation was not preserved")
run("helper-independent-importer", flags + [str(root / "tests/compiler/helper-codegen-consumer.cpp"),
                                           str(build / "helper-codegen.o"), "-nostdlib++",
                                           "-o", str(build / "helper-importer")])
run("helper-independent-run", [str(build / "helper-importer")])

contexts = build / "contexts.cpp"
contexts.write_text('''struct [[pmacro::derive("compiler-test::context_info")]] Global {};
namespace { struct [[pmacro::derive("compiler-test::context_info")]] Normal {}; }
static_assert(from_Global() == 42 && from_Normal() == 42);
''')
run("ordinary-contexts", flags + plugin + ["-c", str(contexts), "-o", str(build / "contexts.o")])

interface = build / "interface.cppm"
dependency = build / "dependency.cppm"
dependency.write_text("export module available; export namespace foreign { using Count = long; struct Value {}; }\n")
run("dependency-module", flags + ["-c", str(dependency), f"-fmodule-output={build / 'available.pcm'}",
                                 "-o", str(build / "available.o")])
interface.write_text('''export module generated;
import available;
export struct [[pmacro::derive("compiler-test::context_info")]] Direct {};
export { struct [[pmacro::derive("compiler-test::context_info")]] InExportBlock {}; }
export namespace context_check {
struct [[pmacro::derive("compiler-test::context_info")]] InExportNamespace {};
}
namespace context_check {
struct [[pmacro::derive("compiler-test::context_info")]] Normal {};
struct Outer { struct [[pmacro::derive("compiler-test::context_info")]] Nested {}; };
}
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
static_assert(from_Direct() == 42 && from_InExportBlock() == 42 && context_check::from_InExportNamespace() == 42);
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
    "helper-owner": ('struct [[pmacro::derive("compiler-test::helper_member")]] Wrong { '
                     '[[pmacro::helper("compiler-test::helper_second", default)]] int value; };',
                     "owner is not present in the enclosing derive group"),
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
     "must take"),
    ("define-overload", '''[[pmacro::define("compiler-test")]]
auto invalid(pmacro::AttributeInput input, pmacro::Context&) noexcept -> std::expected<pmacro::TokenStream, pmacro::Error> { return input.item; }
void invalid(int) {}''', "must not be overloaded"),
    ("define-invoke-noexcept", '''[[pmacro::define("compiler-test")]]
auto invalid(pmacro::FunctionInput input, pmacro::Context&) -> std::expected<pmacro::TokenStream, pmacro::Error> { return input.arguments; }''',
     "must be noexcept"),
    ("define-invoke-reference", '''[[pmacro::define("compiler-test")]]
auto invalid(const pmacro::FunctionInput& input, pmacro::Context&) noexcept -> std::expected<pmacro::TokenStream, pmacro::Error> { return input.arguments; }''',
     "first 'pmacro::define' parameter"),
    ("define-invoke-context", '''[[pmacro::define("compiler-test")]]
auto invalid(pmacro::FunctionInput input, const pmacro::Context&) noexcept -> std::expected<pmacro::TokenStream, pmacro::Error> { return input.arguments; }''',
     "second 'pmacro::define' parameter"),
    ("define-invoke-result", '''[[pmacro::define("compiler-test")]]
auto invalid(pmacro::FunctionInput input, pmacro::Context&) noexcept -> std::expected<pmacro::TokenStream, int> { return input.arguments; }''',
     "must return"),
]:
    path = build / f"{name}.cppm"
    path.write_text("export module invalid_provider;\nimport std;\nimport pmacro;\n" + source + "\n")
    run(name, flags + [f"-fplugin={build / 'pmacro.so'}", "-Xclang", "-plugin-arg-pmacro", "-Xclang",
                      "provider=invalid-provider", "-fsyntax-only", str(path)], diagnostic)

run("invoke-consumer", flags + plugin + [str(root / "tests/compiler/invoke.cpp"), "-nostdlib++",
                                        "-o", str(build / "invoke-consumer")])
run("invoke-consumer-run", [str(build / "invoke-consumer")])
run("invoke-passive", flags + plugin + [str(root / "tests/compiler/invoke-passive.cpp"),
                                       "-nostdlib++", "-o", str(build / "invoke-passive")])
run("invoke-passive-run", [str(build / "invoke-passive")])
run("invoke-conditional", flags + plugin + [str(root / "tests/compiler/invoke-conditional.cpp"),
                                           "-nostdlib++", "-o", str(build / "invoke-conditional")])
run("invoke-conditional-run", [str(build / "invoke-conditional")])

invoke_interface = build / "invoke-interface.cppm"
invoke_interface.write_text('''export module invoked;
import available;
[[pmacro::invoke("compiler-test::function_context", Private, NotExported)]];
export [[pmacro::invoke("compiler-test::function_context", Direct, Required)]];
export {
[[pmacro::invoke("compiler-test::function_context", Block, Inherited)]];
[[pmacro::invoke("compiler-test::declarations",
    struct PublicInput { foreign::Count value; };
    constexpr PublicInput make_input() { return {42}; }
)]];
namespace invoked_block {
[[pmacro::invoke("compiler-test::function_context", Nested, Inherited)]];
[[pmacro::invoke("compiler-test::function_positions")]];
struct Later { foreign::Count value; };
}
}
export namespace invoked_namespace {
[[pmacro::invoke("compiler-test::function_context", Namespace, Inherited)]];
inline namespace version {
[[pmacro::invoke("compiler-test::function_context", Inline, Inherited)]];
}
}
namespace export_deferred {
export {
[[pmacro::invoke("compiler-test::function_positions")]];
}
struct Later { long value; };
}
static_assert(export_deferred::invoked_after == 42);
static_assert(__is_same(export_deferred::invoked_later_type, export_deferred::Later));
static_assert(export_deferred::invoked_end() == sizeof(export_deferred::Later));
static_assert(invoked_Private() == 42 && make_input().value == 42);
''')
run("invoke-interface", flags + plugin + ["-c", str(invoke_interface),
                                         f"-fmodule-output={build / 'invoked.pcm'}",
                                         "-o", str(build / "invoked.o")])
invoke_importer = build / "invoke-importer.cpp"
invoke_importer.write_text('''import invoked;
static_assert(invoked_Direct() == 42 && invoked_Block() == 42);
static_assert(make_input().value == 42 && __is_same(decltype(PublicInput::value), long));
static_assert(invoked_block::invoked_Nested() == 42 && invoked_block::invoked_after == 42);
static_assert(__is_same(invoked_block::invoked_later_type, invoked_block::Later));
static_assert(invoked_block::invoked_end() == sizeof(invoked_block::Later));
static_assert(invoked_namespace::invoked_Namespace() == 42 && invoked_namespace::invoked_Inline() == 42);
static_assert(export_deferred::invoked_after == 42);
int main() { return make_input().value != 42; }
''')
run("invoke-independent-importer", flags + [str(invoke_importer), str(build / "invoked.o"),
                                           str(build / "available.o"), "-nostdlib++",
                                           "-o", str(build / "invoke-importer")])
run("invoke-independent-run", [str(build / "invoke-importer")])
invoke_private = build / "invoke-private.cpp"
invoke_private.write_text('import invoked;\nstatic_assert(invoked_Private() == 42);\n')
run("invoke-private-not-exported", flags + ["-fsyntax-only", str(invoke_private)], "invoked_Private")

invoke_cases = {
    "invoke-record": ('struct Member { [[pmacro::invoke("compiler-test::declarations")]]; };',
                      "namespace-scope"),
    "invoke-function-body": ('void body() { [[pmacro::invoke("compiler-test::declarations")]]; }',
                             "namespace-scope"),
    "invoke-expression": ('int value = [[pmacro::invoke("compiler-test::declarations")]];',
                          "expected"),
    "invoke-attached-record": ('[[pmacro::invoke("compiler-test::declarations")]] struct Attached {};',
                               "misplaced attributes"),
    "invoke-attached-variable": ('[[pmacro::invoke("compiler-test::declarations")]] int attached;',
                                 "standalone"),
    "invoke-attached-function": ('[[pmacro::invoke("compiler-test::declarations")]] void attached();',
                                 "standalone"),
    "invoke-type-attribute": ('struct [[pmacro::invoke("compiler-test::declarations")]] Attached {};',
                              "standalone"),
    "invoke-multiple-attributes": ('[[pmacro::invoke("compiler-test::declarations"), pmacro::invoke("compiler-test::declarations")]];',
                                   "requires source-spelled token input"),
    "invoke-mixed-before": ('[[deprecated, pmacro::invoke("compiler-test::declarations")]];',
                            "pmacro::invoke"),
    "invoke-mixed-after": ('[[pmacro::invoke("compiler-test::declarations"), deprecated]];',
                           "pmacro::invoke"),
    "invoke-separate-attributes": ('[[pmacro::invoke("compiler-test::declarations")]] [[deprecated]];',
                                   "pmacro::invoke"),
    "invoke-unknown-attribute-after": ('[[pmacro::invoke("compiler-test::declarations")]] [[unrecognized::marker]];',
                                       "requires"),
    "invoke-unknown-attribute-before": ('[[unrecognized::marker]] [[pmacro::invoke("compiler-test::declarations")]];',
                                        "requires"),
    "invoke-attribute-across-directive": ('[[unrecognized::marker]]\n#define UNUSED 123\n[[pmacro::invoke("compiler-test::declarations")]];',
                                          "requires"),
    "invoke-unknown-provider": ('[[pmacro::invoke("missing-provider::declarations")]];',
                                "required kind"),
    "invoke-unknown-macro": ('[[pmacro::invoke("compiler-test::missing")]];', "required kind"),
    "invoke-wrong-attr-kind": ('[[pmacro::invoke("compiler-test::replace")]];', "required kind"),
    "invoke-wrong-derive-kind": ('[[pmacro::invoke("compiler-test::after")]];', "required kind"),
    "invoke-as-attr": ('struct [[pmacro::attr("compiler-test::declarations")]] Wrong {};',
                       "required kind"),
    "invoke-as-derive": ('struct [[pmacro::derive("compiler-test::declarations")]] Wrong {};',
                         "required kind"),
    "invoke-bare-identity": ('[[pmacro::invoke("declarations")]];', "identity"),
    "invoke-empty-identity": ('[[pmacro::invoke("")]];', "identity"),
    "invoke-nested-identity": ('[[pmacro::invoke("compiler-test::nested::declarations")]];', "identity"),
    "invoke-missing-identity": ('[[pmacro::invoke()]];', "string"),
    "invoke-non-string-identity": ('[[pmacro::invoke(42)]];', "string"),
    "invoke-wide-identity": ('[[pmacro::invoke(L"compiler-test::declarations")]];', "string"),
    "invoke-utf8-identity": ('[[pmacro::invoke(u8"compiler-test::declarations")]];', "string"),
    "invoke-concatenated-identity": ('[[pmacro::invoke("compiler-test::" "declarations")]];', "string"),
    "invoke-missing-comma": ('[[pmacro::invoke("compiler-test::declarations" int value;)]];', "comma"),
    "invoke-missing-semicolon": ('[[pmacro::invoke("compiler-test::declarations")]]', "expected external declaration"),
    "invoke-unbalanced-brace": ('[[pmacro::invoke("compiler-test::declarations", {)]];', "pmacro::invoke"),
    "invoke-unbalanced-bracket": ('[[pmacro::invoke("compiler-test::declarations", [})]];', "unbalanced"),
    "invoke-import": ('[[pmacro::invoke("compiler-test::declarations", import available;)]];',
                      "must not change module dependencies"),
    "invoke-export-import": ('[[pmacro::invoke("compiler-test::declarations", export import available;)]];',
                             "must not change module dependencies"),
    "invoke-module": ('[[pmacro::invoke("compiler-test::declarations", module;)]];',
                      "must not change module dependencies"),
    "invoke-nested-import": ('[[pmacro::invoke("compiler-test::declarations", namespace bad { import available; })]];',
                             "must not change module dependencies"),
    "invoke-emitted-import": ('namespace bad { [[pmacro::invoke("compiler-test::function_emit", import available;)]]; }',
                              "must not change module dependencies"),
    "invoke-failed-transaction": ('namespace failed { [[pmacro::invoke("compiler-test::function_fail")]]; }',
                                  "provider returned an expansion error"),
    "invoke-output-budget": ('[[pmacro::invoke("compiler-test::function_budget")]];', "output budget exceeded"),
    "invoke-recursion": ('[[pmacro::invoke("compiler-test::function_loop")]];', "recursion limit"),
    "invoke-depth-limit": ('[[pmacro::invoke("compiler-test::function_depth", 64)]];', "recursion limit"),
}
failures = []
for name, (source, diagnostic) in invoke_cases.items():
    path = build / f"{name}.cpp"
    path.write_text(source + "\n")
    run(name, flags + plugin + ["-fsyntax-only", str(path)], diagnostic,
        "uncommitted-invoke-output" if name == "invoke-failed-transaction" else None, failures)

invoke_diagnostic = build / "invoke-diagnostic.cpp"
invoke_diagnostic.write_text('''namespace diagnostic {
[[pmacro::invoke("compiler-test::function_diagnostic", {
    missing_field: Missing
})]];
}
''')
run("invoke-diagnostic-position", flags + plugin + ["-fsyntax-only", str(invoke_diagnostic)],
    f"{invoke_diagnostic}:3:5: error: invoke field diagnostic", failures=failures)

for mode in ["returned", "after", "end"]:
    path = build / f"native-{mode}-type.cpp"
    path.write_text('''namespace native_type {
[[pmacro::invoke("compiler-test::spanned_output", ''' + mode + ''',
    MissingType
)]];
}
''')
    run(f"native-{mode}-type-position", flags + plugin + ["-fsyntax-only", str(path)],
        f"{path}:3:5: error: unknown type name 'MissingType'", failures=failures)

native_token = build / "native-group-token.cpp"
native_token.write_text('''namespace native_token {
[[pmacro::invoke("compiler-test::spanned_output", returned,
    long[missing_bound]
)]];
}
''')
run("native-group-token-position", flags + plugin + ["-fsyntax-only", str(native_token)],
    f"{native_token}:3:10: error: use of undeclared identifier 'missing_bound'", failures=failures)

spanned_nested = build / "native-nested-derive.cpp"
spanned_nested.write_text('''namespace mapped {
using Count = long;
[[pmacro::invoke("compiler-test::spanned_record", (Count), ((40 + 2)), accept)]];
static_assert(__is_same(snapshot_type, Count));
static_assert(snapshot_value == 42 && Spanned{}.value == 42);
}
''')
run("native-nested-derive-snapshot", flags + plugin + ["-fsyntax-only", str(spanned_nested)],
    failures=failures)

spanned_nested_error = build / "native-nested-derive-error.cpp"
spanned_nested_error.write_text('''namespace mapped {
struct Complete {};
[[pmacro::invoke("compiler-test::spanned_record",
    (Complete), (Complete{}), reject
)]];
}
''')
run("native-nested-derive-position", flags + plugin + ["-fsyntax-only", str(spanned_nested_error)],
    f"{spanned_nested_error}:4:6: error: no type named 'Missing'", failures=failures)

native_initializer = build / "native-initializer.cpp"
native_initializer.write_text('''namespace mapped {
[[pmacro::invoke("compiler-test::spanned_record", (long),
    (missing_initializer), accept
)]];
}
''')
run("native-initializer-position", flags + plugin + ["-fsyntax-only", str(native_initializer)],
    f"{native_initializer}:3:6: error: use of undeclared identifier 'missing_initializer'", failures=failures)

if failures:
    raise RuntimeError("\n".join(failures))

print("in-compiler expansion: attr, derive, invoke, scope, tokens, PCM and failures passed")
