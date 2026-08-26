module;
#include <rstd/macro.hpp>

module lito.driver;

import rstd;
import lito.tools;
import lito.core;
import lito.cpp;
import :build.event;
import :build.request;
import :build.artifact;
import :build.documentation;
import :build.result;
import :build.error;
import :build.layout;
import :build.layout_error;
import :build.discovery;
import :build.package_discovery;
import :build.host_tool;
import :build.host_tool_error;
import :build.script_error;
import :build.tool_action_error;
import :build.compile_executor;
import :build.compile_plan;
import :build.profiling;
import :build.prepared_project;
import :cache.error;
import :project.error;
import lito.toolchain.common;
import :project;
import lito.toolchain;
import :cache;
import :build.script;
import :build.resource;
import lito.frontend;
import :build.frontend_analysis;
import :build.frontend_observer;
import lito.system;
import :build.compile_test;
import :build.unit_plan;
import :build.standard_library_module;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;

namespace lito
{

template<typename T>
auto build_failure(String message) -> BuildResult<T> {
    return Err(BuildError::Message(rstd::move(message)));
}

template<typename T>
auto build_failure(ref<str> message) -> BuildResult<T> {
    return Err(BuildError::Message(String::make(message)));
}

auto emit(const BuildRequest&   request,
          BuildEventKind        kind,
          ref<str>              target,
          ref<rstd::path::Path> path) noexcept -> void {
    if (request.observer.is_none()) return;
    const auto& observer = *request.observer;
    if (observer.notify == nullptr) return;
    observer.notify(observer.context, BuildEvent { kind, target, path });
}

struct ResolvedScanExecution {
    usize jobs { usize(1) };
    usize max_in_flight { usize(1) };
};

struct PreparationObserverContext {
    ExternalPreparationTimingReport* report {};
    const Option<BuildEventSink>*    downstream {};
};

auto external_preparation_operation(BuildEventKind kind) -> Option<ExternalPreparationOperation> {
    switch (kind) {
    case BuildEventKind::Fetch: return Some(ExternalPreparationOperation::SourceFetch);
    case BuildEventKind::Extract: return Some(ExternalPreparationOperation::SourceExtract);
    case BuildEventKind::CMakeConfigure: return Some(ExternalPreparationOperation::CMakeConfigure);
    case BuildEventKind::CMakeBuild: return Some(ExternalPreparationOperation::CMakeBuild);
    case BuildEventKind::CMakeInstall: return Some(ExternalPreparationOperation::CMakeInstall);
    case BuildEventKind::CMakeQuery: return Some(ExternalPreparationOperation::CMakeQuery);
    case BuildEventKind::CMakeQueryBuild:
        return Some(ExternalPreparationOperation::CMakeQueryBuild);
    case BuildEventKind::CMakeSnapshot: return Some(ExternalPreparationOperation::CMakeSnapshot);
    case BuildEventKind::CargoMetadata: return Some(ExternalPreparationOperation::CargoMetadata);
    case BuildEventKind::CargoBuild: return Some(ExternalPreparationOperation::CargoBuild);
    case BuildEventKind::CargoReuse: return Some(ExternalPreparationOperation::CargoReuse);
    default: return None();
    }
}

void observe_preparation(void* raw_context, const BuildEvent& event) noexcept {
    auto& context = *static_cast<PreparationObserverContext*>(raw_context);
    if (event.completed) {
        auto operation = external_preparation_operation(event.kind);
        if (operation.is_some()) context.report->record(*operation, event.elapsed);
    }
    if (context.downstream == nullptr || context.downstream->is_none()) return;
    const auto& observer = **context.downstream;
    if (observer.notify != nullptr) observer.notify(observer.context, event);
}

auto resolve_scan_execution(const ScanExecutionPolicy& policy)
    -> BuildResult<ResolvedScanExecution> {
    auto jobs = usize(1);
    if (policy.jobs.is_some()) {
        jobs = *policy.jobs;
    } else {
        auto available = rstd::thread::available_parallelism();
        if (available.is_ok()) jobs = available->get();
    }
    if (jobs == usize {}) {
        return build_failure<ResolvedScanExecution>("scan jobs must be greater than zero"_str);
    }
    auto max_in_flight = policy.max_in_flight.is_some() ? *policy.max_in_flight : jobs;
    if (max_in_flight == usize {}) {
        return build_failure<ResolvedScanExecution>(
            "scan task capacity must be greater than zero"_str);
    }
    return Ok(ResolvedScanExecution {
        .jobs          = jobs,
        .max_in_flight = max_in_flight,
    });
}

} // namespace lito

