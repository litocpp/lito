export module tenon.frontend.preprocessor:session;

import rstd;
import tenon.frontend.lexical;
import :traits;
import :macro;
import :expression;

using namespace rstd::prelude;
using namespace rstd::literals;

using namespace tenon::frontend::lexical;

export namespace tenon::frontend::preprocessor {

struct PreprocessRequest {
  rstd::path::PathBuf source;
  String environment_identity;
  usize maximum_include_depth{usize(200)};
};

struct PreprocessedTranslationUnit {
  SourceManager sources;
  SourceId main_source{};
  Vec<Token> tokens;
  Vec<rstd::path::PathBuf> header_inputs;
  String environment_identity;
  usize input_bytes{};
};

template <typename Sources, typename Includes, typename Builtins,
          typename Pragmas, typename Events, typename Consumer>
class PreprocessorSession {
  struct ConditionalFrame {
    bool parent_active{false};
    bool branch_taken{false};
    bool active{false};
    bool saw_else{false};
    SourceLocation location;
  };

  struct IncludeFrame {
    SourceId source{};
    Option<usize> search_index;
  };

public:
  PreprocessorSession(PreprocessRequest request, Sources &sources,
                      Includes &includes, Builtins &builtins, Pragmas &pragmas,
                      Events &events, Consumer &consumer)
      : request_(rstd::move(request)), source_provider_(sources),
        include_resolver_(includes), builtin_provider_(builtins),
        pragma_handler_(pragmas), event_sink_(events), consumer_(consumer) {}

  auto run() -> Result<PreprocessedTranslationUnit> {
    auto predefined =
        rstd::as<BuiltinProvider>(builtin_provider_).predefined_macros();
    if (predefined.is_err())
      return Err(rstd::move(predefined).unwrap_err());
    for (auto &definition : *predefined)
      (void)macros_.define_shared(rstd::move(definition));

    auto processed = process_file(request_.source.as_path(), None());
    if (processed.is_err())
      return Err(rstd::move(processed).unwrap_err());
    return Ok(PreprocessedTranslationUnit{
        .sources = rstd::move(sources_),
        .main_source = main_source_,
        .tokens = Vec<Token>::make(),
        .header_inputs = Vec<rstd::path::PathBuf>::make(),
        .environment_identity = environment_identity(),
        .input_bytes = input_bytes_,
    });
  }

private:
  auto failure(ref<str> message, SourceLocation location) -> Error {
    auto error = Error::at(String::make(message), location);
    if (location.source < sources_.len()) {
      error.path =
          Some(rstd::path::PathBuf::from(sources_.path(location.source)));
    }
    return error;
  }

  auto failure(String message, SourceLocation location) -> Error {
    auto error = Error::at(rstd::move(message), location);
    if (location.source < sources_.len()) {
      error.path =
          Some(rstd::path::PathBuf::from(sources_.path(location.source)));
    }
    return error;
  }

  auto path_text(ref<rstd::path::Path> path, SourceLocation location)
      -> Result<String> {
    auto text = path.to_str();
    if (text.is_none()) {
      return Err(failure(
          rstd::format("source path '{}' is not valid UTF-8", path), location));
    }
    return Ok(String::make(*text));
  }

  auto emit(Event event) -> Result<empty> {
    return rstd::as<PreprocessorEventSink>(event_sink_).on_event(event);
  }

  auto emit_name(EventKind kind, ref<str> name, SourceLocation location)
      -> Result<empty> {
    return emit(
        Event{.kind = kind, .name = String::make(name), .location = location});
  }

  auto active(const Vec<ConditionalFrame> &conditions) const -> bool {
    return conditions.is_empty() ||
           conditions[conditions.len() - usize(1)].active;
  }

  auto without_newline(const Vec<Token> &tokens, usize begin, usize end)
      -> Result<Vec<Token>> {
    auto result = Vec<Token>::with_capacity(end - begin);
    for (auto index = begin; index < end; ++index) {
      if (tokens[index].kind != TokenKind::Newline)
        result.push(tokens[index].clone());
    }
    return Ok(rstd::move(result));
  }

  auto same_name(const Vec<String> &names, ref<str> value) const -> bool {
    for (const auto &name : names) {
      if (name.as_str() == value)
        return true;
    }
    return false;
  }

  auto is_dynamic_builtin(ref<str> name) const -> bool {
    return name == "__LINE__"_str || name == "__FILE__"_str ||
           name == "__BASE_FILE__"_str || name == "__FILE_NAME__"_str ||
           name == "__INCLUDE_LEVEL__"_str || name == "__COUNTER__"_str ||
           name == "__DATE__"_str || name == "__TIME__"_str ||
           name == "__has_builtin"_str ||
           name == "__has_constexpr_builtin"_str ||
           name == "__has_feature"_str || name == "__has_extension"_str ||
           name == "__has_cpp_attribute"_str || name == "__has_attribute"_str ||
           name == "__has_declspec_attribute"_str ||
           name == "__has_embed"_str || name == "__has_warning"_str ||
           name == "__is_identifier"_str || name == "__has_include"_str ||
           name == "__has_include_next"_str;
  }

  auto number_token(i64 value, const Token &origin) -> Token {
    auto token = origin.clone();
    token.kind = TokenKind::PpNumber;
    token.text = rstd::format("{}", value);
    token.start_of_line = origin.start_of_line;
    return token;
  }

  auto string_token(ref<str> value, const Token &origin) -> Token {
    auto text = String::make();
    text.push_ascii('"');
    for (auto byte : value) {
      if (byte == u8('\\') || byte == u8('"'))
        text.push_ascii('\\');
      text.push_ascii(byte);
    }
    text.push_ascii('"');
    auto token = origin.clone();
    token.kind = TokenKind::StringLiteral;
    token.text = rstd::move(text);
    return token;
  }

  auto environment_identity() const -> String {
    if (builtin_identity_.is_empty())
      return request_.environment_identity.clone();
    return rstd::format("{}:{}", request_.environment_identity.as_str(),
                        builtin_identity_.as_str());
  }

  auto clone_range(const Vec<Token> &input, usize begin, usize end)
      -> Vec<Token> {
    auto result = Vec<Token>::with_capacity(end - begin);
    for (auto index = begin; index < end; ++index)
      result.push(input[index].clone());
    return result;
  }

  auto parameter_index(const MacroDefinition &macro, ref<str> name) const
      -> Option<usize> {
    if (macro.parameters.is_some()) {
      for (auto index = usize{}; index < macro.parameters->len(); ++index) {
        if ((*macro.parameters)[index].as_str() == name)
          return Some(index);
      }
    }
    if (macro.variadic &&
        (name == "__VA_ARGS__"_str || name == macro.variadic_name.as_str())) {
      return Some(macro.parameters.is_some() ? macro.parameters->len()
                                             : usize{});
    }
    return None();
  }

