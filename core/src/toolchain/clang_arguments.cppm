export module tenon.toolchain:clang_arguments;

import rstd;
import tenon.cpp;
import tenon.compiler.arguments;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace tenon
{

auto definition(ref<str> name) -> CompilerArgumentDefinition {
    return CompilerArgumentDefinition {
        .name      = String::make(name),
        .spellings = Vec<CompilerArgumentSpelling>::make(),
    };
}

auto spelling(CompilerArgumentDefinition& definition,
              ref<str>                    value,
              CompilerArgumentValueForm   form = CompilerArgumentValueForm::None) -> void {
    definition.spellings.push(CompilerArgumentSpelling {
        .value = String::make(value),
        .form  = form,
    });
}

auto add(CppArgumentSchema&        schema,
         CppCompilerArgumentKind   kind,
         ref<str>                  name,
         ref<str>                  value,
         CompilerArgumentValueForm form,
         ref<str>                  family = {}) -> void {
    auto item = definition(name);
    spelling(item, value, form);
    schema.add(kind, rstd::move(item), family);
}

auto add_toggles(CppArgumentSchema&      schema,
                 CppCompilerArgumentKind kind,
                 ref<str>                name,
                 ref<str>                family,
                 ref<str>                enabled,
                 ref<str>                disabled) -> void {
    auto item = definition(name);
    spelling(item, enabled);
    spelling(item, disabled);
    schema.add(kind, rstd::move(item), family);
}

} // namespace tenon

