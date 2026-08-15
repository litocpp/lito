module;
#include <rstd/enum.hpp>
#include <rstd/macro.hpp>

export module lito.cpp:compiler.parser;

import rstd;
import :compiler.argument;
import :compiler.binding;
import :compiler.option;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::cpp
{

class CppArgumentParser {
public:
    auto parse(const Vec<String>& arguments, ref<str> source) const
        -> CppOptionResult<CppArgumentLayer>;

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

} // namespace lito::cpp
