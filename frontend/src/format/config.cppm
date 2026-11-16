export module lito.frontend.format:config;

import rstd;
import lito.frontend.lexical;
import :style;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

namespace lito::frontend::format
{
class StyleConfigurationParser;

struct ParensOptionsPatch {
    Option<bool> except_double_parentheses;
    Option<bool> in_conditional_statements;
    Option<bool> in_c_style_casts;
    Option<bool> in_empty_parentheses;
    Option<bool> other;

    auto apply(SpacesInParensOptions base) const -> SpacesInParensOptions {
        if (except_double_parentheses.is_some())
            base.except_double_parentheses = *except_double_parentheses;
        if (in_conditional_statements.is_some())
            base.in_conditional_statements = *in_conditional_statements;
        if (in_c_style_casts.is_some()) base.in_c_style_casts = *in_c_style_casts;
        if (in_empty_parentheses.is_some()) base.in_empty_parentheses = *in_empty_parentheses;
        if (other.is_some()) base.other = *other;
        return base;
    }
};

struct BraceWrappingPatch {
    Option<bool>              after_class;
    Option<bool>              after_struct;
    Option<bool>              after_union;
    Option<bool>              after_enum;
    Option<bool>              after_namespace;
    Option<bool>              after_function;
    Option<ControlBraceStyle> after_control_statement;
    Option<bool>              before_else;
    Option<bool>              before_catch;
    Option<bool>              indent_braces;
    Option<bool>              split_empty_function;
    Option<bool>              split_empty_record;
    Option<bool>              split_empty_namespace;

    auto apply(BraceWrapping base) const -> BraceWrapping {
        if (after_class.is_some()) base.after_class = *after_class;
        if (after_struct.is_some()) base.after_struct = *after_struct;
        if (after_union.is_some()) base.after_union = *after_union;
        if (after_enum.is_some()) base.after_enum = *after_enum;
        if (after_namespace.is_some()) base.after_namespace = *after_namespace;
        if (after_function.is_some()) base.after_function = *after_function;
        if (after_control_statement.is_some())
            base.after_control_statement = *after_control_statement;
        if (before_else.is_some()) base.before_else = *before_else;
        if (before_catch.is_some()) base.before_catch = *before_catch;
        if (indent_braces.is_some()) base.indent_braces = *indent_braces;
        if (split_empty_function.is_some()) base.split_empty_function = *split_empty_function;
        if (split_empty_record.is_some()) base.split_empty_record = *split_empty_record;
        if (split_empty_namespace.is_some()) base.split_empty_namespace = *split_empty_namespace;
        return base;
    }
};
} // namespace lito::frontend::format

export namespace lito::frontend::format
{

class StyleConfiguration {
    bool                         llvm_ {};
    bool                         inherit_parent_ {};
    Option<unsigned>             indent_width_;
    Option<unsigned>             tab_width_;
    Option<UseTab>               use_tab_;
    Option<NamespaceIndentation> namespace_indentation_;
    Option<BraceStyle>           break_before_braces_;
    BraceWrappingPatch           brace_wrapping_;
    Option<SpaceBeforeParens>    space_before_parens_;
    Option<bool>                 space_before_assignment_operators_;
    Option<SpacesInParens>       spaces_in_parens_;
    ParensOptionsPatch           parens_options_;
    Option<bool>                 spaces_in_parentheses_;
    Option<bool>                 space_in_empty_parentheses_;
    Option<bool>                 spaces_in_c_style_cast_parentheses_;
    Option<bool>                 spaces_in_conditional_statement_;
    Option<int>                  access_modifier_offset_;
    Option<bool>                 indent_access_modifiers_;
    Option<ShortFunctionStyle>   short_functions_;
    Option<ShortBlockStyle>      short_blocks_;
    Option<unsigned>             max_empty_lines_to_keep_;
    Option<bool>                 keep_start_of_file_;
    Option<bool>                 keep_end_of_file_;
    Option<bool>                 keep_start_of_block_;
    Option<bool>                 keep_end_of_block_;
    Option<bool>                 legacy_keep_start_of_block_;
    Option<bool>                 legacy_keep_end_of_file_;

