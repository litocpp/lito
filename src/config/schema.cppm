export module tenon.config:schema;

import rstd;
import rstd.toml;
import tenon.model;
import tenon.toolchain.command;

using namespace rstd::prelude;
using namespace rstd::literals;
using Toml = rstd::toml::Value;
using Table = rstd::toml::Table;

namespace tenon {

template <typename T> auto config_failure(String message) -> Result<T> {
  return Err(Error::make(ErrorKind::Config, rstd::move(message)));
}

auto config_member(const Toml &value, ref<str> key) -> Option<ref<Toml>> {
  return value.get(key);
}

auto config_table(const Toml &value, ref<str> context) -> Result<ref<Table>> {
  auto table = value.as_table();
  if (table.is_none()) {
    return config_failure<ref<Table>>(
        rstd::format("{} must be a table", context));
  }
  return Ok(*table);
}

auto root_config_key(ref<str> key) -> bool { return key == "toolchain"_str; }

auto toolchain_config_key(ref<str> key) -> bool {
  return key == "compiler"_str || key == "scanner"_str ||
         key == "archiver"_str || key == "formatter"_str;
}

auto reject_config_unknown(const Table &table, ref<str> context,
                           bool (*allowed)(ref<str>)) -> Result<empty> {
  auto keys = table.keys();
  for (auto key = keys.next(); key.is_some(); key = keys.next()) {
    if (!allowed((**key).as_str())) {
      return config_failure<empty>(rstd::format(
          "{} contains unknown field '{}'", context, (**key).as_str()));
    }
  }
  return Ok(empty{});
}

auto configured_tool(const Toml &toolchain_value, ref<str> key,
                     ref<str> fallback)
    -> Result<PathBuf> {
  auto value = config_member(toolchain_value, key);
  if (value.is_none())
    return Ok(PathBuf::from(fallback));
  auto text = (**value).as_str();
  if (text.is_none()) {
    return config_failure<PathBuf>(
        rstd::format("config.toolchain.{} must be a string", key));
  }
  if (text->is_empty()) {
    return config_failure<PathBuf>(
        rstd::format("config.toolchain.{} must not be empty", key));
  }
  auto path = PathBuf::from(*text);
  if (!path.as_path().is_absolute() &&
      !toolchain::command::is_searchable_tool_name(path.as_path())) {
    return config_failure<PathBuf>(rstd::format(
        "config.toolchain.{} must be an executable name or absolute path",
        key));
  }
  return Ok(rstd::move(path));
}

auto default_toolchain() -> ToolchainSpec {
  return ToolchainSpec{
      .compiler = PathBuf::from("clang++"_str),
      .scanner = PathBuf::from("clang-scan-deps"_str),
      .archiver = PathBuf::from("llvm-ar"_str),
      .formatter = PathBuf::from("clang-format"_str),
  };
}

} // namespace tenon

export namespace tenon {

auto load_project_config(ref<rstd::path::Path> requested_root)
    -> Result<ProjectConfig> {
  auto canonical = rstd::fs::canonicalize(requested_root);
  if (canonical.is_err()) {
    return config_failure<ProjectConfig>(
        rstd::format("cannot resolve project root '{}': {}", requested_root,
                     rstd::move(canonical).unwrap_err()));
  }
  auto root = rstd::move(canonical).unwrap();
  auto metadata = rstd::fs::metadata(root.as_path());
  if (metadata.is_err()) {
    return config_failure<ProjectConfig>(
        rstd::format("cannot inspect project root '{}': {}", root.as_path(),
                     rstd::move(metadata).unwrap_err()));
  }
  if (!metadata->is_dir()) {
    return config_failure<ProjectConfig>(
        rstd::format("project root '{}' is not a directory", root.as_path()));
  }

  auto config_path =
      root.join(PathBuf::from(".tenon/config.toml"_str).as_path());
  auto exists = rstd::fs::exists(config_path.as_path());
  if (exists.is_err()) {
    return config_failure<ProjectConfig>(
        rstd::format("cannot inspect config '{}': {}", config_path.as_path(),
                     rstd::move(exists).unwrap_err()));
  }
  if (!*exists) {
    return Ok(ProjectConfig{
        .root = rstd::move(root),
        .toolchain = default_toolchain(),
    });
  }

  auto contents = rstd::fs::read_to_string(config_path.as_path());
  if (contents.is_err()) {
    return config_failure<ProjectConfig>(
        rstd::format("cannot read config '{}': {}", config_path.as_path(),
                     rstd::move(contents).unwrap_err()));
  }
  auto parsed = rstd::toml::from_str(contents->as_str());
  if (parsed.is_err()) {
    return config_failure<ProjectConfig>(
        rstd::format("cannot parse config '{}': {}", config_path.as_path(),
                     rstd::move(parsed).unwrap_err()));
  }
  auto document = rstd::move(parsed).unwrap();
  auto root_table = config_table(document, "config root"_str);
  if (root_table.is_err())
    return Err(rstd::move(root_table).unwrap_err());
  auto root_known =
      reject_config_unknown(**root_table, "config root"_str, root_config_key);
  if (root_known.is_err())
    return Err(rstd::move(root_known).unwrap_err());

  auto toolchain = default_toolchain();
  auto toolchain_value = config_member(document, "toolchain"_str);
  if (toolchain_value.is_some()) {
    auto table = config_table(**toolchain_value, "config.toolchain"_str);
    if (table.is_err())
      return Err(rstd::move(table).unwrap_err());
    auto known = reject_config_unknown(**table, "config.toolchain"_str,
                                       toolchain_config_key);
    if (known.is_err())
      return Err(rstd::move(known).unwrap_err());
    auto compiler =
        configured_tool(**toolchain_value, "compiler"_str, "clang++"_str);
    auto scanner = configured_tool(**toolchain_value, "scanner"_str,
                                   "clang-scan-deps"_str);
    auto archiver =
        configured_tool(**toolchain_value, "archiver"_str, "llvm-ar"_str);
    auto formatter =
        configured_tool(**toolchain_value, "formatter"_str, "clang-format"_str);
    if (compiler.is_err())
      return Err(rstd::move(compiler).unwrap_err());
    if (scanner.is_err())
      return Err(rstd::move(scanner).unwrap_err());
    if (archiver.is_err())
      return Err(rstd::move(archiver).unwrap_err());
    if (formatter.is_err())
      return Err(rstd::move(formatter).unwrap_err());
    toolchain = ToolchainSpec{
        .compiler = rstd::move(compiler).unwrap(),
        .scanner = rstd::move(scanner).unwrap(),
        .archiver = rstd::move(archiver).unwrap(),
        .formatter = rstd::move(formatter).unwrap(),
    };
  }

  return Ok(ProjectConfig{
      .root = rstd::move(root),
      .toolchain = rstd::move(toolchain),
  });
}

} // namespace tenon