  auto stringify(const Vec<Token> &argument, const Token &origin) -> Token {
    auto text = String::make();
    text.push_ascii('"');
    auto first = true;
    for (const auto &token : argument) {
      if (token.kind == TokenKind::Newline)
        continue;
      if (!first && token.leading_space)
        text.push_ascii(' ');
      for (auto byte : token.text.as_str()) {
        if (byte == u8('\\') || byte == u8('"'))
          text.push_ascii('\\');
        text.push_ascii(byte);
      }
      first = false;
    }
    text.push_ascii('"');
    auto result = origin.clone();
    result.kind = TokenKind::StringLiteral;
    result.text = rstd::move(text);
    return result;
  }

  auto string_contents(const Token &token, ref<str> purpose) -> Result<String> {
    if (token.kind != TokenKind::StringLiteral || token.text.len() < usize(2) ||
        token.text.as_str().as_bytes()[usize{}] != u8('"') ||
        token.text.as_str().as_bytes()[token.text.len() - usize(1)] !=
            u8('"')) {
      return Err(failure(
          rstd::format("{} requires an ordinary string literal", purpose),
          token.expansion));
    }
    auto contents =
        token.text.as_str().get(usize(1), token.text.len() - usize(1));
    if (contents.is_none()) {
      return Err(
          failure(rstd::format("{} has an invalid string literal", purpose),
                  token.expansion));
    }
    return Ok(String::make(*contents));
  }

  auto pragma_macro_name(const Vec<Token> &tokens, SourceLocation location)
      -> Result<String> {
    if (tokens.len() != usize(4) || tokens[usize(1)].text.as_str() != "("_str ||
        tokens[usize(3)].text.as_str() != ")"_str) {
      return Err(failure("macro stack pragma requires one string literal"_str,
                         location));
    }
    auto name = string_contents(tokens[usize(2)], "macro stack pragma"_str);
    if (name.is_err())
      return name;
    auto source = SourceFile::make(
        SourceId{}, SourceBuffer{
                        .path = rstd::path::PathBuf::from(
                            "<pragma-macro-name>"_str),
                        .contents = name->clone(),
                    });
    auto lexed = lex(source);
    if (lexed.is_err() || lexed->len() != usize(1) ||
        (*lexed)[usize{}].kind != TokenKind::Identifier ||
        (*lexed)[usize{}].text.as_str() != name->as_str()) {
      return Err(failure("macro stack pragma requires an identifier name"_str,
                         location));
    }
    return name;
  }

  auto handle_pragma(Vec<Token> tokens, SourceLocation location)
      -> Result<empty> {
    if (tokens.len() == usize(1) &&
        tokens[usize{}].text.as_str() == "once"_str) {
      if (include_stack_.is_empty()) {
        return Err(failure("pragma once has no current source"_str, location));
      }
      auto source = include_stack_[include_stack_.len() - usize(1)].source;
      auto text = sources_.path(source).to_str();
      if (text.is_some())
        once_files_.insert(String::make(*text), empty{});
      return Ok(empty{});
    }
    if (!tokens.is_empty() &&
        (tokens[usize{}].text.as_str() == "push_macro"_str ||
         tokens[usize{}].text.as_str() == "pop_macro"_str)) {
      auto push = tokens[usize{}].text.as_str() == "push_macro"_str;
      auto name = pragma_macro_name(tokens, location);
      if (name.is_err())
        return Err(rstd::move(name).unwrap_err());
      if (push) {
        auto stored = macros_.get(name->as_str());
        auto stack = macro_stacks_.get_mut(name->as_str());
        if (stack.is_none()) {
          auto values = Vec<Option<SharedMacroDefinition>>::make();
          values.push(rstd::move(stored));
          macro_stacks_.insert(rstd::move(name).unwrap(), rstd::move(values));
        } else {
          (**stack).push(rstd::move(stored));
        }
        return Ok(empty{});
      }
      auto stack = macro_stacks_.get_mut(name->as_str());
      if (stack.is_none() || (**stack).is_empty()) {
        return Err(failure(
            rstd::format("no pushed definition for macro '{}'", name->as_str()),
            location));
      }
      auto restored = (**stack).pop().unwrap();
      if (restored.is_some()) {
        (void)macros_.define_shared(rstd::move(restored).unwrap());
      } else {
        (void)macros_.undefine(name->as_str());
      }
      return Ok(empty{});
    }
    auto outcome = rstd::as<PragmaHandler>(pragma_handler_)
                       .handle(PragmaRequest{.tokens = rstd::move(tokens),
                                             .location = location});
    if (outcome.is_err())
      return Err(rstd::move(outcome).unwrap_err());
    return Ok(empty{});
  }

  auto destringize_pragma(const Token &token) -> Result<Vec<Token>> {
    auto encoded = string_contents(token, "_Pragma"_str);
    if (encoded.is_err())
      return Err(rstd::move(encoded).unwrap_err());
    auto contents = String::make();
    auto bytes = encoded->as_str().as_bytes();
    for (auto index = usize{}; index < bytes.len(); ++index) {
      if (bytes[index] == u8('\\') && index + usize(1) < bytes.len() &&
          (bytes[index + usize(1)] == u8('\\') ||
           bytes[index + usize(1)] == u8('"'))) {
        ++index;
      }
      contents.push_ascii(bytes[index]);
    }
    auto source = SourceFile::make(
        token.spelling.source,
        SourceBuffer{
            .path =
                rstd::path::PathBuf::from(sources_.path(token.spelling.source)),
            .contents = rstd::move(contents),
        });
    auto lexed = lex(source);
    if (lexed.is_err())
      return Err(rstd::move(lexed).unwrap_err());
    auto result = Vec<Token>::make();
    for (auto &item : *lexed) {
      if (item.kind == TokenKind::Newline)
        continue;
      item.spelling = token.spelling;
      item.expansion = token.expansion;
      result.push(rstd::move(item));
    }
    return Ok(rstd::move(result));
  }

  auto pasted(Token left, const Token &right) -> Result<Token> {
    left.text.push_str(right.text.as_str());
    if (left.text.is_empty()) {
      return Err(
          failure("token paste produced an empty token"_str, left.expansion));
    }
    auto source = SourceFile::make(
        left.spelling.source,
        SourceBuffer{
            .path =
                rstd::path::PathBuf::from(sources_.path(left.spelling.source)),
            .contents = left.text.clone(),
        });
    auto lexed = lex(source);
    if (lexed.is_err() || lexed->len() != usize(1) ||
        (*lexed)[usize{}].text.as_str() != left.text.as_str()) {
      return Err(failure(
          rstd::format("token paste '{}' did not form one preprocessing token",
                       left.text.as_str()),
          left.expansion));
    }
    left.kind = (*lexed)[usize{}].kind;
    return Ok(rstd::move(left));
  }