    friend class StyleConfigurationParser;

public:
    auto inherits_parent() const noexcept -> bool { return inherit_parent_; }

    auto apply(FormatStyle base = {}) const -> FormatStyle {
        if (llvm_) base = FormatStyle::llvm();
        if (indent_width_.is_some()) base.indent_width = *indent_width_;
        if (tab_width_.is_some()) base.tab_width = *tab_width_;
        if (use_tab_.is_some()) base.use_tab = *use_tab_;
        if (namespace_indentation_.is_some()) base.namespace_indentation = *namespace_indentation_;
        if (break_before_braces_.is_some()) base.break_before_braces = *break_before_braces_;
        base.brace_wrapping = brace_wrapping_.apply(base.brace_wrapping);
        if (space_before_parens_.is_some()) base.space_before_parens = *space_before_parens_;
        if (space_before_assignment_operators_.is_some())
            base.space_before_assignment_operators = *space_before_assignment_operators_;
        if (spaces_in_parens_.is_some()) base.spaces_in_parens = *spaces_in_parens_;
        base.spaces_in_parens_options = parens_options_.apply(base.spaces_in_parens_options);
        auto other                    = spaces_in_parentheses_.is_some() && *spaces_in_parentheses_;
        auto empty = space_in_empty_parentheses_.is_some() && *space_in_empty_parentheses_;
        auto casts =
            spaces_in_c_style_cast_parentheses_.is_some() && *spaces_in_c_style_cast_parentheses_;
        auto conditional =
            spaces_in_conditional_statement_.is_some() && *spaces_in_conditional_statement_;
        if (base.spaces_in_parens != SpacesInParens::Custom &&
            (other || empty || casts || conditional)) {
            base.spaces_in_parens         = SpacesInParens::Custom;
            base.spaces_in_parens_options = SpacesInParensOptions {
                .in_conditional_statements = other || conditional,
                .in_c_style_casts          = casts,
                .in_empty_parentheses      = empty,
                .other                     = other,
            };
        }
        if (access_modifier_offset_.is_some())
            base.access_modifier_offset = *access_modifier_offset_;
        if (indent_access_modifiers_.is_some())
            base.indent_access_modifiers = *indent_access_modifiers_;
        if (short_functions_.is_some()) base.short_functions = *short_functions_;
        if (short_blocks_.is_some()) base.short_blocks = *short_blocks_;
        if (max_empty_lines_to_keep_.is_some())
            base.max_empty_lines_to_keep = *max_empty_lines_to_keep_;
        if (legacy_keep_start_of_block_.is_some())
            base.keep_empty_lines.at_start_of_block = *legacy_keep_start_of_block_;
        if (legacy_keep_end_of_file_.is_some())
            base.keep_empty_lines.at_end_of_file = *legacy_keep_end_of_file_;
        if (keep_start_of_file_.is_some())
            base.keep_empty_lines.at_start_of_file = *keep_start_of_file_;
        if (keep_end_of_file_.is_some()) base.keep_empty_lines.at_end_of_file = *keep_end_of_file_;
        if (keep_start_of_block_.is_some())
            base.keep_empty_lines.at_start_of_block = *keep_start_of_block_;
        if (keep_end_of_block_.is_some())
            base.keep_empty_lines.at_end_of_block = *keep_end_of_block_;
        return base;
    }
};

auto parse_style_configuration(ref<str> text) -> lexical::Result<StyleConfiguration>;

} // namespace lito::frontend::format

