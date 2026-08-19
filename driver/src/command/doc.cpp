module;
#include <rstd/macro.hpp>

module lito.driver;

import rstd;
import rstd.json;
import lito.core;
import :command.doc.event;
import :command.doc.request;
import :command.doc.result;
import :command.doc_error;
import :command.doc_tool;
import :build;
import :build.documentation;
import :build.compile_executor;
import lito.cpp;
import lito.system;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;
using Json      = rstd::json::Value;
using JsonMap   = rstd::json::Map;
using JsonArray = rstd::json::Array;

namespace lito
{

template<typename T>
auto doc_failure(String message) -> DocResult<T> {
    return Err(DocError::Message(rstd::move(message)));
}

template<typename T>
auto doc_failure(ref<str> message) -> DocResult<T> {
    return doc_failure<T>(String::make(message));
}

auto doc_path_text(ref<rstd::path::Path> path, ref<str> context) -> DocResult<String> {
    auto text = path.to_str();
    if (text.is_none()) {
        return doc_failure<String>(rstd::format("{} path '{}' is not valid UTF-8", context, path));
    }
    return Ok(String::make(*text));
}

auto doc_json_string(ref<str> text) -> Json {
    return Json::String(String::make(text));
}

auto selected_package(const BuildSummary& summary, ref<str> name)
    -> Option<ref<cpp::SelectedPackageMetadata>> {
    for (const auto& package : summary.selected_packages) {
        if (package.name.as_str() == name) {
            return Some(
                ref<cpp::SelectedPackageMetadata>::from_raw_parts(rstd::addressof(package)));
        }
    }
    return None();
}

auto selected_library_target(const BuildSummary&                   summary,
                             const lito::package::PackageTargetId& target) -> bool {
    if (target.kind != lito::package::PackageTargetKind::Library) return false;
    for (const auto& selected : summary.selected_targets) {
        if (selected == target) return true;
    }
    return false;
}

struct DocUnitPlan {
    usize   unit {};
    String  request_id;
    String  target;
    PathBuf request;
    PathBuf response;
};

auto extraction_request_json(const BuildSummary&                 summary,
                             const DocumentationBuildUnit&       unit,
                             const cpp::SelectedPackageMetadata& package,
                             ref<str>                            request_id) -> DocResult<String> {
    auto source = rstd_try(doc_path_text(unit.source.as_path(), "documentation source"_str));
    auto package_root =
        rstd_try(doc_path_text(unit.package_root.as_path(), "documentation package root"_str));
    auto working      = rstd_try(doc_path_text(unit.invocation.working_directory.as_path(),
                                               "documentation working directory"_str));
    auto package_json = JsonMap::make();
    package_json.insert(String::make("name"_str), doc_json_string(package.name.as_str()));
    package_json.insert(String::make("version"_str),
                        package.version.is_some() ? doc_json_string(package.version->as_str())
                                                  : doc_json_string(""_str));
    package_json.insert(String::make("identity"_str),
                        doc_json_string(package.source_identity.as_str()));

    auto target_json = JsonMap::make();
    target_json.insert(String::make("name"_str), doc_json_string(unit.target.name.as_str()));
    target_json.insert(String::make("kind"_str), doc_json_string("library"_str));

    auto unit_json = JsonMap::make();
    unit_json.insert(String::make("identity"_str), doc_json_string(unit.source_identity.as_str()));
    unit_json.insert(String::make("kind"_str),
                     doc_json_string(rstd::format("{}", unit.kind).as_str()));
    unit_json.insert(String::make("is_interface"_str), Json::Bool(unit.is_interface));
    unit_json.insert(String::make("source"_str), Json::String(rstd::move(source)));
    unit_json.insert(String::make("module"_str),
                     unit.logical_module.is_some() ? doc_json_string(unit.logical_module->as_str())
                                                   : Json::Null());

    auto arguments = JsonArray::with_capacity(unit.invocation.arguments.len());
    for (const auto& argument : unit.invocation.arguments) {
        arguments.push(doc_json_string(argument.as_str()));
    }
    auto invocation = JsonMap::make();
    invocation.insert(String::make("cwd"_str), Json::String(rstd::move(working)));
    invocation.insert(String::make("arguments"_str), Json::Array(rstd::move(arguments)));

    auto compiler = JsonMap::make();
    compiler.insert(String::make("identity"_str),
                    doc_json_string(summary.compiler.build_identity.as_str()));
    compiler.insert(String::make("target"_str), doc_json_string(summary.compiler.target.as_str()));

    auto imported = JsonArray::with_capacity(unit.bmi_dependencies.len());
    for (const auto& dependency : unit.bmi_dependencies) {
        auto path = rstd_try(doc_path_text(dependency.path.as_path(), "imported BMI"_str));
        auto item = JsonMap::make();
        item.insert(String::make("module"_str), doc_json_string(dependency.logical_name.as_str()));
        item.insert(String::make("path"_str), Json::String(rstd::move(path)));
        item.insert(String::make("identity"_str),
                    doc_json_string(dependency.artifact_identity.as_str()));
        imported.push(Json::Object(rstd::move(item)));
    }

    auto root = JsonMap::make();
    root.insert(String::make("format"_str), doc_json_string("litodoc-extract"_str));
    root.insert(String::make("version"_str), Json::Number(rstd::json::Number::from_u64(u64(1))));
    root.insert(String::make("request_id"_str), doc_json_string(request_id));
    root.insert(String::make("package"_str), Json::Object(rstd::move(package_json)));
    root.insert(String::make("target"_str), Json::Object(rstd::move(target_json)));
    root.insert(String::make("unit"_str), Json::Object(rstd::move(unit_json)));
    root.insert(String::make("invocation"_str), Json::Object(rstd::move(invocation)));
    root.insert(String::make("compiler"_str), Json::Object(rstd::move(compiler)));
    root.insert(String::make("package_root"_str), Json::String(rstd::move(package_root)));
    root.insert(String::make("imported_artifacts"_str), Json::Array(rstd::move(imported)));
    return Ok(
        rstd::json::to_string(Json::Object(rstd::move(root)),
                              rstd::json::FormatOptions { .pretty = true, .indent = usize(2) }));
}

auto response_matches(ref<rstd::path::Path> response, ref<str> request_id) -> DocResult<bool> {
    auto contents = rstd::fs::read_to_string(response);
    if (contents.is_err()) {
        auto error = rstd::move(contents).unwrap_err();
        if (error.kind() == rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound }) {
            return Ok(false);
        }
        return Err(
            doc_io_failure("read litodoc extraction response"_str, response, rstd::move(error)));
    }
    auto parsed = rstd::json::from_str(contents->as_str());
    if (parsed.is_err()) return Ok(false);
    auto format         = parsed->get("format"_str);
    auto version        = parsed->get("version"_str);
    auto actual_request = parsed->get("request_id"_str);
    return Ok(format.is_some() && (**format).as_str() == Some("litodoc-extract-result"_str) &&
              version.is_some() && (**version).as_u64() == Some(u64(1)) &&
              actual_request.is_some() && (**actual_request).as_str() == Some(request_id));
}

