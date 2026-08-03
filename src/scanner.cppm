export module tenon.scanner;

import rstd;
import rstd.json;
import tenon.model;
import tenon.project;
import tenon.package;
import tenon.toolchain;
import tenon.frontend;
import tenon.profiling;

using namespace rstd::prelude;
using namespace rstd::literals;
using Json = rstd::json::Value;
using JsonMap = rstd::json::Map;
using JsonArray = rstd::json::Array;

namespace tenon {

template <typename T> auto scan_failure(String message) -> Result<T> {
  return Err(Error::make(ErrorKind::InvalidRequest, rstd::move(message)));
}

template <typename T> auto scan_failure(ref<str> message) -> Result<T> {
  return Err(Error::make(ErrorKind::InvalidRequest, message));
}

auto scan_path(ref<rstd::path::Path> path) -> Result<String> {
  auto text = path.to_str();
  if (text.is_none()) {
    return scan_failure<String>(
        rstd::format("scan path '{}' is not valid UTF-8", path));
  }
  return Ok(String::make(*text));
}

auto json_string(ref<str> value) -> Json {
  return Json::String(String::make(value));
}

auto json_path(ref<rstd::path::Path> path) -> Result<Json> {
  auto text = scan_path(path);
  if (text.is_err())
    return Err(rstd::move(text).unwrap_err());
  return Ok(Json::String(rstd::move(text).unwrap()));
}

auto json_usize(usize value) -> Json {
  return Json::Number(rstd::json::Number::from_u64(
      u64(static_cast<rstd::uint64_t>(value.to_primitive()))));
}

} // namespace tenon

export namespace tenon {

auto scan(const ScanRequest &request) -> Result<ScanReport> {
  if (request.selection.root.is_empty()) {
    return scan_failure<ScanReport>("scan directory is required"_str);
  }
  if (request.source.is_empty()) {
    return scan_failure<ScanReport>("scan source is required"_str);
  }

  auto requested_source =
      request.source.as_path().is_absolute()
          ? request.source.clone()
          : request.selection.root.join(request.source.as_path());
  auto canonical_source = rstd::fs::canonicalize(requested_source.as_path());
  if (canonical_source.is_err()) {
    return scan_failure<ScanReport>(rstd::format(
        "cannot resolve scan source '{}': {}", requested_source.as_path(),
        rstd::move(canonical_source).unwrap_err()));
  }
  auto source = rstd::move(canonical_source).unwrap();

  auto created_toolchain = ClangToolchain::create(request.configuration.toolchain);
  if (created_toolchain.is_err()) {
    return Err(rstd::move(created_toolchain).unwrap_err());
  }
  auto toolchain = rstd::move(created_toolchain).unwrap();
  auto loaded = resolve_project_metadata(request.selection, request.configuration,
                                         request.sources, toolchain.target_info(),
                                         request.locked, PackageSelectionPurpose::All);
  if (loaded.is_err())
    return Err(rstd::move(loaded).unwrap_err());
  auto metadata = rstd::move(loaded).unwrap();
  auto resolved = resolve_source_discovery(
      metadata, metadata.default_profile.as_str(), request.targets);
  if (resolved.is_err())
    return Err(rstd::move(resolved).unwrap_err());
  auto discovery = rstd::move(resolved).unwrap();
  auto selected = resolve_source_target(metadata, discovery, source.as_path());
  if (selected.is_err())
    return Err(rstd::move(selected).unwrap_err());
  auto source_target = *selected;

  auto created_profiler = ScanProfiler::create();
  if (created_profiler.is_err()) {
    return Err(
        Error::make(ErrorKind::Artifact,
                    rstd::move(created_profiler).unwrap_err_unchecked()));
  }
  auto profiler = rstd::move(created_profiler).unwrap_unchecked();
  auto frontend_service = frontend::FrontendService::make(profiler);
  auto facts = toolchain.preprocess(
      source.as_path(), discovery.contexts[source_target],
      metadata.targets[source_target].manifest.root.as_path(), frontend_service);
  if (facts.is_err())
    return Err(rstd::move(facts).unwrap_err());

  return Ok(ScanReport{
      .target = metadata.targets[source_target].manifest.name.clone(),
      .profile = metadata.profiles[discovery.profile].name.clone(),
      .result = rstd::move(facts).unwrap().result,
  });
}

auto scan_report_json(const ScanReport &report) -> Result<String> {
  auto source = scan_path(report.result.source.as_path());
  if (source.is_err())
    return Err(rstd::move(source).unwrap_err());

  auto provides = JsonArray::make();
  if (report.result.provided.is_some()) {
    auto provided = JsonMap::make();
    provided.insert(String::make("logical-name"_str),
                    json_string(report.result.provided->logical_name.as_str()));
    provided.insert(String::make("is-interface"_str),
                    Json::Bool(report.result.provided->is_interface));
    provided.insert(String::make("source-path"_str),
                    json_string(source->as_str()));
    provides.push(Json::Object(rstd::move(provided)));
  }

  auto required_modules = JsonArray::make();
  for (const auto &imported : report.result.imports) {
    auto required = JsonMap::make();
    required.insert(String::make("logical-name"_str),
                    json_string(imported.logical_name.as_str()));
    auto path = json_path(imported.location.path.as_path());
    if (path.is_err())
      return Err(rstd::move(path).unwrap_err());
    required.insert(String::make("source-path"_str), rstd::move(path).unwrap());
    required.insert(String::make("line"_str),
                    json_usize(imported.location.line));
    required_modules.push(Json::Object(rstd::move(required)));
  }

  auto headers = JsonArray::make();
  for (const auto &header : report.result.header_inputs) {
    auto path = json_path(header.as_path());
    if (path.is_err())
      return Err(rstd::move(path).unwrap_err());
    headers.push(rstd::move(path).unwrap());
  }

  auto implementation = Json::Null();
  if (report.result.implementation_module.is_some()) {
    implementation = json_string(report.result.implementation_module->as_str());
  }

  auto document = JsonMap::make();
  document.insert(String::make("format"_str), json_string("tenon-scan"_str));
  document.insert(String::make("version"_str),
                  Json::Number(rstd::json::Number::from_u64(u64(1))));
  document.insert(String::make("target"_str),
                  json_string(report.target.as_str()));
  document.insert(String::make("profile"_str),
                  json_string(report.profile.as_str()));
  document.insert(String::make("source"_str),
                  Json::String(rstd::move(source).unwrap()));
  document.insert(String::make("provides"_str),
                  Json::Array(rstd::move(provides)));
  document.insert(String::make("implementation-module"_str),
                  rstd::move(implementation));
  document.insert(String::make("requires"_str),
                  Json::Array(rstd::move(required_modules)));
  document.insert(String::make("headers"_str),
                  Json::Array(rstd::move(headers)));
  document.insert(String::make("preprocessor-environment"_str),
                  json_string(report.result.preprocessor_environment.as_str()));
  document.insert(String::make("input-bytes"_str),
                  json_usize(report.result.input_bytes));
  return Ok(rstd::json::to_string(
      Json::Object(rstd::move(document)),
      rstd::json::FormatOptions{.pretty = true, .indent = usize(2)}));
}

} // namespace tenon