  auto collect_arguments(const Vec<Token> &input, usize open)
      -> Result<rstd::tuple<Vec<Vec<Token>>, usize>> {
    auto arguments = Vec<Vec<Token>>::make();
    auto current = Vec<Token>::make();
    auto depth = usize{};
    for (auto index = open + usize(1); index < input.len(); ++index) {
      const auto &token = input[index];
      if (token.text.as_str() == "("_str) {
        ++depth;
        current.push(token.clone());
        continue;
      }
      if (token.text.as_str() == ")"_str) {
        if (depth == usize{}) {
          arguments.push(rstd::move(current));
          return Ok(rstd::tuple{rstd::move(arguments), index + usize(1)});
        }
        --depth;
        current.push(token.clone());
        continue;
      }
      if (token.text.as_str() == ","_str && depth == usize{}) {
        arguments.push(rstd::move(current));
        current = Vec<Token>::make();
        continue;
      }
      current.push(token.clone());
    }
    return Err(
        failure("unterminated macro invocation"_str, input[open].expansion));
  }

  auto variadic_argument(const MacroDefinition &macro,
                         const Vec<Vec<Token>> &arguments, const Token &origin)
      -> Vec<Token> {
    auto result = Vec<Token>::make();
    auto fixed = macro.parameters.is_some() ? macro.parameters->len() : usize{};
    for (auto index = fixed; index < arguments.len(); ++index) {
      if (!result.is_empty()) {
        auto comma = origin.clone();
        comma.kind = TokenKind::Punctuation;
        comma.text = String::make(","_str);
        result.push(rstd::move(comma));
      }
      for (const auto &token : arguments[index])
        result.push(token.clone());
    }
    return result;
  }

  auto substitute(const MacroDefinition &macro,
                  const Vec<Vec<Token>> &arguments, const Token &origin,
                  Vec<String> &disabled) -> Result<Vec<Token>> {
    auto fixed = macro.parameters.is_some() ? macro.parameters->len() : usize{};
    auto argument_count = arguments.len();
    if (!macro.variadic && fixed == usize{} && argument_count == usize(1) &&
        arguments[usize{}].is_empty()) {
      argument_count = usize{};
    }
    if ((!macro.variadic && argument_count != fixed) ||
        (macro.variadic && arguments.len() < fixed)) {
      return Err(
          failure(rstd::format("macro '{}' expects {} argument(s), got {}",
                               macro.name.as_str(), fixed, argument_count),
                  origin.expansion));
    }
    auto variadic = variadic_argument(macro, arguments, origin);
    auto expanded_arguments =
        Vec<Option<Vec<Token>>>::with_capacity(fixed + usize(1));
    for (auto index = usize{}; index <= fixed; ++index)
      expanded_arguments.emplace_back(None());
    auto expanded_argument = [&](usize parameter,
                                 const Vec<Token> &argument)
        -> Result<Vec<Token>> {
      if (expanded_arguments[parameter].is_none()) {
        auto expanded = expand(clone_tokens(argument), disabled);
        if (expanded.is_err())
          return Err(rstd::move(expanded).unwrap_err());
        expanded_arguments[parameter] = Some(rstd::move(expanded).unwrap());
      }
      return Ok(clone_tokens(*expanded_arguments[parameter]));
    };
    auto result = Vec<Token>::make();
    for (auto index = usize{}; index < macro.replacement.len(); ++index) {
      const auto &token = macro.replacement[index];
      if (token.text.as_str() == "__VA_OPT__"_str && macro.variadic &&
          index + usize(1) < macro.replacement.len() &&
          macro.replacement[index + usize(1)].text.as_str() == "("_str) {
        auto nested = collect_arguments(macro.replacement, index + usize(1));
        if (nested.is_err())
          return Err(rstd::move(nested).unwrap_err());
        auto end = nested->template get<1>();
        if (!variadic.is_empty()) {
          auto nested_macro = macro.clone();
          nested_macro.replacement =
              clone_range(macro.replacement, index + usize(2), end - usize(1));
          auto substituted =
              substitute(nested_macro, arguments, origin, disabled);
          if (substituted.is_err())
            return substituted;
          for (auto &item : *substituted)
            result.push(rstd::move(item));
        }
        index = end - usize(1);
        continue;
      }
      if (token.text.as_str() == "#"_str &&
          index + usize(1) < macro.replacement.len()) {
        auto parameter = parameter_index(
            macro, macro.replacement[index + usize(1)].text.as_str());
        if (parameter.is_some()) {
          const auto *argument = &variadic;
          if (*parameter < fixed)
            argument = &arguments[*parameter];
          result.push(stringify(*argument, origin));
          ++index;
          continue;
        }
      }

      auto paste_left =
          index > usize{} &&
          macro.replacement[index - usize(1)].text.as_str() == "##"_str;
      auto parameter = parameter_index(macro, token.text.as_str());
      auto piece = Vec<Token>::make();
      if (parameter.is_some()) {
        const auto *argument = &variadic;
        if (*parameter < fixed)
          argument = &arguments[*parameter];
        auto paste_right =
            index + usize(1) < macro.replacement.len() &&
            macro.replacement[index + usize(1)].text.as_str() == "##"_str;
        if (paste_left || paste_right) {
          piece = clone_tokens(*argument);
        } else {
          auto expanded = expanded_argument(*parameter, *argument);
          if (expanded.is_err())
            return expanded;
          piece = rstd::move(expanded).unwrap();
        }
      } else {
        piece.push(token.clone());
      }

      if (paste_left) {
        if (piece.is_empty()) {
          if (!result.is_empty() &&
              result[result.len() - usize(1)].text.as_str() == ","_str &&
              parameter.is_some() && *parameter == fixed) {
            (void)result.pop();
          }
        } else if (!result.is_empty()) {
          auto left = result.pop().unwrap();
          auto combined = pasted(rstd::move(left), piece[usize{}]);
          if (combined.is_err())
            return Err(rstd::move(combined).unwrap_err());
          result.push(rstd::move(combined).unwrap());
          for (auto part = usize(1); part < piece.len(); ++part) {
            result.push(piece[part].clone());
          }
        } else {
          for (auto &item : piece)
            result.push(rstd::move(item));
        }
      } else if (token.text.as_str() != "##"_str) {
        for (auto &item : piece)
          result.push(rstd::move(item));
      }
    }
    for (auto &token : result)
      token.expansion = origin.expansion;
    return Ok(rstd::move(result));
  }

  auto query_kind(ref<str> name) const -> Option<BuiltinQueryKind> {
    if (name == "__has_builtin"_str)
      return Some(BuiltinQueryKind::HasBuiltin);
    if (name == "__has_constexpr_builtin"_str) {
      return Some(BuiltinQueryKind::HasConstexprBuiltin);
    }
    if (name == "__has_feature"_str)
      return Some(BuiltinQueryKind::HasFeature);
    if (name == "__has_extension"_str)
      return Some(BuiltinQueryKind::HasExtension);
    if (name == "__has_cpp_attribute"_str)
      return Some(BuiltinQueryKind::HasCppAttribute);
    if (name == "__has_attribute"_str)
      return Some(BuiltinQueryKind::HasAttribute);
    if (name == "__has_declspec_attribute"_str) {
      return Some(BuiltinQueryKind::HasDeclspecAttribute);
    }
    if (name == "__has_warning"_str)
      return Some(BuiltinQueryKind::HasWarning);
    if (name == "__is_identifier"_str)
      return Some(BuiltinQueryKind::IsIdentifier);
    return None();
  }