auto write_extraction_request(ref<rstd::path::Path> path, ref<str> contents) -> DocResult<empty> {
    auto parent = path.parent();
    if (parent.is_none()) {
        return doc_failure<empty>(
            rstd::format("documentation request path '{}' has no parent", path));
    }
    auto created = rstd::fs::create_dir_all(*parent);
    if (created.is_err()) {
        return Err(doc_io_failure("create documentation extraction cache"_str,
                                  *parent,
                                  rstd::move(created).unwrap_err()));
    }
    auto written = rstd::fs::write_atomic(path, contents.as_bytes());
    if (written.is_err()) {
        return Err(doc_io_failure(
            "write documentation extraction request"_str, path, rstd::move(written).unwrap_err()));
    }
    return Ok(empty {});
}

auto request_identity(const DocumentationBuildUnit&       unit,
                      const cpp::SelectedPackageMetadata& package,
                      const ResolvedDocTool&              tool) -> String {
    auto value = rstd::format("lito-doc-extract-v2\n{}\n{}\n{}\n{}\n{}\n{}\n{}\n{}",
                              package.source_identity.as_str(),
                              lito::package::package_target_id_text(unit.target).as_str(),
                              unit.source_identity.as_str(),
                              unit.invocation.identity.as_str(),
                              tool.build_identity.as_str(),
                              tool.sdk.identity.as_str(),
                              unit.kind,
                              unit.is_interface);
    for (const auto& dependency : unit.bmi_dependencies) {
        value.push_str(rstd::format("\n{}\n{}",
                                    dependency.logical_name.as_str(),
                                    dependency.artifact_identity.as_str())
                           .as_str());
    }
    return rstd::crypto::sha256_hex(value.as_str());
}

