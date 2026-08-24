export module lito.frontend.preprocessor:macro;

import rstd;
import lito.frontend.lexical;
import :builtin;

using namespace rstd::prelude;
using namespace rstd::literals;

using namespace lito::frontend::lexical;

export namespace lito::frontend::preprocessor
{

enum class MacroReplacementOperationKind
{
    Literal,
    ExpandedParameter,
    RawParameter,
    StringifyParameter,
    VaOpt,
};

struct MacroReplacementOperation {
    MacroReplacementOperationKind kind { MacroReplacementOperationKind::Literal };
    usize                         token_index {};
    usize                         parameter {};
    usize                         end {};
    bool                          paste_before { false };
    bool                          paste_after { false };
    bool                          last_expanded_use { false };
    bool                          valid { true };
};

struct MacroDefinition {
    String              name;
    Option<Vec<String>> parameters;
    bool                variadic { false };
    String              variadic_name;
    Vec<Token>          replacement;
    SourceLocation      location;

    auto set_name(String value) -> void {
        dynamic_builtin_name_ = DynamicBuiltinSet::contains(value.as_str());
        name                  = rstd::move(value);
    }

    auto is_dynamic_builtin() const noexcept -> bool { return dynamic_builtin_name_; }

    auto set_replacement(Vec<Token> value) -> void;

    auto retain_source(SharedSourceSnapshot source) -> void {
        source_owner_ = Some(rstd::move(source));
    }

    auto operations() const noexcept -> const Vec<MacroReplacementOperation>& {
        return operations_;
    }

    auto can_consume_argument(usize parameter) const -> bool {
        return ! contains_va_opt_ && parameter < unexpanded_parameter_uses_.len() &&
               ! unexpanded_parameter_uses_[parameter];
    }

    auto clone() const -> MacroDefinition;

private:
    auto parameter_index(ref<str> value) const -> Option<usize>;
    auto paste_before(usize index, usize begin) const -> bool;
    auto paste_after(usize index, usize end) const -> bool;
    auto compile_range(usize token_begin, usize token_end) -> void;
    auto cloned_parameters() const -> Option<Vec<String>>;
    auto copied_replacement() const -> Vec<Token>;

    Vec<MacroReplacementOperation> operations_;
    Vec<bool>                      unexpanded_parameter_uses_;
    Option<SharedSourceSnapshot>   source_owner_;
    bool                           contains_va_opt_ { false };
    bool                           dynamic_builtin_name_ { false };
};

using SharedMacroDefinition = rstd::sync::Arc<MacroDefinition>;

class MacroTable {
public:
    static auto make() -> MacroTable { return MacroTable {}; }

    auto get(ref<str> name) const -> Option<SharedMacroDefinition> {
        auto found = values_.get(name);
        return found.is_some() ? Some((**found).clone()) : Option<SharedMacroDefinition> {};
    }

    auto contains(ref<str> name) const -> bool { return values_.contains_key(name); }

    auto define(MacroDefinition definition) -> Option<SharedMacroDefinition> {
        auto name   = definition.name.clone();
        auto shared = rstd::sync::Arc<MacroDefinition>::make(rstd::move(definition));
        ++revision_;
        return values_.insert(rstd::move(name), rstd::move(shared));
    }

    auto define_shared(SharedMacroDefinition definition) -> Option<SharedMacroDefinition> {
        auto name = definition->name.clone();
        ++revision_;
        return values_.insert(rstd::move(name), rstd::move(definition));
    }

    auto undefine(ref<str> name) -> Option<SharedMacroDefinition> {
        auto removed = values_.remove(name);
        if (removed.is_some()) ++revision_;
        return removed;
    }

