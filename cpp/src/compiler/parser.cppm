module;
#include <rstd/enum.hpp>
#include <rstd/macro.hpp>

export module lito.cpp:compiler.parser;

import rstd;
import :compiler.argument;
import :compiler.binding;
import :compiler.option;
import :c.compiler;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::cpp
{

class CppArgumentParser {
public:
    auto parse(const Vec<String>& arguments, ref<str> source) const
        -> CppOptionResult<CppArgumentLayer>;

    auto parse_c(const Vec<String>& arguments, ref<str> source) const
        -> CompilerOptionResult<lito::c::CArgumentLayer>;

private:
    friend class CppArgumentSchema;

    CppArgumentParser(CompilerArgumentParser parser, Vec<CppCompilerArgumentBinding> bindings)
        : parser_(rstd::move(parser)), bindings_(rstd::move(bindings)) {}

    CompilerArgumentParser          parser_;
    Vec<CppCompilerArgumentBinding> bindings_;
};

class CppArgumentSchema {
public:
    static auto make() -> CppArgumentSchema;

    auto add(CppCompilerArgumentKind    kind,
             CompilerArgumentDefinition definition,
             ref<str>                   family = {}) -> void;

    auto add_typed(CppCompilerArgument argument, CompilerArgumentDefinition definition) -> void;

    auto build() && -> CppOptionResult<CppArgumentParser>;

private:
    CompilerArgumentSchema          schema_;
    Vec<CppCompilerArgumentBinding> bindings_;
};

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
    auto parsed_result = parser_.parse(arguments);
    if (parsed_result.is_err()) {
        return Err(
            CppOptionError::Argument(rstd::move(parsed_result).unwrap_err(), String::make(source)));
    }
    auto parsed = rstd::move(parsed_result).unwrap();
    auto result = CppArgumentLayer {};
    for (auto& matched : parsed) {
        auto binding  = matched.definition.is_some()
                            ? rstd::addressof(bindings_[*matched.definition])
                            : static_cast<const CppCompilerArgumentBinding*>(nullptr);
        auto argument = rstd_try(make_cpp_compiler_argument(matched, binding, source));
        result.occurrences.push(CppCompilerArgumentOccurrence {
            .argument   = rstd::move(argument),
            .raw_tokens = rstd::move(matched.raw_tokens),
            .range      = matched.range,
            .source     = String::make(source),
        });
    }
    return Ok(rstd::move(result));
}

auto CppArgumentParser::parse_c(const Vec<String>& arguments, ref<str> source) const
    -> CompilerOptionResult<lito::c::CArgumentLayer> {
    auto parsed_result = parser_.parse(arguments);
    if (parsed_result.is_err()) {
        return Err(CompilerOptionError::Argument(rstd::move(parsed_result).unwrap_err(),
                                                 String::make(source)));
    }
    auto parsed = rstd::move(parsed_result).unwrap();
    auto result = lito::c::CArgumentLayer {};
    for (auto& matched : parsed) {
        auto binding  = matched.definition.is_some()
                            ? rstd::addressof(bindings_[*matched.definition])
                            : static_cast<const CppCompilerArgumentBinding*>(nullptr);
        auto argument = rstd_try(make_c_compiler_argument(matched, binding, source));
        result.occurrences.push(lito::c::CCompilerArgumentOccurrence {
            .argument   = rstd::move(argument),
            .raw_tokens = rstd::move(matched.raw_tokens),
            .range      = matched.range,
            .source     = String::make(source),
        });
    }
    return Ok(rstd::move(result));
}

struct ExplicitTargetOption {
    ref<str> value;
    ref<str> source;
};

struct ExplicitTargetOptions {
    Option<ExplicitTargetOption> target;
    Option<ExplicitTargetOption> sysroot;
};

auto explicit_cpp_target_options(const CppArgumentLayer& arguments) -> ExplicitTargetOptions {
    auto result = ExplicitTargetOptions {};
    for (const auto& occurrence : arguments.occurrences) {
        if (! occurrence.argument.is_Common()) continue;
        const auto& common = occurrence.argument.as_Common().argument;
        if (common.is_Target()) {
            result.target = Some(ExplicitTargetOption {
                .value  = common.as_Target().value.as_str(),
                .source = occurrence.source.as_str(),
            });
        } else if (common.is_Sysroot()) {
            result.sysroot = Some(ExplicitTargetOption {
                .value  = common.as_Sysroot().value.as_str(),
                .source = occurrence.source.as_str(),
            });
        }
    }
    return result;
}

auto explicit_cpp_target(const CppArgumentLayer& arguments) -> Option<ref<str>> {
    auto options = explicit_cpp_target_options(arguments);
    return options.target.is_some() ? Some(options.target->value) : Option<ref<str>> {};
}

auto explicit_c_target_options(const lito::c::CArgumentLayer& arguments) -> ExplicitTargetOptions {
    auto result = ExplicitTargetOptions {};
    for (const auto& occurrence : arguments.occurrences) {
        if (! occurrence.argument.is_Common()) continue;
        const auto& common = occurrence.argument.as_Common().argument;
        if (common.is_Target()) {
            result.target = Some(ExplicitTargetOption {
                .value  = common.as_Target().value.as_str(),
                .source = occurrence.source.as_str(),
            });
        } else if (common.is_Sysroot()) {
            result.sysroot = Some(ExplicitTargetOption {
                .value  = common.as_Sysroot().value.as_str(),
                .source = occurrence.source.as_str(),
            });
        }
    }
    return result;
}

auto explicit_c_target(const lito::c::CArgumentLayer& arguments) -> Option<ExplicitTargetOption> {
    return explicit_c_target_options(arguments).target;
}

} // namespace lito::cpp