  auto joined_argument(const Vec<Token> &tokens) -> String {
    auto result = String::make();
    for (const auto &token : tokens) {
      if (token.kind != TokenKind::Newline)
        result.push_str(token.text.as_str());
    }
    return result;
  }

  auto include_query(const Token &origin, ref<str> name,
                     const Vec<Token> &argument) -> Result<bool> {
    if (include_stack_.is_empty()) {
      return Err(
          failure("include query has no current source"_str, origin.expansion));
    }
    auto kind = name == "__has_include_next"_str ? IncludeKind::NextQuoted
                                                 : IncludeKind::Quoted;
    auto header = String::make();
    if (argument.len() == usize(1) &&
        argument[usize{}].kind == TokenKind::StringLiteral) {
      auto text = argument[usize{}].text.as_str();
      auto bytes = text.as_bytes();
      if (bytes.len() < usize(2))
        return Err(failure("invalid include query"_str, origin.expansion));
      auto inside = text.get(usize(1), text.len() - usize(1));
      if (inside.is_none())
        return Err(failure("invalid include query"_str, origin.expansion));
      header = String::make(*inside);
    } else if (argument.len() >= usize(2) &&
               argument[usize{}].text.as_str() == "<"_str &&
               argument[argument.len() - usize(1)].text.as_str() == ">"_str) {
      kind = name == "__has_include_next"_str ? IncludeKind::NextAngled
                                              : IncludeKind::Angled;
      for (auto index = usize(1); index + usize(1) < argument.len(); ++index) {
        header.push_str(argument[index].text.as_str());
      }
    } else {
      return Err(failure("include query requires a quoted or angled header"_str,
                         origin.expansion));
    }
    const auto &frame = include_stack_[include_stack_.len() - usize(1)];
    auto request = IncludeRequest{
        .name = rstd::move(header),
        .kind = kind,
        .including_path =
            rstd::path::PathBuf::from(sources_.path(frame.source)),
        .previous_search_index = frame.search_index,
        .location = origin.expansion,
    };
    auto resolved =
        rstd::as<IncludeResolver>(include_resolver_).resolve(request);
    if (resolved.is_err())
      return Err(rstd::move(resolved).unwrap_err());
    auto event = resolved->is_some()
                     ? emit(Event{
                           .kind = EventKind::IncludeProbeResolved,
                           .name = request.name.clone(),
                           .path = Some((*resolved)->path.clone()),
                           .location = origin.expansion,
                       })
                     : emit_name(EventKind::IncludeProbeNotFound,
                                 request.name.as_str(), origin.expansion);
    if (event.is_err())
      return Err(rstd::move(event).unwrap_err());
    return Ok(resolved->is_some());
  }

  auto prepare_queries(const Vec<Token> &input) -> Result<empty> {
    auto queries = Vec<BuiltinQuery>::make();
    for (auto cursor = usize{}; cursor < input.len(); ++cursor) {
      if (input[cursor].kind != TokenKind::Identifier)
        continue;
      auto kind = query_kind(input[cursor].text.as_str());
      if (kind.is_none())
        continue;
      auto open = cursor + usize(1);
      while (open < input.len() && input[open].kind == TokenKind::Newline)
        ++open;
      if (open >= input.len() || input[open].text.as_str() != "("_str)
        continue;
      auto collected = collect_arguments(input, open);
      if (collected.is_err() || collected->template get<0>().len() != usize(1))
        continue;
      const auto &argument = collected->template get<0>()[usize{}];
      if (*kind == BuiltinQueryKind::HasWarning &&
          (argument.len() != usize(1) ||
           argument[usize{}].kind != TokenKind::StringLiteral)) {
        continue;
      }
      queries.push(BuiltinQuery{
          .kind = *kind,
          .argument = joined_argument(argument),
          .location = input[cursor].expansion,
      });
      cursor = collected->template get<1>() - usize(1);
    }
    if (!queries.is_empty()) {
      auto prepared =
          rstd::as<BuiltinProvider>(builtin_provider_).prepare(queries);
      if (prepared.is_err())
        return prepared;
    }
    return Ok(empty{});
  }