    auto revision() const noexcept -> u32 { return revision_; }

private:
    rstd::collections::HashMap<String, SharedMacroDefinition> values_;
    u32                                                       revision_ {};
};

auto clone_tokens(const Vec<Token>& input) -> Vec<Token> {
    auto result = Vec<Token>::with_capacity(input.len());
    for (const auto& token : input) result.push(token.clone());
    return result;
}

auto parse_macro_definition(const Vec<Token>& line) -> lexical::Result<MacroDefinition> {
    if (line.is_empty() || line[usize {}].kind != TokenKind::Identifier) {
        auto location = line.is_empty() ? SourceLocation {} : line[usize {}].expansion;
        return Err(
            lexical::Error::at(String::make("#define requires an identifier"_str), location));
    }
    auto macro = MacroDefinition {};
    macro.set_name(line[usize {}].text.clone());
    macro.location = line[usize {}].expansion;
    auto index     = usize(1);
    if (index < line.len() && line[index].text.as_str() == "("_str && ! line[index].leading_space) {
        auto parameters = Vec<String>::make();
        ++index;
        if (index < line.len() && line[index].text.as_str() == ")"_str) {
            ++index;
        } else {
            while (index < line.len()) {
                if (line[index].text.as_str() == "..."_str) {
                    macro.variadic      = true;
                    macro.variadic_name = String::make("__VA_ARGS__"_str);
                    ++index;
                    break;
                }
                if (line[index].kind != TokenKind::Identifier) {
                    return Err(lexical::Error::at(String::make("invalid macro parameter"_str),
                                                  line[index].expansion));
                }
                auto name = line[index].text.clone();
                ++index;
                if (index < line.len() && line[index].text.as_str() == "..."_str) {
                    macro.variadic      = true;
                    macro.variadic_name = rstd::move(name);
                    ++index;
                    break;
                }
                parameters.push(rstd::move(name));
                if (index < line.len() && line[index].text.as_str() == ","_str) {
                    ++index;
                    continue;
                }
                break;
            }
            if (index >= line.len() || line[index].text.as_str() != ")"_str) {
                auto location = index < line.len() ? line[index].expansion : macro.location;
                return Err(lexical::Error::at(String::make("unterminated macro parameter list"_str),
                                              location));
            }
            ++index;
        }
        macro.parameters = Some(rstd::move(parameters));
    }
    auto replacement = Vec<Token>::with_capacity(line.len() - index);
    for (; index < line.len(); ++index) replacement.push(line[index].clone());
    macro.set_replacement(rstd::move(replacement));
    return Ok(rstd::move(macro));
}

struct ParsedMacroSource {
    Vec<SharedMacroDefinition> definitions;
};

auto parse_macro_source(SourceBuffer buffer) -> lexical::Result<ParsedMacroSource> {
    auto snapshot = make_source_snapshot(rstd::move(buffer));
    auto source   = SourceFile { .snapshot = snapshot.clone() };
    auto tokens   = lex(source, true);
    if (tokens.is_err()) return Err(rstd::move(tokens).unwrap_err());

    auto definitions = Vec<SharedMacroDefinition>::make();
    for (auto cursor = usize {}; cursor < tokens->len();) {
        auto end = cursor;
        while (end < tokens->len() && (*tokens)[end].kind != TokenKind::Newline) ++end;
        if (cursor == end) {
            cursor = end < tokens->len() ? end + usize(1) : end;
            continue;
        }
        if (cursor + usize(2) > end || (*tokens)[cursor].text.as_str() != "#"_str ||
            (*tokens)[cursor + usize(1)].text.as_str() != "define"_str) {
            return Err(
                lexical::Error::at(String::make("macro source contains a non-define directive"_str),
                                   (*tokens)[cursor].expansion));
        }
        auto line = Vec<Token>::with_capacity(end - cursor - usize(2));
        for (auto index = cursor + usize(2); index < end; ++index) {
            line.push((*tokens)[index].clone());
        }
        auto definition = parse_macro_definition(line);
        if (definition.is_err()) return Err(rstd::move(definition).unwrap_err());
        definition->retain_source(snapshot.clone());
        definitions.push(rstd::sync::Arc<MacroDefinition>::make(rstd::move(definition).unwrap()));
        cursor = end < tokens->len() ? end + usize(1) : end;
    }
    return Ok(ParsedMacroSource {
        .definitions = rstd::move(definitions),
    });
}

auto parse_command_line_macro_definition(ref<str> value) -> lexical::Result<MacroDefinition> {
    auto bytes = value.as_bytes();
    auto equal = usize {};
    while (equal < bytes.len() && bytes[equal] != u8('=')) ++equal;

    auto signature = value.get(usize {}, equal);
    if (signature.is_none() || signature->is_empty()) {
        return Err(lexical::Error::make("command-line macro has an empty signature"_str));
    }
    auto tokens = lex_preprocessing_fragment(String::make(*signature), SourceLocation {});
    if (tokens.is_err()) return Err(rstd::move(tokens).unwrap_err());

    auto replacement = String::make("1"_str);
    if (equal < bytes.len()) {
        auto text = value.get(equal + usize(1), bytes.len());
        if (text.is_none()) {
            return Err(lexical::Error::make("command-line macro has an invalid replacement"_str));
        }
        replacement = String::make(*text);
    }
    auto replacement_tokens =
        lex_preprocessing_fragment(rstd::move(replacement), SourceLocation {});
    if (replacement_tokens.is_err()) {
        return Err(rstd::move(replacement_tokens).unwrap_err());
    }
    for (auto& token : *replacement_tokens) tokens->push(rstd::move(token));
    return parse_macro_definition(*tokens);
}

auto share_macro_definition(MacroDefinition definition) -> SharedMacroDefinition {
    return rstd::sync::Arc<MacroDefinition>::make(rstd::move(definition));
}

} // namespace lito::frontend::preprocessor

