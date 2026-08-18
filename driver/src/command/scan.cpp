module lito.driver;

import rstd;
import rstd.json;
import lito.core;
import :command.error;
import :build.event;
import lito.cpp;
import :project;
import :build.discovery;
import lito.toolchain;
import lito.frontend;
import lito.system;
import :build.layout;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;
using Json      = rstd::json::Value;
using JsonMap   = rstd::json::Map;
using JsonArray = rstd::json::Array;

namespace lito
{

template<typename T>
auto scan_failure(String message) -> CommandResult<T> {
    return Err(CommandError::Message(rstd::move(message)));
}

template<typename T>
auto scan_failure(ref<str> message) -> CommandResult<T> {
    return Err(CommandError::Message(String::make(message)));
}

auto scan_path(ref<rstd::path::Path> path) -> CommandResult<String> {
    auto text = path.to_str();
    if (text.is_none()) {
        return scan_failure<String>(rstd::format("scan path '{}' is not valid UTF-8", path));
    }
    return Ok(String::make(*text));
}

auto scan_json_string(ref<str> value) -> Json {
    return Json::String(String::make(value));
}

auto json_path(ref<rstd::path::Path> path) -> CommandResult<Json> {
    auto text = scan_path(path);
    if (text.is_err()) return Err(rstd::move(text).unwrap_err());
    return Ok(Json::String(rstd::move(text).unwrap()));
}

auto json_usize(usize value) -> Json {
    return Json::Number(
        rstd::json::Number::from_u64(u64(static_cast<uint64_t>(value.to_primitive()))));
}

} // namespace lito

