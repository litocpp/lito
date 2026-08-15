export module lito.toolchain.clang:arguments;

import rstd;
import lito.cpp;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

auto definition(ref<str> name) -> cpp::CompilerArgumentDefinition {
    return cpp::CompilerArgumentDefinition {
        .name      = String::make(name),
        .spellings = Vec<cpp::CompilerArgumentSpelling>::make(),
    };
}

auto spelling(cpp::CompilerArgumentDefinition& definition,
              ref<str>                         value,
              cpp::CompilerArgumentValueForm form = cpp::CompilerArgumentValueForm::None) -> void {
    definition.spellings.push(cpp::CompilerArgumentSpelling {
        .value = String::make(value),
        .form  = form,
    });
}

auto add(cpp::CppArgumentSchema&        schema,
         cpp::CppCompilerArgumentKind   kind,
         ref<str>                       name,
         ref<str>                       value,
         cpp::CompilerArgumentValueForm form,
         ref<str>                       family = {}) -> void {
    auto item = definition(name);
    spelling(item, value, form);
    schema.add(kind, rstd::move(item), family);
}

auto add_toggles(cpp::CppArgumentSchema&      schema,
                 cpp::CppCompilerArgumentKind kind,
                 ref<str>                     name,
                 ref<str>                     family,
                 ref<str>                     enabled,
                 ref<str>                     disabled) -> void {
    auto item = definition(name);
    spelling(item, enabled);
    spelling(item, disabled);
    schema.add(kind, rstd::move(item), family);
}

auto add_warning(cpp::CppArgumentSchema& schema,
                 cpp::CppWarning         warning,
                 bool                    enabled,
                 ref<str>                name,
                 ref<str>                spelling_value) -> void {
    auto item = definition(name);
    spelling(item, spelling_value);
    schema.add_typed(cpp::CppCompilerArgument::Warning(
                         cpp::CppWarningOption { .warning = warning, .enabled = enabled }),
                     rstd::move(item));
}

} // namespace lito

