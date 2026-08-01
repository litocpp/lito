export module tenon.modules:preprocessed;

import rstd;
import tenon.model;
import tenon.preprocessor;

using namespace rstd::prelude;
using namespace rstd::literals;
using StringSet = rstd::collections::BTreeMap<String, empty>;

namespace tenon::modules {

template <typename T> auto preprocessed_failure(String message) -> Result<T> {
  return Err(Error::make(ErrorKind::Toolchain, rstd::move(message)));
}

struct ParsedName {
  String value;
  usize next{};
};

auto token_path(const preprocessor::PreprocessedTranslationUnit &translation,
                const preprocessor::Token &token) -> ref<rstd::path::Path> {
  if (token.presumed_path.is_some())
    return token.presumed_path->as_path();
  return translation.sources.path(token.expansion.source);
}

auto parse_name(const preprocessor::PreprocessedTranslationUnit &translation,
                const Vec<preprocessor::Token> &tokens, usize start,
                ref<str> context) -> Result<Option<ParsedName>> {
  if (start >= tokens.len())
    return Ok(None());
  if (tokens[start].text.as_str() == "<"_str ||
      tokens[start].kind == preprocessor::TokenKind::StringLiteral ||
      tokens[start].kind == preprocessor::TokenKind::HeaderName) {
    return preprocessed_failure<Option<ParsedName>>(rstd::format(
        "{} uses an unsupported header unit at {}:{}", context,
        token_path(translation, tokens[start]), tokens[start].expansion.line));
  }
  auto index = start;
  auto result = String::make();
  if (tokens[index].text.as_str() == ":"_str) {
    result.push_ascii(':');
    ++index;
  }
  if (index >= tokens.len() ||
      tokens[index].kind != preprocessor::TokenKind::Identifier) {
    return Ok(None());
  }
  result.push_str(tokens[index].text.as_str());
  ++index;
  while (index + usize(1) < tokens.len() &&
         (tokens[index].text.as_str() == "."_str ||
          tokens[index].text.as_str() == ":"_str) &&
         tokens[index + usize(1)].kind == preprocessor::TokenKind::Identifier) {
    result.push_str(tokens[index].text.as_str());
    result.push_str(tokens[index + usize(1)].text.as_str());
    index += usize(2);
  }
  if (index >= tokens.len() || tokens[index].text.as_str() != ";"_str)
    return Ok(None());
  return Ok(
      Some(ParsedName{.value = rstd::move(result), .next = index + usize(1)}));
}

auto primary_module(ref<str> declared) -> String {
  auto result = String::make();
  for (auto value : declared) {
    if (value == u8(':'))
      break;
    result.push_ascii(value);
  }
  return result;
}

auto normalized_import(ref<str> imported, ref<str> declared) -> Result<String> {
  if (imported.is_empty() || imported[usize{}] != u8(':'))
    return Ok(String::make(imported));
  if (declared.is_empty()) {
    return preprocessed_failure<String>(String::make(
        "relative partition import appears before a named module declaration"_str));
  }
  auto result = primary_module(declared);
  result.push_str(imported);
  return Ok(rstd::move(result));
}

auto contains_name(const Vec<String> &values, ref<str> name) -> bool {
  for (const auto &value : values) {
    if (value.as_str() == name)
      return true;
  }
  return false;
}

} // namespace tenon::modules