namespace lito::frontend::format
{

enum class StyleGroup
{
    Root,
    KeepEmptyLines,
    SpacesInParensOptions,
    BraceWrapping,
};

auto style_group_name(StyleGroup group) -> ref<str> {
    switch (group) {
    case StyleGroup::KeepEmptyLines: return "KeepEmptyLines"_str;
    case StyleGroup::SpacesInParensOptions: return "SpacesInParensOptions"_str;
    case StyleGroup::BraceWrapping: return "BraceWrapping"_str;
    default: return ""_str;
    }
}

class StyleConfigurationParser {
    ref<str>                                   text_;
    StyleConfiguration                         configuration_;
    rstd::collections::BTreeMap<String, empty> seen_;
    usize                                      offset_ {};
    usize                                      line_ { 1 };
    usize                                      column_ { 1 };
    bool                                       document_started_ {};
    bool                                       document_ended_ {};
    bool                                       fields_ {};
    StyleGroup                                 group_ { StyleGroup::Root };
    bool                                       group_has_fields_ {};
    usize                                      group_line_ {};
    Option<usize>                              child_indent_;

    auto failure(String message) const -> lexical::Error {
        return lexical::Error::at(rstd::move(message),
                                  lexical::SourceLocation {
                                      .offset = offset_,
                                      .line   = line_,
                                      .column = column_,
                                  });
    }

    auto failure(ref<str> message) const -> lexical::Error {
        return failure(String::make(message));
    }

    auto unique(ref<str> key) -> lexical::Result<empty> {
        if (seen_.contains_key(key))
            return Err(failure(rstd::format("duplicate format option '{}'", key)));
        seen_.insert(String::make(key), empty {});
        return Ok(empty {});
    }

    static auto without_comment(ref<str> text) -> ref<str> {
        auto quote = u8 {};
        for (usize index {}; index < text.len(); ++index) {
            auto value = text.as_bytes()[index];
            if (quote == u8('"') && value == u8('\\')) {
                if (index + usize(1) < text.len()) ++index;
            } else if (quote != u8 {}) {
                if (value != quote) continue;
                if (quote == u8('\'') && index + usize(1) < text.len() &&
                    text.as_bytes()[index + usize(1)] == quote)
                    ++index;
                else
                    quote = u8 {};
            } else if (value == u8('"') || value == u8('\'')) {
                quote = value;
            } else if (value == u8('#') &&
                       (index == usize {} || text.as_bytes()[index - usize(1)] == u8(' ') ||
                        text.as_bytes()[index - usize(1)] == u8('\t'))) {
                return text.get(usize {}, index).unwrap();
            }
        }
        return text;
    }

    auto scalar(ref<str> text, ref<str> key) const -> lexical::Result<String> {
        text = text.trim_ascii();
        if (text.is_empty()) return Err(failure(rstd::format("option '{}' requires a value", key)));
        auto first = text.as_bytes()[usize {}];
        if (first != u8('"') && first != u8('\'')) {
            if (first == u8('{') || first == u8('[') || first == u8('&') || first == u8('*') ||
                first == u8('!') || first == u8('|') || first == u8('>'))
                return Err(failure(rstd::format("unsupported YAML value for option '{}'", key)));
            return Ok(String::make(text));
        }
        auto value = Vec<u8>::make();
        for (auto index = usize(1); index < text.len(); ++index) {
            auto byte = text.as_bytes()[index];
            if (byte == first) {
                if (first == u8('\'') && index + usize(1) < text.len() &&
                    text.as_bytes()[index + usize(1)] == first) {
                    value.push(u8('\''));
                    ++index;
                    continue;
                }
                if (! text.get(index + usize(1), text.len()).unwrap().trim_ascii().is_empty())
                    return Err(failure(rstd::format("trailing YAML text for option '{}'", key)));
                return Ok(String::from_utf8(rstd::move(value)).unwrap());
            }
            if (first == u8('"') && byte == u8('\\')) {
                if (++index == text.len())
                    return Err(
                        failure(rstd::format("unterminated quoted value for option '{}'", key)));
                byte = text.as_bytes()[index];
                if (byte == u8('n'))
                    byte = u8('\n');
                else if (byte == u8('r'))
                    byte = u8('\r');
                else if (byte == u8('t'))
                    byte = u8('\t');
                else if (byte != u8('"') && byte != u8('\\'))
                    return Err(
                        failure(rstd::format("unsupported YAML escape for option '{}'", key)));
            }
            value.push(rstd::move(byte));
        }
        return Err(failure(rstd::format("unterminated quoted value for option '{}'", key)));
    }

