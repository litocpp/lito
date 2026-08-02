export module tenon.toolchain:clang_preprocessor_environment;

import rstd;
import tenon.model;
import tenon.process;
import tenon.frontend;
import :clang_options;
import :command;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace tenon {
namespace lexical = frontend::lexical;
namespace preprocessor = frontend::preprocessor;
}

export namespace tenon::toolchain {

struct IncludeSearchEntry {
  PathBuf directory;
  bool system{true};
};

struct BuiltinMacroSnapshot {
  String key;
  String identity;
  lexical::SharedSourceSnapshot source;
  Vec<preprocessor::SharedMacroDefinition> definitions;
};

using SharedBuiltinMacroSnapshot = rstd::rc::Rc<const BuiltinMacroSnapshot>;

struct PreprocessorEnvironment {
  String context_id;
  SharedBuiltinMacroSnapshot builtin_macros;
  lexical::SharedSourceSnapshot command_line_source;
  Vec<preprocessor::SharedMacroDefinition> command_line_definitions;
  Vec<IncludeSearchEntry> include_search;
  Vec<String> query_command;
  String identity;
  PathBuf working_directory;
  rstd::collections::BTreeMap<String, i64> builtin_results;
  Option<String> date;
  Option<String> time;
};

} // namespace tenon::toolchain

