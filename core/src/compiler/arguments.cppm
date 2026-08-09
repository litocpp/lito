module;
#include <rstd/enum.hpp>

export module lito.compiler.arguments;

import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

enum class CompilerArgumentValueForm
{
    None,
    Separate,
    Joined,
    Equals,
    SeparateOrJoined,
    SeparateOrEquals,
    OptionalJoined,
    OptionalEquals,
};

struct CompilerArgumentSourceRange {
    usize begin {};
    usize end {};
};

class CompilerArgumentError {
    RSTD_ENUM(CompilerArgumentError,
              (InvalidDefinition, (String name; String reason;)),
              (DuplicateSpelling, (String spelling;)),
              (MissingValue, (usize index; String spelling;)),
              (EmptyValue, (usize index; String spelling;)))
};

struct CompilerArgumentSpelling {
    String                    value;
    CompilerArgumentValueForm form { CompilerArgumentValueForm::None };
};

struct CompilerArgumentDefinition {
    String                        name;
    Vec<CompilerArgumentSpelling> spellings;
};

struct CompilerArgumentMatch {
    Option<usize>               definition;
    Option<String>              value;
    String                      spelling;
    Vec<String>                 raw_tokens;
    CompilerArgumentSourceRange range;
};

template<typename T>
using CompilerArgumentResult = Result<T, CompilerArgumentError>;

class CompilerArgumentParser;

class CompilerArgumentSchema {
public:
    static auto make() -> CompilerArgumentSchema;

    auto add(CompilerArgumentDefinition definition) -> usize;

    auto build() && -> CompilerArgumentResult<CompilerArgumentParser>;

private:
    Vec<CompilerArgumentDefinition> definitions_;
};

class CompilerArgumentParser {
public:
    auto parse(const Vec<String>& arguments) const
        -> CompilerArgumentResult<Vec<CompilerArgumentMatch>>;

private:
    friend class CompilerArgumentSchema;

    explicit CompilerArgumentParser(Vec<CompilerArgumentDefinition> definitions)
        : definitions_(rstd::move(definitions)) {}

    Vec<CompilerArgumentDefinition> definitions_;
};

auto compiler_argument_error_message(const CompilerArgumentError& error) -> String;

} // namespace lito

namespace lito
{

auto accepts_separate(CompilerArgumentValueForm form) noexcept -> bool {
    return form == CompilerArgumentValueForm::Separate ||
           form == CompilerArgumentValueForm::SeparateOrJoined ||
           form == CompilerArgumentValueForm::SeparateOrEquals;
}

auto accepts_joined(CompilerArgumentValueForm form) noexcept -> bool {
    return form == CompilerArgumentValueForm::Joined ||
           form == CompilerArgumentValueForm::SeparateOrJoined ||
           form == CompilerArgumentValueForm::OptionalJoined;
}

auto accepts_equals(CompilerArgumentValueForm form) noexcept -> bool {
    return form == CompilerArgumentValueForm::Equals ||
           form == CompilerArgumentValueForm::SeparateOrEquals ||
           form == CompilerArgumentValueForm::OptionalEquals;
}

auto requires_value(CompilerArgumentValueForm form) noexcept -> bool {
    return form != CompilerArgumentValueForm::None &&
           form != CompilerArgumentValueForm::OptionalJoined &&
           form != CompilerArgumentValueForm::OptionalEquals;
}

struct SpellingMatch {
    usize                           definition {};
    const CompilerArgumentSpelling* spelling {};
    Option<ref<str>>                attached;
};

auto match_spelling(const Vec<CompilerArgumentDefinition>& definitions, ref<str> token)
    -> Option<SpellingMatch> {
    auto result      = Option<SpellingMatch> {};
    auto prefix_size = usize {};
    for (auto definition = usize {}; definition < definitions.len(); ++definition) {
        for (const auto& spelling : definitions[definition].spellings) {
            auto text = spelling.value.as_str();
            if (token == text) {
                return Some(SpellingMatch {
                    .definition = definition,
                    .spelling   = rstd::addressof(spelling),
                });
            }
            auto attached = Option<ref<str>> {};
            if (accepts_joined(spelling.form) && token.starts_with(text) &&
                token.len() > text.len()) {
                attached = token.get(text.len(), token.len());
            } else if (accepts_equals(spelling.form) && token.starts_with(text) &&
                       token.len() > text.len() && token.as_bytes()[text.len()] == u8('=')) {
                attached = token.get(text.len() + usize(1), token.len());
            }
            if (attached.is_some() && text.len() > prefix_size) {
                prefix_size = text.len();
                result      = Some(SpellingMatch {
                    .definition = definition,
                    .spelling   = rstd::addressof(spelling),
                    .attached   = attached,
                });
            }
        }
    }
    return result;
}

} // namespace lito

