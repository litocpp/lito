export module lito.cpp:identity;

import rstd;
import :model;
import :arguments;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
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

} // namespace lito