namespace lito
{

auto build_with_environment_impl(const BuildRequest&                       request,
                                 const ResolvedProcessEnvironment&         process_environment,
                                 Option<lito::workspace::WorkspaceCatalog> catalog,
                                 Option<PreparedBuildProject>              prepared,
                                 BuildStageTimingReport stage_timing) -> BuildResult<BuildSummary> {
    if (request.selection.root.is_empty()) {
        return build_failure<BuildSummary>("build project root is required"_str);
    }
    auto tool_resolver = lito::tools::ToolResolver(
        process_environment, request.tools.clone(), request.tool_reporter);
    auto profile =
        request.profile.is_some()
            ? request.profile->clone()
            : lito::manifest::BuildProfileName {
                  .value = String::make(request.purpose ==
                                                lito::package::PackageSelectionPurpose::Benchmark
                                            ? "release"_str
                                            : "debug"_str),
              };
    auto execution = resolve_scan_execution(request.execution.scan);
    if (execution.is_err()) return Err(rstd::move(execution).unwrap_err());
    auto preparation_timing  = ExternalPreparationTimingReport {};
    auto preparation_context = PreparationObserverContext {
        .report     = rstd::addressof(preparation_timing),
        .downstream = rstd::addressof(request.observer),
    };
    auto preparation_observer = Some(BuildEventSink {
        .context = rstd::addressof(preparation_context),
        .notify  = observe_preparation,
    });
    auto supplied_prepared    = prepared.is_some();
    auto resolved_project     = [&]() -> BuildResult<PreparedBuildProject> {
        if (prepared.is_some()) return Ok(rstd::move(prepared).unwrap());
        return stage_timing.measure(
            BuildStage::ProjectPrepare, [&]() -> BuildResult<PreparedBuildProject> {
                auto project = prepare_build_project(
                    request.selection,
                    request.configuration,
                    profile,
                    request.build_directory.as_path(),
                    request.sources,
                    request.lock,
                    request.cargo,
                    request.pkg_config,
                    request.cmake,
                    request.cmake_build_overrides,
                    tool_resolver,
                    process_environment,
                    request.locked,
                    request.purpose,
                    execution->jobs,
                    preparation_observer,
                    rstd::move(catalog),
                    request.setup_reporter,
                    request.registries.is_some() ? rstd::addressof(*request.registries) : nullptr);
                if (project.is_err()) {
                    return Err(rstd::into<BuildError>(rstd::move(project).unwrap_err()));
                }
                return Ok(rstd::move(project).unwrap());
            });
    }();
    if (resolved_project.is_err()) return Err(rstd::move(resolved_project).unwrap_err());
    auto  project   = rstd::move(resolved_project).unwrap();
    auto& toolchain = project.toolchain;
    auto& metadata  = project.metadata;
    auto& layout    = project.layout;
    if (supplied_prepared) {
        if (profile.as_str() != metadata.default_profile.as_str()) {
            return build_failure<BuildSummary>(
                rstd::format("prepared project profile '{}' cannot satisfy requested profile '{}'",
                             metadata.default_profile.as_str(),
                             profile.as_str()));
        }
        auto requested_layout = BuildLayout::resolve(metadata.root.as_path(),
                                                     request.build_directory.as_path(),
                                                     metadata.default_profile.as_str(),
                                                     project.platform.output_key.as_str());
        auto canonical_output = rstd::fs::canonicalize(requested_layout.output());
        if (canonical_output.is_err()) {
            return Err(BuildError::System(
                SystemError::Io(String::make("resolve requested prepared build output"_str),
                                PathBuf::from(requested_layout.output()),
                                rstd::move(canonical_output).unwrap_err())));
        }
        if (canonical_output->as_path() != layout.output()) {
            return build_failure<BuildSummary>(
                rstd::format("prepared project output '{}' cannot satisfy requested output '{}'",
                             layout.output(),
                             canonical_output->as_path()));
        }
    }
    auto target_prepare_started = rstd::time::Instant::now();
    auto created_profiler       = ScanProfiler::create();
    if (created_profiler.is_err()) {
        return build_failure<BuildSummary>(rstd::move(created_profiler).unwrap_err_unchecked());
    }
    auto profiler          = rstd::move(created_profiler).unwrap_unchecked();
    auto frontend_observer = FrontendProfileObserver::make(profiler);
    auto frontend_service  = frontend::FrontendService::make(Some(frontend_observer.observer()));

    auto selected = cpp::resolve_source_selection(
        metadata, metadata.default_profile.as_str(), request.targets, request.exact_targets);
    if (selected.is_err()) {
        return Err(rstd::into<BuildError>(rstd::move(selected).unwrap_err()));
    }
    auto selected_targets =
        Vec<lito::package::PackageTargetId>::with_capacity(selected->selected_targets.len());
    for (auto target : selected->selected_targets) {
        selected_targets.push(metadata.targets[target].id.clone());
    }
    for (const auto& requested : request.artifact_link_variants) {
        const cpp::ResolvedTarget* target = nullptr;
        for (auto selected_target : selected->selected_targets) {
            if (metadata.targets[selected_target].id == requested.target) {
                target = rstd::addressof(metadata.targets[selected_target]);
                break;
            }
        }
        if (target == nullptr) {
            return build_failure<BuildSummary>(
                rstd::format("requested install link variant target '{}' is not selected",
                             lito::package::package_target_id_text(requested.target).as_str()));
        }
        if (target->artifact_kind != cpp::ArtifactKind::Executable) {
            return build_failure<BuildSummary>(
                rstd::format("requested install link variant target '{}' is not an executable",
                             lito::package::package_target_id_text(requested.target).as_str()));
        }
    }
    auto selected_packages = Vec<cpp::SelectedPackageMetadata>::make();
    for (const auto& package : metadata.selected_packages) {
        auto version = Option<String> {};
        if (package.version.is_some()) version = Some(package.version->clone());
        selected_packages.push(cpp::SelectedPackageMetadata {
            .name            = package.name.clone(),
            .version         = rstd::move(version),
            .source_identity = package.source_identity.clone(),
            .root            = package.root.clone(),
        });
    }
    auto script_packages = cpp::resolve_build_script_packages(metadata, *selected);
    if (script_packages.is_err()) {
        return Err(rstd::into<BuildError>(rstd::move(script_packages).unwrap_err()));
    }

    auto resolved = profiler.measure(ScanProbe::Plan, [&] {
        return cpp::resolve_native_targets(metadata, selected->clone());
    });
    if (resolved.is_err()) {
        return Err(rstd::into<BuildError>(rstd::move(resolved).unwrap_err()));
    }
    auto native_target_plan = rstd::move(resolved).unwrap();

    auto needs_strip_tool = false;
    for (const auto target : selected->target_order) {
        const auto kind = metadata.targets[target].artifact_kind;
        if (kind == cpp::ArtifactKind::SharedLibrary || kind == cpp::ArtifactKind::Executable ||
            kind == cpp::ArtifactKind::TestExecutable ||
            kind == cpp::ArtifactKind::BenchmarkExecutable) {
            needs_strip_tool = true;
            break;
        }
    }

    auto stripper = as<Clone>(project.target_stripper).clone();
    if (needs_strip_tool &&
        metadata.profiles[native_target_plan.profile].strip != lito::artifact::StripMode::None &&
        stripper.is_none()) {
        const auto tool_requirement = lito::tools::build_profile_tool_requirement(
            lito::tools::HostToolCapability::ArtifactStripping,
            metadata.profiles[native_target_plan.profile].name.as_str(),
            "strip"_str);
        auto resolved_stripper = tool_resolver.require(lito::tools::Tool::Strip, tool_requirement);
        if (resolved_stripper.is_err()) {
            return Err(rstd::into<BuildError>(rstd::move(resolved_stripper).unwrap_err()));
        }
        stripper = Some(rstd::move(resolved_stripper).unwrap().executable);
    }
    auto compile_execution = resolve_compile_execution(request.execution.compile);
    if (compile_execution.is_err()) {
        return Err(rstd::move(compile_execution).unwrap_err());
    }

    auto created_environment =
        CacheEnvironment::create(layout,
                                 metadata.root.as_path(),
                                 metadata.profiles[native_target_plan.profile].name.as_str(),
                                 toolchain.compiler_identity());
    if (created_environment.is_err()) {
        return Err(rstd::into<BuildError>(rstd::move(created_environment).unwrap_err()));
    }
    auto cache_environment = rstd::move(created_environment).unwrap();
    auto scan_cache        = ScanCacheSession::create(cache_environment);

    stage_timing.record(BuildStage::TargetPrepare, target_prepare_started.elapsed());
    auto script_result          = stage_timing.measure(BuildStage::Script, [&] {
        return execute_build_script(metadata,
                                    native_target_plan,
                                    layout,
                                    metadata.default_profile.as_str(),
                                    *script_packages,
                                    *selected,
                                    request.observer,
                                    project.platform.host,
                                    project.platform.effective_target,
                                    toolchain,
                                    tool_resolver,
                                    process_environment,
                                    request.sources,
                                    execution->jobs);
    });
    auto script_report          = rstd_try(rstd::move(script_result));
    auto runtime_result         = stage_timing.measure(BuildStage::RuntimeResource, [&] {
        return resolve_runtime_resources(metadata, layout, selected_targets, request.observer);
    });
    auto runtime_resources      = rstd_try(rstd::move(runtime_result));
    auto scan_started           = rstd::time::Instant::now();
    auto scan_span              = profiler.span(ScanProbe::Total);
    auto toolchain_header_roots = profiler.measure(
        ScanProbe::Environment, [&]() -> BuildResult<Vec<cpp::ResolvedHeaderRoot>> {
            auto roots = Vec<cpp::ResolvedHeaderRoot>::make();
            for (auto target : native_target_plan.target_order) {
                auto resolved =
                    toolchain.header_roots(native_target_plan.contexts[target],
                                           metadata.targets[target].source_root.as_path());
                if (resolved.is_err()) {
                    return Err(rstd::into<BuildError>(rstd::move(resolved).unwrap_err()));
                }
                for (auto& root : *resolved) roots.push(rstd::move(root));
            }
            return Ok(rstd::move(roots));
        });
    if (toolchain_header_roots.is_err()) {
        return Err(rstd::move(toolchain_header_roots).unwrap_err());
    }
    auto header_ownership = cpp::resolve_header_ownership(
        metadata, native_target_plan, rstd::move(toolchain_header_roots).unwrap());
    auto analysis_service = FrontendAnalysisService::make(
        layout, toolchain, header_ownership, frontend_service, scan_cache, profiler);
    auto semantic_scan_graph =
        cpp::SemanticScanGraphBuilder::make(metadata, native_target_plan, header_ownership);

    auto discovered = profiler.measure(ScanProbe::Discovery, [&] {
        return discover_package_sources(metadata,
                                        native_target_plan,
                                        semantic_scan_graph,
                                        analysis_service,
                                        request.observer,
                                        execution->jobs,
                                        execution->max_in_flight);
    });
    if (discovered.is_err()) return Err(rstd::move(discovered).unwrap_err());
    auto source_sets = rstd::move(discovered).unwrap();
    auto finalized   = cpp::finalize_package(rstd::move(metadata), rstd::move(source_sets));
    if (finalized.is_err()) {
        return Err(rstd::into<BuildError>(rstd::move(finalized).unwrap_err()));
    }
    auto package          = rstd::move(finalized).unwrap();
    auto resolved_package = cpp::finalize_package_plan(package, rstd::move(native_target_plan));
    if (resolved_package.is_err()) {
        return Err(rstd::into<BuildError>(rstd::move(resolved_package).unwrap_err()));
    }
    auto package_plan = rstd::move(resolved_package).unwrap();

    auto prepare_span   = profiler.span(ScanProbe::PrepareUnits);
    auto prepared_units = prepare_build_units(package, package_plan, layout, toolchain);
    if (prepared_units.is_err()) return Err(rstd::move(prepared_units).unwrap_err());
    auto  prepared_build       = rstd::move(prepared_units).unwrap();
    auto& target_units         = prepared_build.target_units;
    auto& units                = prepared_build.units;
    auto& scans                = prepared_build.scans;
    auto  preparation_finished = profiler.complete(prepare_span);
    if (preparation_finished.is_err()) {
        return build_failure<BuildSummary>(rstd::move(preparation_finished).unwrap_err_unchecked());
    }

    auto standard_modules = prepare_standard_library_modules(
        prepared_build, scans, analysis_service, layout, toolchain);
    if (standard_modules.is_err()) return Err(rstd::move(standard_modules).unwrap_err());
    auto finalized_scan_graph = rstd::move(semantic_scan_graph).finalize(units, scans);
    if (finalized_scan_graph.is_err()) {
        return build_failure<BuildSummary>(rstd::move(finalized_scan_graph).unwrap_err());
    }
    auto incremental_graph = rstd::move(finalized_scan_graph).unwrap();

    auto convention_valid = profiler.measure(ScanProbe::Conventions, [&] {
        return cpp::validate_module_conventions(package, units, scans);
    });
    if (convention_valid.is_err()) {
        return Err(rstd::into<BuildError>(rstd::move(convention_valid).unwrap_err()));
    }

    auto bmi_format         = toolchain.bmi_format(project.platform);
    auto resolved_semantics = profiler.measure(ScanProbe::ModuleGraph, [&] {
        return cpp::resolve_semantic_build(
            package_plan, units, scans, rstd::move(incremental_graph), bmi_format);
    });
    if (resolved_semantics.is_err()) {
        return Err(rstd::into<BuildError>(rstd::move(resolved_semantics).unwrap_err()));
    }
    auto semantic_graph        = rstd::move(resolved_semantics).unwrap();
    auto scan_graph_statistics = semantic_graph.statistics;
    auto completed_scan        = profiler.complete(scan_span);
    if (completed_scan.is_err()) {
        return build_failure<BuildSummary>(rstd::move(completed_scan).unwrap_err_unchecked());
    }
    auto finished_profile = profiler.finish();
    if (finished_profile.is_err()) {
        return build_failure<BuildSummary>(rstd::move(finished_profile).unwrap_err_unchecked());
    }
    auto scan_profile   = rstd::move(finished_profile).unwrap_unchecked();
    auto source_release = analysis_service.release_source_cache();
    if (source_release.is_err()) {
        auto error = rstd::move(source_release).unwrap_err();
        if (error.kind == frontend::SourceCacheReleaseErrorKind::Closed) {
            return build_failure<BuildSummary>("source cache was already released"_str);
        }
        return build_failure<BuildSummary>(
            rstd::format("cannot release source cache while {} cache entries and {} source loads "
                         "are in flight",
                         error.in_flight_entries,
                         error.active_loads));
    }
    auto source_release_receipt = rstd::move(source_release).unwrap();
    if (! source_release_receipt.released_immediately()) {
        return build_failure<BuildSummary>(
            "source cache memory domain is retained by an active consumer"_str);
    }
    analysis_service.record_source_release(rstd::move(source_release_receipt));
    auto frontend_statistics                            = analysis_service.statistics();
    auto scan_cache_statistics                          = scan_cache.statistics();
    frontend_statistics.persistent_scan_hits            = scan_cache_statistics.hits;
    frontend_statistics.persistent_scan_misses          = scan_cache_statistics.misses;
    frontend_statistics.persistent_scan_uncacheable     = scan_cache_statistics.uncacheable;
    frontend_statistics.persistent_scan_absent          = scan_cache_statistics.absent;
    frontend_statistics.persistent_scan_refresh         = scan_cache_statistics.refresh;
    frontend_statistics.persistent_scan_version         = scan_cache_statistics.version;
    frontend_statistics.persistent_scan_recipe          = scan_cache_statistics.recipe;
    frontend_statistics.persistent_scan_corrupt         = scan_cache_statistics.corrupt;
    frontend_statistics.persistent_scan_environment     = scan_cache_statistics.environment;
    frontend_statistics.persistent_scan_context         = scan_cache_statistics.context;
    frontend_statistics.persistent_scan_source          = scan_cache_statistics.source;
    frontend_statistics.persistent_scan_file_dependency = scan_cache_statistics.file_dependency;
    frontend_statistics.persistent_scan_include_lookup  = scan_cache_statistics.include_lookup;
    frontend_statistics.persistent_scan_embed_lookup    = scan_cache_statistics.embed_lookup;
    frontend_statistics.persistent_scan_external_macro  = scan_cache_statistics.external_macro;
    frontend_statistics.persistent_scan_receipt         = scan_cache_statistics.receipt;
    frontend_statistics.persistent_fingerprint_requests =
        scan_cache_statistics.fingerprint_requests;
    frontend_statistics.persistent_fingerprint_hits   = scan_cache_statistics.fingerprint_hits;
    frontend_statistics.persistent_fingerprint_builds = scan_cache_statistics.fingerprint_builds;
    frontend_statistics.persistent_fingerprint_waits  = scan_cache_statistics.fingerprint_waits;
    frontend_statistics.persistent_fingerprint_wait   = scan_cache_statistics.fingerprint_wait;
    auto scanned                                      = scans.len();
    stage_timing.record(BuildStage::Scan, scan_started.elapsed());

    auto compile_plan_started = rstd::time::Instant::now();
    auto cache                = CompileCacheSession::create(cache_environment, layout.output());
    auto materialized         = materialize_build_actions(package,
                                                          layout,
                                                          toolchain,
                                                          bmi_format,
                                                          units,
                                                          rstd::move(scans),
                                                          rstd::move(semantic_graph),
                                                          selected_targets.as_slice(),
                                                          request.result);
    if (materialized.is_err()) {
        return Err(rstd::move(materialized).unwrap_err());
    }
    auto actions                = rstd::move(materialized).unwrap();
    auto documentation_units    = rstd::move(actions.documentation);
    auto documentation_retained = documentation_retained_bytes(documentation_units);
    stage_timing.record(BuildStage::CompilePlan, compile_plan_started.elapsed());
    auto executed = stage_timing.measure(BuildStage::CompileExecute, [&] {
        return execute_compile_plan(package,
                                    units,
                                    rstd::move(actions.compile),
                                    cache,
                                    toolchain.compile_executor(),
                                    request.observer,
                                    *compile_execution);
    });
    if (executed.is_err()) {
        return Err(rstd::move(executed).unwrap_err());
    }
    auto compile_result                             = rstd::move(executed).unwrap();
    auto compiled                                   = compile_result.compiled;
    auto reused                                     = compile_result.reused;
    auto compile_tests                              = rstd::move(compile_result.compile_tests);
    auto compile_statistics                         = compile_result.statistics;
    compile_statistics.documentation_units          = documentation_units.len();
    compile_statistics.documentation_retained_bytes = documentation_retained;
    auto build_timing                               = rstd::move(compile_result.timing);

    auto cache_finish_started = rstd::time::Instant::now();
    for (auto target : package_plan.target_order) {
        auto records = Vec<PathBuf>::with_capacity(target_units[target].len());
        for (auto unit : target_units[target]) {
            if (units[unit].unit.compile_test == nullptr ||
                units[unit].unit.compile_test->outcome ==
                    lito::manifest::CompileTestOutcome::Success) {
                records.push(units[unit].unit.cache_record.clone());
            }
            if (units[unit].unit.compile_test_record.is_some()) {
                records.push((*units[unit].unit.compile_test_record).clone());
            }
        }
        auto finished = cache.finish_target(layout, package.targets[target].id, records);
        if (finished.is_err()) {
            return Err(rstd::into<BuildError>(rstd::move(finished).unwrap_err()));
        }
    }
    stage_timing.record(BuildStage::CompileCacheFinish, cache_finish_started.elapsed());

    auto artifacts     = Vec<BuiltArtifact>::make();
    auto library_paths = Vec<Option<PathBuf>>::with_capacity(package.targets.len());
    for (auto target = cpp::TargetId {}; target < package.targets.len(); ++target) {
        library_paths.emplace_back(None());
    }
    for (auto target : package_plan.target_order) {
        const auto& target_spec = package.targets[target];
        if (target_spec.artifact_kind != cpp::ArtifactKind::StaticLibrary &&
            target_spec.artifact_kind != cpp::ArtifactKind::TestAttachmentArchive) {
            continue;
        }
        auto archive_started = rstd::time::Instant::now();
        auto archive_path =
            target_spec.test_attachment.is_some()
                ? layout.test_attachment_archive(*target_spec.test_attachment,
                                                 target_spec.archive_stem.as_str())
                : layout.archive(target_spec.id, target_spec.artifact_name.as_str());
        auto objects = Vec<PathBuf>::with_capacity(target_units[target].len());
        for (auto unit : target_units[target]) objects.push(units[unit].unit.object.clone());
        auto target_identity = lito::package::package_target_id_text(target_spec.id);
        emit(request, BuildEventKind::Archive, target_identity.as_str(), archive_path.as_path());
        auto archived =
            toolchain.archive(archive_path.as_path(), objects, target_spec.root.as_path());
        if (archived.is_err()) {
            return Err(rstd::into<BuildError>(rstd::move(archived).unwrap_err()));
        }
        build_timing.record(BuildOperation::Archive, *archived);
        library_paths[target] = Some(archive_path.clone());
        artifacts.push(BuiltArtifact {
            .target        = target_spec.id.clone(),
            .kind          = target_spec.artifact_kind,
            .path          = rstd::move(archive_path),
            .package_root  = target_spec.root.clone(),
            .link_identity = String::make(),
        });
        stage_timing.record(BuildStage::Archive, archive_started.elapsed());
    }

    for (auto target : package_plan.target_order) {
        const auto& target_spec = package.targets[target];
        if (target_spec.artifact_kind == cpp::ArtifactKind::StaticLibrary ||
            target_spec.artifact_kind == cpp::ArtifactKind::TestAttachmentArchive ||
            target_spec.artifact_kind == cpp::ArtifactKind::CompileTest) {
            continue;
        }
        auto link_started = rstd::time::Instant::now();
        auto objects      = Vec<PathBuf>::with_capacity(target_units[target].len());
        for (auto unit : target_units[target]) objects.push(units[unit].unit.object.clone());
        auto link_inputs = Vec<ResolvedLinkInput>::make();
        if (target_spec.artifact_kind == cpp::ArtifactKind::TestExecutable) {
            for (auto candidate : package_plan.target_order) {
                const auto& candidate_spec = package.targets[candidate];
                if (candidate_spec.test_attachment.is_none() ||
                    ! (candidate_spec.test_attachment->test_target == target_spec.id)) {
                    continue;
                }
                if (library_paths[candidate].is_none()) {
                    return build_failure<BuildSummary>(
                        rstd::format("test target '{}' has no attachment archive for '{}'",
                                     lito::package::package_target_id_text(target_spec.id).as_str(),
                                     lito::package::package_target_id_text(
                                         candidate_spec.test_attachment->library_target)
                                         .as_str()));
                }
                link_inputs.push(ResolvedLinkInput::Archive(LinkArchive {
                    .path = (*library_paths[candidate]).clone(),
                    .mode = LinkArchiveMode::Whole,
                }));
            }
        }
        for (const auto& input : package_plan.link_inputs[target]) {
            if (input.is_External()) {
                link_inputs.push(
                    ResolvedLinkInput::External(input.as_External().arguments.clone()));
                continue;
            }
            auto        dependency      = input.as_Target().target;
            const auto& dependency_spec = package.targets[dependency];
            if ((dependency_spec.artifact_kind != cpp::ArtifactKind::StaticLibrary &&
                 dependency_spec.artifact_kind != cpp::ArtifactKind::SharedLibrary) ||
                library_paths[dependency].is_none()) {
                return build_failure<BuildSummary>(rstd::format(
                    "executable target '{}' depends on unavailable "
                    "library target '{}'",
                    lito::package::package_target_id_text(target_spec.id).as_str(),
                    lito::package::package_target_id_text(dependency_spec.id).as_str()));
            }
            if (dependency_spec.artifact_kind == cpp::ArtifactKind::SharedLibrary) {
                link_inputs.push(
                    ResolvedLinkInput::SharedLibrary((*library_paths[dependency]).clone()));
            } else {
                link_inputs.push(ResolvedLinkInput::Archive(LinkArchive {
                    .path = (*library_paths[dependency]).clone(),
                    .mode = LinkArchiveMode::Normal,
                }));
            }
        }
        const RequestedArtifactLinkVariant* install_variant = nullptr;
        for (const auto& requested : request.artifact_link_variants) {
            if (requested.target != target_spec.id) continue;
            if (install_variant != nullptr) {
                return build_failure<BuildSummary>(
                    rstd::format("target '{}' has more than one requested install link variant",
                                 lito::package::package_target_id_text(target_spec.id).as_str()));
            }
            install_variant = rstd::addressof(requested);
        }
        auto link_requirements = package_plan.link_requirements[target].clone();
        auto executable_path =
            target_spec.artifact_kind == cpp::ArtifactKind::SharedLibrary
                ? layout.shared_library(target_spec.id, target_spec.artifact_name.as_str())
                : layout.executable(target_spec.id, target_spec.artifact_name.as_str());
        if (install_variant != nullptr) {
            lito::link::replace_runtime_search_paths(link_requirements,
                                                     install_variant->policy.runtime_search,
                                                     "install artifact policy"_str);
            executable_path = layout.install_executable(target_spec.id,
                                                        target_spec.artifact_name.as_str(),
                                                        install_variant->policy.identity.as_str());
        }
        if (install_variant == nullptr &&
            target_spec.artifact_kind == cpp::ArtifactKind::TestExecutable) {
            executable_path = layout.test(target_spec.id, target_spec.artifact_name.as_str());
        } else if (install_variant == nullptr &&
                   target_spec.artifact_kind == cpp::ArtifactKind::BenchmarkExecutable) {
            executable_path = layout.benchmark(target_spec.id, target_spec.artifact_name.as_str());
        }
        auto target_identity = lito::package::package_target_id_text(target_spec.id);
        emit(request, BuildEventKind::Link, target_identity.as_str(), executable_path.as_path());
        const auto& language_lto = target_spec.language == lito::manifest::PackageLanguage::C
                                       ? package_plan.profile->c.common.codegen.lto
                                       : package_plan.profile->cpp.common.codegen.lto;
        const auto& link_lto     = package_plan.profile->link_lto.is_some()
                                       ? Option<lito::manifest::Lto> {}
                                       : language_lto;
        const auto& microsoft_runtime_library =
            target_spec.language == lito::manifest::PackageLanguage::C
                ? package_plan.profile->c.common.microsoft_runtime_library
                : package_plan.profile->cpp.common.microsoft_runtime_library;
        auto link_context = LinkTargetContext {
            .platform                  = project.platform.clone(),
            .language                  = target_spec.language,
            .standard_library          = package_plan.profile->cpp.abi.standard_library,
            .standard_library_runtime  = request.configuration.standard_library_runtime,
            .microsoft_runtime_library = microsoft_runtime_library,
            .link_standard_library     = target_spec.link_stdlib,
        };
        auto linked = [&] {
            if (target_spec.artifact_kind == cpp::ArtifactKind::SharedLibrary) {
                link_context.soname = Some(target_spec.artifact_name.clone());
                return toolchain.link_shared_library(executable_path.as_path(),
                                                     objects,
                                                     link_inputs,
                                                     rstd::move(link_context),
                                                     link_lto,
                                                     link_requirements,
                                                     package_plan.linker_options[target],
                                                     target_spec.root.as_path());
            }
            return toolchain.link_executable(executable_path.as_path(),
                                             objects,
                                             link_inputs,
                                             link_context,
                                             link_lto,
                                             link_requirements,
                                             package_plan.linker_options[target],
                                             target_spec.root.as_path());
        }();
        if (linked.is_err()) {
            return Err(rstd::into<BuildError>(rstd::move(linked).unwrap_err()));
        }
        build_timing.record(BuildOperation::Link, *linked);
        if (target_spec.artifact_kind == cpp::ArtifactKind::SharedLibrary) {
            library_paths[target] = Some(executable_path.clone());
        }
        if (package_plan.profile->strip != lito::artifact::StripMode::None) {
            if (stripper.is_none()) {
                return build_failure<BuildSummary>("strip tool was not resolved"_str);
            }
            emit(request,
                 BuildEventKind::Strip,
                 target_identity.as_str(),
                 executable_path.as_path());
            auto stripped = toolchain.strip_artifact(executable_path.as_path(),
                                                     stripper->as_path(),
                                                     package_plan.profile->strip,
                                                     target_spec.root.as_path());
            if (stripped.is_err()) {
                return Err(rstd::into<BuildError>(rstd::move(stripped).unwrap_err()));
            }
            build_timing.record(BuildOperation::Strip, *stripped);
        }
        auto link_identity = String::make("lito-built-artifact-link-v3\n"_str);
        link_identity.push_str(toolchain.linker_identity().build_identity.as_str());
        link_identity.push_ascii('\n');
        link_identity.push_str("target="_str);
        link_identity.push_str(project.platform.effective_target.triple.as_str());
        link_identity.push_ascii('\n');
        link_identity.push_str("sysroot="_str);
        if (project.platform.sysroot.is_some()) {
            link_identity.push_str(project.platform.sysroot->as_path().to_string_lossy().as_str());
        }
        link_identity.push_ascii('\n');
        link_identity.push_str("sdk="_str);
        if (project.platform.sdk_identity.is_some()) {
            link_identity.push_str(project.platform.sdk_identity->as_str());
        }
        link_identity.push_ascii('\n');
        link_identity.push_str("stdlib-runtime="_str);
        link_identity.push_str(lito::config::standard_library_runtime_name(
            request.configuration.standard_library_runtime));
        link_identity.push_ascii('\n');
        link_identity.push_str(target_identity.as_str());
        link_identity.push_ascii('\n');
        link_identity.push_str(rstd::format("package-source={}:{}\n",
                                            target_spec.package_source_identity.len(),
                                            target_spec.package_source_identity.as_str())
                                   .as_str());
        link_identity.push_str("lto="_str);
        link_identity.push_str(cpp::cpp_lto_option(link_lto));
        link_identity.push_ascii('\n');
        if (install_variant == nullptr) {
            link_identity.push_str("variant=normal\n"_str);
        } else {
            link_identity.push_str("variant=install\nidentity="_str);
            link_identity.push_str(install_variant->policy.identity.as_str());
            link_identity.push_ascii('\n');
        }
        link_identity.push_str(lito::link::requirements_identity(link_requirements).as_str());
        auto install_link = Option<InstallArtifactLinkPolicy> {};
        if (install_variant != nullptr) install_link = Some(install_variant->policy.clone());
        artifacts.push(BuiltArtifact {
            .target        = target_spec.id.clone(),
            .kind          = target_spec.artifact_kind,
            .path          = rstd::move(executable_path),
            .package_root  = target_spec.root.clone(),
            .install_link  = rstd::move(install_link),
            .link_identity = rstd::move(link_identity),
        });
        stage_timing.record(BuildStage::Link, link_started.elapsed());
    }

    auto product = CompletedBuildProduct {
        .profile = package_plan.profile->name.clone(),
        .target  = project.platform.effective_target.triple.clone(),
        .target_kind =
            String::make(request.configuration.target.is_Android() ? "android"_str : "default"_str),
        .android_abi         = project.platform.android_abi.is_some()
                                   ? project.platform.android_abi->clone()
                                   : String::make(),
        .android_minimum_api = project.platform.android_minimum_api.is_some()
                                   ? *project.platform.android_minimum_api
                                   : u32 {},
        .base_directory      = PathBuf::from(layout.base_directory()),
        .build_directory     = PathBuf::from(layout.output()),
        .artifacts           = rstd::move(artifacts),
        .target_runtimes     = rstd::move(project.target_runtimes),
        .external_assets     = rstd::move(project.external_assets),
    };
    emit(request,
         BuildEventKind::ProductFinalize,
         product.profile.as_str(),
         product.base_directory.as_path());
    auto completed_product = stage_timing.measure(BuildStage::ProductFinalize, [&] {
        return finalize_completed_build_product(rstd::move(product));
    });
    if (completed_product.is_err()) {
        return Err(rstd::into<BuildError>(rstd::move(completed_product).unwrap_err()));
    }

    return Ok(BuildSummary {
        .product                    = rstd::move(completed_product).unwrap(),
        .package                    = package.name.clone(),
        .selected_targets           = rstd::move(selected_targets),
        .selected_packages          = rstd::move(selected_packages),
        .runtime_resources          = rstd::move(runtime_resources),
        .external_source_provenance = rstd::move(project.external_source_provenance),
        .platform                   = project.platform.clone(),
        .language_standard          = request.configuration.language_standard.clone(),
        .scanned                    = scanned,
        .compiled                   = compiled,
        .reused                     = reused,
        .frontend                   = frontend_statistics,
        .scan_graph                 = scan_graph_statistics,
        .toolchain                  = toolchain.statistics(),
        .scan_profile               = rstd::move(scan_profile),
        .compile_execution          = compile_statistics,
        .stage_timing               = rstd::move(stage_timing),
        .external_preparation       = rstd::move(preparation_timing),
        .build_timing               = rstd::move(build_timing),
        .compile_tests              = rstd::move(compile_tests),
        .script                     = rstd::move(script_report),
        .compiler                   = toolchain.compiler_identity().clone(),
        .documentation_units        = rstd::move(documentation_units),
    });
}

} // namespace lito