export namespace lito
{

auto CompilerArgumentSchema::make() -> CompilerArgumentSchema {
    auto result         = CompilerArgumentSchema {};
    result.definitions_ = Vec<CompilerArgumentDefinition>::make();
    return result;
}

auto CompilerArgumentSchema::add(CompilerArgumentDefinition definition) -> usize {
    auto id = definitions_.len();
    definitions_.push(rstd::move(definition));
    return id;
}

auto CompilerArgumentSchema::build() && -> CompilerArgumentResult<CompilerArgumentParser> {
    for (auto definition = usize {}; definition < definitions_.len(); ++definition) {
        if (definitions_[definition].name.is_empty()) {
            return Err(CompilerArgumentError::InvalidDefinition(
                String::make("<unnamed>"_str), String::make("name must not be empty"_str)));
        }
        if (definitions_[definition].spellings.is_empty()) {
            return Err(CompilerArgumentError::InvalidDefinition(
                definitions_[definition].name.clone(),
                String::make("at least one spelling is required"_str)));
        }
        for (auto spelling = usize {}; spelling < definitions_[definition].spellings.len();
             ++spelling) {
            auto value = definitions_[definition].spellings[spelling].value.as_str();
            if (value.is_empty()) {
                return Err(CompilerArgumentError::InvalidDefinition(
                    definitions_[definition].name.clone(),
                    String::make("spelling must not be empty"_str)));
            }
            for (auto candidate_definition = usize {}; candidate_definition <= definition;
                 ++candidate_definition) {
                auto limit = candidate_definition == definition
                                 ? spelling
                                 : definitions_[candidate_definition].spellings.len();
                for (auto candidate = usize {}; candidate < limit; ++candidate) {
                    if (definitions_[candidate_definition].spellings[candidate].value.as_str() ==
                        value) {
                        return Err(CompilerArgumentError::DuplicateSpelling(String::make(value)));
                    }
                }
            }
        }
    }
    return Ok(CompilerArgumentParser(rstd::move(definitions_)));
}

auto CompilerArgumentParser::parse(const Vec<String>& arguments) const
    -> CompilerArgumentResult<Vec<CompilerArgumentMatch>> {
    auto result = Vec<CompilerArgumentMatch>::make();
    for (auto index = usize {}; index < arguments.len(); ++index) {
        auto token   = arguments[index].as_str();
        auto matched = match_spelling(definitions_, token);
        if (matched.is_none()) {
            auto raw = Vec<String>::make();
            raw.push(arguments[index].clone());
            result.push(CompilerArgumentMatch {
                .spelling   = arguments[index].clone(),
                .raw_tokens = rstd::move(raw),
                .range      = CompilerArgumentSourceRange { index, index + usize(1) },
            });
            continue;
        }

        auto selected = *matched;
        auto value    = Option<String> {};
        auto raw      = Vec<String>::make();
        raw.push(arguments[index].clone());
        if (selected.attached.is_some()) {
            if (selected.attached->is_empty()) {
                return Err(
                    CompilerArgumentError::EmptyValue(index, selected.spelling->value.clone()));
            }
            value = Some(String::make(*selected.attached));
        } else if (requires_value(selected.spelling->form)) {
            if (! accepts_separate(selected.spelling->form)) {
                return Err(
                    CompilerArgumentError::EmptyValue(index, selected.spelling->value.clone()));
            }
            if (index + usize(1) >= arguments.len()) {
                return Err(
                    CompilerArgumentError::MissingValue(index, selected.spelling->value.clone()));
            }
            ++index;
            if (arguments[index].is_empty()) {
                return Err(CompilerArgumentError::EmptyValue(index - usize(1),
                                                             selected.spelling->value.clone()));
            }
            value = Some(arguments[index].clone());
            raw.push(arguments[index].clone());
        }
        auto range = CompilerArgumentSourceRange {
            .begin = index + usize(1) - raw.len(),
            .end   = index + usize(1),
        };
        result.push(CompilerArgumentMatch {
            .definition = Some(selected.definition),
            .value      = rstd::move(value),
            .spelling   = selected.spelling->value.clone(),
            .raw_tokens = rstd::move(raw),
            .range      = range,
        });
    }
    return Ok(rstd::move(result));
}

auto compiler_argument_error_message(const CompilerArgumentError& error) -> String {
    RSTD_MATCH(error) {
        RSTD_CASE(InvalidDefinition, name, reason) {
            return rstd::format(
                "compiler argument definition '{}': {}", name.as_str(), reason.as_str());
        }
        RSTD_CASE(DuplicateSpelling, spelling) {
            return rstd::format("compiler argument spelling '{}' is defined more than once",
                                spelling.as_str());
        }
        RSTD_CASE(MissingValue, index, spelling) {
            return rstd::format(
                "compiler option '{}' at argument {} requires a value", spelling.as_str(), index);
        }
        RSTD_CASE(EmptyValue, index, spelling) {
            return rstd::format(
                "compiler option '{}' at argument {} has an empty value", spelling.as_str(), index);
        }
    }
    rstd::unreachable();
}

} // namespace lito