  auto expand(Vec<Token> input, Vec<String> &disabled) -> Result<Vec<Token>> {
    auto prepared = prepare_queries(input);
    if (prepared.is_err())
      return Err(rstd::move(prepared).unwrap_err());
    auto output = Vec<Token>::make();
    for (auto index = usize{}; index < input.len();) {
      auto token = input[index].clone();
      if (token.kind != TokenKind::Identifier || token.disable_expand) {
        output.push(rstd::move(token));
        ++index;
        continue;
      }
      auto name = token.text.as_str();
      if (same_name(disabled, name)) {
        token.disable_expand = true;
        output.push(rstd::move(token));
        ++index;
        continue;
      }
      if (name == "__LINE__"_str) {
        output.push(
            number_token(rstd::as_cast<i64>(token.expansion.line), token));
        ++index;
        continue;
      }
      if (name == "__INCLUDE_LEVEL__"_str) {
        auto level = include_stack_.is_empty()
                         ? usize{}
                         : include_stack_.len() - usize(1);
        output.push(number_token(rstd::as_cast<i64>(level), token));
        ++index;
        continue;
      }
      if (name == "__COUNTER__"_str) {
        output.push(number_token(rstd::as_cast<i64>(counter_), token));
        ++counter_;
        ++index;
        continue;
      }
      if (name == "__FILE__"_str || name == "__FILE_NAME__"_str ||
          name == "__BASE_FILE__"_str) {
        auto source = name == "__BASE_FILE__"_str
                          ? include_stack_[usize{}].source
                          : token.expansion.source;
        auto path = name != "__BASE_FILE__"_str && token.presumed_path.is_some()
                        ? token.presumed_path->as_path()
                        : sources_.path(source);
        if (name == "__FILE_NAME__"_str && path.file_name().is_some()) {
          auto text = (*path.file_name()).to_str();
          if (text.is_some()) {
            output.push(string_token(*text, token));
            ++index;
            continue;
          }
        }
        auto text = path.to_str();
        if (text.is_none())
          return Err(
              failure("source path is not valid UTF-8"_str, token.expansion));
        output.push(string_token(*text, token));
        ++index;
        continue;
      }
      if (name == "__DATE__"_str || name == "__TIME__"_str) {
        auto kind = name == "__DATE__"_str ? BuiltinTextKind::Date
                                           : BuiltinTextKind::Time;
        auto value = rstd::as<BuiltinProvider>(builtin_provider_).text(kind);
        if (value.is_err())
          return Err(rstd::move(value).unwrap_err());
        builtin_identity_.push_str(name);
        builtin_identity_.push_ascii('=');
        builtin_identity_.push_str(value->as_str());
        builtin_identity_.push_ascii(';');
        output.push(string_token(value->as_str(), token));
        ++index;
        continue;
      }
      if (name == "_Pragma"_str) {
        auto open = index + usize(1);
        while (open < input.len() && input[open].kind == TokenKind::Newline)
          ++open;
        if (open >= input.len() || input[open].text.as_str() != "("_str) {
          return Err(
              failure("_Pragma requires parentheses"_str, token.expansion));
        }
        auto collected = collect_arguments(input, open);
        if (collected.is_err())
          return Err(rstd::move(collected).unwrap_err());
        const auto &arguments = collected->template get<0>();
        if (arguments.len() != usize(1)) {
          return Err(failure("_Pragma requires one string literal"_str,
                             token.expansion));
        }
        auto expanded_argument =
            expand(clone_tokens(arguments[usize{}]), disabled);
        if (expanded_argument.is_err()) {
          return Err(rstd::move(expanded_argument).unwrap_err());
        }
        if (expanded_argument->len() != usize(1)) {
          return Err(failure("_Pragma requires one string literal"_str,
                             token.expansion));
        }
        auto pragma = destringize_pragma((*expanded_argument)[usize{}]);
        if (pragma.is_err())
          return Err(rstd::move(pragma).unwrap_err());
        auto handled =
            handle_pragma(rstd::move(pragma).unwrap(), token.expansion);
        if (handled.is_err())
          return Err(rstd::move(handled).unwrap_err());
        index = collected->template get<1>();
        continue;
      }
      if (name == "__has_embed"_str) {
        return Err(failure("builtin '__has_embed' is unsupported"_str,
                           token.expansion));
      }

      auto query = query_kind(name);
      auto include_builtin =
          name == "__has_include"_str || name == "__has_include_next"_str;
      if (query.is_some() || include_builtin) {
        auto open = index + usize(1);
        while (open < input.len() && input[open].kind == TokenKind::Newline)
          ++open;
        if (open >= input.len() || input[open].text.as_str() != "("_str) {
          return Err(
              failure(rstd::format("builtin '{}' requires parentheses", name),
                      token.expansion));
        }
        auto collected = collect_arguments(input, open);
        if (collected.is_err())
          return Err(rstd::move(collected).unwrap_err());
        auto arguments = rstd::move(collected->template get<0>());
        if (arguments.len() != usize(1)) {
          return Err(
              failure(rstd::format("builtin '{}' requires one argument", name),
                      token.expansion));
        }
        auto value = Result<i64>(Ok(i64{}));
        if (include_builtin) {
          auto included = include_query(token, name, arguments[usize{}]);
          if (included.is_err())
            return Err(rstd::move(included).unwrap_err());
          value = Ok(i64(*included));
        } else {
          value = rstd::as<BuiltinProvider>(builtin_provider_)
                      .evaluate(BuiltinQuery{
                          .kind = *query,
                          .argument = joined_argument(arguments[usize{}]),
                          .location = token.expansion,
                      });
        }
        if (value.is_err())
          return Err(rstd::move(value).unwrap_err());
        output.push(number_token(*value, token));
        index = collected->template get<1>();
        continue;
      }

      auto found = macros_.get(name);
      if (found.is_none()) {
        output.push(rstd::move(token));
        ++index;
        continue;
      }
      auto next = index + usize(1);
      if ((**found).parameters.is_some()) {
        auto open = next;
        while (open < input.len() && input[open].kind == TokenKind::Newline)
          ++open;
        if (open >= input.len() || input[open].text.as_str() != "("_str) {
          output.push(rstd::move(token));
          ++index;
          continue;
        }
      }
      auto macro = rstd::move(found).unwrap();
      const auto &definition = *macro.get();
      auto replacement = Vec<Token>::make();
      if (definition.parameters.is_some()) {
        auto open = next;
        while (open < input.len() && input[open].kind == TokenKind::Newline)
          ++open;
        auto collected = collect_arguments(input, open);
        if (collected.is_err())
          return Err(rstd::move(collected).unwrap_err());
        auto substituted =
            substitute(definition, collected->template get<0>(), token,
                       disabled);
        if (substituted.is_err())
          return substituted;
        replacement = rstd::move(substituted).unwrap();
        next = collected->template get<1>();
      } else {
        replacement = clone_tokens(definition.replacement);
        for (auto &item : replacement)
          item.expansion = token.expansion;
      }
      auto event = emit_name(EventKind::MacroExpanded,
                             definition.name.as_str(),
                             token.expansion);
      if (event.is_err())
        return Err(rstd::move(event).unwrap_err());
      disabled.push(definition.name.clone());
      auto rescanned = expand(rstd::move(replacement), disabled);
      (void)disabled.pop();
      if (rescanned.is_err())
        return rescanned;
      for (auto &item : *rescanned)
        output.push(rstd::move(item));
      index = next;
    }
    return Ok(rstd::move(output));
  }

  auto define_macro(const Vec<Token> &line, bool send_event) -> Result<empty> {
    auto parsed = parse_macro_definition(line);
    if (parsed.is_err())
      return Err(rstd::move(parsed).unwrap_err());
    auto macro = rstd::move(parsed).unwrap();
    auto current = macros_.get(macro.name.as_str());
    if (current.is_some() && !same_macro(*current->get(), macro)) {
      return Err(failure(rstd::format("incompatible redefinition of macro '{}'",
                                      macro.name.as_str()),
                         macro.location));
    }
    auto previous = macros_.define(rstd::move(macro));
    (void)previous;
    if (send_event) {
      auto event =
          emit_name(EventKind::MacroDefined, line[usize{}].text.as_str(),
                    line[usize{}].expansion);
      if (event.is_err())
        return event;
    }
    return Ok(empty{});
  }

  auto same_macro(const MacroDefinition &left,
                  const MacroDefinition &right) const -> bool {
    if (left.parameters.is_some() != right.parameters.is_some() ||
        left.variadic != right.variadic ||
        left.variadic_name.as_str() != right.variadic_name.as_str()) {
      return false;
    }
    if (left.parameters.is_some()) {
      if (left.parameters->len() != right.parameters->len())
        return false;
      for (auto index = usize{}; index < left.parameters->len(); ++index) {
        if ((*left.parameters)[index].as_str() !=
            (*right.parameters)[index].as_str()) {
          return false;
        }
      }
    }
    if (left.replacement.len() != right.replacement.len())
      return false;
    for (auto index = usize{}; index < left.replacement.len(); ++index) {
      if (left.replacement[index].kind != right.replacement[index].kind ||
          left.replacement[index].text.as_str() !=
              right.replacement[index].text.as_str()) {
        return false;
      }
    }
    return true;
  }

