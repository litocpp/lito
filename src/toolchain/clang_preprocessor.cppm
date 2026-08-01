export module tenon.toolchain:clang_preprocessor;

import rstd;
import tenon.model;

using namespace rstd::prelude;
using namespace rstd::literals;
using StringSet = rstd::collections::BTreeMap<String, empty>;

namespace tenon::toolchain
{

template<typename T>
auto preprocess_failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Toolchain, rstd::move(message)));
}

template<typename T>
auto preprocess_failure(ref<str> message) -> Result<T> {
    return Err(Error::make(ErrorKind::Toolchain, message));
}

enum class TokenKind
{
    Identifier,
    Punctuation,
    Literal,
};

struct Token {
    TokenKind kind { TokenKind::Punctuation };
    String    text;
    PathBuf   path;
    usize     line {};
};

auto identifier_start(u8 value) noexcept -> bool {
    return (value >= u8('a') && value <= u8('z')) || (value >= u8('A') && value <= u8('Z')) ||
           value == u8('_');
}

auto identifier_continue(u8 value) noexcept -> bool {
    return identifier_start(value) || (value >= u8('0') && value <= u8('9'));
}

auto append_token(Vec<Token>&           tokens,
                  TokenKind             kind,
                  String                text,
                  ref<rstd::path::Path> path,
                  usize                 line) -> void {
    tokens.push(Token {
        .kind = kind,
        .text = rstd::move(text),
        .path = PathBuf::from(path),
        .line = line,
    });
}

auto lex_preprocessed(ref<str> output, ref<rstd::path::Path> source) -> Result<Vec<Token>> {
    auto tokens       = Vec<Token>::make();
    auto bytes        = output.as_bytes();
    auto current_path = PathBuf::from(source);
    auto source_line  = usize(1);
    auto line_start   = true;
    auto index        = usize {};

    while (index < output.len()) {
        const auto value = bytes[index];
        if (line_start && (value == u8(' ') || value == u8('\t'))) {
            ++index;
            continue;
        }
        if (line_start && value == u8('#')) {
            ++index;
            while (index < output.len() && (bytes[index] == u8(' ') || bytes[index] == u8('\t'))) {
                ++index;
            }
            auto marker_line = usize {};
            auto has_line    = false;
            while (index < output.len() && bytes[index] >= u8('0') && bytes[index] <= u8('9')) {
                marker_line =
                    marker_line * usize(10) + usize((bytes[index] - u8('0')).to_primitive());
                has_line = true;
                ++index;
            }
            while (index < output.len() && bytes[index] != u8('"') && bytes[index] != u8('\n')) {
                ++index;
            }
            auto marker_path = String::make();
            if (index < output.len() && bytes[index] == u8('"')) {
                ++index;
                while (index < output.len() && bytes[index] != u8('"')) {
                    if (bytes[index] == u8('\\') && index + usize(1) < output.len()) ++index;
                    marker_path.push_ascii(bytes[index]);
                    ++index;
                }
            }
            while (index < output.len() && bytes[index] != u8('\n')) ++index;
            if (! marker_path.is_empty()) current_path = PathBuf::from(marker_path.as_str());
            if (has_line) source_line = marker_line;
            if (index < output.len()) ++index;
            line_start = true;
            continue;
        }
        line_start = false;
        if (value == u8('\n')) {
            ++source_line;
            ++index;
            line_start = true;
            continue;
        }
        if (value == u8(' ') || value == u8('\t') || value == u8('\r') || value == u8('\f')) {
            ++index;
            continue;
        }
        if (value == u8('/') && index + usize(1) < output.len() &&
            bytes[index + usize(1)] == u8('/')) {
            index += usize(2);
            while (index < output.len() && bytes[index] != u8('\n')) ++index;
            continue;
        }
        if (value == u8('/') && index + usize(1) < output.len() &&
            bytes[index + usize(1)] == u8('*')) {
            index += usize(2);
            while (index + usize(1) < output.len() &&
                   ! (bytes[index] == u8('*') && bytes[index + usize(1)] == u8('/'))) {
                if (bytes[index] == u8('\n')) {
                    ++source_line;
                    line_start = true;
                }
                ++index;
            }
            if (index + usize(1) < output.len()) index += usize(2);
            continue;
        }
        if (value == u8('"') || value == u8('\'')) {
            const auto quote = value;
            auto       text  = String::make();
            text.push_ascii(value);
            ++index;
            while (index < output.len()) {
                const auto literal = bytes[index];
                if (literal == u8('\\') && index + usize(1) < output.len()) {
                    index += usize(2);
                    continue;
                }
                ++index;
                if (literal == quote) break;
                if (literal == u8('\n')) {
                    ++source_line;
                    line_start = true;
                }
            }
            append_token(
                tokens, TokenKind::Literal, rstd::move(text), current_path.as_path(), source_line);
            continue;
        }
        if (identifier_start(value)) {
            auto text = String::make();
            while (index < output.len() && identifier_continue(bytes[index])) {
                text.push_ascii(bytes[index]);
                ++index;
            }
            const auto raw_prefix = text.as_str() == "R"_str || text.as_str() == "u8R"_str ||
                                    text.as_str() == "uR"_str || text.as_str() == "UR"_str ||
                                    text.as_str() == "LR"_str;
            if (raw_prefix && index < output.len() && bytes[index] == u8('"')) {
                ++index;
                auto delimiter = String::make();
                while (index < output.len() && bytes[index] != u8('(') &&
                       bytes[index] != u8('\n')) {
                    delimiter.push_ascii(bytes[index]);
                    ++index;
                }
                if (index < output.len() && bytes[index] == u8('(')) ++index;
                while (index < output.len()) {
                    if (bytes[index] == u8('\n')) ++source_line;
                    if (bytes[index] == u8(')')) {
                        auto matches         = true;
                        auto delimiter_bytes = delimiter.as_str().as_bytes();
                        for (auto offset = usize {}; offset < delimiter.len(); ++offset) {
                            if (index + usize(1) + offset >= output.len() ||
                                bytes[index + usize(1) + offset] != delimiter_bytes[offset]) {
                                matches = false;
                                break;
                            }
                        }
                        const auto quote = index + usize(1) + delimiter.len();
                        if (matches && quote < output.len() && bytes[quote] == u8('"')) {
                            index = quote + usize(1);
                            break;
                        }
                    }
                    ++index;
                }
                append_token(tokens,
                             TokenKind::Literal,
                             String::make("\""_str),
                             current_path.as_path(),
                             source_line);
                continue;
            }
            append_token(tokens,
                         TokenKind::Identifier,
                         rstd::move(text),
                         current_path.as_path(),
                         source_line);
            continue;
        }
        auto punctuation = String::make();
        punctuation.push_ascii(value);
        append_token(tokens,
                     TokenKind::Punctuation,
                     rstd::move(punctuation),
                     current_path.as_path(),
                     source_line);
        ++index;
    }
    return Ok(rstd::move(tokens));
}

