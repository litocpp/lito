export module tenon.frontend.preprocessor:macro;

import rstd;
import tenon.frontend.lexical;

using namespace rstd::prelude;
using namespace rstd::literals;

using namespace tenon::frontend::lexical;

export namespace tenon::frontend::preprocessor
{

auto is_dynamic_builtin_name(ref<str> name) -> bool {
    return name == "__LINE__"_str || name == "__FILE__"_str || name == "__BASE_FILE__"_str ||
           name == "__FILE_NAME__"_str || name == "__INCLUDE_LEVEL__"_str ||
           name == "__COUNTER__"_str || name == "__DATE__"_str || name == "__TIME__"_str ||
           name == "_Pragma"_str || name == "__has_builtin"_str ||
           name == "__has_constexpr_builtin"_str || name == "__has_feature"_str ||
           name == "__has_extension"_str || name == "__has_cpp_attribute"_str ||
           name == "__has_attribute"_str || name == "__has_declspec_attribute"_str ||
           name == "__has_embed"_str || name == "__has_warning"_str ||
           name == "__is_identifier"_str || name == "__is_target_arch"_str ||
           name == "__is_target_vendor"_str || name == "__is_target_os"_str ||
           name == "__is_target_environment"_str || name == "__is_target_variant_os"_str ||
           name == "__is_target_variant_environment"_str || name == "__has_include"_str ||
           name == "__has_include_next"_str;
}

struct MacroDefinition {
    String              name;
    Option<Vec<String>> parameters;
    bool                variadic { false };
    String              variadic_name;
    Vec<Token>          replacement;
    SourceLocation      location;

    auto set_name(String value) -> void {
        dynamic_builtin_name_ = is_dynamic_builtin_name(value.as_str());
        name                  = rstd::move(value);
    }

    auto is_dynamic_builtin() const noexcept -> bool { return dynamic_builtin_name_; }

    auto parameter_index(ref<str> value) const -> Option<usize> {
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

    auto set_replacement(Vec<Token> value) -> void {
        replacement = rstd::move(value);
        auto count  = parameters.is_some() ? parameters->len() : usize {};
        if (variadic) ++count;
        expanded_parameter_last_uses_ = Vec<Option<usize>>::with_capacity(count);
        unexpanded_parameter_uses_    = Vec<bool>::with_capacity(count);
        for (auto index = usize {}; index < count; ++index)
            expanded_parameter_last_uses_.emplace_back(None());
        for (auto index = usize {}; index < count; ++index) unexpanded_parameter_uses_.push(false);
        contains_va_opt_ = false;
        va_opt_ends_     = Vec<Option<usize>>::with_capacity(replacement.len());
        for (auto index = usize {}; index < replacement.len(); ++index)
            va_opt_ends_.emplace_back(None());
        for (auto index = usize {}; index < replacement.len(); ++index) {
            if (variadic && replacement[index].text.as_str() == "__VA_OPT__"_str &&
                index + usize(1) < replacement.len() &&
                replacement[index + usize(1)].text.as_str() == "("_str) {
                contains_va_opt_ = true;
                auto depth       = usize {};
                for (auto cursor = index + usize(2); cursor < replacement.len(); ++cursor) {
                    auto text = replacement[cursor].text.as_str();
                    if (text == "("_str) {
                        ++depth;
                    } else if (text == ")"_str) {
                        if (depth == usize {}) {
                            va_opt_ends_[index] = Some(cursor + usize(1));
                            index               = cursor;
                            break;
                        }
                        --depth;
                    }
                }
                continue;
            }
            auto parameter = parameter_index(replacement[index].text.as_str());
            if (parameter.is_none()) continue;
            auto paste_left =
                index > usize {} && replacement[index - usize(1)].text.as_str() == "##"_str;
            auto paste_right = index + usize(1) < replacement.len() &&
                               replacement[index + usize(1)].text.as_str() == "##"_str;
            auto stringified =
                index > usize {} && replacement[index - usize(1)].text.as_str() == "#"_str;
            if (paste_left || paste_right || stringified) {
                unexpanded_parameter_uses_[*parameter] = true;
            } else {
                expanded_parameter_last_uses_[*parameter] = Some(index);
            }
        }
    }

    auto is_last_expanded_use(usize parameter, usize replacement_index) const -> bool {
        return parameter < expanded_parameter_last_uses_.len() &&
               expanded_parameter_last_uses_[parameter].is_some() &&
               *expanded_parameter_last_uses_[parameter] == replacement_index;
    }

    auto va_opt_end(usize replacement_index) const -> Option<usize> {
        if (replacement_index >= va_opt_ends_.len()) return None();
        return va_opt_ends_[replacement_index];
    }

    auto can_consume_argument(usize parameter) const -> bool {
        return ! contains_va_opt_ && parameter < unexpanded_parameter_uses_.len() &&
               ! unexpanded_parameter_uses_[parameter];
    }

    auto cloned_parameters() const -> Option<Vec<String>> {
        auto copied_parameters = Option<Vec<String>> {};
        if (parameters.is_some()) {
            auto values = Vec<String>::with_capacity(parameters->len());
            for (const auto& parameter : *parameters) values.push(parameter.clone());
            copied_parameters = Some(rstd::move(values));
        }
        return copied_parameters;
    }

    auto with_replacement(Vec<Token> value) const -> MacroDefinition {
        auto result = MacroDefinition {};
        result.set_name(name.clone());
        result.parameters    = cloned_parameters();
        result.variadic      = variadic;
        result.variadic_name = variadic_name.clone();
        result.location      = location;
        result.set_replacement(rstd::move(value));
        return result;
    }

    auto clone() const -> MacroDefinition {
        auto copied_replacement = Vec<Token>::with_capacity(replacement.len());
        for (const auto& token : replacement) copied_replacement.push(token.clone());
        return with_replacement(rstd::move(copied_replacement));
    }

private:
    Vec<Option<usize>> expanded_parameter_last_uses_;
    Vec<bool>          unexpanded_parameter_uses_;
    Vec<Option<usize>> va_opt_ends_;
    bool               contains_va_opt_ { false };
    bool               dynamic_builtin_name_ { false };
};

using SharedMacroDefinition = rstd::rc::Rc<const MacroDefinition>;

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
        auto shared = rstd::rc::make_rc<MacroDefinition>(rstd::move(definition)).to_const();
        ++revision_;
        return values_.insert(rstd::move(name), rstd::move(shared));
    }

    auto define_shared(SharedMacroDefinition definition) -> Option<SharedMacroDefinition> {
        auto name = definition.get()->name.clone();
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

auto share_macro_definition(MacroDefinition definition) -> SharedMacroDefinition {
    return rstd::rc::make_rc<MacroDefinition>(rstd::move(definition)).to_const();
}

} // namespace tenon::frontend::preprocessor