struct ExtractionTask {
    usize                      plan {};
    PathBuf                    executable;
    PathBuf                    request;
    PathBuf                    response;
    PathBuf                    working_directory;
    String                     request_id;
    ResolvedProcessEnvironment environment;
};

struct ExtractionCompletion {
    usize            plan {};
    DocResult<empty> result;
};

auto run_extraction(ExtractionTask task) -> ExtractionCompletion {
    auto arguments  = Vec<String>::make();
    auto executable = doc_path_text(task.executable.as_path(), "litodoc executable"_str);
    auto request    = doc_path_text(task.request.as_path(), "litodoc request"_str);
    auto response   = doc_path_text(task.response.as_path(), "litodoc response"_str);
    if (executable.is_err())
        return { .plan = task.plan, .result = Err(rstd::move(executable).unwrap_err()) };
    if (request.is_err())
        return { .plan = task.plan, .result = Err(rstd::move(request).unwrap_err()) };
    if (response.is_err())
        return { .plan = task.plan, .result = Err(rstd::move(response).unwrap_err()) };
    arguments.push(rstd::move(executable).unwrap());
    arguments.push(String::make("extract"_str));
    arguments.push(String::make("--request"_str));
    arguments.push(rstd::move(request).unwrap());
    arguments.push(String::make("--response"_str));
    arguments.push(rstd::move(response).unwrap());
    auto executed =
        run_command(arguments, task.environment, Some(task.working_directory.as_path()));
    if (executed.is_err()) {
        return { .plan   = task.plan,
                 .result = Err(rstd::into<DocError>(rstd::move(executed).unwrap_err())) };
    }
    if (executed->exit_code != i32 {}) {
        return {
            .plan   = task.plan,
            .result = Err(DocError::Execution(String::make("litodoc extract"_str),
                                              task.executable.clone(),
                                              executed->exit_code,
                                              rstd::move(executed->standard_output),
                                              rstd::move(executed->standard_error))),
        };
    }
    auto valid = response_matches(task.response.as_path(), task.request_id.as_str());
    if (valid.is_err()) return { .plan = task.plan, .result = Err(rstd::move(valid).unwrap_err()) };
    if (! *valid) {
        return {
            .plan   = task.plan,
            .result = Err(DocError::Protocol(task.response.clone(),
                                             String::make("response does not match request"_str))),
        };
    }
    return { .plan = task.plan, .result = Ok(empty {}) };
}