struct ParsedName {
    String value;
    usize  next {};
};

auto parse_name(const Vec<Token>& tokens, usize start, ref<str> context)
    -> Result<Option<ParsedName>> {
    if (start >= tokens.len()) return Ok(None());
    if (tokens[start].text.as_str() == "<"_str || tokens[start].kind == TokenKind::Literal) {
        return preprocess_failure<Option<ParsedName>>(
            rstd::format("{} uses an unsupported header unit at {}:{}",
                         context,
                         tokens[start].path.as_path(),
                         tokens[start].line));
    }
    auto index  = start;
    auto result = String::make();
    if (tokens[index].text.as_str() == ":"_str) {
        result.push_ascii(':');
        ++index;
    }
    if (index >= tokens.len() || tokens[index].kind != TokenKind::Identifier) {
        return Ok(None());
    }
    result.push_str(tokens[index].text.as_str());
    ++index;
    while (index + usize(1) < tokens.len() &&
           (tokens[index].text.as_str() == "."_str || tokens[index].text.as_str() == ":"_str) &&
           tokens[index + usize(1)].kind == TokenKind::Identifier) {
        result.push_str(tokens[index].text.as_str());
        result.push_str(tokens[index + usize(1)].text.as_str());
        index += usize(2);
    }
    if (index >= tokens.len() || tokens[index].text.as_str() != ";"_str) return Ok(None());
    return Ok(Some(ParsedName { .value = rstd::move(result), .next = index + usize(1) }));
}

auto primary_module(ref<str> declared) -> String {
    auto result = String::make();
    for (auto value : declared) {
        if (value == u8(':')) break;
        result.push_ascii(value);
    }
    return result;
}

auto normalized_import(ref<str> imported, ref<str> declared) -> Result<String> {
    if (imported.is_empty() || imported[usize {}] != u8(':')) {
        return Ok(String::make(imported));
    }
    if (declared.is_empty()) {
        return preprocess_failure<String>(
            "relative partition import appears before a named module declaration"_str);
    }
    auto result = primary_module(declared);
    result.push_str(imported);
    return Ok(rstd::move(result));
}