namespace lito::frontend::preprocessor
{

auto MacroDefinition::parameter_index(ref<str> value) const -> Option<usize> {
    if (parameters.is_some()) {
        for (auto index = usize {}; index < parameters->len(); ++index) {
            if ((*parameters)[index].as_str() == value) return Some(index);
        }
    }
    if (variadic && (value == "__VA_ARGS__"_str || value == variadic_name.as_str())) {
        return Some(parameters.is_some() ? parameters->len() : usize {});
    }
    return None();
}

auto MacroDefinition::set_replacement(Vec<Token> value) -> void {
    replacement = rstd::move(value);
    operations_ = Vec<MacroReplacementOperation>::make();
    auto count  = parameters.is_some() ? parameters->len() : usize {};
    if (variadic) ++count;
    unexpanded_parameter_uses_ = Vec<bool>::with_capacity(count);
    for (auto index = usize {}; index < count; ++index) unexpanded_parameter_uses_.push(false);
    contains_va_opt_ = false;
    compile_range(usize {}, replacement.len());
}

auto MacroDefinition::paste_before(usize index, usize begin) const -> bool {
    return index > begin && replacement[index - usize(1)].text.as_str() == "##"_str;
}

auto MacroDefinition::paste_after(usize index, usize end) const -> bool {
    return index + usize(1) < end && replacement[index + usize(1)].text.as_str() == "##"_str;
}

auto MacroDefinition::compile_range(usize token_begin, usize token_end) -> void {
    auto operation_begin = operations_.len();
    for (auto index = token_begin; index < token_end;) {
        const auto& token = replacement[index];
        if (variadic && token.text.as_str() == "__VA_OPT__"_str && index + usize(1) < token_end &&
            replacement[index + usize(1)].text.as_str() == "("_str) {
            contains_va_opt_ = true;
            auto closing     = Option<usize> {};
            auto depth       = usize {};
            for (auto cursor = index + usize(2); cursor < token_end; ++cursor) {
                auto text = replacement[cursor].text.as_str();
                if (text == "("_str) {
                    ++depth;
                } else if (text == ")"_str) {
                    if (depth == usize {}) {
                        closing = Some(cursor);
                        break;
                    }
                    --depth;
                }
            }
            auto operation_index = operations_.len();
            operations_.push(MacroReplacementOperation {
                .kind         = MacroReplacementOperationKind::VaOpt,
                .token_index  = index,
                .paste_before = paste_before(index, token_begin),
                .valid        = closing.is_some(),
            });
            if (closing.is_none()) {
                operations_[operation_index].end = operations_.len();
                index                            = token_end;
                continue;
            }
            compile_range(index + usize(2), *closing);
            operations_[operation_index].end = operations_.len();
            operations_[operation_index].paste_after =
                *closing + usize(1) < token_end &&
                replacement[*closing + usize(1)].text.as_str() == "##"_str;
            index = *closing + usize(1);
            continue;
        }
        if (token.text.as_str() == "#"_str && index + usize(1) < token_end) {
            auto parameter = parameter_index(replacement[index + usize(1)].text.as_str());
            if (parameter.is_some()) {
                unexpanded_parameter_uses_[*parameter] = true;
                operations_.push(MacroReplacementOperation {
                    .kind         = MacroReplacementOperationKind::StringifyParameter,
                    .token_index  = index,
                    .parameter    = *parameter,
                    .paste_before = paste_before(index, token_begin),
                    .paste_after  = index + usize(2) < token_end &&
                                    replacement[index + usize(2)].text.as_str() == "##"_str,
                });
                index += usize(2);
                continue;
            }
        }
        if (token.text.as_str() == "##"_str) {
            ++index;
            continue;
        }
        auto parameter = parameter_index(token.text.as_str());
        if (parameter.is_some()) {
            auto before = paste_before(index, token_begin);
            auto after  = paste_after(index, token_end);
            auto raw    = before || after;
            if (raw) unexpanded_parameter_uses_[*parameter] = true;
            operations_.push(MacroReplacementOperation {
                .kind         = raw ? MacroReplacementOperationKind::RawParameter
                                    : MacroReplacementOperationKind::ExpandedParameter,
                .token_index  = index,
                .parameter    = *parameter,
                .paste_before = before,
                .paste_after  = after,
            });
        } else {
            operations_.push(MacroReplacementOperation {
                .kind         = MacroReplacementOperationKind::Literal,
                .token_index  = index,
                .paste_before = paste_before(index, token_begin),
                .paste_after  = paste_after(index, token_end),
            });
        }
        ++index;
    }

    auto operation_end = operations_.len();
    auto last_uses     = Vec<Option<usize>>::with_capacity(unexpanded_parameter_uses_.len());
    for (auto index = usize {}; index < unexpanded_parameter_uses_.len(); ++index) {
        last_uses.emplace_back(None());
    }
    for (auto index = operation_begin; index < operation_end;) {
        const auto& operation = operations_[index];
        if (operation.kind == MacroReplacementOperationKind::VaOpt) {
            index = operation.end;
            continue;
        }
        if (operation.kind == MacroReplacementOperationKind::ExpandedParameter) {
            last_uses[operation.parameter] = Some(index);
        }
        ++index;
    }
    for (const auto& last : last_uses) {
        if (last.is_some()) operations_[*last].last_expanded_use = true;
    }
}

auto MacroDefinition::cloned_parameters() const -> Option<Vec<String>> {
    auto copied_parameters = Option<Vec<String>> {};
    if (parameters.is_some()) {
        auto values = Vec<String>::with_capacity(parameters->len());
        for (const auto& parameter : *parameters) values.push(parameter.clone());
        copied_parameters = Some(rstd::move(values));
    }
    return copied_parameters;
}

auto MacroDefinition::copied_replacement() const -> Vec<Token> {
    auto copied_replacement = Vec<Token>::with_capacity(replacement.len());
    for (const auto& token : replacement) copied_replacement.push(token.clone());
    return copied_replacement;
}

auto MacroDefinition::clone() const -> MacroDefinition {
    auto result = MacroDefinition {};
    result.set_name(name.clone());
    result.parameters    = cloned_parameters();
    result.variadic      = variadic;
    result.variadic_name = variadic_name.clone();
    result.location      = location;
    result.source_owner_ = as<Clone>(source_owner_).clone();
    result.set_replacement(copied_replacement());
    return result;
}

} // namespace lito::frontend::preprocessor