    auto number(ref<str> value, ref<str> key) const -> lexical::Result<unsigned> {
        if (value.is_empty())
            return Err(
                failure(rstd::format("option '{}' requires a decimal unsigned integer", key)));
        unsigned result {};
        for (auto byte : value.as_bytes()) {
            if (byte < u8('0') || byte > u8('9'))
                return Err(
                    failure(rstd::format("option '{}' requires a decimal unsigned integer", key)));
            auto digit = static_cast<unsigned>(byte.to_primitive() - '0');
            if (result > (unsigned(-1) - digit) / 10)
                return Err(
                    failure(rstd::format("unsigned value for option '{}' is out of range", key)));
            result = result * 10 + digit;
        }
        return Ok(result);
    }

    auto boolean(ref<str> value, ref<str> key) const -> lexical::Result<bool> {
        if (value == "true"_str || value == "True"_str || value == "TRUE"_str) return Ok(true);
        if (value == "false"_str || value == "False"_str || value == "FALSE"_str) return Ok(false);
        return Err(failure(rstd::format("option '{}' requires true or false", key)));
    }

    auto signed_number(ref<str> value, ref<str> key) const -> lexical::Result<int> {
        if (value.is_empty())
            return Err(failure(rstd::format("option '{}' requires a signed decimal integer", key)));
        auto negative = value.starts_with("-"_str);
        if (negative || value.starts_with("+"_str))
            value = value.get(usize(1), value.len()).unwrap();
        auto magnitude = number(value, key);
        if (magnitude.is_err()) return Err(rstd::move(magnitude).unwrap_err());
        constexpr auto maximum = unsigned(-1) / 2;
        if (*magnitude > maximum + (negative ? 1u : 0u))
            return Err(failure(rstd::format("signed value for option '{}' is out of range", key)));
        if (negative && *magnitude == maximum + 1u) return Ok(-static_cast<int>(maximum) - 1);
        return Ok(negative ? -static_cast<int>(*magnitude) : static_cast<int>(*magnitude));
    }

    auto boolean_field(ref<str> key, StyleGroup group) -> Option<bool>* {
        auto& result = configuration_;
        if (group == StyleGroup::KeepEmptyLines) {
            if (key == "AtStartOfFile"_str) return rstd::addressof(result.keep_start_of_file_);
            if (key == "AtEndOfFile"_str) return rstd::addressof(result.keep_end_of_file_);
            if (key == "AtStartOfBlock"_str) return rstd::addressof(result.keep_start_of_block_);
            if (key == "AtEndOfBlock"_str) return rstd::addressof(result.keep_end_of_block_);
        } else if (group == StyleGroup::SpacesInParensOptions) {
            if (key == "ExceptDoubleParentheses"_str)
                return rstd::addressof(result.parens_options_.except_double_parentheses);
            if (key == "InConditionalStatements"_str)
                return rstd::addressof(result.parens_options_.in_conditional_statements);
            if (key == "InCStyleCasts"_str)
                return rstd::addressof(result.parens_options_.in_c_style_casts);
            if (key == "InEmptyParentheses"_str)
                return rstd::addressof(result.parens_options_.in_empty_parentheses);
            if (key == "Other"_str) return rstd::addressof(result.parens_options_.other);
        } else if (group == StyleGroup::BraceWrapping) {
            auto& wrapping = result.brace_wrapping_;
            if (key == "AfterClass"_str) return rstd::addressof(wrapping.after_class);
            if (key == "AfterStruct"_str) return rstd::addressof(wrapping.after_struct);
            if (key == "AfterUnion"_str) return rstd::addressof(wrapping.after_union);
            if (key == "AfterEnum"_str) return rstd::addressof(wrapping.after_enum);
            if (key == "AfterNamespace"_str) return rstd::addressof(wrapping.after_namespace);
            if (key == "AfterFunction"_str) return rstd::addressof(wrapping.after_function);
            if (key == "BeforeElse"_str) return rstd::addressof(wrapping.before_else);
            if (key == "BeforeCatch"_str) return rstd::addressof(wrapping.before_catch);
            if (key == "IndentBraces"_str) return rstd::addressof(wrapping.indent_braces);
            if (key == "SplitEmptyFunction"_str)
                return rstd::addressof(wrapping.split_empty_function);
            if (key == "SplitEmptyRecord"_str) return rstd::addressof(wrapping.split_empty_record);
            if (key == "SplitEmptyNamespace"_str)
                return rstd::addressof(wrapping.split_empty_namespace);
        } else {
            if (key == "SpaceBeforeAssignmentOperators"_str)
                return rstd::addressof(result.space_before_assignment_operators_);
            if (key == "SpacesInParentheses"_str)
                return rstd::addressof(result.spaces_in_parentheses_);
            if (key == "SpaceInEmptyParentheses"_str)
                return rstd::addressof(result.space_in_empty_parentheses_);
            if (key == "SpacesInCStyleCastParentheses"_str)
                return rstd::addressof(result.spaces_in_c_style_cast_parentheses_);
            if (key == "SpacesInConditionalStatement"_str)
                return rstd::addressof(result.spaces_in_conditional_statement_);
            if (key == "IndentAccessModifiers"_str)
                return rstd::addressof(result.indent_access_modifiers_);
            if (key == "KeepEmptyLinesAtTheStartOfBlocks"_str)
                return rstd::addressof(result.legacy_keep_start_of_block_);
            if (key == "KeepEmptyLinesAtEOF"_str)
                return rstd::addressof(result.legacy_keep_end_of_file_);
        }
        return nullptr;
    }