auto contains_name(const Vec<String>& values, ref<str> name) -> bool {
    for (const auto& value : values) {
        if (value.as_str() == name) return true;
    }
    return false;
}

} // namespace tenon::toolchain

export namespace tenon::toolchain
{

auto parse_preprocessed_module(ref<str> output, ref<rstd::path::Path> source)
    -> Result<PreprocessedModuleFacts> {
    auto lexed = lex_preprocessed(output, source);
    if (lexed.is_err()) return Err(rstd::move(lexed).unwrap_err());
    auto tokens = rstd::move(lexed).unwrap();
    auto facts  = PreprocessedModuleFacts {
        .source       = PathBuf::from(source),
        .output_bytes = output.len(),
    };
    auto declared     = String::make();
    auto import_names = StringSet::make();
    auto brace_depth  = usize {};
    auto index        = usize {};
    while (index < tokens.len()) {
        const auto& token = tokens[index];
        if (token.text.as_str() == "{"_str) {
            ++brace_depth;
            ++index;
            continue;
        }
        if (token.text.as_str() == "}"_str) {
            if (brace_depth != usize {}) --brace_depth;
            ++index;
            continue;
        }
        if (brace_depth != usize {}) {
            ++index;
            continue;
        }

        auto exported    = false;
        auto keyword     = token.text.as_str();
        auto declaration = index;
        if (keyword == "export"_str && index + usize(1) < tokens.len()) {
            const auto next = tokens[index + usize(1)].text.as_str();
            if (next == "module"_str || next == "import"_str) {
                exported    = true;
                keyword     = next;
                declaration = index + usize(1);
            }
        }
        if (keyword == "module"_str) {
            if (declaration + usize(1) < tokens.len() &&
                tokens[declaration + usize(1)].text.as_str() == ";"_str) {
                index = declaration + usize(2);
                continue;
            }
            auto parsed = parse_name(tokens, declaration + usize(1), "module declaration"_str);
            if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
            if (parsed->is_none()) {
                ++index;
                continue;
            }
            auto name = rstd::move(*parsed).unwrap();
            if (name.value.as_str() == ":private"_str) {
                index = name.next;
                continue;
            }
            if (! declared.is_empty()) {
                return preprocess_failure<PreprocessedModuleFacts>(
                    rstd::format("multiple named module declarations in '{}'", source));
            }
            declared = name.value.clone();
            if (exported) {
                facts.provided = Some(ProvidedModule {
                    .logical_name = name.value.clone(),
                    .is_interface = true,
                });
            } else if (name.value.as_str().contains(":"_str)) {
                facts.provided = Some(ProvidedModule {
                    .logical_name = name.value.clone(),
                    .is_interface = false,
                });
            } else {
                facts.implementation_module = Some(name.value.clone());
            }
            index = name.next;
            continue;
        }
        if (keyword == "import"_str) {
            auto parsed = parse_name(tokens, declaration + usize(1), "import declaration"_str);
            if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
            if (parsed->is_none()) {
                ++index;
                continue;
            }
            auto name       = rstd::move(*parsed).unwrap();
            auto normalized = normalized_import(name.value.as_str(), declared.as_str());
            if (normalized.is_err()) return Err(rstd::move(normalized).unwrap_err());
            auto logical_name = rstd::move(normalized).unwrap();
            if (! import_names.contains_key(logical_name.as_str())) {
                import_names.insert(logical_name.clone(), empty {});
                facts.imports.push(ModuleImport {
                    .logical_name = rstd::move(logical_name),
                    .location =
                        SourceLocation {
                            .path = token.path.clone(),
                            .line = token.line,
                        },
                });
            }
            index = name.next;
            continue;
        }
        ++index;
    }
    return Ok(rstd::move(facts));
}

auto scan_from_preprocessed(const PreprocessedModuleFacts& facts, UnitId unit) -> ScanResult {
    auto result = ScanResult { .unit = unit };
    if (facts.provided.is_some()) {
        result.provided = Some(ProvidedModule {
            .logical_name = facts.provided->logical_name.clone(),
            .is_interface = facts.provided->is_interface,
        });
    }
    if (facts.implementation_module.is_some()) {
        result.required_modules.push(facts.implementation_module->clone());
    }
    for (const auto& imported : facts.imports) {
        if (! contains_name(result.required_modules, imported.logical_name.as_str())) {
            result.required_modules.push(imported.logical_name.clone());
        }
    }
    for (const auto& header : facts.header_inputs) result.header_inputs.push(header.clone());
    return result;
}

} // namespace tenon::toolchain
