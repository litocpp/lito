export module lito.cpp:compiler.identity;

import rstd;
import :compiler.option;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito::cpp
{

template<typename Key, typename Value>
using IdentityMap = rstd::collections::BTreeMap<Key, Value>;

auto macro_name(ref<str> value) -> ref<str> {
    auto separator = value.find("="_str);
    return separator.is_some() ? value.split_at(*separator).get<0>() : value;
}

auto macro_states(const Vec<CppMacroDirective>& values) -> IdentityMap<String, String> {
    auto result = IdentityMap<String, String>::make();
    for (const auto& value : values) {
        auto state =
            String::make(value.action == CppMacroAction::Define ? "define:"_str : "undefine:"_str);
        state.push_str(value.value.as_str());
        result.insert(String::make(macro_name(value.value.as_str())), rstd::move(state));
    }
    return result;
}

auto push_identity(String& output, ref<str> key, ref<str> value) -> void;

auto standard_library_mode_macro(ref<str> name) -> bool {
    return name == "_GLIBCXX_USE_CXX11_ABI"_str || name == "_GLIBCXX_DEBUG"_str ||
           name == "_GLIBCXX_ASSERTIONS"_str || name == "_GLIBCXX_PARALLEL"_str ||
           name == "_LIBCPP_HARDENING_MODE"_str || name == "_LIBCPP_ASSERTION_SEMANTIC"_str;
}

auto standard_library_abi_macro(ref<str> name) -> bool {
    return name == "_GLIBCXX_USE_CXX11_ABI"_str || name == "_GLIBCXX_DEBUG"_str ||
           name == "_GLIBCXX_PARALLEL"_str;
}

auto standard_library_macro_identity(const CppCompileOptions& options, bool abi_only) -> String {
    auto states = macro_states(options.preprocessor.macros);
    auto result = String::make();
    auto values = states.into_iter();
    while (auto value = values.next()) {
        auto entry = rstd::move(value).unwrap();
        auto name  = entry.template get<0>().as_str();
        if ((abi_only && ! standard_library_abi_macro(name)) ||
            (! abi_only && ! standard_library_mode_macro(name))) {
            continue;
        }
        push_identity(result, name, entry.template get<1>().as_str());
    }
    return result;
}

auto family_options_identity(ref<str> prefix, const Vec<CppFamilyOption>& values) -> String {
    auto result = String::make();
    for (const auto& value : values) {
        push_identity(result,
                      rstd::format("{}:{}", prefix, value.family.as_str()).as_str(),
                      value.value.as_str());
    }
    return result;
}

auto string_options_identity(ref<str> key, const Vec<String>& values) -> String {
    auto result = String::make();
    for (const auto& value : values) push_identity(result, key, value.as_str());
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
                  "sized-deallocation"_str,
                  cpp_sized_deallocation_name(options.language.sized_deallocation));
    push_identity(output,
                  "stdlib"_str,
                  options.abi.standard_library == lito::config::StandardLibrary::Libstdcxx
                      ? "libstdc++"_str
                      : "libc++"_str);
    if (options.abi.resolved_standard_library.is_some()) {
        push_identity(output,
                      "resolved-stdlib"_str,
                      options.abi.resolved_standard_library->headers_identity.as_str());
    }
    push_identity(output, "posix-threads"_str, lito::compiler::uses_posix_threads(options.common));
    for (const auto& value : options.language.modes) {
        push_identity(output,
                      rstd::format("language:{}", value.family.as_str()).as_str(),
                      value.value.as_str());
    }
    for (const auto& value : options.abi.modes) {
        push_identity(
            output, rstd::format("abi:{}", value.family.as_str()).as_str(), value.value.as_str());
    }
    if (options.common.target.target.is_some()) {
        push_identity(output, "target"_str, options.common.target.target->as_str());
    }
    if (options.common.target.sysroot.is_some()) {
        push_identity(output, "sysroot"_str, options.common.target.sysroot->as_str());
    }
    for (const auto& value : options.target.features) {
        push_identity(output,
                      rstd::format("target:{}", value.family.as_str()).as_str(),
                      value.value.as_str());
    }
}

} // namespace lito::cpp

