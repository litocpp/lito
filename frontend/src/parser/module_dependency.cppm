export module lito.frontend.parser:module_dependency;

import rstd;
import lito.frontend.result;
import lito.frontend.preprocessor;

using namespace rstd::prelude;
using namespace rstd::literals;
using ImportIndexMap = rstd::collections::BTreeMap<String, usize>;

namespace lito::frontend::parser
{

namespace preprocessor = lito::frontend::preprocessor;
namespace lexical      = lito::frontend::lexical;

template<typename T>
auto frontend_failure(String message) -> lexical::Result<T> {
    return Err(lexical::Error::make(rstd::move(message)));
}

template<typename T>
auto frontend_failure(ref<str> message) -> lexical::Result<T> {
    return Err(lexical::Error::make(message));
}

struct ParsedName {
    String value;
};

struct PendingImport {
    String                      logical_name;
    lexical::SourceLocation     location;
    Option<rstd::path::PathBuf> presumed_path;
    bool                        exported { false };
};

auto parse_name(const Vec<lexical::Token>& tokens, usize start, ref<str> context)
    -> lexical::Result<Option<ParsedName>> {
    if (start >= tokens.len()) return Ok(None());
    if (tokens[start].text.as_str() == "<"_str ||
        tokens[start].kind == lexical::TokenKind::StringLiteral ||
        tokens[start].kind == lexical::TokenKind::HeaderName) {
        return frontend_failure<Option<ParsedName>>(
            rstd::format("{} uses an unsupported header unit at line {}",
                         context,
                         tokens[start].expansion.line));
    }
    auto index  = start;
    auto result = String::make();
    if (tokens[index].text.as_str() == ":"_str) {
        result.push_ascii(':');
        ++index;
    }
    if (index >= tokens.len() || tokens[index].kind != lexical::TokenKind::Identifier) {
        return Ok(None());
    }
    result.push_str(tokens[index].text.as_str());
    ++index;
    while (index + usize(1) < tokens.len() &&
           (tokens[index].text.as_str() == "."_str || tokens[index].text.as_str() == ":"_str) &&
           tokens[index + usize(1)].kind == lexical::TokenKind::Identifier) {
        result.push_str(tokens[index].text.as_str());
        result.push_str(tokens[index + usize(1)].text.as_str());
        index += usize(2);
    }
    if (index >= tokens.len() || tokens[index].text.as_str() != ";"_str) return Ok(None());
    return Ok(Some(ParsedName { .value = rstd::move(result) }));
}

auto primary_module(ref<str> declared) -> String {
    auto separator = declared.find(":"_str);
    if (separator.is_none()) return String::make(declared);
    return String::make(declared.get(usize {}, *separator).unwrap());
}

auto normalized_import(ref<str> imported, ref<str> declared) -> lexical::Result<String> {
    if (imported.is_empty() || imported[usize {}] != u8(':')) return Ok(String::make(imported));
    if (declared.is_empty()) {
        return frontend_failure<String>(String::make(
            "relative partition import appears before a named module declaration"_str));
    }
    auto result = primary_module(declared);
    result.push_str(imported);
    return Ok(rstd::move(result));
}

auto copied_path(const Option<rstd::path::PathBuf>& path) -> Option<rstd::path::PathBuf> {
    return path.is_some() ? Some(rstd::path::PathBuf::from(path->as_path()))
                          : Option<rstd::path::PathBuf> {};
}

} // namespace lito::frontend::parser

export namespace lito::frontend::parser
{

class ModuleDependencyConsumer {
public:
    static auto make() -> ModuleDependencyConsumer { return ModuleDependencyConsumer {}; }

    auto consume(Vec<lexical::Token> tokens) -> lexical::Result<empty> {
        for (auto& token : tokens) {
            if (token.kind == lexical::TokenKind::Newline) continue;
            auto text = token.text.as_str();
            if (text == "{"_str) {
                ++brace_depth_;
                candidate_        = Vec<lexical::Token>::make();
                ignore_statement_ = false;
                continue;
            }
            if (text == "}"_str) {
                if (brace_depth_ != usize {}) --brace_depth_;
                if (brace_depth_ == usize {}) {
                    candidate_        = Vec<lexical::Token>::make();
                    ignore_statement_ = false;
                }
                continue;
            }
            if (brace_depth_ != usize {}) continue;

            if (ignore_statement_) {
                if (text == "module"_str || text == "import"_str || text == "export"_str) {
                    ignore_statement_ = false;
                    candidate_.push(rstd::move(token));
                } else if (text == ";"_str) {
                    ignore_statement_ = false;
                }
                continue;
            }
            if (candidate_.is_empty()) {
                if (text == "module"_str || text == "import"_str || text == "export"_str) {
                    candidate_.push(rstd::move(token));
                } else if (text != ";"_str) {
                    ignore_statement_ = true;
                }
                continue;
            }
            if (candidate_.len() == usize(1) &&
                candidate_[usize {}].text.as_str() == "export"_str && text != "module"_str &&
                text != "import"_str) {
                candidate_        = Vec<lexical::Token>::make();
                ignore_statement_ = text != ";"_str;
                continue;
            }
            auto complete = text == ";"_str;
            candidate_.push(rstd::move(token));
            if (complete) {
                auto parsed = parse_candidate();
                candidate_  = Vec<lexical::Token>::make();
                if (parsed.is_err()) return parsed;
            }
        }
        return Ok(empty {});
    }