namespace tenon::toolchain {

template <typename T> auto environment_failure(String message) -> Result<T> {
  return Err(Error::make(ErrorKind::Toolchain, rstd::move(message)));
}

template <typename T> auto environment_failure(ref<str> message) -> Result<T> {
  return Err(Error::make(ErrorKind::Toolchain, message));
}

auto clone_command(const Vec<String> &source) -> Vec<String> {
  auto result = Vec<String>::with_capacity(source.len());
  for (const auto &argument : source)
    result.push(argument.clone());
  return result;
}

template <typename Callback>
auto each_line(ref<str> text, Callback &&callback) -> Result<empty> {
  auto bytes = text.as_bytes();
  auto begin = usize{};
  while (begin <= bytes.len()) {
    auto end = begin;
    while (end < bytes.len() && bytes[end] != u8('\n') &&
           bytes[end] != u8('\r'))
      ++end;
    auto line = text.get(begin, end);
    if (line.is_none())
      return environment_failure<empty>("invalid UTF-8 line boundary"_str);
    auto result = callback(*line);
    if (result.is_err())
      return result;
    if (end == bytes.len())
      break;
    if (bytes[end] == u8('\r') && end + usize(1) < bytes.len() &&
        bytes[end + usize(1)] == u8('\n')) {
      ++end;
    }
    begin = end + usize(1);
  }
  return Ok(empty{});
}

auto parse_macro_dump(ref<str> output) -> Result<Vec<preprocessor::MacroSeed>> {
  auto macros = Vec<preprocessor::MacroSeed>::make();
  auto parsed = each_line(output, [&macros](ref<str> raw) -> Result<empty> {
    auto line = raw.trim_ascii();
    if (line.is_empty())
      return Ok(empty{});
    constexpr auto prefix = "#define "_str;
    if (!line.starts_with(prefix)) {
      return environment_failure<empty>(
          rstd::format("unexpected clang++ -dM output line: {}", line));
    }
    auto definition = line.get(prefix.len(), line.len());
    if (definition.is_none() || definition->is_empty()) {
      return environment_failure<empty>(
          "clang++ -dM emitted an empty definition"_str);
    }
    macros.push(
        preprocessor::MacroSeed{.definition = String::make(*definition)});
    return Ok(empty{});
  });
  if (parsed.is_err())
    return Err(rstd::move(parsed).unwrap_err());
  return Ok(rstd::move(macros));
}

struct ParsedMacroSet {
  lexical::SharedSourceSnapshot source;
  Vec<preprocessor::SharedMacroDefinition> definitions;
};

auto parse_macro_seeds(const Vec<preprocessor::MacroSeed> &seeds,
                       ref<str> source_name) -> Result<ParsedMacroSet> {
  auto text = String::make();
  for (const auto &seed : seeds) {
    text.push_str("#define "_str);
    text.push_str(seed.definition.as_str());
    text.push_ascii('\n');
  }
  auto snapshot = lexical::make_source_snapshot(lexical::SourceBuffer{
      .path = PathBuf::from(source_name),
      .contents = rstd::move(text),
  });
  auto source = lexical::SourceFile{.snapshot = snapshot.clone()};
  auto tokens = lexical::lex(source, true);
  if (tokens.is_err()) {
    return environment_failure<ParsedMacroSet>(
        rstd::move(tokens).unwrap_err().message.clone());
  }
  auto definitions = Vec<preprocessor::SharedMacroDefinition>::make();
  for (auto cursor = usize{}; cursor < tokens->len();) {
    auto end = cursor;
    while (end < tokens->len() &&
           (*tokens)[end].kind != lexical::TokenKind::Newline) {
      ++end;
    }
    if (cursor == end) {
      cursor = end < tokens->len() ? end + usize(1) : end;
      continue;
    }
    if (cursor + usize(2) > end ||
        (*tokens)[cursor].text.as_str() != "#"_str ||
        (*tokens)[cursor + usize(1)].text.as_str() != "define"_str) {
      return environment_failure<ParsedMacroSet>(
          "invalid cached predefined macro line"_str);
    }
    auto line = Vec<lexical::Token>::make();
    for (auto index = cursor + usize(2); index < end; ++index)
      line.push((*tokens)[index].clone());
    auto definition = preprocessor::parse_macro_definition(line);
    if (definition.is_err()) {
      return environment_failure<ParsedMacroSet>(
          rstd::move(definition).unwrap_err().message.clone());
    }
    definitions.push(preprocessor::share_macro_definition(
        rstd::move(definition).unwrap()));
    cursor = end < tokens->len() ? end + usize(1) : end;
  }
  return Ok(ParsedMacroSet{
      .source = rstd::move(snapshot),
      .definitions = rstd::move(definitions),
  });
}

auto command_line_macro_seeds(const Vec<String> &definitions)
    -> Vec<preprocessor::MacroSeed> {
  auto seeds = Vec<preprocessor::MacroSeed>::with_capacity(definitions.len());
  for (const auto &definition : definitions) {
    auto bytes = definition.as_str().as_bytes();
    auto equal = usize{};
    while (equal < bytes.len() && bytes[equal] != u8('='))
      ++equal;
    auto value = String::make();
    auto name = definition.as_str().get(usize{}, equal);
    if (name.is_some())
      value.push_str(*name);
    value.push_ascii(' ');
    if (equal < bytes.len()) {
      auto replacement =
          definition.as_str().get(equal + usize(1), bytes.len());
      if (replacement.is_some())
        value.push_str(*replacement);
    } else {
      value.push_ascii('1');
    }
    seeds.push(preprocessor::MacroSeed{.definition = rstd::move(value)});
  }
  return seeds;
}

auto parse_include_search(ref<str> output) -> Result<Vec<IncludeSearchEntry>> {
  auto entries = Vec<IncludeSearchEntry>::make();
  auto inside = false;
  auto saw_start = false;
  auto saw_end = false;
  auto system = true;
  auto parsed = each_line(output, [&](ref<str> raw) -> Result<empty> {
    auto line = raw.trim_ascii();
    if (line == "#include \"...\" search starts here:"_str) {
      inside = true;
      saw_start = true;
      system = false;
      return Ok(empty{});
    }
    if (line == "#include <...> search starts here:"_str) {
      inside = true;
      saw_start = true;
      system = true;
      return Ok(empty{});
    }
    if (line == "End of search list."_str) {
      if (inside)
        saw_end = true;
      inside = false;
      return Ok(empty{});
    }
    if (!inside || line.is_empty())
      return Ok(empty{});
    if (line.contains("(framework directory)"_str)) {
      return environment_failure<empty>(
          rstd::format("framework include search is unsupported: {}", line));
    }
    auto directory = PathBuf::from(line);
    auto canonical = rstd::fs::canonicalize(directory.as_path());
    if (canonical.is_err()) {
      return environment_failure<empty>(rstd::format(
          "cannot resolve clang include directory '{}': {}",
          directory.as_path(), rstd::move(canonical).unwrap_err()));
    }
    entries.push(IncludeSearchEntry{.directory = rstd::move(canonical).unwrap(),
                                    .system = system});
    return Ok(empty{});
  });
  if (parsed.is_err())
    return Err(rstd::move(parsed).unwrap_err());
  if (!saw_start || !saw_end) {
    return environment_failure<Vec<IncludeSearchEntry>>(
        "clang++ -E -v did not emit a complete include search list"_str);
  }
  return Ok(rstd::move(entries));
}

auto macro_snapshot_identity(const Vec<preprocessor::MacroSeed> &macros,
                             ref<str> key) -> String {
  constexpr rstd::uint64_t offset = 14695981039346656037ull;
  constexpr rstd::uint64_t prime = 1099511628211ull;
  auto hash = offset;
  auto add = [&hash](ref<str> value) {
    for (auto byte : value) {
      hash ^= byte.to_primitive();
      hash *= prime;
    }
    hash ^= 0;
    hash *= prime;
  };
  add("tenon-clang-builtin-macros-v1"_str);
  add(key);
  for (const auto &macro : macros)
    add(macro.definition.as_str());
  static constexpr char digits[] = "0123456789abcdef";
  char result[16];
  for (rstd::size_t index = 0; index < 16; ++index) {
    result[15 - index] = digits[hash & 0xfu];
    hash >>= 4u;
  }
  return String::make(ref<str>::from_raw_parts_unchecked(
      reinterpret_cast<const byte *>(result), usize(16)));
}

auto environment_identity(ref<str> builtin_identity,
                          const Vec<IncludeSearchEntry> &includes,
                          ref<str> context_id) -> Result<String> {
  constexpr rstd::uint64_t offset = 14695981039346656037ull;
  constexpr rstd::uint64_t prime = 1099511628211ull;
  auto hash = offset;
  auto add = [&hash](ref<str> value) {
    for (auto byte : value) {
      hash ^= byte.to_primitive();
      hash *= prime;
    }
    hash ^= 0;
    hash *= prime;
  };
  add("tenon-clang-preprocessor-environment-v1"_str);
  add(context_id);
  add(builtin_identity);
  for (const auto &include : includes) {
    auto text = include.directory.as_path().to_str();
    if (text.is_none()) {
      return environment_failure<String>(
          rstd::format("include directory '{}' is not valid UTF-8",
                       include.directory.as_path()));
    }
    add(*text);
    add(include.system ? "system"_str : "quote"_str);
  }
  static constexpr char digits[] = "0123456789abcdef";
  char result[16];
  for (rstd::size_t index = 0; index < 16; ++index) {
    result[15 - index] = digits[hash & 0xfu];
    hash >>= 4u;
  }
  return Ok(String::make(ref<str>::from_raw_parts_unchecked(
      reinterpret_cast<const byte *>(result), usize(16))));
}

auto preprocessor_error(const Error &error) -> preprocessor::Error {
  return preprocessor::Error::make(error.message.clone());
}

} // namespace tenon::toolchain