export namespace tenon
{

auto make_clang_cpp_argument_parser() -> CppOptionResult<CppArgumentParser> {
    auto schema = CppArgumentSchema::make();

    add(schema,
        CppCompilerArgumentKind::MacroDefine,
        "define"_str,
        "-D"_str,
        CompilerArgumentValueForm::SeparateOrJoined);
    add(schema,
        CppCompilerArgumentKind::MacroUndefine,
        "undefine"_str,
        "-U"_str,
        CompilerArgumentValueForm::SeparateOrJoined);
    add(schema,
        CppCompilerArgumentKind::IncludeDirectory,
        "include-directory"_str,
        "-I"_str,
        CompilerArgumentValueForm::SeparateOrJoined);

    auto target = definition("target"_str);
    spelling(target, "--target"_str, CompilerArgumentValueForm::SeparateOrEquals);
    spelling(target, "-target"_str, CompilerArgumentValueForm::SeparateOrEquals);
    schema.add(CppCompilerArgumentKind::Target, rstd::move(target));

    auto sysroot = definition("sysroot"_str);
    spelling(sysroot, "--sysroot"_str, CompilerArgumentValueForm::SeparateOrEquals);
    spelling(sysroot, "-isysroot"_str, CompilerArgumentValueForm::SeparateOrEquals);
    schema.add(CppCompilerArgumentKind::Sysroot, rstd::move(sysroot));

    add(schema,
        CppCompilerArgumentKind::OwnedLanguageStandard,
        "language-standard"_str,
        "-std"_str,
        CompilerArgumentValueForm::SeparateOrEquals);
    add(schema,
        CppCompilerArgumentKind::OwnedStandardLibrary,
        "standard-library"_str,
        "-stdlib"_str,
        CompilerArgumentValueForm::Equals);

    auto bmi = definition("bmi-representation"_str);
    spelling(bmi, "-fmodules-reduced-bmi"_str);
    spelling(bmi, "-fno-modules-reduced-bmi"_str);
    schema.add(CppCompilerArgumentKind::OwnedBmiRepresentation, rstd::move(bmi));

    add_toggles(
        schema, CppCompilerArgumentKind::OwnedRtti, "rtti"_str, {}, "-frtti"_str, "-fno-rtti"_str);
    add_toggles(schema,
                CppCompilerArgumentKind::OwnedExceptions,
                "exceptions"_str,
                {},
                "-fexceptions"_str,
                "-fno-exceptions"_str);

    auto optimization = definition("optimization"_str);
    spelling(optimization, "-O"_str);
    spelling(optimization, "-O0"_str);
    spelling(optimization, "-O1"_str);
    spelling(optimization, "-O2"_str);
    spelling(optimization, "-O3"_str);
    spelling(optimization, "-O4"_str);
    spelling(optimization, "-Og"_str);
    spelling(optimization, "-Os"_str);
    spelling(optimization, "-Oz"_str);
    spelling(optimization, "-Ofast"_str);
    schema.add(CppCompilerArgumentKind::OwnedOptimization, rstd::move(optimization));
    add(schema,
        CppCompilerArgumentKind::OwnedDebugInfo,
        "debug-info"_str,
        "-g"_str,
        CompilerArgumentValueForm::OptionalJoined);

    add_toggles(schema,
                CppCompilerArgumentKind::CodegenMode,
                "position-independent-code"_str,
                "pic"_str,
                "-fPIC"_str,
                "-fno-PIC"_str);
    auto pic_aliases = definition("position-independent-code-aliases"_str);
    spelling(pic_aliases, "-fpic"_str);
    spelling(pic_aliases, "-fPIE"_str);
    spelling(pic_aliases, "-fpie"_str);
    spelling(pic_aliases, "-fno-pic"_str);
    spelling(pic_aliases, "-fno-PIE"_str);
    spelling(pic_aliases, "-fno-pie"_str);
    schema.add(CppCompilerArgumentKind::CodegenMode, rstd::move(pic_aliases), "pic"_str);

    add_toggles(schema,
                CppCompilerArgumentKind::LanguageMode,
                "blocks"_str,
                "blocks"_str,
                "-fblocks"_str,
                "-fno-blocks"_str);
    add_toggles(schema,
                CppCompilerArgumentKind::LanguageMode,
                "coroutines"_str,
                "coroutines"_str,
                "-fcoroutines"_str,
                "-fno-coroutines"_str);
    add_toggles(schema,
                CppCompilerArgumentKind::LanguageMode,
                "hosted"_str,
                "hosted"_str,
                "-fhosted"_str,
                "-ffreestanding"_str);
    add_toggles(schema,
                CppCompilerArgumentKind::LanguageMode,
                "char8-t"_str,
                "char8-t"_str,
                "-fchar8_t"_str,
                "-fno-char8_t"_str);

    add_toggles(schema,
                CppCompilerArgumentKind::AbiMode,
                "sized-deallocation"_str,
                "sized-deallocation"_str,
                "-fsized-deallocation"_str,
                "-fno-sized-deallocation"_str);
    add_toggles(schema,
                CppCompilerArgumentKind::AbiMode,
                "char-signedness"_str,
                "char-signedness"_str,
                "-fsigned-char"_str,
                "-funsigned-char"_str);
    add_toggles(schema,
                CppCompilerArgumentKind::AbiMode,
                "short-enums"_str,
                "short-enums"_str,
                "-fshort-enums"_str,
                "-fno-short-enums"_str);
    add_toggles(schema,
                CppCompilerArgumentKind::AbiMode,
                "short-wchar"_str,
                "short-wchar"_str,
                "-fshort-wchar"_str,
                "-fno-short-wchar"_str);

    add(schema,
        CppCompilerArgumentKind::TargetMode,
        "architecture"_str,
        "-march"_str,
        CompilerArgumentValueForm::SeparateOrEquals,
        "arch"_str);
    add(schema,
        CppCompilerArgumentKind::TargetMode,
        "cpu"_str,
        "-mcpu"_str,
        CompilerArgumentValueForm::SeparateOrEquals,
        "cpu"_str);
    add(schema,
        CppCompilerArgumentKind::TargetMode,
        "tune"_str,
        "-mtune"_str,
        CompilerArgumentValueForm::SeparateOrEquals,
        "tune"_str);
    add(schema,
        CppCompilerArgumentKind::TargetMode,
        "target-abi"_str,
        "-mabi"_str,
        CompilerArgumentValueForm::SeparateOrEquals,
        "abi"_str);

    add(schema,
        CppCompilerArgumentKind::VendorCodegen,
        "llvm-backend"_str,
        "-mllvm"_str,
        CompilerArgumentValueForm::SeparateOrEquals);
    add(schema,
        CppCompilerArgumentKind::TargetMode,
        "target-mode"_str,
        "-m"_str,
        CompilerArgumentValueForm::Joined);

    add(schema,
        CppCompilerArgumentKind::Instrumentation,
        "sanitizer"_str,
        "-fsanitize"_str,
        CompilerArgumentValueForm::Equals);
    add(schema,
        CppCompilerArgumentKind::Instrumentation,
        "no-sanitizer"_str,
        "-fno-sanitize"_str,
        CompilerArgumentValueForm::Equals);
    add(schema,
        CppCompilerArgumentKind::Instrumentation,
        "sanitizer-trap"_str,
        "-fsanitize-trap"_str,
        CompilerArgumentValueForm::Equals);
    add(schema,
        CppCompilerArgumentKind::Instrumentation,
        "no-sanitizer-trap"_str,
        "-fno-sanitize-trap"_str,
        CompilerArgumentValueForm::Equals);
    add(schema,
        CppCompilerArgumentKind::Instrumentation,
        "openmp"_str,
        "-fopenmp"_str,
        CompilerArgumentValueForm::OptionalEquals);
    add(schema,
        CppCompilerArgumentKind::Instrumentation,
        "no-openmp"_str,
        "-fno-openmp"_str,
        CompilerArgumentValueForm::OptionalEquals);
    add(schema,
        CppCompilerArgumentKind::Instrumentation,
        "pointer-authentication"_str,
        "-fptrauth"_str,
        CompilerArgumentValueForm::OptionalJoined);
    add(schema,
        CppCompilerArgumentKind::Instrumentation,
        "no-pointer-authentication"_str,
        "-fno-ptrauth"_str,
        CompilerArgumentValueForm::OptionalJoined);

    add(schema,
        CppCompilerArgumentKind::Diagnostic,
        "warning"_str,
        "-W"_str,
        CompilerArgumentValueForm::Joined);
    auto pedantic = definition("pedantic"_str);
    spelling(pedantic, "-pedantic"_str);
    spelling(pedantic, "-pedantic-errors"_str);
    schema.add(CppCompilerArgumentKind::Diagnostic, rstd::move(pedantic));

    add(schema,
        CppCompilerArgumentKind::VendorLanguage,
        "microsoft-language"_str,
        "-fms-"_str,
        CompilerArgumentValueForm::Joined);
    add(schema,
        CppCompilerArgumentKind::VendorLanguage,
        "no-microsoft-language"_str,
        "-fno-ms-"_str,
        CompilerArgumentValueForm::Joined);

    auto native_preprocessor = [&](ref<str> name, ref<str> value) {
        add(schema,
            CppCompilerArgumentKind::VendorPreprocessorUnsupported,
            name,
            value,
            CompilerArgumentValueForm::SeparateOrEquals);
    };
    native_preprocessor("forced-include"_str, "-include"_str);
    native_preprocessor("forced-macros"_str, "-imacros"_str);
    native_preprocessor("precompiled-header"_str, "-include-pch"_str);
    native_preprocessor("module-map"_str, "-fmodule-map-file"_str);
    native_preprocessor("module-file"_str, "-fmodule-file"_str);
    native_preprocessor("module-cache"_str, "-fmodules-cache-path"_str);

    return rstd::move(schema).build();
}

} // namespace tenon