export namespace lito::cpp
{

enum class CppAbiCompatibilityField
{
    StandardLibrary,
    StandardLibraryHeaders,
    StandardLibraryModes,
    AbiModes,
    Target,
    TargetFeatures,
    Instrumentation,
};

struct CppAbiCompatibilityDifference {
    CppAbiCompatibilityField field { CppAbiCompatibilityField::StandardLibrary };
    String                   provider;
    String                   consumer;
};

constexpr auto cpp_abi_compatibility_field_name(CppAbiCompatibilityField field) -> ref<str> {
    switch (field) {
    case CppAbiCompatibilityField::StandardLibrary: return "standard library"_str;
    case CppAbiCompatibilityField::StandardLibraryHeaders:
        return "resolved standard library headers"_str;
    case CppAbiCompatibilityField::StandardLibraryModes: return "standard library ABI modes"_str;
    case CppAbiCompatibilityField::AbiModes: return "C++ ABI modes"_str;
    case CppAbiCompatibilityField::Target: return "target"_str;
    case CppAbiCompatibilityField::TargetFeatures: return "target ABI features"_str;
    case CppAbiCompatibilityField::Instrumentation: return "instrumentation runtime"_str;
    }
    return "C++ ABI"_str;
}

auto is_cpp_standard_library_mode_macro(ref<str> definition) -> bool {
    return standard_library_mode_macro(macro_name(definition));
}

auto cpp_standard_library_modes_identity(const CppCompileOptions& options) -> String {
    auto result = String::make("lito-cpp-stdlib-modes-v1\n"_str);
    result.push_str(standard_library_macro_identity(options, false).as_str());
    return result;
}

auto cpp_abi_compatibility_identity(const CppCompileOptions& options) -> String {
    auto result = String::make("lito-cpp-abi-compatibility-v1\n"_str);
    push_identity(result,
                  "stdlib"_str,
                  options.abi.standard_library == lito::config::StandardLibrary::Libstdcxx
                      ? "libstdc++"_str
                      : "libc++"_str);
    if (options.abi.resolved_standard_library.is_some()) {
        push_identity(result,
                      "resolved-stdlib"_str,
                      options.abi.resolved_standard_library->headers_identity.as_str());
    }
    for (const auto& value : options.abi.modes) {
        push_identity(
            result, rstd::format("abi:{}", value.family.as_str()).as_str(), value.value.as_str());
    }
    if (options.common.target.target.is_some()) {
        push_identity(result, "target"_str, options.common.target.target->as_str());
    }
    result.push_str(family_options_identity("target"_str, options.target.features).as_str());
    result.push_str(
        string_options_identity("instrumentation"_str, options.codegen.instrumentation).as_str());
    result.push_str(standard_library_macro_identity(options, true).as_str());
    return result;
}

auto check_cpp_abi_compatibility(const CppCompileOptions& provider,
                                 const CppCompileOptions& consumer)
    -> Option<CppAbiCompatibilityDifference> {
    const auto difference = [](CppAbiCompatibilityField field,
                               ref<str>                 provider_value,
                               ref<str> consumer_value) -> Option<CppAbiCompatibilityDifference> {
        if (provider_value == consumer_value) return None();
        return Some(CppAbiCompatibilityDifference {
            .field    = field,
            .provider = String::make(provider_value),
            .consumer = String::make(consumer_value),
        });
    };

    auto found = difference(CppAbiCompatibilityField::StandardLibrary,
                            lito::config::standard_library_name(provider.abi.standard_library),
                            lito::config::standard_library_name(consumer.abi.standard_library));
    if (found.is_some()) return found;
    auto provider_headers = provider.abi.resolved_standard_library.is_some()
                                ? provider.abi.resolved_standard_library->headers_identity.as_str()
                                : "<unresolved>"_str;
    auto consumer_headers = consumer.abi.resolved_standard_library.is_some()
                                ? consumer.abi.resolved_standard_library->headers_identity.as_str()
                                : "<unresolved>"_str;
    found                 = difference(
        CppAbiCompatibilityField::StandardLibraryHeaders, provider_headers, consumer_headers);
    if (found.is_some()) return found;
    auto provider_stdlib_modes = standard_library_macro_identity(provider, true);
    auto consumer_stdlib_modes = standard_library_macro_identity(consumer, true);
    found                      = difference(CppAbiCompatibilityField::StandardLibraryModes,
                                            provider_stdlib_modes.as_str(),
                                            consumer_stdlib_modes.as_str());
    if (found.is_some()) return found;
    auto provider_abi = family_options_identity("abi"_str, provider.abi.modes);
    auto consumer_abi = family_options_identity("abi"_str, consumer.abi.modes);
    found             = difference(
        CppAbiCompatibilityField::AbiModes, provider_abi.as_str(), consumer_abi.as_str());
    if (found.is_some()) return found;
    auto provider_target = provider.common.target.target.is_some()
                               ? provider.common.target.target->as_str()
                               : "<compiler-default>"_str;
    auto consumer_target = consumer.common.target.target.is_some()
                               ? consumer.common.target.target->as_str()
                               : "<compiler-default>"_str;
    found = difference(CppAbiCompatibilityField::Target, provider_target, consumer_target);
    if (found.is_some()) return found;
    auto provider_features = family_options_identity("target"_str, provider.target.features);
    auto consumer_features = family_options_identity("target"_str, consumer.target.features);
    found                  = difference(CppAbiCompatibilityField::TargetFeatures,
                                        provider_features.as_str(),
                                        consumer_features.as_str());
    if (found.is_some()) return found;
    auto provider_instrumentation =
        string_options_identity("instrumentation"_str, provider.codegen.instrumentation);
    auto consumer_instrumentation =
        string_options_identity("instrumentation"_str, consumer.codegen.instrumentation);
    return difference(CppAbiCompatibilityField::Instrumentation,
                      provider_instrumentation.as_str(),
                      consumer_instrumentation.as_str());
}

auto cpp_compile_identity(const CppCompileOptions& options) -> String {
    auto result = String::make("lito-cpp-compile-context-v4\n"_str);
    append_semantic_identity(result, options);
    push_identity(
        result, "optimization"_str, cpp_optimization_option(options.common.codegen.optimization));
    push_identity(result, "debug-info"_str, cpp_debug_option(options.common.codegen.debug_info));
    push_identity(result, "lto"_str, cpp_lto_option(options.common.codegen.lto));
    push_identity(
        result, "position-independent-code"_str, options.common.codegen.position_independent_code);
    push_identity(result,
                  "symbol-visibility"_str,
                  cpp_symbol_visibility_name(options.codegen.visibility.symbols));
    push_identity(result,
                  "type-visibility"_str,
                  options.codegen.visibility.types.is_some()
                      ? cpp_symbol_visibility_name(*options.codegen.visibility.types)
                      : "inherit"_str);
    push_identity(
        result, "inline-visibility-hidden"_str, options.codegen.visibility.inlines_hidden);
    for (const auto& value : options.codegen.modes) {
        push_identity(result,
                      rstd::format("codegen:{}", value.family.as_str()).as_str(),
                      value.value.as_str());
    }
    for (const auto& value : options.codegen.instrumentation) {
        push_identity(result, "instrumentation"_str, value.as_str());
    }
    for (const auto& value : options.diagnostics.warnings) {
        push_identity(result,
                      rstd::format("warning:{}", cpp_warning_name(value.warning)).as_str(),
                      value.enabled ? "enabled"_str : "disabled"_str);
    }
    for (const auto& include : options.preprocessor.include_directories) {
        auto text = include.path.as_path().to_str();
        push_identity(result,
                      include.kind == CppIncludeDirectoryKind::System ? "system-include"_str
                                                                      : "include"_str,
                      text.is_some() ? *text : "<non-utf8>"_str);
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

auto cpp_scan_identity(const CppCompileOptions& options) -> String {
    auto result = String::make("lito-cpp-scan-context-v3\n"_str);
    append_semantic_identity(result, options);
    push_identity(
        result, "optimization"_str, cpp_optimization_option(options.common.codegen.optimization));
    push_identity(
        result, "position-independent-code"_str, options.common.codegen.position_independent_code);
    for (const auto& value : options.codegen.modes) {
        push_identity(result,
                      rstd::format("codegen:{}", value.family.as_str()).as_str(),
                      value.value.as_str());
    }
    for (const auto& value : options.codegen.instrumentation) {
        push_identity(result, "instrumentation"_str, value.as_str());
    }
    for (const auto& include : options.preprocessor.include_directories) {
        auto text = include.path.as_path().to_str();
        push_identity(result,
                      include.kind == CppIncludeDirectoryKind::System ? "system-include"_str
                                                                      : "include"_str,
                      text.is_some() ? *text : "<non-utf8>"_str);
    }
    for (const auto& macro : options.preprocessor.macros) {
        push_identity(result,
                      macro.action == CppMacroAction::Define ? "define"_str : "undefine"_str,
                      macro.value.as_str());
    }
    for (const auto& value : options.vendor) {
        if (value.effect == CppVendorOptionEffect::Codegen ||
            value.effect == CppVendorOptionEffect::Diagnostic) {
            continue;
        }
        push_identity(result, option_effect_name(value.effect), value.value.as_str());
    }
    return result;
}

auto cpp_bmi_compatibility_identity(const CppCompileOptions& options) -> String {
    auto result = String::make("lito-cpp-bmi-compatibility-v2\n"_str);
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
    auto result = String::make("lito-cpp-public-requirements-v2\n"_str);
    for (const auto& include : requirements.include_directories) {
        auto text = include.path.as_path().to_str();
        push_identity(result,
                      include.kind == CppIncludeDirectoryKind::System ? "system-include"_str
                                                                      : "include"_str,
                      text.is_some() ? *text : "<non-utf8>"_str);
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
            if (include.path.as_path() == required.path.as_path() &&
                include.kind == required.kind) {
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
        auto entry = rstd::move(value).unwrap();
        if (standard_library_mode_macro(entry.template get<0>().as_str())) continue;
        auto consumer = consumer_macros.get(entry.template get<0>().as_str());
        if (consumer.is_none() || (**consumer).as_str() != entry.template get<1>().as_str()) {
            return false;
        }
    }
    return true;
}

} // namespace lito::cpp