export namespace tenon::toolchain {

auto query_builtin_macro_snapshot(const Vec<String> &base_command,
                                  ref<str> key,
                                  ref<rstd::path::Path> working_directory)
    -> Result<SharedBuiltinMacroSnapshot> {
  auto macro_command = clone_command(base_command);
  command::push_option(macro_command, clang_options::DUMP_MACROS);
  command::push_option(macro_command, clang_options::PREPROCESS);
  command::push_option(macro_command, clang_options::LANGUAGE);
  command::push_option(macro_command, clang_options::CXX_SOURCE);
  command::push_option(macro_command, clang_options::STANDARD_INPUT);
  auto macro_output =
      run_command_with_input(macro_command, ""_str, Some(working_directory));
  if (macro_output.is_err())
    return Err(rstd::move(macro_output).unwrap_err());
  if (macro_output->exit_code != i32{}) {
    return environment_failure<SharedBuiltinMacroSnapshot>(rstd::format(
        "clang++ -dM failed\n{}\n{}", command_text(macro_command).as_str(),
        macro_output->standard_error.as_str()));
  }
  auto macros = parse_macro_dump(macro_output->standard_output.as_str());
  if (macros.is_err())
    return Err(rstd::move(macros).unwrap_err());
  auto parsed = parse_macro_seeds(*macros, "<built-in>"_str);
  if (parsed.is_err())
    return Err(rstd::move(parsed).unwrap_err());
  auto values = rstd::move(parsed).unwrap();
  return Ok(rstd::rc::make_rc<BuiltinMacroSnapshot>(BuiltinMacroSnapshot{
                .key = String::make(key),
                .identity = macro_snapshot_identity(*macros, key),
                .source = rstd::move(values.source),
                .definitions = rstd::move(values.definitions),
            })
                .to_const());
}

auto query_preprocessor_environment(const Vec<String> &base_command,
                                    ref<str> context_id,
                                    ref<rstd::path::Path> working_directory,
                                    SharedBuiltinMacroSnapshot builtin_macros,
                                    const Vec<String> &definitions)
    -> Result<PreprocessorEnvironment> {
  auto command_line_seeds = command_line_macro_seeds(definitions);
  auto command_line_macros =
      parse_macro_seeds(command_line_seeds, "<command-line>"_str);
  if (command_line_macros.is_err())
    return Err(rstd::move(command_line_macros).unwrap_err());
  auto command_line_values = rstd::move(command_line_macros).unwrap();

  auto include_command = clone_command(base_command);
  command::push_option(include_command, clang_options::PREPROCESS);
  command::push_option(include_command, clang_options::VERBOSE);
  command::push_option(include_command, clang_options::LANGUAGE);
  command::push_option(include_command, clang_options::CXX_SOURCE);
  command::push_option(include_command, clang_options::STANDARD_INPUT);
  auto include_output =
      run_command_with_input(include_command, ""_str, Some(working_directory));
  if (include_output.is_err())
    return Err(rstd::move(include_output).unwrap_err());
  if (include_output->exit_code != i32{}) {
    return environment_failure<PreprocessorEnvironment>(rstd::format(
        "clang++ -E -v failed\n{}\n{}", command_text(include_command).as_str(),
        include_output->standard_error.as_str()));
  }
  auto includes = parse_include_search(include_output->standard_error.as_str());
  if (includes.is_err())
    return Err(rstd::move(includes).unwrap_err());
  auto identity = environment_identity(builtin_macros.get()->identity.as_str(),
                                       *includes, context_id);
  if (identity.is_err())
    return Err(rstd::move(identity).unwrap_err());
  return Ok(PreprocessorEnvironment{
      .context_id = String::make(context_id),
      .builtin_macros = rstd::move(builtin_macros),
      .command_line_source = rstd::move(command_line_values.source),
      .command_line_definitions =
          rstd::move(command_line_values.definitions),
      .include_search = rstd::move(includes).unwrap(),
      .query_command = clone_command(base_command),
      .identity = rstd::move(identity).unwrap(),
  });
}

class ClangIncludeResolver {
public:
  explicit ClangIncludeResolver(const PreprocessorEnvironment &environment)
      : environment_(environment) {}