    auto set(ref<str> key, ref<str> value, StyleGroup group) -> lexical::Result<empty> {
        auto canonical = group == StyleGroup::Root
                             ? String::make(key)
                             : rstd::format("{}.{}", style_group_name(group), key);
        auto distinct  = unique(canonical.as_str());
        if (distinct.is_err()) return distinct;
        auto decoded = scalar(value, canonical.as_str());
        if (decoded.is_err()) return Err(rstd::move(decoded).unwrap_err());
        auto  text   = decoded->as_str();
        auto& result = configuration_;
        if (auto field = boolean_field(key, group); field != nullptr) {
            auto parsed = boolean(text, canonical.as_str());
            if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
            *field = Some(*parsed);
        } else if (group == StyleGroup::BraceWrapping && key == "AfterControlStatement"_str) {
            if (text == "Never"_str || text == "false"_str)
                result.brace_wrapping_.after_control_statement = Some(ControlBraceStyle::Never);
            else if (text == "MultiLine"_str)
                result.brace_wrapping_.after_control_statement = Some(ControlBraceStyle::MultiLine);
            else if (text == "Always"_str || text == "true"_str)
                result.brace_wrapping_.after_control_statement = Some(ControlBraceStyle::Always);
            else
                return unsupported_value(canonical.as_str(), text);
        } else if (group != StyleGroup::Root) {
            return Err(failure(rstd::format("unsupported format option '{}'", canonical.as_str())));
        } else if (key == "IndentWidth"_str || key == "TabWidth"_str ||
                   key == "MaxEmptyLinesToKeep"_str || key == "ColumnLimit"_str) {
            auto parsed = number(text, key);
            if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
            if (key == "IndentWidth"_str)
                result.indent_width_ = Some(*parsed);
            else if (key == "TabWidth"_str) {
                if (*parsed == 0) return Err(failure("option 'TabWidth' must be nonzero"_str));
                result.tab_width_ = Some(*parsed);
            } else if (key == "MaxEmptyLinesToKeep"_str)
                result.max_empty_lines_to_keep_ = Some(*parsed);
            else if (*parsed != 0)
                return Err(failure("builtin format only supports ColumnLimit: 0"_str));
        } else if (key == "AccessModifierOffset"_str) {
            auto parsed = signed_number(text, key);
            if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
            result.access_modifier_offset_ = Some(*parsed);
        } else if (key == "SpacesInParens"_str) {
            if (text == "Never"_str)
                result.spaces_in_parens_ = Some(SpacesInParens::Never);
            else if (text == "Custom"_str)
                result.spaces_in_parens_ = Some(SpacesInParens::Custom);
            else
                return unsupported_value(key, text);
        } else if (key == "UseTab"_str) {
            if (text == "Never"_str)
                result.use_tab_ = Some(UseTab::Never);
            else if (text == "ForIndentation"_str)
                result.use_tab_ = Some(UseTab::ForIndentation);
            else
                return unsupported_value(key, text);
        } else if (key == "NamespaceIndentation"_str) {
            if (text == "None"_str)
                result.namespace_indentation_ = Some(NamespaceIndentation::None);
            else if (text == "Inner"_str)
                result.namespace_indentation_ = Some(NamespaceIndentation::Inner);
            else if (text == "All"_str)
                result.namespace_indentation_ = Some(NamespaceIndentation::All);
            else
                return unsupported_value(key, text);
        } else if (key == "BreakBeforeBraces"_str) {
            if (text == "Attach"_str)
                result.break_before_braces_ = Some(BraceStyle::Attach);
            else if (text == "Allman"_str)
                result.break_before_braces_ = Some(BraceStyle::Allman);
            else if (text == "Custom"_str)
                result.break_before_braces_ = Some(BraceStyle::Custom);
            else
                return unsupported_value(key, text);
        } else if (key == "SpaceBeforeParens"_str) {
            if (text == "Never"_str)
                result.space_before_parens_ = Some(SpaceBeforeParens::Never);
            else if (text == "ControlStatements"_str)
                result.space_before_parens_ = Some(SpaceBeforeParens::ControlStatements);
            else if (text == "Always"_str)
                result.space_before_parens_ = Some(SpaceBeforeParens::Always);
            else
                return unsupported_value(key, text);
        } else if (key == "AllowShortFunctionsOnASingleLine"_str) {
            if (text == "None"_str || text == "false"_str)
                result.short_functions_ = Some(ShortFunctionStyle::None);
            else if (text == "Empty"_str)
                result.short_functions_ = Some(ShortFunctionStyle::Empty);
            else if (text == "All"_str || text == "true"_str)
                result.short_functions_ = Some(ShortFunctionStyle::All);
            else
                return unsupported_value(key, text);
        } else if (key == "AllowShortBlocksOnASingleLine"_str) {
            if (text == "Never"_str || text == "false"_str)
                result.short_blocks_ = Some(ShortBlockStyle::Never);
            else if (text == "Empty"_str)
                result.short_blocks_ = Some(ShortBlockStyle::Empty);
            else if (text == "Always"_str || text == "true"_str)
                result.short_blocks_ = Some(ShortBlockStyle::Always);
            else
                return unsupported_value(key, text);
        } else if (key == "Language"_str) {
            if (text != "Cpp"_str) return unsupported_value(key, text);
        } else if (key == "BasedOnStyle"_str) {
            if (text == "LLVM"_str)
                result.llvm_ = true;
            else if (text == "InheritParentConfig"_str)
                result.inherit_parent_ = true;
            else
                return unsupported_value(key, text);
        } else {
            return Err(failure(rstd::format("unsupported format option '{}'", key)));
        }
        return Ok(empty {});
    }

