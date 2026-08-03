export module tenon.cpp;

import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

using PathBuf = rstd::path::PathBuf;

export namespace tenon
{

enum class StandardLibrary
{
    Libstdcxx,
    Libcxx,
};

enum class CppOptimization
{
    Default,
    None,
    Level1,
    Level2,
    Level3,
    Level4,
    Debug,
    Size,
    SizeMin,
    Fast,
};

enum class CppDebugInfo
{
    None,
    Full,
};

enum class CppMacroAction
{
    Define,
    Undefine,
};

enum class CppVendorOptionEffect
{
    Preprocessor,
    Language,
    Abi,
    Codegen,
    Diagnostic,
    Unknown,
};

struct CppMacroDirective {
    CppMacroAction action { CppMacroAction::Define };
    String         value;
};

struct CppFamilyOption {
    String family;
    String value;
};

struct CppVendorOption {
    String                value;
    CppVendorOptionEffect effect { CppVendorOptionEffect::Unknown };
};

struct CppLanguageOptions {
    String               standard;
    bool                 exceptions { false };
    bool                 rtti { false };
    Vec<CppFamilyOption> modes;
};

struct CppAbiOptions {
    StandardLibrary      standard_library { StandardLibrary::Libstdcxx };
    Vec<CppFamilyOption> modes;
};

struct CppTargetOptions {
    Option<String>       target;
    Option<String>       sysroot;
    Vec<CppFamilyOption> features;
};

struct CppPreprocessorOptions {
    Vec<PathBuf>           include_directories;
    Vec<CppMacroDirective> macros;
};

struct CppCodegenOptions {
    CppOptimization      optimization { CppOptimization::Default };
    CppDebugInfo         debug_info { CppDebugInfo::None };
    Vec<CppFamilyOption> modes;
    Vec<String>          instrumentation;
};

struct CppDiagnosticOptions {
    Vec<String> options;
};

struct CppCompileOptions {
    CppLanguageOptions     language;
    CppAbiOptions          abi;
    CppTargetOptions       target;
    CppPreprocessorOptions preprocessor;
    CppCodegenOptions      codegen;
    CppDiagnosticOptions   diagnostics;
    Vec<CppVendorOption>   vendor;
};

struct CppPublicRequirements {
    Vec<PathBuf>           include_directories;
    Vec<CppMacroDirective> macros;
};

struct CppOptionLayer {
    Vec<PathBuf> include_directories;
    Vec<String>  definitions;
    Vec<String>  options;
};

template<typename T>
using CppOptionResult = Result<T, String>;

auto is_supported_cpp_standard(ref<str> standard) noexcept -> bool {
    return standard == "c++20"_str || standard == "c++23"_str || standard == "c++26"_str ||
           standard == "c++2b"_str || standard == "c++2c"_str;
}

auto canonical_cpp_standard(ref<str> standard) noexcept -> ref<str> {
    if (standard == "c++2b"_str) return "c++23"_str;
    if (standard == "c++2c"_str) return "c++26"_str;
    return standard;
}

auto cpp_optimization_option(CppOptimization value) noexcept -> ref<str> {
    switch (value) {
    case CppOptimization::Default: return ""_str;
    case CppOptimization::None: return "-O0"_str;
    case CppOptimization::Level1: return "-O1"_str;
    case CppOptimization::Level2: return "-O2"_str;
    case CppOptimization::Level3: return "-O3"_str;
    case CppOptimization::Level4: return "-O4"_str;
    case CppOptimization::Debug: return "-Og"_str;
    case CppOptimization::Size: return "-Os"_str;
    case CppOptimization::SizeMin: return "-Oz"_str;
    case CppOptimization::Fast: return "-Ofast"_str;
    }
    return ""_str;
}

auto cpp_debug_option(CppDebugInfo value) noexcept -> ref<str> {
    return value == CppDebugInfo::Full ? "-g"_str : ""_str;
}

auto clone_cpp_options(const CppCompileOptions& input) -> CppCompileOptions;

auto cpp_public_requirements(const CppCompileOptions& input) -> CppPublicRequirements;

auto clone_cpp_public_requirements(const CppPublicRequirements& input) -> CppPublicRequirements;

auto merge_cpp_public_requirements(CppPublicRequirements input, const CppPublicRequirements& extra)
    -> CppPublicRequirements;

auto make_cpp_options(ref<str>        language_standard,
                      StandardLibrary standard_library,
                      bool            exceptions,
                      bool            rtti,
                      CppOptimization optimization,
                      CppDebugInfo    debug_info,
                      CppOptionLayer  layer = {}) -> CppOptionResult<CppCompileOptions>;

auto apply_cpp_option_layer(CppCompileOptions input, CppOptionLayer layer)
    -> CppOptionResult<CppCompileOptions>;

auto merge_cpp_options(CppCompileOptions input, const CppCompileOptions& extra)
    -> CppOptionResult<CppCompileOptions>;

auto cpp_compile_identity(const CppCompileOptions& options) -> String;

auto cpp_bmi_compatibility_identity(const CppCompileOptions& options) -> String;

auto cpp_public_requirements_identity(const CppPublicRequirements& requirements) -> String;

auto cpp_public_requirements_satisfied(const CppPublicRequirements& requirements,
                                       const CppCompileOptions&     consumer) -> bool;

} // namespace tenon

