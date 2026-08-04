export module tenon.frontend.parser:documentation;

import rstd;
import tenon.frontend.result;
import tenon.frontend.preprocessor;
import :module_dependency;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace tenon::frontend::parser
{

namespace lexical      = tenon::frontend::lexical;
namespace preprocessor = tenon::frontend::preprocessor;

auto documentation_span(const preprocessor::PreprocessedTranslationUnit& translation,
                        lexical::SourceLocation                          begin,
                        lexical::SourceLocation                          end) -> DocumentationSpan {
    return DocumentationSpan {
        .path         = rstd::path::PathBuf::from(translation.sources.path(begin.source)),
        .begin_line   = begin.line,
        .begin_column = begin.column,
        .end_line     = end.line,
        .end_column   = end.column,
    };
}

auto normalized_comment(const lexical::CommentTrivia& comment) -> String {
    auto raw   = comment.text.as_str();
    auto begin = usize(3);
    auto end   = raw.len();
    if (comment.style == lexical::CommentStyle::Block && end >= usize(2)) end -= usize(2);
    if (begin > end) return String::make();
    auto contents = raw.get(begin, end);
    if (contents.is_none()) return String::make();
    if (comment.style == lexical::CommentStyle::Line) return String::make(contents->trim_ascii());
    auto result     = String::make();
    auto bytes      = contents->as_bytes();
    auto line_begin = usize {};
    while (line_begin <= bytes.len()) {
        auto line_end = line_begin;
        while (line_end < bytes.len() && bytes[line_end] != u8('\n') && bytes[line_end] != u8('\r'))
            ++line_end;
        auto line = contents->get(line_begin, line_end).unwrap().trim_ascii();
        if (line.starts_with("*"_str)) line = line.get(usize(1), line.len()).unwrap().trim_ascii();
        if (! result.is_empty()) result.push_ascii('\n');
        result.push_str(line);
        if (line_end == bytes.len()) break;
        if (bytes[line_end] == u8('\r') && line_end + usize(1) < bytes.len() &&
            bytes[line_end + usize(1)] == u8('\n'))
            ++line_end;
        line_begin = line_end + usize(1);
    }
    return String::make(result.as_str().trim_ascii());
}

auto declaration_spelling(const Vec<lexical::Token>& tokens, usize begin, usize end) -> String {
    auto result = String::make();
    for (auto index = begin; index < end; ++index) {
        if (! result.is_empty()) result.push_ascii(' ');
        result.push_str(tokens[index].text.as_str());
    }
    return result;
}

auto token_identifier(const lexical::Token& token) -> bool {
    return lexical::is_cpp_identifier_token(token, "c++20"_str);
}

auto doc_command(ref<str> text, ref<str> command) -> bool {
    if (! text.starts_with(command)) return false;
    return text.len() == command.len() || text[command.len()] == u8(' ');
}

struct DocumentationScope {
    Option<usize>     parent;
    String            qualified_name;
    DeclarationKind   kind { DeclarationKind::Namespace };
    DeclarationAccess access { DeclarationAccess::Public };
    bool              exported { false };
    usize             brace_depth {};
};

class DocumentationParser {
public:
    DocumentationParser(const preprocessor::PreprocessedTranslationUnit& translation,
                        const FrontendResult&                            dependencies)
        : translation_(&translation),
          dependencies_(&dependencies),
          main_source_(translation.main_source) {
        for (const auto& token : translation.tokens) {
            if (token.kind != lexical::TokenKind::Newline &&
                token.expansion.source == main_source_) {
                tokens_.push(token.clone());
            }
        }
        unit_.source = rstd::path::PathBuf::from(translation.sources.path(translation.main_source));
        unit_.source_snapshot = translation.sources.snapshot(translation.main_source);
    }

    auto parse() -> lexical::Result<DocumentationUnit> {
        if (dependencies_->provided.is_some()) {
            unit_.logical_module = dependencies_->provided->logical_name.clone();
            unit_.is_interface   = dependencies_->provided->is_interface;
        } else if (dependencies_->implementation_module.is_some()) {
            unit_.logical_module = dependencies_->implementation_module->clone();
        }

        parse_tokens();
        advance_comments(unit_.source_snapshot.get()->contents.len());
        if (! pending_comments_.is_empty()) {
            add_diagnostic(DocumentationSeverity::Warning,
                           "orphan-doc-comment"_str,
                           "documentation comment is not attached to a declaration"_str,
                           pending_comments_[usize {}].span);
            pending_comments_ = Vec<DocumentationComment>::make();
        }
        if (hidden_depth_ != usize {}) {
            auto location = DocumentationSpan { .path = unit_.source.clone() };
            add_diagnostic(DocumentationSeverity::Warning,
                           "unterminated-doxygen-cond"_str,
                           "Doxygen @cond has no matching @endcond"_str,
                           location);
        }
        return Ok(rstd::move(unit_));
    }

private:
    auto add_diagnostic(DocumentationSeverity    severity,
                        ref<str>                 code,
                        ref<str>                 message,
                        const DocumentationSpan& span) -> void {
        unit_.diagnostics.push(DocumentationDiagnostic {
            .severity = severity,
            .code     = String::make(code),
            .message  = String::make(message),
            .span =
                DocumentationSpan {
                    .path         = span.path.clone(),
                    .begin_line   = span.begin_line,
                    .begin_column = span.begin_column,
                    .end_line     = span.end_line,
                    .end_column   = span.end_column,
                },
        });
    }

    auto append_module_comment(DocumentationComment comment) -> void {
        if (unit_.module_comment.is_none()) {
            unit_.module_comment = Some(rstd::move(comment));
            return;
        }
        if (! unit_.module_comment->text.is_empty()) unit_.module_comment->text.push_ascii('\n');
        unit_.module_comment->text.push_str(comment.text.as_str());
        unit_.module_comment->span.end_line   = comment.span.end_line;
        unit_.module_comment->span.end_column = comment.span.end_column;
    }

    auto advance_comments(usize offset) -> void {
        while (comment_cursor_ < translation_->active_comments.len()) {
            const auto& trivia = translation_->active_comments[comment_cursor_];
            ++comment_cursor_;
            if (trivia.begin.source != main_source_) continue;
            if (trivia.begin.offset > offset) {
                --comment_cursor_;
                break;
            }
            if (trivia.kind == lexical::CommentKind::Ordinary) {
                pending_comments_ = Vec<DocumentationComment>::make();
                continue;
            }
            auto normalized = normalized_comment(trivia);
            if (trivia.kind == lexical::CommentKind::OuterDocumentation) {
                if (doc_command(normalized.as_str(), "@cond"_str)) {
                    ++hidden_depth_;
                    pending_comments_ = Vec<DocumentationComment>::make();
                    continue;
                }
                if (doc_command(normalized.as_str(), "@endcond"_str)) {
                    if (hidden_depth_ != usize {}) --hidden_depth_;
                    pending_comments_ = Vec<DocumentationComment>::make();
                    continue;
                }
                if (doc_command(normalized.as_str(), "\\name"_str)) {
                    auto name      = normalized.as_str().get(usize(5), normalized.len()).unwrap();
                    pending_group_ = Some(String::make(name.trim_ascii()));
                    pending_comments_ = Vec<DocumentationComment>::make();
                    continue;
                }
                if (normalized.as_str() == "@{"_str) {
                    active_group_     = rstd::move(pending_group_);
                    pending_group_    = None();
                    pending_comments_ = Vec<DocumentationComment>::make();
                    continue;
                }
                if (normalized.as_str() == "@}"_str) {
                    active_group_     = None();
                    pending_group_    = None();
                    pending_comments_ = Vec<DocumentationComment>::make();
                    continue;
                }
            }
            auto kind    = trivia.kind == lexical::CommentKind::InnerDocumentation
                               ? DocumentationCommentKind::Inner
                               : DocumentationCommentKind::Outer;
            auto comment = DocumentationComment {
                .kind = kind,
                .text = rstd::move(normalized),
                .span = documentation_span(*translation_, trivia.begin, trivia.end),
            };
            if (kind == DocumentationCommentKind::Inner) {
                append_module_comment(rstd::move(comment));
            } else if (! trivia.start_of_line) {
                ++unit_.unsupported;
                add_diagnostic(DocumentationSeverity::Warning,
                               "trailing-doc-comment"_str,
                               "trailing documentation comments are not supported"_str,
                               comment.span);
                pending_comments_ = Vec<DocumentationComment>::make();
            } else {
                pending_comments_.push(rstd::move(comment));
            }
        }
    }

    auto take_comment() -> Option<DocumentationComment> {
        if (pending_comments_.is_empty()) return None();
        auto first = rstd::move(pending_comments_[usize {}]);
        for (auto index = usize(1); index < pending_comments_.len(); ++index) {
            if (! first.text.is_empty()) first.text.push_ascii('\n');
            first.text.push_str(pending_comments_[index].text.as_str());
            first.span.end_line   = pending_comments_[index].span.end_line;
            first.span.end_column = pending_comments_[index].span.end_column;
        }
        pending_comments_ = Vec<DocumentationComment>::make();
        return Some(rstd::move(first));
    }

    auto current_parent() const -> Option<usize> {
        if (scopes_.is_empty()) return None();
        return scopes_[scopes_.len() - usize(1)].parent;
    }

    auto current_qualified() const -> ref<str> {
        if (scopes_.is_empty()) return ref<str> {};
        return scopes_[scopes_.len() - usize(1)].qualified_name.as_str();
    }

    auto current_namespace() const -> String {
        auto index = scopes_.len();
        while (index != usize {}) {
            --index;
            if (scopes_[index].kind == DeclarationKind::Namespace)
                return scopes_[index].qualified_name.clone();
        }
        return String::make();
    }

    auto current_access() const -> DeclarationAccess {
        if (scopes_.is_empty()) return DeclarationAccess::Public;
        return scopes_[scopes_.len() - usize(1)].access;
    }

    auto current_exported() const -> bool {
        if (! export_blocks_.is_empty()) return true;
        if (scopes_.is_empty()) return false;
        return scopes_[scopes_.len() - usize(1)].exported;
    }

    auto qualified_name(ref<str> name) const -> String {
        auto prefix = current_qualified();
        if (prefix.is_empty()) return String::make(name);
        return rstd::format("{}::{}", prefix, name);
    }

    auto token_span(usize begin, usize end) const -> DocumentationSpan {
        auto last = end > begin ? end - usize(1) : begin;
        return documentation_span(*translation_, tokens_[begin].expansion, tokens_[last].expansion);
    }

    auto add_declaration(DeclarationKind kind,
                         ref<str>        name,
                         usize           begin,
                         usize           end,
                         bool            explicitly_exported,
                         bool            is_definition) -> usize {
        advance_comments(tokens_[begin].spelling.offset);
        auto access   = current_access();
        auto exported = explicitly_exported || current_exported();
        if (! scopes_.is_empty() &&
            scopes_[scopes_.len() - usize(1)].kind == DeclarationKind::Record)
            exported =
                scopes_[scopes_.len() - usize(1)].exported && access == DeclarationAccess::Public;
        exported     = exported && hidden_depth_ == usize {};
        auto comment = take_comment();
        if (exported && access == DeclarationAccess::Public) {
            if (comment.is_some())
                ++unit_.documented;
            else
                ++unit_.undocumented;
        }
        auto id = unit_.declarations.len();
        unit_.declarations.push(DeclarationOutline {
            .kind           = kind,
            .name           = String::make(name),
            .qualified_name = qualified_name(name),
            .namespace_name = current_namespace(),
            .signature      = declaration_spelling(tokens_, begin, end),
            .is_definition  = is_definition,
            .exported       = exported,
            .access         = access,
            .parent         = current_parent(),
            .group   = active_group_.is_some() ? Some(active_group_->clone()) : Option<String> {},
            .comment = rstd::move(comment),
            .span    = token_span(begin, end),
        });
        return id;
    }

    auto skip_balanced(usize open, ref<str> left, ref<str> right) const -> usize {
        auto depth = usize {};
        for (auto index = open; index < tokens_.len(); ++index) {
            if (tokens_[index].text.as_str() == left) {
                ++depth;
            } else if (tokens_[index].text.as_str() == right) {
                if (depth != usize {}) --depth;
                if (depth == usize {}) return index + usize(1);
            }
        }
        return tokens_.len();
    }

    auto statement_end(usize begin) const -> usize {
        auto parentheses = usize {};
        auto brackets    = usize {};
        for (auto index = begin; index < tokens_.len(); ++index) {
            auto text = tokens_[index].text.as_str();
            if (text == "("_str)
                ++parentheses;
            else if (text == ")"_str && parentheses != usize {})
                --parentheses;
            else if (text == "["_str)
                ++brackets;
            else if (text == "]"_str && brackets != usize {})
                --brackets;
            else if (parentheses == usize {} && brackets == usize {}) {
                if (text == "{"_str && requires_expression_open(index)) {
                    auto next = skip_balanced(index, "{"_str, "}"_str);
                    if (next >= tokens_.len()) return tokens_.len();
                    index = next - usize(1);
                    continue;
                }
                if (text == ";"_str || text == "{"_str || text == "}"_str) return index;
            }
        }
        return tokens_.len();
    }

    auto requires_expression_open(usize open) const -> bool {
        if (open == usize {}) return false;
        auto previous = open - usize(1);
        if (tokens_[previous].text.as_str() == "requires"_str) return true;
        if (tokens_[previous].text.as_str() != ")"_str) return false;
        auto depth = usize(1);
        while (previous != usize {}) {
            --previous;
            auto text = tokens_[previous].text.as_str();
            if (text == ")"_str) {
                ++depth;
            } else if (text == "("_str) {
                --depth;
                if (depth == usize {})
                    return previous != usize {} &&
                           tokens_[previous - usize(1)].text.as_str() == "requires"_str;
            }
        }
        return false;
    }

    auto name_before(usize begin, usize end) const -> Option<usize> {
        auto index = end;
        while (index > begin) {
            --index;
            if (token_identifier(tokens_[index])) return Some(index);
        }
        return None();
    }

    auto function_name(usize begin, usize end) const -> Option<usize> {
        auto depth = usize {};
        for (auto index = begin; index < end; ++index) {
            auto text = tokens_[index].text.as_str();
            if (text == "{"_str && depth == usize {} && requires_expression_open(index)) {
                auto next = skip_balanced(index, "{"_str, "}"_str);
                if (next >= end) return None();
                index = next - usize(1);
                continue;
            }
            if (text == "("_str) {
                if (depth == usize {} && index != begin &&
                    token_identifier(tokens_[index - usize(1)]))
                    return Some(index - usize(1));
                ++depth;
            } else if (text == ")"_str && depth != usize {}) {
                --depth;
            }
        }
        return None();
    }

    auto parse_reexport(usize begin, usize end) -> void {
        auto name = String::make();
        for (auto index = begin; index < end; ++index) name.push_str(tokens_[index].text.as_str());
        if (name.is_empty()) return;
        unit_.reexports.push(DocumentationReexport {
            .logical_module = rstd::move(name),
            .span           = token_span(begin, end),
        });
    }

    auto parse_named_scope(usize begin, usize keyword, DeclarationKind kind, bool explicit_export)
        -> Option<usize> {
        auto name_index = keyword + usize(1);
        if (kind == DeclarationKind::Enum && name_index < tokens_.len() &&
            (tokens_[name_index].text.as_str() == "class"_str ||
             tokens_[name_index].text.as_str() == "struct"_str))
            ++name_index;
        if (name_index >= tokens_.len() || ! token_identifier(tokens_[name_index])) return None();
        auto boundary = statement_end(name_index + usize(1));
        if (boundary >= tokens_.len()) return Some(tokens_.len());
        auto name = tokens_[name_index].text.clone();
        if (kind == DeclarationKind::Namespace) {
            for (auto index = name_index + usize(1); index + usize(1) < boundary;
                 index += usize(2)) {
                if (tokens_[index].text.as_str() != "::"_str ||
                    ! token_identifier(tokens_[index + usize(1)]))
                    break;
                name.push_str("::"_str);
                name.push_str(tokens_[index + usize(1)].text.as_str());
            }
        }
        auto id = add_declaration(kind,
                                  name.as_str(),
                                  begin,
                                  boundary + usize(1),
                                  explicit_export,
                                  tokens_[boundary].text.as_str() == "{"_str);
        if (tokens_[boundary].text.as_str() == "{"_str) {
            if (kind == DeclarationKind::Enum) {
                auto next = skip_balanced(boundary, "{"_str, "}"_str);
                if (next < tokens_.len() && tokens_[next].text.as_str() == ";"_str) ++next;
                return Some(next);
            }
            ++brace_depth_;
            auto access =
                kind == DeclarationKind::Record && tokens_[keyword].text.as_str() == "class"_str
                    ? DeclarationAccess::Private
                    : DeclarationAccess::Public;
            scopes_.push(DocumentationScope {
                .parent         = Some(id),
                .qualified_name = unit_.declarations[id].qualified_name.clone(),
                .kind           = kind,
                .access         = access,
                .exported       = unit_.declarations[id].exported,
                .brace_depth    = brace_depth_,
            });
            return Some(boundary + usize(1));
        }
        return Some(boundary + usize(1));
    }

    auto parse_simple(usize begin, usize keyword, bool explicit_export) -> usize {
        auto boundary = statement_end(keyword);
        if (boundary >= tokens_.len()) return tokens_.len();
        for (auto index = keyword; index < boundary; ++index) {
            auto candidate = tokens_[index].text.as_str();
            if (candidate == "using"_str || candidate == "typedef"_str ||
                candidate == "concept"_str) {
                keyword = index;
                break;
            }
        }
        auto end  = boundary + usize(1);
        auto kind = DeclarationKind::Variable;
        auto name = Option<String> {};
        auto word = tokens_[keyword].text.as_str();
        if (word == "using"_str || word == "typedef"_str) {
            kind                = DeclarationKind::Alias;
            auto operator_index = Option<usize> {};
            for (auto index = keyword + usize(1); index < boundary; ++index) {
                if (tokens_[index].text.as_str() == "operator"_str) {
                    operator_index = Some(index);
                    break;
                }
            }
            if (operator_index.is_some()) {
                auto operator_name = String::make("operator "_str);
                for (auto index = *operator_index + usize(1); index < boundary; ++index)
                    operator_name.push_str(tokens_[index].text.as_str());
                name = Some(rstd::move(operator_name));
            } else if (word == "using"_str && keyword + usize(2) < boundary &&
                       tokens_[keyword + usize(2)].text.as_str() == "="_str) {
                name = Some(tokens_[keyword + usize(1)].text.clone());
            } else {
                auto found = name_before(keyword + usize(1), boundary);
                if (found.is_some()) name = Some(tokens_[*found].text.clone());
            }
        } else if (word == "concept"_str) {
            kind = DeclarationKind::Concept;
            if (keyword + usize(1) < boundary)
                name = Some(tokens_[keyword + usize(1)].text.clone());
        } else {
            auto found = function_name(keyword, boundary);
            if (found.is_some()) {
                kind = DeclarationKind::Function;
                name = Some(tokens_[*found].text.clone());
            } else {
                found = name_before(keyword, boundary);
                if (found.is_some()) name = Some(tokens_[*found].text.clone());
                if (! scopes_.is_empty() &&
                    scopes_[scopes_.len() - usize(1)].kind == DeclarationKind::Record)
                    kind = DeclarationKind::Field;
            }
        }
        if (name.is_some()) {
            add_declaration(kind,
                            name->as_str(),
                            begin,
                            end,
                            explicit_export,
                            kind != DeclarationKind::Function ||
                                tokens_[boundary].text.as_str() == "{"_str);
        } else {
            advance_comments(tokens_[begin].spelling.offset);
            auto comment = take_comment();
            if (comment.is_some()) {
                ++unit_.unsupported;
                add_diagnostic(DocumentationSeverity::Error,
                               "unsupported-documented-declaration"_str,
                               "documented declaration could not be classified"_str,
                               comment->span);
            }
        }
        if (tokens_[boundary].text.as_str() == "{"_str) {
            end = skip_balanced(boundary, "{"_str, "}"_str);
            if (end < tokens_.len() && tokens_[end].text.as_str() == ";"_str) ++end;
        }
        return end;
    }

    auto parse_tokens() -> void {
        for (auto cursor = usize {}; cursor < tokens_.len();) {
            auto text = tokens_[cursor].text.as_str();
            if (text == "}"_str) {
                if (! export_blocks_.is_empty() &&
                    export_blocks_[export_blocks_.len() - usize(1)] == brace_depth_)
                    (void)export_blocks_.pop();
                if (brace_depth_ != usize {}) --brace_depth_;
                while (! scopes_.is_empty() &&
                       scopes_[scopes_.len() - usize(1)].brace_depth > brace_depth_)
                    (void)scopes_.pop();
                ++cursor;
                if (cursor < tokens_.len() && tokens_[cursor].text.as_str() == ";"_str) ++cursor;
                continue;
            }
            if (! scopes_.is_empty() &&
                scopes_[scopes_.len() - usize(1)].kind == DeclarationKind::Record &&
                (text == "public"_str || text == "protected"_str || text == "private"_str) &&
                cursor + usize(1) < tokens_.len() &&
                tokens_[cursor + usize(1)].text.as_str() == ":"_str) {
                scopes_[scopes_.len() - usize(1)].access =
                    text == "public"_str      ? DeclarationAccess::Public
                    : text == "protected"_str ? DeclarationAccess::Protected
                                              : DeclarationAccess::Private;
                cursor += usize(2);
                continue;
            }

            auto begin           = cursor;
            auto explicit_export = false;
            if (text == "export"_str) {
                explicit_export = true;
                ++cursor;
                if (cursor < tokens_.len() && tokens_[cursor].text.as_str() == "{"_str) {
                    ++brace_depth_;
                    export_blocks_.push(usize(brace_depth_.to_primitive()));
                    ++cursor;
                    continue;
                }
                if (cursor >= tokens_.len()) break;
                text = tokens_[cursor].text.as_str();
            }

            if (text == "module"_str || text == "import"_str) {
                auto end = statement_end(cursor);
                if (explicit_export && text == "import"_str && end < tokens_.len())
                    parse_reexport(cursor + usize(1), end);
                cursor = end < tokens_.len() ? end + usize(1) : end;
                continue;
            }
            if (text == "namespace"_str) {
                auto next =
                    parse_named_scope(begin, cursor, DeclarationKind::Namespace, explicit_export);
                cursor = next.is_some() ? *next : parse_simple(begin, cursor, explicit_export);
                continue;
            }
            if (text == "class"_str || text == "struct"_str || text == "union"_str) {
                auto next =
                    parse_named_scope(begin, cursor, DeclarationKind::Record, explicit_export);
                cursor = next.is_some() ? *next : parse_simple(begin, cursor, explicit_export);
                continue;
            }
            if (text == "enum"_str) {
                auto next =
                    parse_named_scope(begin, cursor, DeclarationKind::Enum, explicit_export);
                cursor = next.is_some() ? *next : parse_simple(begin, cursor, explicit_export);
                continue;
            }
            cursor = parse_simple(begin, cursor, explicit_export);
        }
    }

    const preprocessor::PreprocessedTranslationUnit* translation_ {};
    const FrontendResult*                            dependencies_ {};
    lexical::SourceId                                main_source_ {};
    Vec<lexical::Token>                              tokens_;
    DocumentationUnit                                unit_;
    Vec<DocumentationScope>                          scopes_;
    Vec<usize>                                       export_blocks_;
    Vec<DocumentationComment>                        pending_comments_;
    Option<String>                                   pending_group_;
    Option<String>                                   active_group_;
    usize                                            comment_cursor_ {};
    usize                                            brace_depth_ {};
    usize                                            hidden_depth_ {};
};

} // namespace tenon::frontend::parser

export namespace tenon::frontend::parser
{

auto parse_documentation(const preprocessor::PreprocessedTranslationUnit& translation)
    -> lexical::Result<DocumentationUnit> {
    auto dependencies = parse_module_dependencies(translation);
    if (dependencies.is_err()) return Err(rstd::move(dependencies).unwrap_err());
    return DocumentationParser(translation, *dependencies).parse();
}

auto parse_documentation(const preprocessor::PreprocessedTranslationUnit& translation,
                         const FrontendResult& dependencies) -> lexical::Result<DocumentationUnit> {
    return DocumentationParser(translation, dependencies).parse();
}

} // namespace tenon::frontend::parser