export namespace lito
{

auto make_clang_cpp_argument_parser() -> cpp::CppOptionResult<cpp::CppArgumentParser> {
    auto schema = cpp::CppArgumentSchema::make();

    add(schema,
        cpp::CppCompilerArgumentKind::MacroDefine,
        "define"_str,
        "-D"_str,
        cpp::CompilerArgumentValueForm::SeparateOrJoined);
    add(schema,
        cpp::CppCompilerArgumentKind::MacroUndefine,
        "undefine"_str,
        "-U"_str,
        cpp::CompilerArgumentValueForm::SeparateOrJoined);
    add(schema,
        cpp::CppCompilerArgumentKind::IncludeDirectory,
        "include-directory"_str,
        "-I"_str,
        cpp::CompilerArgumentValueForm::SeparateOrJoined);
    add(schema,
        cpp::CppCompilerArgumentKind::SystemIncludeDirectory,
        "system-include-directory"_str,
        "-isystem"_str,
        cpp::CompilerArgumentValueForm::SeparateOrJoined);

    auto target = definition("target"_str);
    spelling(target, "--target"_str, cpp::CompilerArgumentValueForm::SeparateOrEquals);
    spelling(target, "-target"_str, cpp::CompilerArgumentValueForm::SeparateOrEquals);
    schema.add(cpp::CppCompilerArgumentKind::Target, rstd::move(target));

    auto sysroot = definition("sysroot"_str);
    spelling(sysroot, "--sysroot"_str, cpp::CompilerArgumentValueForm::SeparateOrEquals);
    spelling(sysroot, "-isysroot"_str, cpp::CompilerArgumentValueForm::SeparateOrEquals);
    schema.add(cpp::CppCompilerArgumentKind::Sysroot, rstd::move(sysroot));

    add(schema,
        cpp::CppCompilerArgumentKind::OwnedLanguageStandard,
        "language-standard"_str,
        "-std"_str,
        cpp::CompilerArgumentValueForm::SeparateOrEquals);
    add(schema,
        cpp::CppCompilerArgumentKind::OwnedStandardLibrary,
        "standard-library"_str,
        "-stdlib"_str,
        cpp::CompilerArgumentValueForm::Equals);

    auto bmi = definition("bmi-representation"_str);
    spelling(bmi, "-fmodules-reduced-bmi"_str);
    spelling(bmi, "-fno-modules-reduced-bmi"_str);
    schema.add(cpp::CppCompilerArgumentKind::OwnedBmiRepresentation, rstd::move(bmi));

    add_toggles(schema,
                cpp::CppCompilerArgumentKind::OwnedRtti,
                "rtti"_str,
                {},
                "-frtti"_str,
                "-fno-rtti"_str);
    add_toggles(schema,
                cpp::CppCompilerArgumentKind::OwnedExceptions,
                "exceptions"_str,
                {},
                "-fexceptions"_str,
                "-fno-exceptions"_str);

    auto optimization = definition("optimization"_str);
    spelling(optimization, "-O"_str, cpp::CompilerArgumentValueForm::OptionalJoined);
    schema.add(cpp::CppCompilerArgumentKind::OwnedOptimization, rstd::move(optimization));
    add(schema,
        cpp::CppCompilerArgumentKind::OwnedDebugInfo,
        "debug-info"_str,
        "-g"_str,
        cpp::CompilerArgumentValueForm::OptionalJoined);
    auto lto = definition("lto"_str);
    spelling(lto, "-flto"_str, cpp::CompilerArgumentValueForm::OptionalEquals);
    spelling(lto, "-fno-lto"_str);
    schema.add(cpp::CppCompilerArgumentKind::OwnedLto, rstd::move(lto));

    auto pic = definition("position-independent-code"_str);
    spelling(pic, "-fPIC"_str);
    schema.add_typed(cpp::CppCompilerArgument::PositionIndependentCode(true), rstd::move(pic));
    auto no_pic = definition("no-position-independent-code"_str);
    spelling(no_pic, "-fno-PIC"_str);
    schema.add_typed(cpp::CppCompilerArgument::PositionIndependentCode(false), rstd::move(no_pic));
    auto pic_aliases = definition("position-independent-code-aliases"_str);
    spelling(pic_aliases, "-fpic"_str);
    spelling(pic_aliases, "-fPIE"_str);
    spelling(pic_aliases, "-fpie"_str);
    spelling(pic_aliases, "-fno-pic"_str);
    spelling(pic_aliases, "-fno-PIE"_str);
    spelling(pic_aliases, "-fno-pie"_str);
    schema.add(cpp::CppCompilerArgumentKind::CodegenMode, rstd::move(pic_aliases), "pic"_str);

    add_toggles(schema,
                cpp::CppCompilerArgumentKind::LanguageMode,
                "blocks"_str,
                "blocks"_str,
                "-fblocks"_str,
                "-fno-blocks"_str);
    add_toggles(schema,
                cpp::CppCompilerArgumentKind::LanguageMode,
                "coroutines"_str,
                "coroutines"_str,
                "-fcoroutines"_str,
                "-fno-coroutines"_str);
    add_toggles(schema,
                cpp::CppCompilerArgumentKind::LanguageMode,
                "hosted"_str,
                "hosted"_str,
                "-fhosted"_str,
                "-ffreestanding"_str);
    add_toggles(schema,
                cpp::CppCompilerArgumentKind::LanguageMode,
                "char8-t"_str,
                "char8-t"_str,
                "-fchar8_t"_str,
                "-fno-char8_t"_str);

    auto sized_deallocation = definition("sized-deallocation"_str);
    spelling(sized_deallocation, "-fsized-deallocation"_str);
    schema.add_typed(
        cpp::CppCompilerArgument::SizedDeallocation(cpp::CppSizedDeallocation::Enabled),
        rstd::move(sized_deallocation));
    auto no_sized_deallocation = definition("no-sized-deallocation"_str);
    spelling(no_sized_deallocation, "-fno-sized-deallocation"_str);
    schema.add_typed(
        cpp::CppCompilerArgument::SizedDeallocation(cpp::CppSizedDeallocation::Disabled),
        rstd::move(no_sized_deallocation));
    add_toggles(schema,
                cpp::CppCompilerArgumentKind::AbiMode,
                "char-signedness"_str,
                "char-signedness"_str,
                "-fsigned-char"_str,
                "-funsigned-char"_str);
    add_toggles(schema,
                cpp::CppCompilerArgumentKind::AbiMode,
                "short-enums"_str,
                "short-enums"_str,
                "-fshort-enums"_str,
                "-fno-short-enums"_str);
    add_toggles(schema,
                cpp::CppCompilerArgumentKind::AbiMode,
                "short-wchar"_str,
                "short-wchar"_str,
                "-fshort-wchar"_str,
                "-fno-short-wchar"_str);

    add(schema,
        cpp::CppCompilerArgumentKind::TargetMode,
        "architecture"_str,
        "-march"_str,
        cpp::CompilerArgumentValueForm::SeparateOrEquals,
        "arch"_str);
    add(schema,
        cpp::CppCompilerArgumentKind::TargetMode,
        "cpu"_str,
        "-mcpu"_str,
        cpp::CompilerArgumentValueForm::SeparateOrEquals,
        "cpu"_str);
    add(schema,
        cpp::CppCompilerArgumentKind::TargetMode,
        "tune"_str,
        "-mtune"_str,
        cpp::CompilerArgumentValueForm::SeparateOrEquals,
        "tune"_str);
    add(schema,
        cpp::CppCompilerArgumentKind::TargetMode,
        "target-abi"_str,
        "-mabi"_str,
        cpp::CompilerArgumentValueForm::SeparateOrEquals,
        "abi"_str);

    add(schema,
        cpp::CppCompilerArgumentKind::VendorCodegen,
        "llvm-backend"_str,
        "-mllvm"_str,
        cpp::CompilerArgumentValueForm::SeparateOrEquals);
    add(schema,
        cpp::CppCompilerArgumentKind::LanguageMode,
        "posix-threads"_str,
        "-pthread"_str,
        cpp::CompilerArgumentValueForm::None,
        "posix-threads"_str);
    add(schema,
        cpp::CppCompilerArgumentKind::TargetMode,
        "target-mode"_str,
        "-m"_str,
        cpp::CompilerArgumentValueForm::Joined);

    add(schema,
        cpp::CppCompilerArgumentKind::Instrumentation,
        "sanitizer"_str,
        "-fsanitize"_str,
        cpp::CompilerArgumentValueForm::Equals);
    add(schema,
        cpp::CppCompilerArgumentKind::Instrumentation,
        "no-sanitizer"_str,
        "-fno-sanitize"_str,
        cpp::CompilerArgumentValueForm::Equals);
    add(schema,
        cpp::CppCompilerArgumentKind::Instrumentation,
        "sanitizer-trap"_str,
        "-fsanitize-trap"_str,
        cpp::CompilerArgumentValueForm::Equals);
    add(schema,
        cpp::CppCompilerArgumentKind::Instrumentation,
        "no-sanitizer-trap"_str,
        "-fno-sanitize-trap"_str,
        cpp::CompilerArgumentValueForm::Equals);
    add(schema,
        cpp::CppCompilerArgumentKind::Instrumentation,
        "openmp"_str,
        "-fopenmp"_str,
        cpp::CompilerArgumentValueForm::OptionalEquals);
    add(schema,
        cpp::CppCompilerArgumentKind::Instrumentation,
        "no-openmp"_str,
        "-fno-openmp"_str,
        cpp::CompilerArgumentValueForm::OptionalEquals);
    add(schema,
        cpp::CppCompilerArgumentKind::Instrumentation,
        "pointer-authentication"_str,
        "-fptrauth"_str,
        cpp::CompilerArgumentValueForm::OptionalJoined);
    add(schema,
        cpp::CppCompilerArgumentKind::Instrumentation,
        "no-pointer-authentication"_str,
        "-fno-ptrauth"_str,
        cpp::CompilerArgumentValueForm::OptionalJoined);

    add_warning(schema, cpp::CppWarning::All, true, "all-warnings"_str, "-Wall"_str);
    add_warning(schema, cpp::CppWarning::Pedantic, true, "pedantic-warnings"_str, "-Wpedantic"_str);
    add_warning(schema,
                cpp::CppWarning::GnuStatementExpression,
                false,
                "gnu-statement-expression-warnings"_str,
                "-Wno-gnu-statement-expression"_str);
    add_warning(schema,
                cpp::CppWarning::DeprecatedDeclarations,
                false,
                "deprecated-declaration-warnings"_str,
                "-Wno-deprecated-declarations"_str);
    add_warning(schema,
                cpp::CppWarning::UnknownAttributes,
                true,
                "unknown-attribute-warnings"_str,
                "-Wunknown-attributes"_str);
    add(schema,
        cpp::CppCompilerArgumentKind::Diagnostic,
        "warning"_str,
        "-W"_str,
        cpp::CompilerArgumentValueForm::Joined);
    auto pedantic = definition("pedantic"_str);
    spelling(pedantic, "-pedantic"_str);
    spelling(pedantic, "-pedantic-errors"_str);
    schema.add(cpp::CppCompilerArgumentKind::Diagnostic, rstd::move(pedantic));

    add(schema,
        cpp::CppCompilerArgumentKind::VendorLanguage,
        "microsoft-language"_str,
        "-fms-"_str,
        cpp::CompilerArgumentValueForm::Joined);
    add(schema,
        cpp::CppCompilerArgumentKind::VendorLanguage,
        "no-microsoft-language"_str,
        "-fno-ms-"_str,
        cpp::CompilerArgumentValueForm::Joined);

    auto native_preprocessor = [&](ref<str> name, ref<str> value) {
        add(schema,
            cpp::CppCompilerArgumentKind::VendorPreprocessorUnsupported,
            name,
            value,
            cpp::CompilerArgumentValueForm::SeparateOrEquals);
    };
    native_preprocessor("forced-include"_str, "-include"_str);
    native_preprocessor("forced-macros"_str, "-imacros"_str);
    native_preprocessor("precompiled-header"_str, "-include-pch"_str);
    native_preprocessor("module-map"_str, "-fmodule-map-file"_str);
    native_preprocessor("module-file"_str, "-fmodule-file"_str);
    native_preprocessor("module-cache"_str, "-fmodules-cache-path"_str);

    return rstd::move(schema).build();
}

} // namespace lito