namespace tenon
{

template<typename Key, typename Value>
using BTreeMap = rstd::collections::BTreeMap<Key, Value>;

auto option_error(ref<str> message) -> CppOptionResult<CppCompileOptions> {
    return Err(String::make(message));
}

auto option_error(String message) -> CppOptionResult<CppCompileOptions> {
    return Err(rstd::move(message));
}

auto clone_family_options(const Vec<CppFamilyOption>& values) -> Vec<CppFamilyOption> {
    auto result = Vec<CppFamilyOption>::with_capacity(values.len());
    for (const auto& value : values) {
        result.push(CppFamilyOption {
            .family = value.family.clone(),
            .value  = value.value.clone(),
        });
    }
    return result;
}

auto clone_strings(const Vec<String>& values) -> Vec<String> {
    auto result = Vec<String>::with_capacity(values.len());
    for (const auto& value : values) result.push(value.clone());
    return result;
}

auto clone_paths(const Vec<PathBuf>& values) -> Vec<PathBuf> {
    auto result = Vec<PathBuf>::with_capacity(values.len());
    for (const auto& value : values) result.push(value.clone());
    return result;
}

auto append_unique(Vec<String>& output, String value) -> void {
    for (const auto& existing : output) {
        if (existing.as_str() == value.as_str()) return;
    }
    output.push(rstd::move(value));
}

auto append_unique(Vec<PathBuf>& output, PathBuf value) -> void {
    for (const auto& existing : output) {
        if (existing.as_path() == value.as_path()) return;
    }
    output.push(rstd::move(value));
}

auto attached_value(ref<str> option, ref<str> prefix) -> Option<ref<str>> {
    if (! option.starts_with(prefix) || option.len() <= prefix.len()) return None();
    return option.get(prefix.len(), option.len());
}

auto target_option_family(ref<str> option) -> String {
    auto begin = option.starts_with("-mno-"_str) ? usize(5) : usize(2);
    auto end   = begin;
    while (end < option.len() && option.as_bytes()[end] != u8('=')) ++end;
    auto name = option.get(begin, end);
    return name.is_some() ? String::make(*name) : String::make(option);
}

auto toggle_family(ref<str> option) -> Option<ref<str>> {
    if (option == "-fPIC"_str || option == "-fpic"_str || option == "-fPIE"_str ||
        option == "-fpie"_str || option == "-fno-PIC"_str || option == "-fno-pic"_str ||
        option == "-fno-PIE"_str || option == "-fno-pie"_str) {
        return Some("pic"_str);
    }
    if (option == "-fblocks"_str || option == "-fno-blocks"_str) return Some("blocks"_str);
    if (option == "-fcoroutines"_str || option == "-fno-coroutines"_str) {
        return Some("coroutines"_str);
    }
    if (option == "-fsized-deallocation"_str || option == "-fno-sized-deallocation"_str) {
        return Some("sized-deallocation"_str);
    }
    if (option == "-ffreestanding"_str || option == "-fhosted"_str) return Some("hosted"_str);
    if (option == "-fsigned-char"_str || option == "-funsigned-char"_str) {
        return Some("char-signedness"_str);
    }
    if (option == "-fshort-enums"_str || option == "-fno-short-enums"_str) {
        return Some("short-enums"_str);
    }
    if (option == "-fshort-wchar"_str || option == "-fno-short-wchar"_str) {
        return Some("short-wchar"_str);
    }
    if (option == "-fchar8_t"_str || option == "-fno-char8_t"_str) return Some("char8-t"_str);
    return None();
}

auto optimization(ref<str> option) -> Option<CppOptimization> {
    if (option == "-O"_str || option == "-O1"_str) return Some(CppOptimization::Level1);
    if (option == "-O0"_str) return Some(CppOptimization::None);
    if (option == "-O2"_str) return Some(CppOptimization::Level2);
    if (option == "-O3"_str) return Some(CppOptimization::Level3);
    if (option == "-O4"_str) return Some(CppOptimization::Level4);
    if (option == "-Og"_str) return Some(CppOptimization::Debug);
    if (option == "-Os"_str) return Some(CppOptimization::Size);
    if (option == "-Oz"_str) return Some(CppOptimization::SizeMin);
    if (option == "-Ofast"_str) return Some(CppOptimization::Fast);
    return None();
}

auto family_map(const Vec<CppFamilyOption>& values) -> BTreeMap<String, String> {
    auto result = BTreeMap<String, String>::make();
    for (const auto& value : values) result.insert(value.family.clone(), value.value.clone());
    return result;
}

auto family_values(BTreeMap<String, String> values) -> Vec<CppFamilyOption> {
    auto result   = Vec<CppFamilyOption>::with_capacity(values.len());
    auto iterator = values.into_iter();
    while (auto value = iterator.next()) {
        auto entry = rstd::move(value).unwrap();
        result.push(CppFamilyOption {
            .family = rstd::move(entry.template get<0>()),
            .value  = rstd::move(entry.template get<1>()),
        });
    }
    return result;
}

auto macro_clone(const Vec<CppMacroDirective>& values) -> Vec<CppMacroDirective> {
    auto result = Vec<CppMacroDirective>::with_capacity(values.len());
    for (const auto& value : values) {
        result.push(CppMacroDirective {
            .action = value.action,
            .value  = value.value.clone(),
        });
    }
    return result;
}

auto vendor_clone(const Vec<CppVendorOption>& values) -> Vec<CppVendorOption> {
    auto result = Vec<CppVendorOption>::with_capacity(values.len());
    for (const auto& value : values) {
        result.push(CppVendorOption {
            .value  = value.value.clone(),
            .effect = value.effect,
        });
    }
    return result;
}

auto macro_name(ref<str> value) -> ref<str> {
    auto separator = value.find("="_str);
    return separator.is_some() ? value.split_at(*separator).get<0>() : value;
}

auto macro_states(const Vec<CppMacroDirective>& values) -> BTreeMap<String, String> {
    auto result = BTreeMap<String, String>::make();
    for (const auto& value : values) {
        auto state =
            String::make(value.action == CppMacroAction::Define ? "define:"_str : "undefine:"_str);
        state.push_str(value.value.as_str());
        result.insert(String::make(macro_name(value.value.as_str())), rstd::move(state));
    }
    return result;
}

auto push_identity(String& output, ref<str> key, ref<str> value) -> void {
    output.push_str(key);
    output.push_ascii('=');
    output.push_str(rstd::format("{}:{}\n", value.len(), value).as_str());
}

auto push_identity(String& output, ref<str> key, bool value) -> void {
    push_identity(output, key, value ? "true"_str : "false"_str);
}

auto option_effect_name(CppVendorOptionEffect effect) -> ref<str> {
    switch (effect) {
    case CppVendorOptionEffect::Preprocessor: return "preprocessor"_str;
    case CppVendorOptionEffect::Language: return "language"_str;
    case CppVendorOptionEffect::Abi: return "abi"_str;
    case CppVendorOptionEffect::Codegen: return "codegen"_str;
    case CppVendorOptionEffect::Diagnostic: return "diagnostic"_str;
    case CppVendorOptionEffect::Unknown: return "unknown"_str;
    }
    return "unknown"_str;
}

auto append_semantic_identity(String& output, const CppCompileOptions& options) -> void {
    push_identity(output, "standard"_str, options.language.standard.as_str());
    push_identity(output, "exceptions"_str, options.language.exceptions);
    push_identity(output, "rtti"_str, options.language.rtti);
    push_identity(output,
                  "stdlib"_str,
                  options.abi.standard_library == StandardLibrary::Libstdcxx ? "libstdc++"_str
                                                                             : "libc++"_str);
    for (const auto& value : options.language.modes) {
        push_identity(output,
                      rstd::format("language:{}", value.family.as_str()).as_str(),
                      value.value.as_str());
    }
    for (const auto& value : options.abi.modes) {
        push_identity(
            output, rstd::format("abi:{}", value.family.as_str()).as_str(), value.value.as_str());
    }
    if (options.target.target.is_some()) {
        push_identity(output, "target"_str, options.target.target->as_str());
    }
    if (options.target.sysroot.is_some()) {
        push_identity(output, "sysroot"_str, options.target.sysroot->as_str());
    }
    for (const auto& value : options.target.features) {
        push_identity(output,
                      rstd::format("target:{}", value.family.as_str()).as_str(),
                      value.value.as_str());
    }
}

} // namespace tenon