  auto resolve(const preprocessor::IncludeRequest &request)
      -> preprocessor::Result<Option<preprocessor::IncludeResolution>> {
    auto next = request.kind == preprocessor::IncludeKind::NextQuoted ||
                request.kind == preprocessor::IncludeKind::NextAngled;
    auto quoted = request.kind == preprocessor::IncludeKind::Quoted ||
                  request.kind == preprocessor::IncludeKind::NextQuoted;
    auto start = next && request.previous_search_index.is_some()
                     ? *request.previous_search_index + usize(1)
                     : usize{};
    if (quoted && !next && start == usize{}) {
      auto parent = request.including_path.as_path().parent();
      if (parent.is_some()) {
        auto resolved =
            candidate(*parent, request.name.as_str(), usize{}, false);
        if (resolved.is_err() || resolved->is_some())
          return resolved;
      }
      start = usize(1);
    }
    if (start == usize{})
      start = usize(1);
    for (auto index = start; index <= environment_.include_search.len();
         ++index) {
      const auto &entry = environment_.include_search[index - usize(1)];
      auto resolved = candidate(entry.directory.as_path(),
                                request.name.as_str(), index, entry.system);
      if (resolved.is_err() || resolved->is_some())
        return resolved;
    }
    return Ok(None());
  }

private:
  auto candidate(ref<rstd::path::Path> directory, ref<str> name,
                 usize search_index, bool system)
      -> preprocessor::Result<Option<preprocessor::IncludeResolution>> {
    auto requested =
        PathBuf::from(directory).join(PathBuf::from(name).as_path());
    auto exists = rstd::fs::exists(requested.as_path());
    if (exists.is_err()) {
      return Err(preprocessor::Error::make(
          rstd::format("cannot inspect include candidate '{}': {}",
                       requested.as_path(), rstd::move(exists).unwrap_err())));
    }
    if (!*exists)
      return Ok(None());
    auto canonical = rstd::fs::canonicalize(requested.as_path());
    if (canonical.is_err()) {
      return Err(preprocessor::Error::make(rstd::format(
          "cannot resolve include candidate '{}': {}", requested.as_path(),
          rstd::move(canonical).unwrap_err())));
    }
    return Ok(Some(preprocessor::IncludeResolution{
        .path = rstd::move(canonical).unwrap(),
        .search_index = search_index,
        .system = system,
    }));
  }