export namespace tenon::modules {

auto parse_preprocessed_module(
    const preprocessor::PreprocessedTranslationUnit &translation)
    -> Result<PreprocessedModuleFacts> {
  const auto &tokens = translation.tokens;
  auto facts = PreprocessedModuleFacts{
      .source =
          PathBuf::from(translation.sources.path(translation.main_source)),
      .header_inputs =
          [&]() {
            auto paths =
                Vec<PathBuf>::with_capacity(translation.header_inputs.len());
            for (const auto &path : translation.header_inputs)
              paths.push(path.clone());
            return paths;
          }(),
      .preprocessor_environment = translation.environment_identity.clone(),
      .input_bytes = translation.input_bytes,
  };
  auto declared = String::make();
  auto import_names = StringSet::make();
  auto brace_depth = usize{};
  auto index = usize{};
  while (index < tokens.len()) {
    const auto &token = tokens[index];
    if (token.kind == preprocessor::TokenKind::Newline) {
      ++index;
      continue;
    }
    if (token.text.as_str() == "{"_str) {
      ++brace_depth;
      ++index;
      continue;
    }
    if (token.text.as_str() == "}"_str) {
      if (brace_depth != usize{})
        --brace_depth;
      ++index;
      continue;
    }
    if (brace_depth != usize{}) {
      ++index;
      continue;
    }

    auto exported = false;
    auto keyword = token.text.as_str();
    auto declaration = index;
    if (keyword == "export"_str) {
      auto next_index = index + usize(1);
      while (next_index < tokens.len() &&
             tokens[next_index].kind == preprocessor::TokenKind::Newline) {
        ++next_index;
      }
      if (next_index < tokens.len()) {
        const auto next = tokens[next_index].text.as_str();
        if (next == "module"_str || next == "import"_str) {
          exported = true;
          keyword = next;
          declaration = next_index;
        }
      }
    }
    if (keyword == "module"_str) {
      if (declaration + usize(1) < tokens.len() &&
          tokens[declaration + usize(1)].text.as_str() == ";"_str) {
        index = declaration + usize(2);
        continue;
      }
      auto parsed = parse_name(translation, tokens, declaration + usize(1),
                               "module declaration"_str);
      if (parsed.is_err())
        return Err(rstd::move(parsed).unwrap_err());
      if (parsed->is_none()) {
        ++index;
        continue;
      }
      auto name = rstd::move(*parsed).unwrap();
      if (name.value.as_str() == ":private"_str) {
        index = name.next;
        continue;
      }
      if (!declared.is_empty()) {
        return preprocessed_failure<PreprocessedModuleFacts>(
            rstd::format("multiple named module declarations in '{}'",
                         facts.source.as_path()));
      }
      declared = name.value.clone();
      if (exported) {
        facts.provided = Some(ProvidedModule{.logical_name = name.value.clone(),
                                             .is_interface = true});
      } else if (name.value.as_str().contains(":"_str)) {
        facts.provided = Some(ProvidedModule{.logical_name = name.value.clone(),
                                             .is_interface = false});
      } else {
        facts.implementation_module = Some(name.value.clone());
      }
      index = name.next;
      continue;
    }
    if (keyword == "import"_str) {
      auto parsed = parse_name(translation, tokens, declaration + usize(1),
                               "import declaration"_str);
      if (parsed.is_err())
        return Err(rstd::move(parsed).unwrap_err());
      if (parsed->is_none()) {
        ++index;
        continue;
      }
      auto name = rstd::move(*parsed).unwrap();
      auto normalized =
          normalized_import(name.value.as_str(), declared.as_str());
      if (normalized.is_err())
        return Err(rstd::move(normalized).unwrap_err());
      auto logical_name = rstd::move(normalized).unwrap();
      if (!import_names.contains_key(logical_name.as_str())) {
        import_names.insert(logical_name.clone(), empty{});
        facts.imports.push(ModuleImport{
            .logical_name = rstd::move(logical_name),
            .location =
                SourceLocation{
                    .path = PathBuf::from(token_path(translation, token)),
                    .line = token.expansion.line,
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

auto scan_from_preprocessed(const PreprocessedModuleFacts &facts, UnitId unit)
    -> ScanResult {
  auto result = ScanResult{
      .unit = unit,
      .preprocessor_environment = facts.preprocessor_environment.clone(),
  };
  if (facts.provided.is_some()) {
    result.provided = Some(ProvidedModule{
        .logical_name = facts.provided->logical_name.clone(),
        .is_interface = facts.provided->is_interface,
    });
  }
  if (facts.implementation_module.is_some()) {
    result.implementation_module = Some(facts.implementation_module->clone());
    result.required_modules.push(facts.implementation_module->clone());
  }
  for (const auto &imported : facts.imports) {
    if (!contains_name(result.required_modules,
                       imported.logical_name.as_str())) {
      result.required_modules.push(imported.logical_name.clone());
    }
  }
  for (const auto &header : facts.header_inputs)
    result.header_inputs.push(header.clone());
  return result;
}

} // namespace tenon::modules