    auto unsupported_value(ref<str> key, ref<str> value) const -> lexical::Result<empty> {
        return Err(
            failure(rstd::format("unsupported value '{}' for format option '{}'", value, key)));
    }

    auto finish_mapping() -> lexical::Result<empty> {
        if (group_ != StyleGroup::Root && ! group_has_fields_)
            return Err(failure(rstd::format("option '{}' at line {} requires a mapping",
                                            style_group_name(group_),
                                            group_line_)));
        group_        = StyleGroup::Root;
        child_indent_ = None();
        return Ok(empty {});
    }

    auto parse_line(ref<str> line) -> lexical::Result<empty> {
        for (auto byte : line.as_bytes()) {
            if (byte < u8(0x20) && byte != u8('\t'))
                return Err(failure("unsupported control character in format configuration"_str));
        }
        auto indent = usize {};
        while (indent < line.len() && line.as_bytes()[indent] == u8(' ')) ++indent;
        column_   = indent + usize(1);
        auto body = without_comment(line.get(indent, line.len()).unwrap()).trim_ascii();
        if (body.is_empty()) return Ok(empty {});
        if (indent < line.len() && line.as_bytes()[indent] == u8('\t'))
            return Err(failure("YAML indentation must use spaces"_str));
        if (body == "---"_str || body == "..."_str) {
            if (indent != usize {})
                return Err(failure("document markers must start at column one"_str));
            if (body == "---"_str) {
                if (document_started_ || document_ended_ || fields_)
                    return Err(failure("multiple YAML documents are not supported"_str));
                document_started_ = true;
            } else {
                if (document_ended_) return Err(failure("duplicate YAML document end"_str));
                document_ended_ = true;
            }
            return finish_mapping();
        }
        if (document_ended_) return Err(failure("unexpected content after YAML document end"_str));
        if (body.starts_with("-"_str) || body.starts_with("{"_str) || body.starts_with("["_str))
            return Err(failure("only YAML block mappings are supported"_str));
        auto colon = body.find(":"_str);
        if (colon.is_none()) return Err(failure("expected a format option followed by ':'"_str));
        auto value_offset = *colon + usize(1);
        if (value_offset < body.len() && body.as_bytes()[value_offset] != u8(' ') &&
            body.as_bytes()[value_offset] != u8('\t'))
            return Err(failure("expected whitespace after ':'"_str));
        auto key = scalar(body.get(usize {}, *colon).unwrap(), "key"_str);
        if (key.is_err()) return Err(rstd::move(key).unwrap_err());
        auto value = body.get(value_offset, body.len()).unwrap().trim_ascii();
        if (indent != usize {}) {
            if (group_ == StyleGroup::Root)
                return Err(
                    failure(rstd::format("unexpected nested format option '{}'", key->as_str())));
            if (child_indent_.is_some() && *child_indent_ != indent)
                return Err(failure(
                    rstd::format("inconsistent {} mapping indentation", style_group_name(group_))));
            child_indent_ = Some(indent);
            auto result   = set(key->as_str(), value, group_);
            if (result.is_ok()) group_has_fields_ = true;
            return result;
        }
        auto finished = finish_mapping();
        if (finished.is_err()) return finished;
        fields_    = true;
        auto group = StyleGroup::Root;
        if (key->as_str() == "KeepEmptyLines"_str)
            group = StyleGroup::KeepEmptyLines;
        else if (key->as_str() == "SpacesInParensOptions"_str)
            group = StyleGroup::SpacesInParensOptions;
        else if (key->as_str() == "BraceWrapping"_str)
            group = StyleGroup::BraceWrapping;
        if (group != StyleGroup::Root) {
            auto distinct = unique(key->as_str());
            if (distinct.is_err()) return distinct;
            if (! value.is_empty())
                return Err(
                    failure(rstd::format("option '{}' requires a block mapping", key->as_str())));
            group_            = group;
            group_has_fields_ = false;
            group_line_       = line_;
            return Ok(empty {});
        }
        return set(key->as_str(), value, StyleGroup::Root);
    }

public:
    explicit StyleConfigurationParser(ref<str> text): text_(text) {}