  auto replace_defined(const Vec<Token> &line) -> Result<Vec<Token>> {
    auto result = Vec<Token>::make();
    for (auto index = usize{}; index < line.len();) {
      const auto &token = line[index];
      if (token.text.as_str() != "defined"_str) {
        result.push(token.clone());
        ++index;
        continue;
      }
      auto cursor = index + usize(1);
      auto parenthesized =
          cursor < line.len() && line[cursor].text.as_str() == "("_str;
      if (parenthesized)
        ++cursor;
      if (cursor >= line.len() || line[cursor].kind != TokenKind::Identifier) {
        return Err(
            failure("defined requires an identifier"_str, token.expansion));
      }
      auto value = macros_.contains(line[cursor].text.as_str()) ||
                   is_dynamic_builtin(line[cursor].text.as_str());
      ++cursor;
      if (parenthesized) {
        if (cursor >= line.len() || line[cursor].text.as_str() != ")"_str) {
          return Err(
              failure("defined requires a closing ')'"_str, token.expansion));
        }
        ++cursor;
      }
      result.push(number_token(i64(value), token));
      index = cursor;
    }
    return Ok(rstd::move(result));
  }

  auto condition_value(const Vec<Token> &line) -> Result<bool> {
    auto defined = replace_defined(line);
    if (defined.is_err())
      return Err(rstd::move(defined).unwrap_err());
    auto disabled = Vec<String>::make();
    auto expanded = expand(rstd::move(defined).unwrap(), disabled);
    if (expanded.is_err())
      return Err(rstd::move(expanded).unwrap_err());
    auto filtered = Vec<Token>::make();
    for (auto &token : *expanded) {
      if (token.kind != TokenKind::Newline)
        filtered.push(rstd::move(token));
    }
    auto value = evaluate_expression(filtered);
    if (value.is_err())
      return Err(rstd::move(value).unwrap_err());
    return Ok(*value != i64{});
  }

  auto directive_message(const Vec<Token> &line, usize begin) -> String {
    auto result = String::make();
    for (auto index = begin; index < line.len(); ++index) {
      if (!result.is_empty())
        result.push_ascii(' ');
      result.push_str(line[index].text.as_str());
    }
    return result;
  }

  auto include_name(const Vec<Token> &line, IncludeKind &kind,
                    SourceLocation location) -> Result<String> {
    if (line.len() == usize(1) &&
        line[usize{}].kind == TokenKind::StringLiteral) {
      auto text = line[usize{}].text.as_str();
      if (text.len() < usize(2))
        return Err(failure("invalid #include header"_str, location));
      auto inner = text.get(usize(1), text.len() - usize(1));
      if (inner.is_none())
        return Err(failure("invalid #include header"_str, location));
      return Ok(String::make(*inner));
    }
    if (line.len() >= usize(2) && line[usize{}].text.as_str() == "<"_str &&
        line[line.len() - usize(1)].text.as_str() == ">"_str) {
      kind = kind == IncludeKind::NextQuoted ? IncludeKind::NextAngled
                                             : IncludeKind::Angled;
      auto name = String::make();
      for (auto index = usize(1); index + usize(1) < line.len(); ++index) {
        name.push_str(line[index].text.as_str());
      }
      return Ok(rstd::move(name));
    }
    return Err(
        failure("#include requires a quoted or angled header"_str, location));
  }

  auto detected_include_guard(const Vec<Token> &tokens) -> Option<String> {
    auto cursor = usize{};
    while (cursor < tokens.len() && tokens[cursor].kind == TokenKind::Newline)
      ++cursor;
    if (cursor + usize(2) >= tokens.len() || !tokens[cursor].start_of_line ||
        tokens[cursor].text.as_str() != "#"_str ||
        tokens[cursor + usize(1)].text.as_str() != "ifndef"_str ||
        tokens[cursor + usize(2)].kind != TokenKind::Identifier) {
      return None();
    }
    auto guard = tokens[cursor + usize(2)].text.clone();
    while (cursor < tokens.len() && tokens[cursor].kind != TokenKind::Newline)
      ++cursor;
    while (cursor < tokens.len() && tokens[cursor].kind == TokenKind::Newline)
      ++cursor;
    if (cursor + usize(2) >= tokens.len() || !tokens[cursor].start_of_line ||
        tokens[cursor].text.as_str() != "#"_str ||
        tokens[cursor + usize(1)].text.as_str() != "define"_str ||
        tokens[cursor + usize(2)].text.as_str() != guard.as_str()) {
      return None();
    }
    auto last = tokens.len();
    while (last > usize{} && tokens[last - usize(1)].kind == TokenKind::Newline)
      --last;
    if (last < usize(2))
      return None();
    auto line = last - usize(1);
    while (line > usize{} && !tokens[line].start_of_line)
      --line;
    if (line >= last || tokens[line].text.as_str() != "#"_str ||
        line + usize(1) >= last ||
        tokens[line + usize(1)].text.as_str() != "endif"_str) {
      return None();
    }
    return Some(rstd::move(guard));
  }

  auto handle_include(const Vec<Token> &line, bool next,
                      SourceLocation location) -> Result<empty> {
    auto expanded = Result<Vec<Token>>(Ok(clone_tokens(line)));
    auto direct_header = line.len() == usize(1) &&
                         line[usize{}].kind == TokenKind::StringLiteral;
    direct_header =
        direct_header ||
        (line.len() >= usize(2) && line[usize{}].text.as_str() == "<"_str &&
         line[line.len() - usize(1)].text.as_str() == ">"_str);
    if (!direct_header) {
      auto disabled = Vec<String>::make();
      expanded = expand(clone_tokens(line), disabled);
      if (expanded.is_err())
        return Err(rstd::move(expanded).unwrap_err());
    }
    auto kind = next ? IncludeKind::NextQuoted : IncludeKind::Quoted;
    auto name = include_name(*expanded, kind, location);
    if (name.is_err())
      return Err(rstd::move(name).unwrap_err());
    const auto &frame = include_stack_[include_stack_.len() - usize(1)];
    auto resolved = rstd::as<IncludeResolver>(include_resolver_)
                        .resolve(IncludeRequest{
                            .name = name->clone(),
                            .kind = kind,
                            .including_path = rstd::path::PathBuf::from(
                                sources_.path(frame.source)),
                            .previous_search_index = frame.search_index,
                            .location = location,
                        });
    if (resolved.is_err())
      return Err(rstd::move(resolved).unwrap_err());
    if (resolved->is_none()) {
      auto event =
          emit_name(EventKind::IncludeNotFound, name->as_str(), location);
      if (event.is_err())
        return Err(rstd::move(event).unwrap_err());
      return Err(failure(
          rstd::format("header '{}' was not found", name->as_str()), location));
    }
    auto value = rstd::move(resolved).unwrap().unwrap();
    auto event = emit(Event{
        .kind = EventKind::IncludeResolved,
        .name = rstd::move(name).unwrap(),
        .path = Some(value.path.clone()),
        .location = location,
    });
    if (event.is_err())
      return Err(rstd::move(event).unwrap_err());
    return process_file(value.path.as_path(), Some(value.search_index));
  }