namespace lito
{

auto build_with_environment(const BuildRequest&               request,
                            const ResolvedProcessEnvironment& process_environment)
    -> BuildResult<BuildSummary> {
    auto total_started = rstd::time::Instant::now();
    auto summary       = build_with_environment_impl(
        request, process_environment, None(), None(), BuildStageTimingReport {});
    if (summary.is_err()) return Err(rstd::move(summary).unwrap_err());
    summary->stage_timing.record(BuildStage::Total, total_started.elapsed());
    return summary;
}

auto build_resolved_project(BuildRequest request, lito::workspace::ResolvedProjectEntry project)
    -> BuildResult<BuildSummary> {
    auto total_started     = rstd::time::Instant::now();
    request.selection.root = rstd::move(project.root);
    auto stage_timing      = BuildStageTimingReport {};
    auto environment       = stage_timing.measure(BuildStage::Environment, [&] {
        return ResolvedProcessEnvironment::resolve(request.environment);
    });
    if (environment.is_err()) {
        return Err(rstd::into<BuildError>(rstd::move(environment).unwrap_err()));
    }
    auto summary = build_with_environment_impl(
        request, *environment, Some(rstd::move(project.catalog)), None(), rstd::move(stage_timing));
    if (summary.is_err()) return Err(rstd::move(summary).unwrap_err());
    summary->stage_timing.record(BuildStage::Total, total_started.elapsed());
    return summary;
}

auto build_prepared_project(const BuildRequest&               request,
                            const ResolvedProcessEnvironment& environment,
                            PreparedBuildProject project) -> BuildResult<BuildSummary> {
    auto total_started = rstd::time::Instant::now();
    auto summary       = build_with_environment_impl(
        request, environment, None(), Some(rstd::move(project)), BuildStageTimingReport {});
    if (summary.is_err()) return Err(rstd::move(summary).unwrap_err());
    summary->stage_timing.record(BuildStage::Total, total_started.elapsed());
    return summary;
}

auto build(const BuildRequest& request) -> BuildResult<BuildSummary> {
    auto total_started = rstd::time::Instant::now();
    if (request.selection.root.is_empty()) {
        return build_failure<BuildSummary>("build project root is required"_str);
    }
    const auto profile =
        request.profile.is_some()
            ? request.profile->clone()
            : lito::manifest::BuildProfileName {
                  .value = String::make(request.purpose ==
                                                lito::package::PackageSelectionPurpose::Benchmark
                                            ? "release"_str
                                            : "debug"_str),
              };
    auto stage_timing = BuildStageTimingReport {};
    auto publication  = stage_timing.measure(BuildStage::ProductBegin, [&] {
        return begin_build_product_publication(
            request.selection.root.as_path(), request.build_directory.as_path(), profile.as_str());
    });
    if (publication.is_err()) {
        return Err(rstd::into<BuildError>(rstd::move(publication).unwrap_err()));
    }
    auto environment = stage_timing.measure(BuildStage::Environment, [&] {
        return ResolvedProcessEnvironment::resolve(request.environment);
    });
    if (environment.is_err()) {
        return Err(rstd::into<BuildError>(rstd::move(environment).unwrap_err()));
    }
    auto summary = build_with_environment_impl(
        request, *environment, None(), None(), rstd::move(stage_timing));
    if (summary.is_err()) return Err(rstd::move(summary).unwrap_err());
    emit(request,
         BuildEventKind::ProductPublish,
         summary->product.profile.as_str(),
         publication->state.as_path());
    auto completed = summary->stage_timing.measure(BuildStage::ProductPublish, [&] {
        return complete_build_product_publication(*publication, summary->product);
    });
    if (completed.is_err()) {
        return Err(rstd::into<BuildError>(rstd::move(completed).unwrap_err()));
    }
    summary->stage_timing.record(BuildStage::Total, total_started.elapsed());
    return Ok(rstd::move(summary).unwrap());
}

} // namespace lito