namespace lito
{

auto scan_output_format_name(ScanOutputFormat format) -> ref<str> {
    switch (format) {
    case ScanOutputFormat::Lito: return "lito"_str;
    case ScanOutputFormat::P1689: return "p1689"_str;
    }
    return "lito"_str;
}

auto parse_scan_output_format(ref<str> name) -> CommandResult<ScanOutputFormat> {
    if (name == "lito"_str) return Ok(ScanOutputFormat::Lito);
    if (name == "p1689"_str) return Ok(ScanOutputFormat::P1689);
    return scan_failure<ScanOutputFormat>(
        rstd::format("unknown scan output format '{}'; expected lito or p1689", name));
}

auto scan(const ScanRequest& request) -> CommandResult<ScanReport> {
    if (request.selection.root.is_empty()) {
        return scan_failure<ScanReport>("scan directory is required"_str);
    }
    if (request.source.is_empty()) {
        return scan_failure<ScanReport>("scan source is required"_str);
    }

    auto requested_source = request.source.as_path().is_absolute()
                                ? request.source.clone()
                                : request.selection.root.join(request.source.as_path());
    auto canonical_source = rstd::fs::canonicalize(requested_source.as_path());
    if (canonical_source.is_err()) {
        return Err(
            CommandError::System(SystemError::Io(String::make("resolve scan source"_str),
                                                 requested_source.clone(),
                                                 rstd::move(canonical_source).unwrap_err())));
    }
    auto source = rstd::move(canonical_source).unwrap();

    auto environment = ResolvedProcessEnvironment::resolve(request.environment);
    if (environment.is_err()) {
        return Err(rstd::into<CommandError>(rstd::move(environment).unwrap_err()));
    }
    auto tool_resolver = ToolResolver(*environment);
    auto profile =
        request.profile.is_some() ? request.profile->clone() : lito::manifest::BuildProfileName {};
    auto requested_output = PathBuf::make();
    auto jobs             = usize(1);
    auto available        = rstd::thread::available_parallelism();
    if (available.is_ok()) jobs = available->get();
    auto prepared = prepare_build_project(request.selection,
                                          request.configuration,
                                          profile,
                                          requested_output.as_path(),
                                          request.sources,
                                          request.lock,
                                          request.pkg_config,
                                          request.cmake,
                                          request.cmake_build_overrides,
                                          tool_resolver,
                                          *environment,
                                          request.locked,
                                          lito::package::PackageSelectionPurpose::All,
                                          jobs,
                                          request.observer);
    if (prepared.is_err()) {
        return Err(rstd::into<CommandError>(rstd::move(prepared).unwrap_err()));
    }
    auto  project   = rstd::move(prepared).unwrap();
    auto& toolchain = project.toolchain;
    auto& metadata  = project.metadata;
    auto  resolved =
        cpp::resolve_source_discovery(metadata, metadata.default_profile.as_str(), request.targets);
    if (resolved.is_err()) {
        return Err(rstd::into<CommandError>(rstd::move(resolved).unwrap_err()));
    }
    auto discovery = rstd::move(resolved).unwrap();
    auto selected  = resolve_source_target(metadata, discovery, source.as_path());
    if (selected.is_err()) {
        return Err(rstd::into<CommandError>(rstd::move(selected).unwrap_err()));
    }
    auto source_target = *selected;

    const auto& target          = metadata.targets[source_target];
    auto        relative_source = source.as_path().strip_prefix(target.source_root.as_path());
    if (relative_source.is_none() || relative_source->is_empty()) {
        return scan_failure<ScanReport>(
            rstd::format("source '{}' has no build-relative path in target '{}'",
                         source.as_path(),
                         lito::package::package_target_id_text(target.id).as_str()));
    }
    auto primary_output = project.layout.object(target.id, *relative_source);
    if (primary_output.is_err()) {
        return Err(rstd::into<CommandError>(rstd::move(primary_output).unwrap_err()));
    }

    auto frontend_service = frontend::FrontendService::make();
    auto facts = toolchain.preprocess(source.as_path(),
                                      discovery.contexts[source_target],
                                      target.compile_metadata,
                                      metadata.targets[source_target].source_root.as_path(),
                                      frontend_service);
    if (facts.is_err()) {
        return Err(rstd::into<CommandError>(rstd::move(facts).unwrap_err()));
    }

    return Ok(ScanReport {
        .target         = lito::package::package_target_id_text(target.id),
        .profile        = metadata.profiles[discovery.profile].name.clone(),
        .primary_output = rstd::move(primary_output).unwrap(),
        .result         = rstd::move(facts).unwrap().result,
    });
}

auto lito_scan_report_json(const ScanReport& report) -> CommandResult<String> {
    auto source = scan_path(report.result.source.as_path());
    if (source.is_err()) return Err(rstd::move(source).unwrap_err());

    auto provides = JsonArray::make();
    if (report.result.provided.is_some()) {
        auto provided = JsonMap::make();
        provided.insert(String::make("logical-name"_str),
                        scan_json_string(report.result.provided->logical_name.as_str()));
        provided.insert(String::make("is-interface"_str),
                        Json::Bool(report.result.provided->is_interface));
        provided.insert(String::make("source-path"_str), scan_json_string(source->as_str()));
        provides.push(Json::Object(rstd::move(provided)));
    }

    auto required_modules = JsonArray::make();
    for (const auto& imported : report.result.imports) {
        auto required = JsonMap::make();
        required.insert(String::make("logical-name"_str),
                        scan_json_string(imported.logical_name.as_str()));
        required.insert(String::make("exported"_str), Json::Bool(imported.exported));
        auto path = json_path(imported.location.path.as_path());
        if (path.is_err()) return Err(rstd::move(path).unwrap_err());
        required.insert(String::make("source-path"_str), rstd::move(path).unwrap());
        required.insert(String::make("line"_str), json_usize(imported.location.line));
        required_modules.push(Json::Object(rstd::move(required)));
    }

    auto headers = JsonArray::make();
    for (const auto& header : report.result.header_inputs) {
        auto path = json_path(header.as_path());
        if (path.is_err()) return Err(rstd::move(path).unwrap_err());
        headers.push(rstd::move(path).unwrap());
    }

    auto embedded = JsonArray::make();
    for (const auto& input : report.result.embedded_inputs) {
        auto path = json_path(input.path.as_path());
        if (path.is_err()) return Err(rstd::move(path).unwrap_err());
        auto value = JsonMap::make();
        value.insert(String::make("digest"_str), scan_json_string(input.digest.as_str()));
        value.insert(String::make("length"_str), json_usize(input.length));
        value.insert(String::make("offset"_str), json_usize(input.offset));
        value.insert(String::make("path"_str), rstd::move(path).unwrap());
        value.insert(String::make("size"_str),
                     Json::Number(rstd::json::Number::from_u64(input.size)));
        embedded.push(Json::Object(rstd::move(value)));
    }

    auto implementation = Json::Null();
    if (report.result.implementation_module.is_some()) {
        implementation = scan_json_string(report.result.implementation_module->as_str());
    }

    auto external_macros = JsonArray::with_capacity(report.result.external_macros.len());
    for (const auto& macro : report.result.external_macros) {
        auto value      = JsonMap::make();
        auto definition = Json::Null();
        if (macro.compiler_definition.is_some()) {
            definition = scan_json_string(macro.compiler_definition->as_str());
        }
        value.insert(String::make("compiler-definition"_str), rstd::move(definition));
        value.insert(String::make("dependency-key"_str),
                     scan_json_string(macro.dependency_key.as_str()));
        value.insert(String::make("name"_str), scan_json_string(macro.name.as_str()));
        value.insert(String::make("state"_str),
                     scan_json_string(macro.state == frontend::ExternalMacroState::Defined
                                          ? "defined"_str
                                          : "undefined"_str));
        value.insert(String::make("value-identity"_str),
                     scan_json_string(macro.value_identity.as_str()));
        external_macros.push(Json::Object(rstd::move(value)));
    }

    auto document = JsonMap::make();
    document.insert(String::make("format"_str), scan_json_string("lito-scan"_str));
    document.insert(String::make("version"_str),
                    Json::Number(rstd::json::Number::from_u64(u64(4))));
    document.insert(String::make("target"_str), scan_json_string(report.target.as_str()));
    document.insert(String::make("profile"_str), scan_json_string(report.profile.as_str()));
    document.insert(String::make("source"_str), Json::String(rstd::move(source).unwrap()));
    document.insert(String::make("provides"_str), Json::Array(rstd::move(provides)));
    document.insert(String::make("implementation-module"_str), rstd::move(implementation));
    document.insert(String::make("requires"_str), Json::Array(rstd::move(required_modules)));
    document.insert(String::make("headers"_str), Json::Array(rstd::move(headers)));
    document.insert(String::make("embedded-resources"_str), Json::Array(rstd::move(embedded)));
    document.insert(String::make("external-macros"_str), Json::Array(rstd::move(external_macros)));
    document.insert(String::make("preprocessor-environment"_str),
                    scan_json_string(report.result.preprocessor_environment.as_str()));
    document.insert(String::make("input-bytes"_str), json_usize(report.result.input_bytes));
    return Ok(
        rstd::json::to_string(Json::Object(rstd::move(document)),
                              rstd::json::FormatOptions { .pretty = true, .indent = usize(2) }));
}

auto p1689_scan_report_json(const ScanReport& report) -> CommandResult<String> {
    auto primary_output = json_path(report.primary_output.as_path());
    if (primary_output.is_err()) return Err(rstd::move(primary_output).unwrap_err());
    auto source = json_path(report.result.source.as_path());
    if (source.is_err()) return Err(rstd::move(source).unwrap_err());

    auto rule = JsonMap::make();
    rule.insert(String::make("primary-output"_str), rstd::move(primary_output).unwrap());

    if (report.result.provided.is_some()) {
        auto provided = JsonMap::make();
        provided.insert(String::make("logical-name"_str),
                        scan_json_string(report.result.provided->logical_name.as_str()));
        provided.insert(String::make("source-path"_str), rstd::move(source).unwrap());
        provided.insert(String::make("is-interface"_str),
                        Json::Bool(report.result.provided->is_interface));
        auto provides = JsonArray::make();
        provides.push(Json::Object(rstd::move(provided)));
        rule.insert(String::make("provides"_str), Json::Array(rstd::move(provides)));
    }

    auto required_names = Vec<String>::make();
    if (report.result.implementation_module.is_some()) {
        required_names.push(report.result.implementation_module->clone());
    }
    for (const auto& imported : report.result.imports) {
        auto exists = false;
        for (const auto& required : required_names) {
            if (required.as_str() == imported.logical_name.as_str()) {
                exists = true;
                break;
            }
        }
        if (! exists) required_names.push(imported.logical_name.clone());
    }
    if (! required_names.is_empty()) {
        auto required_modules = JsonArray::with_capacity(required_names.len());
        for (const auto& name : required_names) {
            auto required = JsonMap::make();
            required.insert(String::make("logical-name"_str), scan_json_string(name.as_str()));
            required_modules.push(Json::Object(rstd::move(required)));
        }
        rule.insert(String::make("requires"_str), Json::Array(rstd::move(required_modules)));
    }

    auto rules = JsonArray::with_capacity(usize(1));
    rules.push(Json::Object(rstd::move(rule)));
    auto document = JsonMap::make();
    document.insert(String::make("version"_str),
                    Json::Number(rstd::json::Number::from_u64(u64(1))));
    document.insert(String::make("revision"_str),
                    Json::Number(rstd::json::Number::from_u64(u64 {})));
    document.insert(String::make("rules"_str), Json::Array(rstd::move(rules)));
    return Ok(
        rstd::json::to_string(Json::Object(rstd::move(document)),
                              rstd::json::FormatOptions { .pretty = true, .indent = usize(2) }));
}

auto scan_report_json(const ScanReport& report, ScanOutputFormat format) -> CommandResult<String> {
    switch (format) {
    case ScanOutputFormat::Lito: return lito_scan_report_json(report);
    case ScanOutputFormat::P1689: return p1689_scan_report_json(report);
    }
    return scan_failure<String>("unsupported scan output format"_str);
}

} // namespace lito