auto execute_extractions(const DocRequest&                 request,
                         const BuildSummary&               summary,
                         const ResolvedDocTool&            tool,
                         const ResolvedProcessEnvironment& environment,
                         Vec<DocUnitPlan>&                 plans) -> DocResult<usize> {
    auto pending = Vec<usize>::make();
    auto reused  = usize {};
    for (usize index {}; index < plans.len(); ++index) {
        auto valid =
            response_matches(plans[index].response.as_path(), plans[index].request_id.as_str());
        if (valid.is_err()) return Err(rstd::move(valid).unwrap_err());
        if (*valid) {
            ++reused;
            emit_doc(request.observer,
                     DocEventKind::ExtractReuse,
                     plans[index].target.as_str(),
                     summary.documentation_units[plans[index].unit].source.as_path());
        } else {
            pending.emplace_back(index);
        }
    }
    if (pending.is_empty()) return Ok(reused);

    auto policy = resolve_compile_execution(request.build.execution.compile);
    if (policy.is_err()) return Err(rstd::into<DocError>(rstd::move(policy).unwrap_err()));
    auto jobs = policy->jobs < pending.len() ? policy->jobs : pending.len();
    auto pool = rstd::thread::ThreadPoolBuilder::make()
                    .worker_count(jobs)
                    .thread_name(String::make("lito-doc"_str))
                    .build();
    if (pool.is_err()) {
        return Err(DocError::Io(String::make("create documentation worker pool"_str),
                                PathBuf::make(),
                                rstd::move(pool).unwrap_err_unchecked()));
    }
    auto threads = rstd::move(pool).unwrap_unchecked();
    auto tasks = rstd::thread::BlockingTaskSet<ExtractionCompletion>::make(threads.handle(), jobs);
    if (tasks.is_err()) {
        return Err(DocError::Io(String::make("create documentation task set"_str),
                                PathBuf::make(),
                                rstd::move(tasks).unwrap_err_unchecked()));
    }
    auto task_set = rstd::move(tasks).unwrap_unchecked();
    auto next     = usize {};
    auto active   = usize {};
    while (next < pending.len() || active != usize {}) {
        while (next < pending.len() && active < jobs) {
            auto        plan_index = pending[next];
            auto&       plan       = plans[plan_index];
            const auto& unit       = summary.documentation_units[plan.unit];
            emit_doc(request.observer,
                     DocEventKind::Extract,
                     plan.target.as_str(),
                     unit.source.as_path());
            auto submitted = task_set.try_submit(
                [task = ExtractionTask {
                     .plan              = plan_index,
                     .executable        = tool.executable.clone(),
                     .request           = plan.request.clone(),
                     .response          = plan.response.clone(),
                     .working_directory = unit.invocation.working_directory.clone(),
                     .request_id        = plan.request_id.clone(),
                     .environment       = environment.clone(),
                 }]() mutable {
                    return run_extraction(rstd::move(task));
                });
            if (submitted.is_err()) {
                task_set.cancel_pending();
                task_set.close();
                rstd::move(threads).join();
                return doc_failure<usize>("cannot submit documentation extraction task"_str);
            }
            ++next;
            ++active;
        }
        auto completion = task_set.recv();
        if (completion.is_none()) {
            task_set.cancel_pending();
            task_set.close();
            rstd::move(threads).join();
            return doc_failure<usize>("documentation task set closed before completion"_str);
        }
        auto value = rstd::move(completion).unwrap_unchecked().into_value();
        if (value.is_none()) {
            task_set.cancel_pending();
            task_set.close();
            rstd::move(threads).join();
            return doc_failure<usize>("documentation extraction task was cancelled"_str);
        }
        auto outcome = rstd::move(value).unwrap_unchecked();
        --active;
        if (outcome.result.is_err()) {
            auto error = rstd::move(outcome.result).unwrap_err();
            task_set.cancel_pending();
            task_set.close();
            rstd::move(threads).join();
            return Err(rstd::move(error));
        }
    }
    task_set.close();
    rstd::move(threads).join();
    return Ok(reused);
}

struct PackageResponses {
    const cpp::SelectedPackageMetadata* package {};
    String                              root_module;
    struct Response {
        PathBuf path;
        String  digest;
    };
    Vec<Response> responses;
};