  const PreprocessorEnvironment &environment_;
};

class ClangBuiltinProvider {
public:
  ClangBuiltinProvider(PreprocessorEnvironment &environment,
                       ref<rstd::path::Path> working_directory)
      : environment_(environment),
        working_directory_(PathBuf::from(working_directory)) {}

  auto predefined_macros()
      -> preprocessor::Result<Vec<preprocessor::SharedMacroDefinition>> {
    auto result = Vec<preprocessor::SharedMacroDefinition>::with_capacity(
        environment_.builtin_macros.get()->definitions.len() +
        environment_.command_line_definitions.len());
    for (const auto &definition :
         environment_.builtin_macros.get()->definitions) {
      result.push(definition.clone());
    }
    for (const auto &definition : environment_.command_line_definitions)
      result.push(definition.clone());
    return Ok(rstd::move(result));
  }

  auto prepare(const Vec<preprocessor::BuiltinQuery> &queries)
      -> preprocessor::Result<empty> {
    auto pending = Vec<preprocessor::BuiltinQuery>::make();
    auto keys = Vec<String>::make();
    auto seen = rstd::collections::BTreeMap<String, empty>::make();
    for (const auto &query : queries) {
      auto key = query_key(query);
      if (environment_.builtin_results.contains_key(key.as_str()) ||
          seen.contains_key(key.as_str())) {
        continue;
      }
      seen.insert(key.clone(), empty{});
      keys.push(rstd::move(key));
      pending.push(query.clone());
    }
    if (pending.is_empty())
      return Ok(empty{});

    auto source = String::make();
    for (auto index = usize{}; index < pending.len(); ++index) {
      source.push_str(rstd::format("TENON_BUILTIN_QUERY_{} {}({})\n", index,
                                   builtin_name(pending[index].kind),
                                   pending[index].argument.as_str())
                          .as_str());
    }
    auto command_line = clone_command(environment_.query_command);
    command::push_option(command_line, clang_options::PREPROCESS);
    command::push_option(command_line, clang_options::NO_LINE_MARKERS);
    command::push_option(command_line, clang_options::LANGUAGE);
    command::push_option(command_line, clang_options::CXX_SOURCE);
    command::push_option(command_line, clang_options::STANDARD_INPUT);
    auto output = run_command_with_input(command_line, source.as_str(),
                                         Some(working_directory_.as_path()));
    if (output.is_err())
      return Err(preprocessor_error(rstd::move(output).unwrap_err()));
    if (output->exit_code != i32{}) {
      return Err(preprocessor::Error::at(
          rstd::format("clang builtin query batch failed: {}",
                       output->standard_error.as_str()),
          pending[usize{}].location));
    }

    auto values = Vec<i64>::make();
    auto parsed = each_line(
        output->standard_output.as_str(),
        [&values](ref<str> raw) -> Result<empty> {
          auto line = raw.trim_ascii();
          if (line.is_empty())
            return Ok(empty{});
          constexpr auto prefix = "TENON_BUILTIN_QUERY_"_str;
          if (!line.starts_with(prefix)) {
            return environment_failure<empty>(rstd::format(
                "unexpected clang builtin query output: {}", line));
          }
          auto cursor = prefix.len();
          while (cursor < line.len() && line.as_bytes()[cursor] >= u8('0') &&
                 line.as_bytes()[cursor] <= u8('9')) {
            ++cursor;
          }
          while (cursor < line.len() && line.as_bytes()[cursor] == u8(' '))
            ++cursor;
          auto value_text = line.get(cursor, line.len());
          if (value_text.is_none() || value_text->is_empty()) {
            return environment_failure<empty>(
                "clang builtin query emitted no value"_str);
          }
          auto probe_source = preprocessor::SourceFile::make(
              preprocessor::SourceId{},
              preprocessor::SourceBuffer{
                  .path = PathBuf::from("<clang-builtin-query>"_str),
                  .contents = String::make(*value_text),
              });
          auto tokens = preprocessor::lex(probe_source);
          if (tokens.is_err()) {
            return environment_failure<empty>(
                rstd::move(tokens).unwrap_err().message.clone());
          }
          auto filtered = Vec<preprocessor::Token>::make();
          for (auto &token : *tokens) {
            if (token.kind != preprocessor::TokenKind::Newline) {
              filtered.push(rstd::move(token));
            }
          }
          auto value = preprocessor::evaluate_expression(filtered);
          if (value.is_err()) {
            return environment_failure<empty>(
                rstd::move(value).unwrap_err().message.clone());
          }
          values.push(i64(*value));
          return Ok(empty{});
        });
    if (parsed.is_err())
      return Err(preprocessor_error(rstd::move(parsed).unwrap_err()));
    if (values.len() != pending.len()) {
      return Err(preprocessor::Error::at(
          rstd::format("clang builtin query returned {} values for {} queries",
                       values.len(), pending.len()),
          pending[usize{}].location));
    }
    for (auto index = usize{}; index < values.len(); ++index) {
      environment_.builtin_results.insert(rstd::move(keys[index]),
                                          i64(values[index]));
    }
    return Ok(empty{});
  }

