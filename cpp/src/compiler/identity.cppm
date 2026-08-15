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

} // namespace lito::cpp

export namespace lito::cpp
{

auto cpp_compile_identity(const CppCompileOptions& options) -> String {
    auto result = String::make("lito-cpp-compile-context-v1\n"_str);
    append_semantic_identity(result, options);
    push_identity(
        result, "optimization"_str, cpp_optimization_option(options.codegen.optimization));
    push_identity(result, "debug-info"_str, cpp_debug_option(options.codegen.debug_info));
    push_identity(result, "lto"_str, cpp_lto_option(options.codegen.lto));
    push_identity(
        result, "position-independent-code"_str, options.codegen.position_independent_code);
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
    auto result = String::make("lito-cpp-scan-context-v1\n"_str);
    append_semantic_identity(result, options);
    push_identity(
        result, "optimization"_str, cpp_optimization_option(options.codegen.optimization));
    push_identity(
        result, "position-independent-code"_str, options.codegen.position_independent_code);
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
    auto result = String::make("lito-cpp-bmi-compatibility-v1\n"_str);
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
    auto result = String::make("lito-cpp-public-requirements-v1\n"_str);
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
        auto entry    = rstd::move(value).unwrap();
        auto consumer = consumer_macros.get(entry.template get<0>().as_str());
        if (consumer.is_none() || (**consumer).as_str() != entry.template get<1>().as_str()) {
            return false;
        }
    }
    return true;
}

} // namespace lito::cpp