auto site_manifest_json(const BuildSummary&     summary,
                        const Vec<DocUnitPlan>& plans,
                        ref<rstd::path::Path>   output,
                        ref<rstd::path::Path>   data_output,
                        const Option<PathBuf>&  frontend,
                        bool                    data_only,
                        bool                    package_publication) -> DocResult<String> {
    auto packages = Vec<PackageResponses>::make();
    for (const auto& selected : summary.selected_packages) {
        packages.push(PackageResponses { .package = rstd::addressof(selected) });
    }
    for (const auto& plan : plans) {
        const auto& unit = summary.documentation_units[plan.unit];
        for (auto& package : packages) {
            if (package.package->name.as_str() != unit.target.package.as_str()) continue;
            auto contents = rstd::fs::read_to_string(plan.response.as_path());
            if (contents.is_err()) {
                return Err(doc_io_failure("read documentation response"_str,
                                          plan.response.as_path(),
                                          rstd::move(contents).unwrap_err()));
            }
            package.responses.push(PackageResponses::Response {
                .path   = plan.response.clone(),
                .digest = rstd::crypto::sha256_hex(contents->as_str()),
            });
            if (unit.root_module.is_some()) {
                if (package.root_module.is_empty()) {
                    package.root_module = unit.root_module->clone();
                } else if (package.root_module.as_str() != unit.root_module->as_str()) {
                    return doc_failure<String>(rstd::format(
                        "package '{}' has conflicting documentation root modules '{}' and '{}'",
                        package.package->name.as_str(),
                        package.root_module.as_str(),
                        unit.root_module->as_str()));
                }
            }
        }
    }

    auto package_values = JsonArray::make();
    for (const auto& package : packages) {
        if (package.responses.is_empty()) continue;
        auto root = rstd_try(
            doc_path_text(package.package->root.as_path(), "documentation package root"_str));
        auto responses = JsonArray::with_capacity(package.responses.len());
        for (const auto& response : package.responses) {
            auto value = JsonMap::make();
            value.insert(String::make("path"_str),
                         Json::String(rstd_try(doc_path_text(response.path.as_path(),
                                                             "documentation response"_str))));
            value.insert(String::make("digest"_str), doc_json_string(response.digest.as_str()));
            responses.push(Json::Object(rstd::move(value)));
        }
        auto value = JsonMap::make();
        value.insert(String::make("name"_str), doc_json_string(package.package->name.as_str()));
        value.insert(String::make("version"_str),
                     package.package->version.is_some()
                         ? doc_json_string(package.package->version->as_str())
                         : doc_json_string(""_str));
        value.insert(String::make("source_identity"_str),
                     doc_json_string(package.package->source_identity.as_str()));
        value.insert(String::make("root_module"_str),
                     doc_json_string(package.root_module.as_str()));
        value.insert(String::make("profile"_str), doc_json_string(summary.profile.as_str()));
        value.insert(String::make("root"_str), Json::String(rstd::move(root)));
        value.insert(String::make("toolchain_version"_str),
                     doc_json_string(summary.compiler.version.as_str()));
        value.insert(String::make("toolchain_target"_str),
                     doc_json_string(summary.compiler.target.as_str()));
        value.insert(String::make("language_standard"_str),
                     doc_json_string(summary.language_standard.as_str()));
        value.insert(String::make("responses"_str), Json::Array(rstd::move(responses)));
        package_values.push(Json::Object(rstd::move(value)));
    }
    auto output_text = rstd_try(doc_path_text(output, "documentation output"_str));
    auto data_text   = rstd_try(doc_path_text(data_output, "documentation data output"_str));
    auto root        = JsonMap::make();
    root.insert(String::make("format"_str), doc_json_string("litodoc-site"_str));
    root.insert(String::make("version"_str), Json::Number(rstd::json::Number::from_u64(u64(1))));
    root.insert(String::make("title"_str), doc_json_string(summary.package.as_str()));
    root.insert(String::make("output"_str), Json::String(rstd::move(output_text)));
    root.insert(String::make("data_output"_str), Json::String(rstd::move(data_text)));
    if (frontend.is_some()) {
        root.insert(String::make("frontend"_str),
                    Json::String(rstd_try(
                        doc_path_text(frontend->as_path(), "documentation frontend"_str))));
    }
    root.insert(String::make("data_only"_str), Json::Bool(data_only));
    root.insert(String::make("publication"_str),
                doc_json_string(package_publication ? "package-set"_str : "site"_str));
    root.insert(String::make("data_api"_str), Json::Number(rstd::json::Number::from_u64(u64(2))));
    root.insert(String::make("template_api"_str),
                Json::Number(rstd::json::Number::from_u64(u64(1))));
    root.insert(String::make("packages"_str), Json::Array(rstd::move(package_values)));
    return Ok(
        rstd::json::to_string(Json::Object(rstd::move(root)),
                              rstd::json::FormatOptions { .pretty = true, .indent = usize(2) }));
}

auto run_generate(const ResolvedDocTool&            tool,
                  ref<rstd::path::Path>             manifest,
                  const ResolvedProcessEnvironment& environment) -> DocResult<empty> {
    auto executable = rstd_try(doc_path_text(tool.executable.as_path(), "litodoc executable"_str));
    auto manifest_text = rstd_try(doc_path_text(manifest, "litodoc site manifest"_str));
    auto arguments     = Vec<String>::make();
    arguments.push(rstd::move(executable));
    arguments.push(String::make("generate"_str));
    arguments.push(String::make("--manifest"_str));
    arguments.push(rstd::move(manifest_text));
    auto executed = run_command(arguments, environment);
    if (executed.is_err()) return Err(rstd::into<DocError>(rstd::move(executed).unwrap_err()));
    if (executed->exit_code != i32 {}) {
        return Err(DocError::Execution(String::make("litodoc generate"_str),
                                       tool.executable.clone(),
                                       executed->exit_code,
                                       rstd::move(executed->standard_output),
                                       rstd::move(executed->standard_error)));
    }
    return Ok(empty {});
}

} // namespace lito