  auto evaluate(const preprocessor::BuiltinQuery &query)
      -> preprocessor::Result<i64> {
    auto key = query_key(query);
    auto cached = environment_.builtin_results.get(key.as_str());
    if (cached.is_some())
      return Ok(**cached);
    auto one = Vec<preprocessor::BuiltinQuery>::make();
    one.push(query.clone());
    auto prepared = prepare(one);
    if (prepared.is_err())
      return Err(rstd::move(prepared).unwrap_err());
    auto result = environment_.builtin_results.get(key.as_str());
    if (result.is_none()) {
      return Err(preprocessor::Error::at(
          String::make("clang builtin query did not produce a result"_str),
          query.location));
    }
    return Ok(**result);
  }

  auto text(preprocessor::BuiltinTextKind kind)
      -> preprocessor::Result<String> {
    if (environment_.date.is_none() || environment_.time.is_none()) {
      auto queried = query_text_builtins();
      if (queried.is_err())
        return Err(rstd::move(queried).unwrap_err());
    }
    const auto &value = kind == preprocessor::BuiltinTextKind::Date
                            ? environment_.date
                            : environment_.time;
    if (value.is_none()) {
      return Err(preprocessor::Error::make(
          "clang did not initialize a requested text builtin"_str));
    }
    return Ok(value->clone());
  }

private:
  auto query_text_builtins() -> preprocessor::Result<empty> {
    auto command_line = clone_command(environment_.query_command);
    command::push_option(command_line, clang_options::PREPROCESS);
    command::push_option(command_line, clang_options::NO_LINE_MARKERS);
    command::push_option(command_line, clang_options::LANGUAGE);
    command::push_option(command_line, clang_options::CXX_SOURCE);
    command::push_option(command_line, clang_options::STANDARD_INPUT);
    auto output = run_command_with_input(
        command_line,
        "TENON_BUILTIN_DATE __DATE__\nTENON_BUILTIN_TIME __TIME__\n"_str,
        Some(working_directory_.as_path()));
    if (output.is_err())
      return Err(preprocessor_error(rstd::move(output).unwrap_err()));
    if (output->exit_code != i32{}) {
      return Err(preprocessor::Error::make(
          rstd::format("clang text builtin query failed: {}",
                       output->standard_error.as_str())));
    }
    auto parsed = each_line(
        output->standard_output.as_str(),
        [this](ref<str> raw) -> Result<empty> {
          auto line = raw.trim_ascii();
          auto value = Option<ref<str>>{};
          auto target = static_cast<Option<String> *>(nullptr);
          constexpr auto date_prefix = "TENON_BUILTIN_DATE "_str;
          constexpr auto time_prefix = "TENON_BUILTIN_TIME "_str;
          if (line.starts_with(date_prefix)) {
            value = line.get(date_prefix.len(), line.len());
            target = rstd::addressof(environment_.date);
          } else if (line.starts_with(time_prefix)) {
            value = line.get(time_prefix.len(), line.len());
            target = rstd::addressof(environment_.time);
          } else if (!line.is_empty()) {
            return environment_failure<empty>(
                rstd::format("unexpected clang text builtin output: {}", line));
          }
          if (target == nullptr)
            return Ok(empty{});
          auto text = value->trim_ascii();
          if (text.len() < usize(2) || text.as_bytes()[usize{}] != u8('"') ||
              text.as_bytes()[text.len() - usize(1)] != u8('"')) {
            return environment_failure<empty>(
                rstd::format("invalid clang text builtin value: {}", text));
          }
          auto inner = text.get(usize(1), text.len() - usize(1));
          if (inner.is_none()) {
            return environment_failure<empty>(
                "invalid clang text builtin boundary"_str);
          }
          *target = Some(String::make(*inner));
          return Ok(empty{});
        });
    if (parsed.is_err())
      return Err(preprocessor_error(rstd::move(parsed).unwrap_err()));
    if (environment_.date.is_none() || environment_.time.is_none()) {
      return Err(preprocessor::Error::make(
          "clang text builtin query returned an incomplete snapshot"_str));
    }
    return Ok(empty{});
  }

