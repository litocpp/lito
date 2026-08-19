module;
#include <rstd/enum.hpp>
#include <rstd/macro.hpp>

export module lito.cpp:compiler.policy;

import rstd;
import :compiler.binding;
import :compiler.parser;
import :compiler.identity;
import :compiler.option;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito::cpp
{

template<typename Key, typename Value>
using PolicyMap = rstd::collections::BTreeMap<Key, Value>;

auto option_error(ref<str> message) -> CppOptionResult<CppCompileOptions> {
    return Err(CppOptionError::Message(String::make(message)));
}

auto option_error(String message) -> CppOptionResult<CppCompileOptions> {
    return Err(CppOptionError::Message(rstd::move(message)));
}

auto append_unique(Vec<String>& output, String value) -> void {
    for (const auto& existing : output) {
        if (existing.as_str() == value.as_str()) return;
    }
    output.push(rstd::move(value));
}

auto append_unique(Vec<CppIncludeDirectory>& output, CppIncludeDirectory value) -> void {
    for (const auto& existing : output) {
        if (existing.path.as_path() == value.path.as_path() && existing.kind == value.kind) return;
    }
    output.push(rstd::move(value));
}

auto default_cpp_warnings() -> Vec<lito::compiler::CompilerWarningOption> {
    auto result = Vec<lito::compiler::CompilerWarningOption>::make();
    result.push(lito::compiler::CompilerWarningOption {
        .warning = lito::compiler::CompilerWarning::All,
    });
    result.push(lito::compiler::CompilerWarningOption {
        .warning = lito::compiler::CompilerWarning::Pedantic,
    });
    result.push(lito::compiler::CompilerWarningOption {
        .warning = lito::compiler::CompilerWarning::GnuStatementExpression,
        .enabled = false,
    });
    result.push(lito::compiler::CompilerWarningOption {
        .warning = lito::compiler::CompilerWarning::DeprecatedDeclarations,
        .enabled = false,
    });
    result.push(lito::compiler::CompilerWarningOption {
        .warning = lito::compiler::CompilerWarning::UnknownAttributes,
    });
    return result;
}

auto family_map(const Vec<CppFamilyOption>& values) -> PolicyMap<String, String> {
    auto result = PolicyMap<String, String>::make();
    for (const auto& value : values) result.insert(value.family.clone(), value.value.clone());
    return result;
}

auto family_values(PolicyMap<String, String> values) -> Vec<CppFamilyOption> {
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

} // namespace lito::cpp

export namespace lito::cpp
{

auto cpp_public_requirements(const CppCompileOptions& input) -> CppPublicRequirements {
    return CppPublicRequirements {
        .include_directories = as<Clone>(input.preprocessor.include_directories).clone(),
        .macros              = as<Clone>(input.preprocessor.macros).clone(),
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

auto apply_cpp_option_layer(CppCompileOptions input, CppOptionLayer layer)
    -> CppOptionResult<CppCompileOptions>;

auto make_cpp_options(ref<str>                       language_standard,
                      lito::config::StandardLibrary  standard_library,
                      bool                           exceptions,
                      bool                           rtti,
                      lito::compiler::CodegenOptions codegen,
                      CppOptionLayer layer = {}) -> CppOptionResult<CppCompileOptions> {
    auto result = CppCompileOptions {
        .common =
            lito::compiler::CommonCompileOptions {
                .codegen = rstd::move(codegen),
            },
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
        .codegen = CppCodegenOptions {},
        .diagnostics =
            lito::compiler::DiagnosticOptions {
                .warnings = default_cpp_warnings(),
            },
    };
    return apply_cpp_option_layer(rstd::move(result), rstd::move(layer));
}

auto make_cpp_options(ref<str>                      language_standard,
                      lito::config::StandardLibrary standard_library,
                      bool                          exceptions,
                      bool                          rtti,
                      lito::manifest::Optimization  optimization,
                      lito::manifest::DebugInfo     debug_info,
                      CppOptionLayer layer = {}) -> CppOptionResult<CppCompileOptions> {
    return make_cpp_options(language_standard,
                            standard_library,
                            exceptions,
                            rtti,
                            lito::compiler::CodegenOptions {
                                .optimization = Some(optimization),
                                .debug_info   = Some(debug_info),
                                .lto          = Some(lito::manifest::Lto::Off),
                            },
                            rstd::move(layer));
}

auto apply_cpp_option_layer(CppCompileOptions input, CppOptionLayer layer)
    -> CppOptionResult<CppCompileOptions> {
    for (auto& include : layer.include_directories) {
        append_unique(input.preprocessor.include_directories,
                      CppIncludeDirectory {
                          .path = rstd::move(include),
                          .kind = CppIncludeDirectoryKind::User,
                      });
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
    auto instrumentation = PolicyMap<String, empty>::make();
    for (const auto& value : input.codegen.instrumentation) {
        instrumentation.insert(value.clone(), empty {});
    }

    for (auto& occurrence : layer.arguments.occurrences) {
        auto delta = rstd_try(rstd::try_from<CppOptionDelta>(rstd::move(occurrence)));
        RSTD_MATCH(rstd::move(delta.argument)) {
            RSTD_CASE(Macro, directive) {
                input.preprocessor.macros.push(rstd::move(directive));
            }
            RSTD_CASE(IncludeDirectory, directory) {
                append_unique(input.preprocessor.include_directories, rstd::move(directory));
            }
            RSTD_CASE(Common, argument) {
                lito::compiler::apply_common_compiler_argument(
                    input.common, input.diagnostics, rstd::move(argument));
            }
            RSTD_CASE(CodegenSetting, setting) {
                static_cast<void>(setting);
                return option_error("invalid prevalidated Lito-owned codegen option"_str);
            }
            RSTD_CASE(OwnedSetting, setting) {
                static_cast<void>(setting);
                return option_error("invalid prevalidated Lito-owned compiler option"_str);
            }
            RSTD_CASE(Family, domain, family, value) {
                auto* modes = rstd::addressof(language_modes);
                switch (domain) {
                case CppOptionFamilyDomain::Language: break;
                case CppOptionFamilyDomain::Abi: modes = rstd::addressof(abi_modes); break;
                case CppOptionFamilyDomain::Target: modes = rstd::addressof(target_modes); break;
                case CppOptionFamilyDomain::Codegen: modes = rstd::addressof(codegen_modes); break;
                }
                modes->insert(rstd::move(family), rstd::move(value));
            }
            RSTD_CASE(Instrumentation, value) {
                instrumentation.insert(rstd::move(value), empty {});
            }
            RSTD_CASE(SymbolVisibility, value) {
                input.codegen.visibility.symbols = value;
            }
            RSTD_CASE(TypeVisibility, value) {
                input.codegen.visibility.types = Some(value);
            }
            RSTD_CASE(InlineVisibilityHidden, enabled) {
                input.codegen.visibility.inlines_hidden = enabled;
            }
            RSTD_CASE(SizedDeallocation, value) {
                input.language.sized_deallocation = value;
            }
            RSTD_CASE(Diagnostic, value) {
                append_unique(input.diagnostics.options, rstd::move(value));
            }
            RSTD_CASE(Vendor, option) {
                input.vendor.push(rstd::move(option));
            }
        }
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
    auto layer = CppOptionLayer {};
    for (const auto& include : extra.preprocessor.include_directories) {
        layer.arguments.occurrences.push(CppCompilerArgumentOccurrence {
            .argument   = CppCompilerArgument::IncludeDirectory(as<Clone>(include).clone()),
            .raw_tokens = Vec<String>::make(),
            .source     = String::make("merged C++ context"_str),
        });
    }
    for (const auto& macro : extra.preprocessor.macros) {
        if (macro.action == CppMacroAction::Define) {
            layer.definitions.push(macro.value.clone());
        } else {
            layer.arguments.occurrences.push(CppCompilerArgumentOccurrence {
                .argument = CppCompilerArgument::Macro(CppMacroDirective {
                    .action = CppMacroAction::Undefine,
                    .value  = macro.value.clone(),
                }),
            });
        }
    }
    for (const auto& value : extra.language.modes) {
        layer.arguments.occurrences.push(CppCompilerArgumentOccurrence {
            .argument = CppCompilerArgument::Family(
                CppOptionFamilyDomain::Language, value.family.clone(), value.value.clone()),
        });
    }
    for (const auto& value : extra.abi.modes) {
        layer.arguments.occurrences.push(CppCompilerArgumentOccurrence {
            .argument = CppCompilerArgument::Family(
                CppOptionFamilyDomain::Abi, value.family.clone(), value.value.clone()),
        });
    }
    if (lito::compiler::uses_posix_threads(extra.common)) {
        layer.arguments.occurrences.push(CppCompilerArgumentOccurrence {
            .argument =
                CppCompilerArgument::Common(lito::compiler::CommonCompilerArgument::Threading(
                    lito::compiler::ThreadingModel::Posix)),
        });
    }
    for (const auto& value : extra.target.features) {
        layer.arguments.occurrences.push(CppCompilerArgumentOccurrence {
            .argument = CppCompilerArgument::Family(
                CppOptionFamilyDomain::Target, value.family.clone(), value.value.clone()),
        });
    }
    for (const auto& value : extra.codegen.modes) {
        layer.arguments.occurrences.push(CppCompilerArgumentOccurrence {
            .argument = CppCompilerArgument::Family(
                CppOptionFamilyDomain::Codegen, value.family.clone(), value.value.clone()),
        });
    }
    for (const auto& value : extra.codegen.instrumentation) {
        layer.arguments.occurrences.push(CppCompilerArgumentOccurrence {
            .argument = CppCompilerArgument::Instrumentation(value.clone()),
        });
    }
    layer.arguments.occurrences.push(CppCompilerArgumentOccurrence {
        .argument = CppCompilerArgument::Common(
            lito::compiler::CommonCompilerArgument::PositionIndependentCode(
                extra.common.codegen.position_independent_code)),
    });
    layer.arguments.occurrences.push(CppCompilerArgumentOccurrence {
        .argument = CppCompilerArgument::SymbolVisibility(extra.codegen.visibility.symbols),
    });
    if (extra.codegen.visibility.types.is_some()) {
        layer.arguments.occurrences.push(CppCompilerArgumentOccurrence {
            .argument = CppCompilerArgument::TypeVisibility(
                CppSymbolVisibility(*extra.codegen.visibility.types)),
        });
    } else {
        input.codegen.visibility.types = None();
    }
    layer.arguments.occurrences.push(CppCompilerArgumentOccurrence {
        .argument =
            CppCompilerArgument::InlineVisibilityHidden(extra.codegen.visibility.inlines_hidden),
    });
    for (const auto& value : extra.diagnostics.warnings) {
        layer.arguments.occurrences.push(CppCompilerArgumentOccurrence {
            .argument =
                CppCompilerArgument::Common(lito::compiler::CommonCompilerArgument::Warning(value)),
        });
    }
    for (const auto& value : extra.diagnostics.options) {
        layer.arguments.occurrences.push(CppCompilerArgumentOccurrence {
            .argument = CppCompilerArgument::Diagnostic(value.clone()),
        });
    }
    for (const auto& value : extra.vendor) {
        layer.arguments.occurrences.push(CppCompilerArgumentOccurrence {
            .argument = CppCompilerArgument::Vendor(as<Clone>(value).clone()),
        });
    }
    return apply_cpp_option_layer(rstd::move(input), rstd::move(layer));
}

} // namespace lito::cpp
