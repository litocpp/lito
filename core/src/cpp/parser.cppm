module;
#include <rstd/enum.hpp>
#include <rstd/macro.hpp>

export module lito.cpp:parser;

import rstd;
import lito.compiler.arguments;
import :model;
import :arguments;

using namespace rstd::prelude;
using namespace rstd::literals;
using Clone = rstd::clone::Clone;

export namespace lito
{

auto CppArgumentSchema::make() -> CppArgumentSchema {
    auto result      = CppArgumentSchema {};
    result.schema_   = CompilerArgumentSchema::make();
    result.bindings_ = Vec<CppCompilerArgumentBinding>::make();
    return result;
}

auto CppArgumentSchema::add(CppCompilerArgumentKind    kind,
                            CompilerArgumentDefinition definition,
                            ref<str>                   family) -> void {
    schema_.add(rstd::move(definition));
    bindings_.push(CppCompilerArgumentBinding {
        .kind   = kind,
        .family = String::make(family),
    });
}

auto CppArgumentSchema::add_typed(CppCompilerArgument        argument,
                                  CompilerArgumentDefinition definition) -> void {
    schema_.add(rstd::move(definition));
    bindings_.push(CppCompilerArgumentBinding {
        .typed = Some(rstd::move(argument)),
    });
}

auto CppArgumentSchema::build() && -> CppOptionResult<CppArgumentParser> {
    auto parser = rstd_try(rstd::move(schema_).build());
    return Ok(CppArgumentParser(rstd::move(parser), rstd::move(bindings_)));
}

auto CppArgumentParser::parse(const Vec<String>& arguments, ref<str> source) const
    -> CppOptionResult<CppArgumentLayer> {
    auto parsed = rstd_try(parser_.parse(arguments));
    auto result = CppArgumentLayer {};
    for (auto& matched : parsed) {
        auto binding = matched.definition.is_some()
                           ? rstd::addressof(bindings_[*matched.definition])
                           : static_cast<const CppCompilerArgumentBinding*>(nullptr);
        result.occurrences.push(CppCompilerArgumentOccurrence {
            .argument   = make_cpp_compiler_argument(matched, binding),
            .raw_tokens = rstd::move(matched.raw_tokens),
            .range      = matched.range,
            .source     = String::make(source),
        });
    }
    return Ok(rstd::move(result));
}

auto explicit_cpp_target(const CppArgumentLayer& arguments) -> Option<ref<str>> {
    auto result = Option<ref<str>> {};
    for (const auto& occurrence : arguments.occurrences) {
        if (occurrence.argument.is_Target()) {
            result = Some(occurrence.argument.as_Target().value.as_str());
        }
    }
    return result;
}

auto CppMacroDirective::clone() const -> CppMacroDirective {
    return CppMacroDirective {
        .action = action,
        .value  = value.clone(),
    };
}

auto CppIncludeDirectory::clone() const -> CppIncludeDirectory {
    return CppIncludeDirectory {
        .path = path.clone(),
        .kind = kind,
    };
}

auto CppFamilyOption::clone() const -> CppFamilyOption {
    return CppFamilyOption {
        .family = family.clone(),
        .value  = value.clone(),
    };
}

auto CppVendorOption::clone() const -> CppVendorOption {
    return CppVendorOption {
        .value                           = value.clone(),
        .raw_tokens                      = as<Clone>(raw_tokens).clone(),
        .effect                          = effect,
        .native_preprocessor_unsupported = native_preprocessor_unsupported,
        .preserve_raw_tokens             = preserve_raw_tokens,
    };
}

auto CppCompilerArgument::clone() const -> CppCompilerArgument {
    RSTD_MATCH(*this) {
        RSTD_CASE(Macro, directive) {
            return CppCompilerArgument::Macro(CppMacroDirective {
                .action = directive.action,
                .value  = directive.value.clone(),
            });
        }
        RSTD_CASE(IncludeDirectory, directory) {
            return CppCompilerArgument::IncludeDirectory(as<Clone>(directory).clone());
        }
        RSTD_CASE(Target, value) {
            return CppCompilerArgument::Target(value.clone());
        }
        RSTD_CASE(Sysroot, value) {
            return CppCompilerArgument::Sysroot(value.clone());
        }
        RSTD_CASE(OwnedSetting, setting) {
            return CppCompilerArgument::OwnedSetting(setting);
        }
        RSTD_CASE(Family, domain, family, value) {
            return CppCompilerArgument::Family(domain, family.clone(), value.clone());
        }
        RSTD_CASE(Instrumentation, value) {
            return CppCompilerArgument::Instrumentation(value.clone());
        }
        RSTD_CASE(PositionIndependentCode, enabled) {
            return CppCompilerArgument::PositionIndependentCode(enabled);
        }
        RSTD_CASE(SizedDeallocation, value) {
            return CppCompilerArgument::SizedDeallocation(value);
        }
        RSTD_CASE(Warning, option) {
            return CppCompilerArgument::Warning(option);
        }
        RSTD_CASE(Diagnostic, value) {
            return CppCompilerArgument::Diagnostic(value.clone());
        }
        RSTD_CASE(Vendor, option) {
            return CppCompilerArgument::Vendor(as<Clone>(option).clone());
        }
    }
    rstd::unreachable();
}

auto CppCompilerArgumentOccurrence::clone() const -> CppCompilerArgumentOccurrence {
    return CppCompilerArgumentOccurrence {
        .argument   = as<Clone>(argument).clone(),
        .raw_tokens = as<Clone>(raw_tokens).clone(),
        .range      = range,
        .source     = source.clone(),
    };
}

auto CppArgumentLayer::clone() const -> CppArgumentLayer {
    return CppArgumentLayer {
        .occurrences = as<Clone>(occurrences).clone(),
    };
}

auto CppCompileOptions::clone() const -> CppCompileOptions {
    const auto& input  = *this;
    auto        result = CppCompileOptions {
        .language =
            CppLanguageOptions {
                .standard           = input.language.standard.clone(),
                .exceptions         = input.language.exceptions,
                .rtti               = input.language.rtti,
                .sized_deallocation = input.language.sized_deallocation,
                .modes              = as<Clone>(input.language.modes).clone(),
            },
        .abi =
            CppAbiOptions {
                .standard_library = input.abi.standard_library,
                .modes            = as<Clone>(input.abi.modes).clone(),
            },
        .preprocessor =
            CppPreprocessorOptions {
                .include_directories = as<Clone>(input.preprocessor.include_directories).clone(),
                .macros              = as<Clone>(input.preprocessor.macros).clone(),
            },
        .codegen =
            CppCodegenOptions {
                .optimization              = input.codegen.optimization,
                .debug_info                = input.codegen.debug_info,
                .lto                       = input.codegen.lto,
                .position_independent_code = input.codegen.position_independent_code,
                .modes                     = as<Clone>(input.codegen.modes).clone(),
                .instrumentation           = as<Clone>(input.codegen.instrumentation).clone(),
            },
        .diagnostics =
            CppDiagnosticOptions {
                .warnings = input.diagnostics.warnings.clone(),
                .options  = as<Clone>(input.diagnostics.options).clone(),
            },
        .vendor = as<Clone>(input.vendor).clone(),
    };
    if (input.target.target.is_some()) result.target.target = Some(input.target.target->clone());
    if (input.target.sysroot.is_some()) {
        result.target.sysroot = Some(input.target.sysroot->clone());
    }
    result.target.features = as<Clone>(input.target.features).clone();
    return result;
}

} // namespace lito