    auto parse() -> lexical::Result<StyleConfiguration> {
        if (text_.starts_with("\xef\xbb\xbf"_str)) offset_ = usize(3);
        while (offset_ < text_.len()) {
            auto next = lexical::physical_line_end(text_.as_bytes(), offset_);
            auto end  = next;
            while (end > offset_ && (text_.as_bytes()[end - usize(1)] == u8('\r') ||
                                     text_.as_bytes()[end - usize(1)] == u8('\n')))
                --end;
            auto result = parse_line(text_.get(offset_, end).unwrap());
            if (result.is_err()) return Err(rstd::move(result).unwrap_err());
            offset_ = next;
            ++line_;
        }
        auto finished = finish_mapping();
        if (finished.is_err()) return Err(rstd::move(finished).unwrap_err());
        return Ok(rstd::move(configuration_));
    }
};

struct PendingStyleDirectory {
    String                     directory;
    Option<StyleConfiguration> configuration;
};

auto load_style_configuration(ref<rstd::path::Path> directory)
    -> lexical::Result<Option<StyleConfiguration>> {
    const ref<str> names[] = { ".clang-format"_str, "_clang-format"_str };
    for (auto name : names) {
        auto path     = PathBuf::from(directory).join(PathBuf::from(name).as_path());
        auto metadata = rstd::fs::metadata(path.as_path());
        if (metadata.is_err()) {
            auto error = rstd::move(metadata).unwrap_err();
            if (error.kind() == rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound })
                continue;
            return Err(lexical::Error {
                .message = rstd::format("cannot inspect format configuration: {}", error),
                .path    = Some(rstd::move(path)),
            });
        }
        if (! metadata->is_file()) continue;
        auto contents = rstd::fs::read_to_string(path.as_path());
        if (contents.is_err())
            return Err(lexical::Error {
                .message = rstd::format("cannot read format configuration: {}",
                                        rstd::move(contents).unwrap_err()),
                .path    = Some(rstd::move(path)),
            });
        auto parsed = parse_style_configuration(contents->as_str());
        if (parsed.is_err()) {
            auto error = rstd::move(parsed).unwrap_err();
            error.path = Some(rstd::move(path));
            return Err(rstd::move(error));
        }
        return Ok(Some(rstd::move(parsed).unwrap()));
    }
    return Ok(None());
}

} // namespace lito::frontend::format

