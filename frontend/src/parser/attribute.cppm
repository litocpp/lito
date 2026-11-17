export module lito.frontend.parser:attribute;

import rstd;
import lito.frontend.result;
import lito.frontend.preprocessor;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito::frontend::parser
{

struct AttributeParseState {
    AttributeSyntax        syntax { AttributeSyntax::Cpp };
    bool                   first_open {};
    bool                   active {};
    bool                   first_close {};
    bool                   complete {};
    bool                   using_prefix {};
    bool                   using_colon {};
    usize                  opening {};
    usize                  parentheses {};
    usize                  braces {};
    usize                  squares {};
    Option<String>         using_scope;
    Option<String>         scope;
    Option<lexical::Token> name;
};

struct PendingAttributeUse {
    AttributeSyntax syntax { AttributeSyntax::Cpp };
    String          scope;
    lexical::Token  name;
};

} // namespace lito::frontend::parser

export namespace lito::frontend::parser
{

class AttributeConsumer {
public:
    auto consume(const lexical::Token& token) -> bool {
        if (token.kind == lexical::TokenKind::Newline) return false;
        while (states_.len() <= token.expansion.source) states_.emplace_back();
        auto&       state = states_[token.expansion.source];
        const auto& text  = token.text;
        if (state.opening != usize {}) {
            if (text == "("_str) {
                --state.opening;
                if (state.opening == usize {}) state.active = true;
                return true;
            }
            state = AttributeParseState {};
        }
        if (! state.active) {
            if (text == "__attribute__"_str || text == "__attribute"_str ||
                text == "__declspec"_str) {
                state.syntax =
                    text == "__declspec"_str ? AttributeSyntax::Declspec : AttributeSyntax::Gnu;
                state.opening    = state.syntax == AttributeSyntax::Gnu ? usize(2) : usize(1);
                state.first_open = false;
                return true;
            }
            if (text == "["_str) {
                if (state.first_open) {
                    state = AttributeParseState { .active = true };
                    return true;
                }
                state.first_open = true;
            } else {
                state.first_open = false;
            }
            return false;
        }
        if (state.parentheses != usize {} || state.braces != usize {} ||
            state.squares != usize {}) {
            if (text == "("_str)
                ++state.parentheses;
            else if (text == ")"_str && state.parentheses != usize {})
                --state.parentheses;
            else if (text == "{"_str)
                ++state.braces;
            else if (text == "}"_str && state.braces != usize {})
                --state.braces;
            else if (text == "["_str)
                ++state.squares;
            else if (text == "]"_str && state.squares != usize {})
                --state.squares;
            return true;
        }
        auto closing = state.syntax == AttributeSyntax::Cpp ? text == "]"_str : text == ")"_str;
        if (closing) {
            if (state.first_close || state.syntax == AttributeSyntax::Declspec) {
                append(state);
                state = AttributeParseState {};
            } else {
                state.first_close = true;
            }
            return true;
        }
        state.first_close = false;
        if (text == ","_str) {
            append(state);
            reset_name(state);
            return true;
        }
        if (text == "("_str || text == "{"_str || text == "["_str) {
            append(state);
            state.complete = true;
            if (text == "("_str) state.parentheses = usize(1);
            if (text == "{"_str) state.braces = usize(1);
            if (text == "["_str) state.squares = usize(1);
            return true;
        }
        if (state.syntax == AttributeSyntax::Declspec &&
            token.kind == lexical::TokenKind::Identifier) {
            append(state);
            reset_name(state);
        }
        if (state.complete) return true;
        if (token.kind == lexical::TokenKind::Identifier) {
            if (state.using_prefix) {
                if (state.using_scope.is_none()) {
                    state.using_scope = Some(String::make(text.utf8().unwrap()));
                    return true;
                }
                if (! state.using_colon) {
                    state.complete = true;
                    return true;
                }
            }
            if (state.name.is_none() && state.scope.is_none() && ! state.using_prefix &&
                state.syntax == AttributeSyntax::Cpp && text == "using"_str) {
                state.using_prefix = true;
                return true;
            }
            if (state.name.is_none())
                state.name = Some(token.clone());
            else
                state.complete = true;
            return true;
        }
        if (text == ":"_str && state.using_scope.is_some()) {
            state.using_colon = true;
            return true;
        }
        if (text == "::"_str && state.name.is_some() && state.scope.is_none()) {
            state.scope = Some(String::make(state.name->text.utf8().unwrap()));
            state.name  = None();
            return true;
        }
        state.complete = true;
        return true;
    }

    auto finish(const preprocessor::PreprocessedTranslationUnit& translation,
                FrontendResult&                                  result) -> void {
        for (const auto& state : states_) {
            if (state.active || state.opening != usize {}) result.attribute_scopes.complete = false;
        }
        for (auto& pending : attributes_) {
            const auto& token = pending.name;
            if (! pending.scope.is_empty()) {
                result.attribute_scopes.observe(pending.scope.as_str());
                if (pending.syntax == AttributeSyntax::Cpp &&
                    token.expansion.source == translation.main_source) {
                    auto duplicate = false;
                    for (const auto& existing : result.scoped_attributes) {
                        if (existing.scope == pending.scope &&
                            existing.name == token.text.utf8().unwrap())
                            duplicate = true;
                    }
                    if (! duplicate)
                        result.scoped_attributes.push(ScopedAttributeUse {
                            .scope = pending.scope.clone(),
                            .name  = String::make(token.text.utf8().unwrap()),
                        });
                }
            }
            auto system = false;
            for (auto source : translation.system_sources) {
                if (source == token.expansion.source) system = true;
            }
            if (system) continue;
            result.attribute_uses.push(AttributeUse {
                .syntax = pending.syntax,
                .scope  = rstd::move(pending.scope),
                .name   = String::make(token.text.utf8().unwrap()),
                .location =
                    DependencyLocation {
                        .path = token.presumed_path.is_some()
                                    ? token.presumed_path->clone()
                                    : rstd::path::PathBuf::from(
                                          translation.sources.path(token.expansion.source)),
                        .line = token.expansion.line,
                    },
                .column = token.expansion.column,
            });
        }
    }

private:
    static auto reset_name(AttributeParseState& state) -> void {
        state.name     = None();
        state.scope    = None();
        state.complete = false;
    }

    auto append(AttributeParseState& state) -> void {
        if (state.name.is_none()) return;
        attributes_.push(PendingAttributeUse {
            .syntax = state.syntax,
            .scope  = state.scope.is_some()         ? rstd::move(state.scope).unwrap()
                      : state.using_scope.is_some() ? state.using_scope->clone()
                                                    : String::make(),
            .name   = rstd::move(state.name).unwrap(),
        });
        state.name  = None();
        state.scope = None();
    }

    Vec<AttributeParseState> states_;
    Vec<PendingAttributeUse> attributes_;
};

} // namespace lito::frontend::parser