export namespace tenon
{

auto clone_cpp_options(const CppCompileOptions& input) -> CppCompileOptions {
    auto result = CppCompileOptions {
        .language =
            CppLanguageOptions {
                .standard   = input.language.standard.clone(),
                .exceptions = input.language.exceptions,
                .rtti       = input.language.rtti,
                .modes      = clone_family_options(input.language.modes),
            },
        .abi =
            CppAbiOptions {
                .standard_library = input.abi.standard_library,
                .modes            = clone_family_options(input.abi.modes),
            },
        .preprocessor =
            CppPreprocessorOptions {
                .include_directories = clone_paths(input.preprocessor.include_directories),
                .macros              = macro_clone(input.preprocessor.macros),
            },
        .codegen =
            CppCodegenOptions {
                .optimization    = input.codegen.optimization,
                .debug_info      = input.codegen.debug_info,
                .modes           = clone_family_options(input.codegen.modes),
                .instrumentation = clone_strings(input.codegen.instrumentation),
            },
        .diagnostics =
            CppDiagnosticOptions {
                .options = clone_strings(input.diagnostics.options),
            },
        .vendor = vendor_clone(input.vendor),
    };
    if (input.target.target.is_some()) result.target.target = Some(input.target.target->clone());
    if (input.target.sysroot.is_some()) {
        result.target.sysroot = Some(input.target.sysroot->clone());
    }
    result.target.features = clone_family_options(input.target.features);
    return result;
}

auto cpp_public_requirements(const CppCompileOptions& input) -> CppPublicRequirements {
    return CppPublicRequirements {
        .include_directories = clone_paths(input.preprocessor.include_directories),
        .macros              = macro_clone(input.preprocessor.macros),
    };
}

auto clone_cpp_public_requirements(const CppPublicRequirements& input) -> CppPublicRequirements {
    return CppPublicRequirements {
        .include_directories = clone_paths(input.include_directories),
        .macros              = macro_clone(input.macros),
    };
}

auto merge_cpp_public_requirements(CppPublicRequirements input, const CppPublicRequirements& extra)
    -> CppPublicRequirements {
    for (const auto& include : extra.include_directories) {
        append_unique(input.include_directories, include.clone());
    }
    for (const auto& macro : extra.macros) {
        input.macros.push(CppMacroDirective {
            .action = macro.action,
            .value  = macro.value.clone(),
        });
    }
    return input;
}

auto make_cpp_options(ref<str>        language_standard,
                      StandardLibrary standard_library,
                      bool            exceptions,
                      bool            rtti,
                      CppOptimization optimization_value,
                      CppDebugInfo    debug_info,
                      CppOptionLayer  layer) -> CppOptionResult<CppCompileOptions> {
    auto result = CppCompileOptions {
        .language =
            CppLanguageOptions {
                .standard   = String::make(canonical_cpp_standard(language_standard)),
                .exceptions = exceptions,
                .rtti       = rtti,
            },
        .abi =
            CppAbiOptions {
                .standard_library = standard_library,
            },
        .codegen =
            CppCodegenOptions {
                .optimization = optimization_value,
                .debug_info   = debug_info,
            },
    };
    return apply_cpp_option_layer(rstd::move(result), rstd::move(layer));
}

auto apply_cpp_option_layer(CppCompileOptions input, CppOptionLayer layer)
    -> CppOptionResult<CppCompileOptions> {
    for (auto& include : layer.include_directories) {
        append_unique(input.preprocessor.include_directories, rstd::move(include));
    }
    for (auto& definition : layer.definitions) {
        input.preprocessor.macros.push(CppMacroDirective {
            .action = CppMacroAction::Define,
            .value  = rstd::move(definition),
        });
    }

    auto language_modes  = family_map(input.language.modes);
    auto abi_modes       = family_map(input.abi.modes);
    auto target_modes    = family_map(input.target.features);
    auto codegen_modes   = family_map(input.codegen.modes);
    auto instrumentation = BTreeMap<String, empty>::make();
    for (const auto& value : input.codegen.instrumentation) {
        instrumentation.insert(value.clone(), empty {});
    }

    for (auto index = usize {}; index < layer.options.len(); ++index) {
        auto option     = layer.options[index].as_str();
        auto take_value = [&](ref<str> purpose) -> CppOptionResult<String> {
            if (index + usize(1) >= layer.options.len()) {
                return Err(rstd::format("compiler option '{}' requires {}", option, purpose));
            }
            ++index;
            return Ok(layer.options[index].clone());
        };

        if (option == "-std"_str || option.starts_with("-std="_str) ||
            option.starts_with("-stdlib="_str) || option == "-fmodules-reduced-bmi"_str ||
            option == "-fno-modules-reduced-bmi"_str || option == "-frtti"_str ||
            option == "-fno-rtti"_str || option == "-fexceptions"_str ||
            option == "-fno-exceptions"_str || optimization(option).is_some() ||
            option == "-g"_str || option == "-g0"_str) {
            return option_error(
                rstd::format("compiler option '{}' overrides a Tenon-owned setting", option));
        }
        if (option == "-D"_str || option == "-U"_str) {
            auto value = take_value("a macro name"_str);
            if (value.is_err()) return Err(rstd::move(value).unwrap_err());
            input.preprocessor.macros.push(CppMacroDirective {
                .action = option == "-D"_str ? CppMacroAction::Define : CppMacroAction::Undefine,
                .value  = rstd::move(value).unwrap(),
            });
            continue;
        }
        if (option.starts_with("-D"_str) || option.starts_with("-U"_str)) {
            auto value = option.get(usize(2), option.len());
            if (value.is_none() || value->is_empty()) {
                return option_error(rstd::format("compiler option '{}' has no macro name", option));
            }
            input.preprocessor.macros.push(CppMacroDirective {
                .action = option.starts_with("-D"_str) ? CppMacroAction::Define
                                                       : CppMacroAction::Undefine,
                .value  = String::make(*value),
            });
            continue;
        }
        if (option == "-I"_str) {
            auto value = take_value("an include directory"_str);
            if (value.is_err()) return Err(rstd::move(value).unwrap_err());
            append_unique(input.preprocessor.include_directories,
                          PathBuf::from(rstd::move(value).unwrap()));
            continue;
        }
        auto include = attached_value(option, "-I"_str);
        if (include.is_some()) {
            append_unique(input.preprocessor.include_directories, PathBuf::from(*include));
            continue;
        }
        if (option == "--target"_str || option == "-target"_str) {
            auto value = take_value("a target triple"_str);
            if (value.is_err()) return Err(rstd::move(value).unwrap_err());
            input.target.target = Some(rstd::move(value).unwrap());
            continue;
        }
        auto target = attached_value(option, "--target="_str);
        if (target.is_none()) target = attached_value(option, "-target="_str);
        if (target.is_some()) {
            input.target.target = Some(String::make(*target));
            continue;
        }
        if (option == "--sysroot"_str || option == "-isysroot"_str) {
            auto value = take_value("a sysroot"_str);
            if (value.is_err()) return Err(rstd::move(value).unwrap_err());
            input.target.sysroot = Some(rstd::move(value).unwrap());
            continue;
        }
        auto sysroot = attached_value(option, "--sysroot="_str);
        if (sysroot.is_none()) sysroot = attached_value(option, "-isysroot="_str);
        if (sysroot.is_some()) {
            input.target.sysroot = Some(String::make(*sysroot));
            continue;
        }
        auto toggle = toggle_family(option);
        if (toggle.is_some()) {
            if (*toggle == "pic"_str) {
                codegen_modes.insert(String::make(*toggle), layer.options[index].clone());
            } else if (*toggle == "sized-deallocation"_str || *toggle == "char-signedness"_str ||
                       *toggle == "short-enums"_str || *toggle == "short-wchar"_str) {
                abi_modes.insert(String::make(*toggle), layer.options[index].clone());
            } else {
                language_modes.insert(String::make(*toggle), layer.options[index].clone());
            }
            continue;
        }
        if (option == "-march"_str || option == "-mcpu"_str || option == "-mtune"_str ||
            option == "-mabi"_str) {
            auto value = take_value("a value"_str);
            if (value.is_err()) return Err(rstd::move(value).unwrap_err());
            auto canonical = rstd::format("{}={}", option, value->as_str());
            target_modes.insert(target_option_family(canonical.as_str()), rstd::move(canonical));
            continue;
        }
        if (option.starts_with("-m"_str) && option != "-mllvm"_str &&
            ! option.starts_with("-mllvm="_str)) {
            target_modes.insert(target_option_family(option), layer.options[index].clone());
            continue;
        }
        if (option.starts_with("-fsanitize="_str) || option.starts_with("-fno-sanitize="_str) ||
            option.starts_with("-fsanitize-trap="_str) ||
            option.starts_with("-fno-sanitize-trap="_str) || option.starts_with("-fopenmp"_str) ||
            option.starts_with("-fno-openmp"_str) || option.starts_with("-fptrauth"_str) ||
            option.starts_with("-fno-ptrauth"_str)) {
            instrumentation.insert(layer.options[index].clone(), empty {});
            continue;
        }
        if (option.starts_with("-W"_str) || option == "-pedantic"_str ||
            option == "-pedantic-errors"_str) {
            append_unique(input.diagnostics.options, layer.options[index].clone());
            continue;
        }
        if (option.starts_with("-fms-"_str) || option.starts_with("-fno-ms-"_str)) {
            input.vendor.push(CppVendorOption {
                .value  = layer.options[index].clone(),
                .effect = CppVendorOptionEffect::Language,
            });
            continue;
        }
        if (option == "-mllvm"_str) {
            auto value = take_value("an LLVM backend argument"_str);
            if (value.is_err()) return Err(rstd::move(value).unwrap_err());
            input.vendor.push(CppVendorOption {
                .value  = rstd::format("-mllvm={}", value->as_str()),
                .effect = CppVendorOptionEffect::Codegen,
            });
            continue;
        }
        if (option.starts_with("-mllvm="_str)) {
            input.vendor.push(CppVendorOption {
                .value  = layer.options[index].clone(),
                .effect = CppVendorOptionEffect::Codegen,
            });
            continue;
        }
        input.vendor.push(CppVendorOption {
            .value  = layer.options[index].clone(),
            .effect = CppVendorOptionEffect::Unknown,
        });
    }

    input.language.modes  = family_values(rstd::move(language_modes));
    input.abi.modes       = family_values(rstd::move(abi_modes));
    input.target.features = family_values(rstd::move(target_modes));
    input.codegen.modes   = family_values(rstd::move(codegen_modes));
    input.codegen.instrumentation.clear();
    auto instrumentation_values = instrumentation.into_iter();
    while (auto value = instrumentation_values.next()) {
        auto entry = rstd::move(value).unwrap();
        input.codegen.instrumentation.push(rstd::move(entry.template get<0>()));
    }
    return Ok(rstd::move(input));
}

auto merge_cpp_options(CppCompileOptions input, const CppCompileOptions& extra)
    -> CppOptionResult<CppCompileOptions> {
    if (cpp_bmi_compatibility_identity(input).as_str() !=
        cpp_bmi_compatibility_identity(extra).as_str()) {
        return option_error("cannot merge C++ contexts with different semantic or ABI options"_str);
    }
    auto layer                = CppOptionLayer {};
    layer.include_directories = clone_paths(extra.preprocessor.include_directories);
    for (const auto& macro : extra.preprocessor.macros) {
        if (macro.action == CppMacroAction::Define) {
            layer.definitions.push(macro.value.clone());
        } else {
            auto value = String::make("-U"_str);
            value.push_str(macro.value.as_str());
            layer.options.push(rstd::move(value));
        }
    }
    for (const auto& value : extra.language.modes) layer.options.push(value.value.clone());
    for (const auto& value : extra.abi.modes) layer.options.push(value.value.clone());
    for (const auto& value : extra.target.features) layer.options.push(value.value.clone());
    for (const auto& value : extra.codegen.modes) layer.options.push(value.value.clone());
    for (const auto& value : extra.codegen.instrumentation) layer.options.push(value.clone());
    for (const auto& value : extra.diagnostics.options) layer.options.push(value.clone());
    for (const auto& value : extra.vendor) layer.options.push(value.value.clone());
    return apply_cpp_option_layer(rstd::move(input), rstd::move(layer));
}

auto cpp_compile_identity(const CppCompileOptions& options) -> String {
    auto result = String::make("tenon-cpp-compile-context-v1\n"_str);
    append_semantic_identity(result, options);
    push_identity(
        result, "optimization"_str, cpp_optimization_option(options.codegen.optimization));
    push_identity(result, "debug-info"_str, cpp_debug_option(options.codegen.debug_info));
    for (const auto& value : options.codegen.modes) {
        push_identity(result,
                      rstd::format("codegen:{}", value.family.as_str()).as_str(),
                      value.value.as_str());
    }
    for (const auto& value : options.codegen.instrumentation) {
        push_identity(result, "instrumentation"_str, value.as_str());
    }
    for (const auto& include : options.preprocessor.include_directories) {
        auto text = include.as_path().to_str();
        push_identity(result, "include"_str, text.is_some() ? *text : "<non-utf8>"_str);
    }
    for (const auto& macro : options.preprocessor.macros) {
        push_identity(result,
                      macro.action == CppMacroAction::Define ? "define"_str : "undefine"_str,
                      macro.value.as_str());
    }
    for (const auto& value : options.diagnostics.options) {
        push_identity(result, "diagnostic"_str, value.as_str());
    }
    for (const auto& value : options.vendor) {
        push_identity(result, option_effect_name(value.effect), value.value.as_str());
    }
    return result;
}

auto cpp_bmi_compatibility_identity(const CppCompileOptions& options) -> String {
    auto result = String::make("tenon-cpp-bmi-compatibility-v1\n"_str);
    append_semantic_identity(result, options);
    for (const auto& value : options.vendor) {
        if (value.effect == CppVendorOptionEffect::Language ||
            value.effect == CppVendorOptionEffect::Abi ||
            value.effect == CppVendorOptionEffect::Preprocessor ||
            value.effect == CppVendorOptionEffect::Unknown) {
            push_identity(result, option_effect_name(value.effect), value.value.as_str());
        }
    }
    return result;
}

auto cpp_public_requirements_identity(const CppPublicRequirements& requirements) -> String {
    auto result = String::make("tenon-cpp-public-requirements-v1\n"_str);
    for (const auto& include : requirements.include_directories) {
        auto text = include.as_path().to_str();
        push_identity(result, "include"_str, text.is_some() ? *text : "<non-utf8>"_str);
    }
    for (const auto& macro : requirements.macros) {
        push_identity(result,
                      macro.action == CppMacroAction::Define ? "define"_str : "undefine"_str,
                      macro.value.as_str());
    }
    return result;
}

auto cpp_public_requirements_satisfied(const CppPublicRequirements& requirements,
                                       const CppCompileOptions&     consumer) -> bool {
    for (const auto& required : requirements.include_directories) {
        auto available = false;
        for (const auto& include : consumer.preprocessor.include_directories) {
            if (include.as_path() == required.as_path()) {
                available = true;
                break;
            }
        }
        if (! available) return false;
    }

    auto required_macros = macro_states(requirements.macros);
    auto consumer_macros = macro_states(consumer.preprocessor.macros);
    auto values          = required_macros.into_iter();
    while (auto value = values.next()) {
        auto entry    = rstd::move(value).unwrap();
        auto consumer = consumer_macros.get(entry.template get<0>().as_str());
        if (consumer.is_none() || (**consumer).as_str() != entry.template get<1>().as_str()) {
            return false;
        }
    }
    return true;
}

} // namespace tenon