    auto finish(const preprocessor::PreprocessedTranslationUnit& translation)
        -> lexical::Result<FrontendResult> {
        auto facts = FrontendResult {
            .source = rstd::path::PathBuf::from(translation.sources.path(translation.main_source)),
            .provided              = rstd::move(provided_),
            .implementation_module = rstd::move(implementation_module_),
            .header_inputs =
                [&]() {
                    auto paths =
                        Vec<rstd::path::PathBuf>::with_capacity(translation.header_inputs.len());
                    for (const auto& path : translation.header_inputs) paths.push(path.clone());
                    return paths;
                }(),
            .preprocessor_environment = translation.environment_identity.clone(),
            .input_bytes              = translation.input_bytes,
        };
        for (auto& pending : imports_) {
            auto path =
                pending.presumed_path.is_some()
                    ? rstd::move(pending.presumed_path).unwrap()
                    : rstd::path::PathBuf::from(translation.sources.path(pending.location.source));
            facts.imports.push(ModuleImport {
                .logical_name = rstd::move(pending.logical_name),
                .location =
                    DependencyLocation { .path = rstd::move(path), .line = pending.location.line },
                .exported = pending.exported,
            });
        }
        return Ok(rstd::move(facts));
    }

private:
    auto parse_candidate() -> lexical::Result<empty> {
        auto declaration = usize {};
        auto exported    = candidate_[usize {}].text.as_str() == "export"_str;
        if (exported) declaration = usize(1);
        if (declaration >= candidate_.len()) return Ok(empty {});
        auto keyword = candidate_[declaration].text.as_str();
        if (keyword == "module"_str && declaration + usize(1) < candidate_.len() &&
            candidate_[declaration + usize(1)].text.as_str() == ";"_str) {
            return Ok(empty {});
        }
        auto parsed = parse_name(candidate_,
                                 declaration + usize(1),
                                 keyword == "module"_str ? "module declaration"_str
                                                         : "import declaration"_str);
        if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
        if (parsed->is_none()) return Ok(empty {});
        auto name = rstd::move(*parsed).unwrap().value;
        if (keyword == "module"_str) {
            if (name.as_str() == ":private"_str) return Ok(empty {});
            if (! declared_.is_empty()) {
                return frontend_failure<empty>(
                    "multiple named module declarations in one translation unit"_str);
            }
            declared_ = name.clone();
            if (exported) {
                provided_ =
                    Some(ProvidedModule { .logical_name = rstd::move(name), .is_interface = true });
            } else if (name.as_str().contains(":"_str)) {
                provided_ = Some(
                    ProvidedModule { .logical_name = rstd::move(name), .is_interface = false });
            } else {
                implementation_module_ = Some(rstd::move(name));
            }
            return Ok(empty {});
        }
        auto normalized = normalized_import(name.as_str(), declared_.as_str());
        if (normalized.is_err()) return Err(rstd::move(normalized).unwrap_err());
        auto logical_name = rstd::move(normalized).unwrap();
        auto existing = import_names_.get(logical_name.as_str());
        if (existing.is_some()) {
            if (exported) imports_[**existing].exported = true;
            return Ok(empty {});
        }
        import_names_.insert(logical_name.clone(), imports_.len());
        const auto& origin = candidate_[declaration];
        imports_.push(PendingImport {
            .logical_name  = rstd::move(logical_name),
            .location      = origin.expansion,
            .presumed_path = copied_path(origin.presumed_path),
            .exported      = exported,
        });
        return Ok(empty {});
    }

    Option<ProvidedModule> provided_;
    Option<String>         implementation_module_;
    String                 declared_;
    ImportIndexMap         import_names_;
    Vec<PendingImport>     imports_;
    Vec<lexical::Token>    candidate_;
    usize                  brace_depth_ {};
    bool                   ignore_statement_ { false };
};

auto parse_module_dependencies(const preprocessor::PreprocessedTranslationUnit& translation)
    -> lexical::Result<FrontendResult> {
    auto consumer = ModuleDependencyConsumer::make();
    auto tokens   = Vec<lexical::Token>::with_capacity(translation.tokens.len());
    for (const auto& token : translation.tokens) tokens.push(token.clone());
    auto consumed = consumer.consume(rstd::move(tokens));
    if (consumed.is_err()) return Err(rstd::move(consumed).unwrap_err());
    return consumer.finish(translation);
}

} // namespace lito::frontend::parser