  auto handle_conditional(ref<str> directive, const Vec<Token> &rest,
                          SourceLocation location,
                          Vec<ConditionalFrame> &conditions) -> Result<bool> {
    if (directive == "if"_str || directive == "ifdef"_str ||
        directive == "ifndef"_str) {
      auto parent = active(conditions);
      auto value = false;
      if (parent) {
        if (directive == "if"_str) {
          auto evaluated = condition_value(rest);
          if (evaluated.is_err())
            return Err(rstd::move(evaluated).unwrap_err());
          value = *evaluated;
        } else {
          if (rest.len() != usize(1) ||
              rest[usize{}].kind != TokenKind::Identifier) {
            return Err(failure(
                "conditional directive requires one identifier"_str, location));
          }
          value = macros_.contains(rest[usize{}].text.as_str()) ||
                  is_dynamic_builtin(rest[usize{}].text.as_str());
          if (directive == "ifndef"_str)
            value = !value;
        }
      }
      conditions.push(ConditionalFrame{
          .parent_active = parent,
          .branch_taken = parent && value,
          .active = parent && value,
          .location = location,
      });
      auto event = emit_name(EventKind::Conditional,
                             value ? "true"_str : "false"_str, location);
      if (event.is_err())
        return Err(rstd::move(event).unwrap_err());
      return Ok(true);
    }
    if (directive == "elif"_str || directive == "elifdef"_str ||
        directive == "elifndef"_str) {
      if (conditions.is_empty())
        return Err(failure("#elif without #if"_str, location));
      auto &frame = conditions[conditions.len() - usize(1)];
      if (frame.saw_else)
        return Err(failure("#elif after #else"_str, location));
      auto value = false;
      if (frame.parent_active && !frame.branch_taken) {
        if (directive == "elif"_str) {
          auto evaluated = condition_value(rest);
          if (evaluated.is_err())
            return Err(rstd::move(evaluated).unwrap_err());
          value = *evaluated;
        } else {
          if (rest.len() != usize(1) ||
              rest[usize{}].kind != TokenKind::Identifier) {
            return Err(failure(
                "conditional directive requires one identifier"_str, location));
          }
          value = macros_.contains(rest[usize{}].text.as_str()) ||
                  is_dynamic_builtin(rest[usize{}].text.as_str());
          if (directive == "elifndef"_str)
            value = !value;
        }
      }
      frame.active = frame.parent_active && !frame.branch_taken && value;
      frame.branch_taken = frame.branch_taken || frame.active;
      return Ok(true);
    }
    if (directive == "else"_str) {
      if (conditions.is_empty())
        return Err(failure("#else without #if"_str, location));
      auto &frame = conditions[conditions.len() - usize(1)];
      if (frame.saw_else)
        return Err(failure("duplicate #else"_str, location));
      frame.saw_else = true;
      frame.active = frame.parent_active && !frame.branch_taken;
      frame.branch_taken = true;
      return Ok(true);
    }
    if (directive == "endif"_str) {
      if (conditions.is_empty())
        return Err(failure("#endif without #if"_str, location));
      (void)conditions.pop();
      return Ok(true);
    }
    return Ok(false);
  }

  auto flush_normal(Vec<Token> &normal) -> Result<empty> {
    if (normal.is_empty())
      return Ok(empty{});
    auto disabled = Vec<String>::make();
    auto expanded = expand(rstd::move(normal), disabled);
    normal = Vec<Token>::make();
    if (expanded.is_err())
      return Err(rstd::move(expanded).unwrap_err());
    return rstd::as<PreprocessedTokenConsumer>(consumer_).consume(
        rstd::move(expanded).unwrap());
  }