namespace lito
{

auto doc(DocRequest request) -> DocResult<DocSummary> {
    auto environment = ResolvedProcessEnvironment::resolve(request.build.environment);
    if (environment.is_err())
        return Err(rstd::into<DocError>(rstd::move(environment).unwrap_err()));
    auto built = build_with_environment(request.build, *environment);
    if (built.is_err()) return Err(rstd::into<DocError>(rstd::move(built).unwrap_err()));
    auto summary = rstd::move(built).unwrap();
    auto tool =
        resolve_doc_tool(request.build, request.config, summary, *environment, request.observer);
    if (tool.is_err()) return Err(rstd::move(tool).unwrap_err());

    auto cache_root = summary.output.join(PathBuf::from("lito-doc-cache"_str).as_path());
    auto plans      = Vec<DocUnitPlan>::make();
    for (usize index {}; index < summary.documentation_units.len(); ++index) {
        const auto& unit = summary.documentation_units[index];
        if (! selected_library_target(summary, unit.target)) continue;
        auto package = selected_package(summary, unit.target.package.as_str());
        if (package.is_none()) {
            return doc_failure<DocSummary>(rstd::format(
                "documentation unit '{}' has no selected package metadata", unit.source.as_path()));
        }
        auto request_id    = request_identity(unit, **package, *tool);
        auto directory     = cache_root.join(PathBuf::from(request_id.as_str()).as_path());
        auto request_path  = directory.join(PathBuf::from("request.json"_str).as_path());
        auto response_path = directory.join(PathBuf::from("response.json"_str).as_path());
        auto request_json  = extraction_request_json(summary, unit, **package, request_id.as_str());
        if (request_json.is_err()) return Err(rstd::move(request_json).unwrap_err());
        rstd_try(write_extraction_request(request_path.as_path(), request_json->as_str()));
        plans.push(DocUnitPlan {
            .unit       = index,
            .request_id = rstd::move(request_id),
            .target     = lito::package::package_target_id_text(unit.target),
            .request    = rstd::move(request_path),
            .response   = rstd::move(response_path),
        });
    }
    if (plans.is_empty()) {
        return doc_failure<DocSummary>(
            "selected packages do not contain a documentable library target"_str);
    }

    auto reused = execute_extractions(request, summary, *tool, *environment, plans);
    if (reused.is_err()) return Err(rstd::move(reused).unwrap_err());
    auto output        = request.output.is_empty()
                             ? summary.output.join(PathBuf::from("doc"_str).as_path())
                             : rstd::move(request.output);
    auto data_output   = request.data_output.is_empty()
                             ? summary.output.join(PathBuf::from("doc-data"_str).as_path())
                             : rstd::move(request.data_output);
    auto manifest_path = cache_root.join(PathBuf::from("site.json"_str).as_path());
    auto manifest      = site_manifest_json(summary,
                                            plans,
                                            output.as_path(),
                                            data_output.as_path(),
                                            request.frontend,
                                            request.data_only,
                                            request.package_publication);
    if (manifest.is_err()) return Err(rstd::move(manifest).unwrap_err());
    rstd_try(write_extraction_request(manifest_path.as_path(), manifest->as_str()));
    emit_doc(request.observer, DocEventKind::Generate, summary.package.as_str(), output.as_path());
    rstd_try(run_generate(*tool, manifest_path.as_path(), *environment));
    auto publication_receipt = Option<PathBuf> {};
    if (request.package_publication) {
        publication_receipt =
            Some(output.join(PathBuf::from("publication-set.json"_str).as_path()));
    }
    return Ok(DocSummary {
        .build               = rstd::move(summary),
        .tool                = tool->executable.clone(),
        .output              = rstd::move(output),
        .data_output         = rstd::move(data_output),
        .publication_receipt = rstd::move(publication_receipt),
        .extracted           = plans.len() - *reused,
        .reused              = *reused,
    });
}

} // namespace lito