  auto query_key(const preprocessor::BuiltinQuery &query) const -> String {
    return rstd::format("{}:{}", builtin_name(query.kind),
                        query.argument.as_str());
  }

  auto builtin_name(preprocessor::BuiltinQueryKind kind) const -> ref<str> {
    switch (kind) {
    case preprocessor::BuiltinQueryKind::HasBuiltin:
      return "__has_builtin"_str;
    case preprocessor::BuiltinQueryKind::HasConstexprBuiltin:
      return "__has_constexpr_builtin"_str;
    case preprocessor::BuiltinQueryKind::HasFeature:
      return "__has_feature"_str;
    case preprocessor::BuiltinQueryKind::HasExtension:
      return "__has_extension"_str;
    case preprocessor::BuiltinQueryKind::HasCppAttribute:
      return "__has_cpp_attribute"_str;
    case preprocessor::BuiltinQueryKind::HasAttribute:
      return "__has_attribute"_str;
    case preprocessor::BuiltinQueryKind::HasDeclspecAttribute:
      return "__has_declspec_attribute"_str;
    case preprocessor::BuiltinQueryKind::HasWarning:
      return "__has_warning"_str;
    case preprocessor::BuiltinQueryKind::IsIdentifier:
      return "__is_identifier"_str;
    }
    __builtin_unreachable();
  }

  PreprocessorEnvironment &environment_;
  PathBuf working_directory_;
};

class ClangPragmaHandler {
public:
  auto handle(const preprocessor::PragmaRequest &)
      -> preprocessor::Result<preprocessor::PragmaOutcome> {
    return Ok(preprocessor::PragmaOutcome::Ignored);
  }
};

class DependencyEvents {
public:
  auto on_event(const preprocessor::Event &event)
      -> preprocessor::Result<empty> {
    if (event.kind != preprocessor::EventKind::IncludeResolved ||
        event.path.is_none()) {
      return Ok(empty{});
    }
    auto text = event.path->as_path().to_str();
    if (text.is_none()) {
      return Err(preprocessor::Error::at(
          String::make("resolved include path is not valid UTF-8"_str),
          event.location));
    }
    if (!paths_.contains_key(*text)) {
      paths_.insert(String::make(*text), empty{});
      headers_.push(event.path->clone());
    }
    return Ok(empty{});
  }

  auto take_headers() -> Vec<PathBuf> {
    auto result = rstd::move(headers_);
    headers_ = Vec<PathBuf>::make();
    paths_ = rstd::collections::BTreeMap<String, empty>::make();
    return result;
  }

private:
  rstd::collections::BTreeMap<String, empty> paths_;
  Vec<PathBuf> headers_;
};

} // namespace tenon::toolchain