  auto process_file(ref<rstd::path::Path> path, Option<usize> search_index)
      -> Result<empty> {
    if (include_stack_.len() >= request_.maximum_include_depth) {
      return Err(Error::make(
          rstd::format("maximum include depth exceeded at '{}'", path)));
    }
    auto path_value = path.to_str();
    if (path_value.is_some() && once_files_.contains_key(*path_value)) {
      return Ok(empty{});
    }
    if (path_value.is_some()) {
      auto guard = include_guards_.get(*path_value);
      if (guard.is_some() && macros_.contains((**guard).as_str())) {
        return Ok(empty{});
      }
    }
    auto loaded = rstd::as<SourceProvider>(source_provider_).load(path);
    if (loaded.is_err())
      return Err(rstd::move(loaded).unwrap_err());
    input_bytes_ += loaded->get()->snapshot.get()->contents.len();
    auto source = sources_.add(loaded->get()->snapshot.clone());
    auto main_file = include_stack_.is_empty();
    if (main_file)
      main_source_ = source;
    auto tokens = Vec<Token>::with_capacity(loaded->get()->tokens.len());
    for (const auto &cached : loaded->get()->tokens) {
      auto token = cached.clone();
      token.spelling.source = source;
      token.expansion.source = source;
      tokens.push(rstd::move(token));
    }
    auto prepared_queries = prepare_queries(tokens);
    if (prepared_queries.is_err()) {
      return Err(rstd::move(prepared_queries).unwrap_err());
    }
    if (path_value.is_some()) {
      auto guard = detected_include_guard(tokens);
      if (guard.is_some()) {
        include_guards_.insert(String::make(*path_value),
                               rstd::move(guard).unwrap());
      }
    }
    include_stack_.push(
        IncludeFrame{.source = source, .search_index = search_index});
    auto entered = emit(Event{
        .kind = EventKind::EnterFile,
        .path = Some(rstd::path::PathBuf::from(sources_.path(source))),
        .location = SourceLocation{.source = source},
    });
    if (entered.is_err())
      return Err(rstd::move(entered).unwrap_err());

    auto normal = Vec<Token>::make();
    auto conditions = Vec<ConditionalFrame>::make();
    for (auto cursor = usize{}; cursor < tokens.len();) {
      auto end = cursor;
      while (end < tokens.len() && tokens[end].kind != TokenKind::Newline)
        ++end;
      auto directive = cursor < end && tokens[cursor].start_of_line &&
                       tokens[cursor].text.as_str() == "#"_str;
      if (!directive) {
        if (active(conditions)) {
          for (auto index = cursor; index < end; ++index)
            normal.push(tokens[index].clone());
          if (end < tokens.len())
            normal.push(tokens[end].clone());
        }
        cursor = end < tokens.len() ? end + usize(1) : end;
        continue;
      }

      auto flushed = flush_normal(normal);
      if (flushed.is_err())
        return Err(rstd::move(flushed).unwrap_err());
      auto line = without_newline(tokens, cursor + usize(1), end);
      if (line.is_err())
        return Err(rstd::move(line).unwrap_err());
      if (line->is_empty()) {
        cursor = end < tokens.len() ? end + usize(1) : end;
        continue;
      }
      if ((*line)[usize{}].kind != TokenKind::Identifier) {
        return Err(failure("invalid preprocessing directive"_str,
                           (*line)[usize{}].expansion));
      }
      auto keyword = (*line)[usize{}].text.as_str();
      auto location = (*line)[usize{}].expansion;
      auto rest = clone_range(*line, usize(1), line->len());
      auto conditional =
          handle_conditional(keyword, rest, location, conditions);
      if (conditional.is_err())
        return Err(rstd::move(conditional).unwrap_err());
      if (*conditional) {
        cursor = end < tokens.len() ? end + usize(1) : end;
        continue;
      }
      if (!active(conditions)) {
        cursor = end < tokens.len() ? end + usize(1) : end;
        continue;
      }

      if (keyword == "define"_str) {
        auto result = define_macro(rest, true);
        if (result.is_err())
          return Err(rstd::move(result).unwrap_err());
      } else if (keyword == "undef"_str) {
        if (rest.len() != usize(1) ||
            rest[usize{}].kind != TokenKind::Identifier) {
          return Err(failure("#undef requires one identifier"_str, location));
        }
        auto removed = macros_.undefine(rest[usize{}].text.as_str());
        (void)removed;
        auto event = emit_name(EventKind::MacroUndefined,
                               rest[usize{}].text.as_str(), location);
        if (event.is_err())
          return Err(rstd::move(event).unwrap_err());
      } else if (keyword == "include"_str || keyword == "include_next"_str) {
        auto included =
            handle_include(rest, keyword == "include_next"_str, location);
        if (included.is_err())
          return Err(rstd::move(included).unwrap_err());
      } else if (keyword == "pragma"_str) {
        auto handled = handle_pragma(rstd::move(rest), location);
        if (handled.is_err())
          return Err(rstd::move(handled).unwrap_err());
      } else if (keyword == "error"_str) {
        return Err(
            failure(rstd::format("#error {}",
                                 directive_message(rest, usize{}).as_str()),
                    location));
      } else if (keyword == "warning"_str) {
        auto event =
            emit_name(EventKind::Diagnostic,
                      directive_message(rest, usize{}).as_str(), location);
        if (event.is_err())
          return Err(rstd::move(event).unwrap_err());
      } else if (keyword == "line"_str) {
        auto disabled = Vec<String>::make();
        auto expanded = expand(rstd::move(rest), disabled);
        if (expanded.is_err())
          return Err(rstd::move(expanded).unwrap_err());
        if (expanded->is_empty() ||
            (*expanded)[usize{}].kind != TokenKind::PpNumber) {
          return Err(failure("#line requires a line number"_str, location));
        }
        auto number = Vec<Token>::make();
        number.push((*expanded)[usize{}].clone());
        auto value = evaluate_expression(number);
        if (value.is_err() || *value <= i64{}) {
          return Err(
              failure("#line requires a positive line number"_str, location));
        }
        auto presumed = Option<rstd::path::PathBuf>{};
        if (expanded->len() > usize(1)) {
          if ((*expanded)[usize(1)].kind != TokenKind::StringLiteral) {
            return Err(failure("#line file name must be a string literal"_str,
                               location));
          }
          auto text = (*expanded)[usize(1)].text.as_str();
          if (text.len() < usize(2)) {
            return Err(failure("invalid #line file name"_str, location));
          }
          auto inner = text.get(usize(1), text.len() - usize(1));
          if (inner.is_none()) {
            return Err(failure("invalid #line file name"_str, location));
          }
          presumed = Some(rstd::path::PathBuf::from(*inner));
        }
        auto physical = end < tokens.len()
                            ? tokens[end].expansion.line + usize(1)
                            : location.line + usize(1);
        auto requested = rstd::as_cast<usize>(*value);
        for (auto adjust = end < tokens.len() ? end + usize(1) : end;
             adjust < tokens.len(); ++adjust) {
          if (tokens[adjust].expansion.line >= physical) {
            auto line = requested + (tokens[adjust].expansion.line - physical);
            tokens[adjust].spelling.line = line;
            tokens[adjust].expansion.line = line;
            if (presumed.is_some()) {
              tokens[adjust].presumed_path =
                  Some(rstd::path::PathBuf::from(presumed->as_path()));
            }
          }
        }
      } else {
        return Err(failure(
            rstd::format("unsupported preprocessing directive '#{}'", keyword),
            location));
      }
      cursor = end < tokens.len() ? end + usize(1) : end;
    }
    auto flushed = flush_normal(normal);
    if (flushed.is_err())
      return Err(rstd::move(flushed).unwrap_err());
    if (!conditions.is_empty()) {
      return Err(failure("unterminated conditional directive"_str,
                         conditions[conditions.len() - usize(1)].location));
    }
    auto exited = emit(Event{
        .kind = EventKind::ExitFile,
        .path = Some(rstd::path::PathBuf::from(sources_.path(source))),
        .location = SourceLocation{.source = source},
    });
    if (exited.is_err())
      return Err(rstd::move(exited).unwrap_err());
    (void)include_stack_.pop();
    return Ok(empty{});
  }

  PreprocessRequest request_;
  Sources &source_provider_;
  Includes &include_resolver_;
  Builtins &builtin_provider_;
  Pragmas &pragma_handler_;
  Events &event_sink_;
  Consumer &consumer_;
  SourceManager sources_;
  MacroTable macros_;
  Vec<IncludeFrame> include_stack_;
  rstd::collections::BTreeMap<String, empty> once_files_;
  rstd::collections::BTreeMap<String, String> include_guards_;
  rstd::collections::BTreeMap<String, Vec<Option<SharedMacroDefinition>>>
      macro_stacks_;
  usize counter_{};
  usize input_bytes_{};
  SourceId main_source_{};
  String builtin_identity_;
};

class CollectPreprocessedTokens {
public:
  auto consume(Vec<Token> tokens) -> Result<empty> {
    for (auto &token : tokens)
      tokens_.push(rstd::move(token));
    return Ok(empty{});
  }

  auto take() -> Vec<Token> { return rstd::move(tokens_); }

private:
  Vec<Token> tokens_;
};

template <typename Sources, typename Includes, typename Builtins,
          typename Pragmas, typename Events, typename Consumer>
auto preprocess_to(PreprocessRequest request, Sources &sources,
                   Includes &includes, Builtins &builtins, Pragmas &pragmas,
                   Events &events, Consumer &consumer)
    -> Result<PreprocessedTranslationUnit> {
  return PreprocessorSession<Sources, Includes, Builtins, Pragmas, Events,
                             Consumer>(rstd::move(request), sources, includes,
                                       builtins, pragmas, events, consumer)
      .run();
}

template <typename Sources, typename Includes, typename Builtins,
          typename Pragmas, typename Events>
auto preprocess(PreprocessRequest request, Sources &sources, Includes &includes,
                Builtins &builtins, Pragmas &pragmas, Events &events)
    -> Result<PreprocessedTranslationUnit> {
  auto consumer = CollectPreprocessedTokens{};
  auto result = preprocess_to(rstd::move(request), sources, includes, builtins,
                              pragmas, events, consumer);
  if (result.is_err())
    return Err(rstd::move(result).unwrap_err());
  result->tokens = consumer.take();
  return result;
}

} // namespace tenon::frontend::preprocessor