auto lito::frontend::format::parse_style_configuration(ref<str> text)
    -> lexical::Result<StyleConfiguration> {
    return StyleConfigurationParser(text).parse();
}

export namespace lito::frontend::format
{

class StyleResolver {
    rstd::collections::BTreeMap<String, FormatStyle> directories_;

public:
    auto for_source(ref<rstd::path::Path> source) -> lexical::Result<FormatStyle> {
        auto directory = PathBuf::from(source);
        if (directory.as_path().is_relative()) {
            auto absolute = rstd::fs::canonicalize(source);
            if (absolute.is_err())
                return Err(lexical::Error {
                    .message = rstd::format("cannot resolve format source: {}",
                                            rstd::move(absolute).unwrap_err()),
                    .path    = Some(PathBuf::from(source)),
                });
            directory = rstd::move(absolute).unwrap();
        }
        if (! directory.pop())
            return Err(lexical::Error {
                .message = "format source must have a parent directory"_Str,
                .path    = Some(PathBuf::from(source)),
            });
        auto pending = Vec<PendingStyleDirectory>::make();
        auto style   = FormatStyle {};
        for (;;) {
            auto text = directory.as_path().to_str();
            if (text.is_none())
                return Err(lexical::Error {
                    .message = "format configuration directory is not UTF-8"_Str,
                    .path    = Some(directory.clone()),
                });
            auto cached = directories_.get(*text);
            if (cached.is_some()) {
                style = **cached;
                break;
            }
            auto configuration = load_style_configuration(directory.as_path());
            if (configuration.is_err()) return Err(rstd::move(configuration).unwrap_err());
            if (configuration->is_some() && ! (**configuration).inherits_parent()) {
                style = (**configuration).apply();
                directories_.insert(String::make(*text), style);
                break;
            }
            pending.push(PendingStyleDirectory {
                .directory     = String::make(*text),
                .configuration = rstd::move(configuration).unwrap(),
            });
            if (! directory.pop()) break;
        }
        for (auto index = pending.len(); index != usize {};) {
            auto& entry = pending[--index];
            if (entry.configuration.is_some()) style = entry.configuration->apply(style);
            directories_.insert(rstd::move(entry.directory), style);
        }
        return Ok(style);
    }
};

} // namespace lito::frontend::format
