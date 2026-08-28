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
import :build.plugin;
import :build.proc_macro;
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

auto build_archive_target(const BuildRequest&                        request,
                          const BuildLayout&                         layout,
                          const ClangToolchain&                      toolchain,
                          ArchiveCacheSession&                       cache,
                          const cpp::TargetSpec&                     target,
                          const Vec<cpp::PreparedUnit>&              units,
                          const Vec<cpp::UnitId>&                    target_units,
                          const Vec<Option<CachedArtifactIdentity>>& object_identities,
                          BuildTimingReport& timing) -> BuildResult<PathBuf> {
    auto archive =
        target.test_attachment.is_some()
            ? layout.test_attachment_archive(*target.test_attachment, target.archive_stem.as_str())
            : layout.archive(target.id, target.artifact_name.as_str());
    auto objects = Vec<PathBuf>::with_capacity(target_units.len());
    auto inputs  = Vec<CachedArtifactIdentity>::with_capacity(target_units.len());
    for (auto unit : target_units) {
        objects.push(units[unit].unit.object.clone());
        if (unit >= object_identities.len() || object_identities[unit].is_none()) {
            return build_failure<PathBuf>(
                rstd::format("archive input '{}' has no compiled object identity",
                             units[unit].unit.object.as_path()));
        }
        inputs.push(object_identities[unit]->clone());
    }
    auto identity   = lito::package::package_target_id_text(target.id);
    auto invocation = toolchain.prepare_archive(archive.as_path(), objects, target.root.as_path());
    if (invocation.is_err()) {
        return Err(rstd::into<BuildError>(rstd::move(invocation).unwrap_err()));
    }
    auto decision = cache.evaluate(
        identity.as_str(), layout.cache_archive(target.id).as_path(), *invocation, inputs);
    if (decision.is_err()) {
        return Err(rstd::into<BuildError>(rstd::move(decision).unwrap_err()));
    }
    if (decision->current()) {
        emit(request, BuildEventKind::ArchiveReuse, identity.as_str(), archive.as_path());
        return Ok(rstd::move(archive));
    }
    auto begun = cache.begin_archive(*decision);
    if (begun.is_err()) {
        return Err(rstd::into<BuildError>(rstd::move(begun).unwrap_err()));
    }
    emit(request, BuildEventKind::Archive, identity.as_str(), archive.as_path());
    auto archived = toolchain.execute_archive(*invocation);
    if (archived.is_err()) {
        return Err(rstd::into<BuildError>(rstd::move(archived).unwrap_err()));
    }
    timing.record(BuildOperation::Archive, *archived);
    auto committed = cache.commit_success(*decision);
    if (committed.is_err()) {
        return Err(rstd::into<BuildError>(rstd::move(committed).unwrap_err()));
    }
    return Ok(rstd::move(archive));
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
                                 BuildStageTimingReport                    stage_timing,
                                 bool prepared_defaults = false) -> BuildResult<BuildSummary> {
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
    auto project     = rstd::move(resolved_project).unwrap();
    auto plugin_host = Option<BuildSummary> {};
    if (project.plugin_host.is_some()) {
        auto host  = rstd::move(project.plugin_host).unwrap();
        auto built = stage_timing.measure(BuildStage::Plugin, [&] {
            return build_with_environment_impl(request,
                                               process_environment,
                                               None(),
                                               Some(rstd::move(*host)),
                                               BuildStageTimingReport {},
                                               true);
        });
        if (built.is_err()) return Err(rstd::move(built).unwrap_err());
        plugin_host = Some(rstd::move(built).unwrap());
    }
    auto& toolchain = project.toolchain;
    auto& metadata  = project.metadata;
    auto& layout    = project.layout;
    if (supplied_prepared && ! prepared_defaults) {
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

    auto        default_target_names   = Vec<String>::make();
    auto        default_exact_targets  = Vec<lito::package::PackageTargetId>::make();
    const auto& requested_target_names = prepared_defaults ? default_target_names : request.targets;
    const auto& requested_exact_targets =
        prepared_defaults ? default_exact_targets : request.exact_targets;
    auto selected = cpp::resolve_source_selection(metadata,
                                                  metadata.default_profile.as_str(),
                                                  requested_target_names,
                                                  requested_exact_targets);
    if (selected.is_err()) {
        return Err(rstd::into<BuildError>(rstd::move(selected).unwrap_err()));
    }
    auto selected_targets =
        Vec<lito::package::PackageTargetId>::with_capacity(selected->selected_targets.len());
    for (auto target : selected->selected_targets) {
        selected_targets.push(metadata.targets[target].id.clone());
    }
    const auto* artifact_link_variants =
        prepared_defaults ? static_cast<const Vec<RequestedArtifactLinkVariant>*>(nullptr)
                          : rstd::addressof(request.artifact_link_variants);
    if (artifact_link_variants != nullptr)
        for (const auto& requested : *artifact_link_variants) {
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
    auto compiler_plugin_sdk =
        prepare_compiler_plugin_sdk(metadata, native_target_plan, toolchain, process_environment);
    if (compiler_plugin_sdk.is_err()) {
        return Err(rstd::move(compiler_plugin_sdk).unwrap_err());
    }
    auto plugin_sdk = rstd::move(compiler_plugin_sdk).unwrap();

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
    if (plugin_sdk.is_some()) {
        for (auto& root : plugin_sdk->header_roots) {
            toolchain_header_roots->push(rstd::move(root));
        }
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

    auto initial_plan_started = rstd::time::Instant::now();
    auto cache                = CompileCacheSession::create(cache_environment, layout.output());
    auto archive_cache        = ArchiveCacheSession::create(cache_environment, layout.output());
    auto proc_macro_sources   = Vec<u8>::make();
    if (plugin_host.is_some()) {
        auto selected = select_proc_macro_sources(package, package_plan, target_units, scans);
        if (selected.is_err()) return Err(rstd::move(selected).unwrap_err());
        proc_macro_sources = rstd::move(selected).unwrap();
    }
    auto materialized = materialize_build_actions(package,
                                                  layout,
                                                  toolchain,
                                                  bmi_format,
                                                  units,
                                                  rstd::move(scans),
                                                  rstd::move(semantic_graph),
                                                  selected_targets.as_slice(),
                                                  request.result);
    if (materialized.is_err()) return Err(rstd::move(materialized).unwrap_err());
    auto actions                = rstd::move(materialized).unwrap();
    auto documentation_units    = rstd::move(actions.documentation);
    auto documentation_retained = documentation_retained_bytes(documentation_units);
    auto initial_plan_elapsed   = initial_plan_started.elapsed();
    auto compile_plan_recorded  = false;

    auto prerequisite_selection = Vec<u8>::with_capacity(actions.compile.nodes.len());
    auto prerequisite_identities =
        Vec<Option<CachedArtifactIdentity>>::with_capacity(actions.compile.nodes.len());
    for (auto unit = cpp::UnitId {}; unit < actions.compile.nodes.len(); ++unit) {
        prerequisite_selection.push(u8 {});
        prerequisite_identities.emplace_back();
    }

    auto rebuild_after_source_transformation = [&]() -> BuildResult<empty> {
        auto authoritative_profiler_result = ScanProfiler::create();
        if (authoritative_profiler_result.is_err()) {
            return build_failure<empty>(
                rstd::move(authoritative_profiler_result).unwrap_err_unchecked());
        }
        auto authoritative_profiler = rstd::move(authoritative_profiler_result).unwrap_unchecked();
        auto authoritative_observer = FrontendProfileObserver::make(authoritative_profiler);
        auto authoritative_frontend =
            frontend::FrontendService::make(Some(authoritative_observer.observer()));
        for (const auto& target : package.targets) {
            for (const auto& source : target.sources) {
                if (source.transformed.is_none()) continue;
                if (! authoritative_frontend.add_source_overlay(
                        source.transformed->logical_path.as_path(),
                        source.transformed->physical_path.as_path())) {
                    return build_failure<empty>(
                        rstd::format("cannot register transformed source overlay for '{}'",
                                     source.transformed->logical_path.as_path()));
                }
            }
        }
        auto authoritative_cache    = ScanCacheSession::create(cache_environment);
        auto authoritative_analysis = FrontendAnalysisService::make(layout,
                                                                    toolchain,
                                                                    header_ownership,
                                                                    authoritative_frontend,
                                                                    authoritative_cache,
                                                                    authoritative_profiler);
        auto authoritative_graph =
            cpp::SemanticScanGraphBuilder::make(package, package_plan, header_ownership);
        auto authoritative_span = authoritative_profiler.span(ScanProbe::Total);
        for (auto target : package_plan.target_order) {
            auto& target_spec = package.targets[target];
            for (auto& source : target_spec.sources) {
                auto graph_unit = authoritative_graph.register_project_unit(
                    target,
                    source.path.as_path(),
                    package_plan.contexts[target].scan_id.as_str(),
                    package_plan.contexts[target].id.as_str());
                auto analyzed = authoritative_analysis.analyze(target_spec.id,
                                                               source.relative_path.as_path(),
                                                               source.origin_identity.as_str(),
                                                               source.path.as_path(),
                                                               package_plan.contexts[target],
                                                               target_spec.compile_metadata,
                                                               target_spec.root.as_path());
                if (analyzed.is_err()) return Err(rstd::move(analyzed).unwrap_err());
                auto projected = authoritative_analysis.project(rstd::move(analyzed).unwrap(),
                                                                target_spec.language);
                if (projected.is_err()) {
                    return build_failure<empty>(rstd::move(projected).unwrap_err());
                }
                source.scan_artifact = Some(rstd::move(projected).unwrap());
                auto completed = authoritative_graph.complete(graph_unit, *source.scan_artifact);
                if (completed.is_err()) {
                    return build_failure<empty>(rstd::move(completed).unwrap_err());
                }
            }
        }
        auto discovery_finished = authoritative_graph.finish_discovery();
        if (discovery_finished.is_err()) {
            return build_failure<empty>(rstd::move(discovery_finished).unwrap_err());
        }

        auto authoritative_units = prepare_build_units(package, package_plan, layout, toolchain);
        if (authoritative_units.is_err()) {
            return Err(rstd::move(authoritative_units).unwrap_err());
        }
        prepared_build                      = rstd::move(authoritative_units).unwrap();
        auto authoritative_standard_modules = prepare_standard_library_modules(
            prepared_build, scans, authoritative_analysis, layout, toolchain);
        if (authoritative_standard_modules.is_err()) {
            return Err(rstd::move(authoritative_standard_modules).unwrap_err());
        }
        auto authoritative_incremental = rstd::move(authoritative_graph).finalize(units, scans);
        if (authoritative_incremental.is_err()) {
            return build_failure<empty>(rstd::move(authoritative_incremental).unwrap_err());
        }
        auto authoritative_conventions = cpp::validate_module_conventions(package, units, scans);
        if (authoritative_conventions.is_err()) {
            return Err(rstd::into<BuildError>(rstd::move(authoritative_conventions).unwrap_err()));
        }
        auto authoritative_semantics = cpp::resolve_semantic_build(
            package_plan, units, scans, rstd::move(authoritative_incremental).unwrap(), bmi_format);
        if (authoritative_semantics.is_err()) {
            return Err(rstd::into<BuildError>(rstd::move(authoritative_semantics).unwrap_err()));
        }
        scan_graph_statistics            = authoritative_semantics->statistics;
        auto authoritative_span_finished = authoritative_profiler.complete(authoritative_span);
        if (authoritative_span_finished.is_err()) {
            return build_failure<empty>(
                rstd::move(authoritative_span_finished).unwrap_err_unchecked());
        }
        auto authoritative_release = authoritative_analysis.release_source_cache();
        if (authoritative_release.is_err()) {
            return build_failure<empty>("cannot release authoritative source cache"_str);
        }
        authoritative_analysis.record_source_release(rstd::move(authoritative_release).unwrap());
        frontend_statistics.add(authoritative_analysis.statistics());
        auto authoritative_profile = authoritative_profiler.finish();
        if (authoritative_profile.is_err()) {
            return build_failure<empty>(rstd::move(authoritative_profile).unwrap_err_unchecked());
        }
        scan_profile = rstd::move(authoritative_profile).unwrap_unchecked();
        scanned += scans.len();

        auto authoritative_actions =
            materialize_build_actions(package,
                                      layout,
                                      toolchain,
                                      bmi_format,
                                      units,
                                      rstd::move(scans),
                                      rstd::move(authoritative_semantics).unwrap(),
                                      selected_targets.as_slice(),
                                      request.result);
        if (authoritative_actions.is_err()) {
            return Err(rstd::move(authoritative_actions).unwrap_err());
        }
        actions                = rstd::move(authoritative_actions).unwrap();
        documentation_units    = rstd::move(actions.documentation);
        documentation_retained = documentation_retained_bytes(documentation_units);
        prerequisite_selection.clear();
        prerequisite_identities.clear();
        for (auto unit = cpp::UnitId {}; unit < actions.compile.nodes.len(); ++unit) {
            prerequisite_selection.push(u8 {});
            prerequisite_identities.emplace_back();
        }
        return Ok(empty {});
    };

    if (plugin_host.is_some()) {
        auto plugin_action_started = rstd::time::Instant::now();
        auto macro_roots           = Vec<cpp::UnitId>::make();
        for (auto unit = cpp::UnitId {}; unit < proc_macro_sources.len(); ++unit) {
            if (proc_macro_sources[unit] != u8 {}) macro_roots.emplace_back(unit);
        }
        prerequisite_selection = compile_plan_prerequisite_closure(actions.compile, macro_roots);
        for (auto root : macro_roots) prerequisite_selection[root] = u8 {};
        auto prepared_dependencies =
            execute_compile_plan_selection(package,
                                           units,
                                           actions.compile,
                                           prerequisite_selection,
                                           rstd::move(prerequisite_identities),
                                           cache,
                                           toolchain.compile_executor(),
                                           request.observer,
                                           *compile_execution);
        if (prepared_dependencies.is_err()) {
            return Err(rstd::move(prepared_dependencies).unwrap_err());
        }
        auto prepared_dependency_result = rstd::move(prepared_dependencies).unwrap();
        prerequisite_identities         = rstd::move(prepared_dependency_result.object_identities);
        auto transformed = transform_proc_macro_sources(layout,
                                                        toolchain,
                                                        package,
                                                        package_plan,
                                                        units,
                                                        target_units,
                                                        proc_macro_sources,
                                                        plugin_host->proc_macro_aggregates);
        if (transformed.is_err()) return Err(rstd::move(transformed).unwrap_err());
        if (*transformed) {
            stage_timing.record(BuildStage::Plugin, initial_plan_elapsed);
            auto authoritative_plan_started = rstd::time::Instant::now();
            rstd_try(rebuild_after_source_transformation());
            stage_timing.record(BuildStage::CompilePlan, authoritative_plan_started.elapsed());
            compile_plan_recorded = true;
        }
        stage_timing.record(BuildStage::Plugin, plugin_action_started.elapsed());
    }
    if (! compile_plan_recorded) {
        stage_timing.record(BuildStage::CompilePlan, initial_plan_elapsed);
    }

    auto object_identities  = rstd::move(prerequisite_identities);
    auto compiled           = usize {};
    auto reused             = usize {};
    auto compile_tests      = Vec<CompileTestExecution>::make();
    auto compile_statistics = CompileExecutionStatistics {};
    auto build_timing       = BuildTimingReport {};
    auto library_paths      = Vec<Option<PathBuf>>::with_capacity(package.targets.len());
    for (auto target = cpp::TargetId {}; target < package.targets.len(); ++target) {
        library_paths.emplace_back();
    }
    auto compiler_plugins = Vec<BuiltCompilerPlugin>::make();
    if (plugin_host.is_some()) {
        compiler_plugins.reserve(plugin_host->compiler_plugins.len());
        for (const auto& product : plugin_host->compiler_plugins) {
            compiler_plugins.push(product.clone());
        }
    }
    auto plugin_targets = Vec<u8>::with_capacity(package.targets.len());
    for (auto target = cpp::TargetId {}; target < package.targets.len(); ++target) {
        plugin_targets.push(u8 {});
    }
    auto pending_plugin_targets = Vec<cpp::TargetId>::make();
    for (auto target : package_plan.target_order) {
        if (package.targets[target].artifact_kind == cpp::ArtifactKind::CompilerPlugin) {
            pending_plugin_targets.emplace_back(target);
        }
    }
    while (! pending_plugin_targets.is_empty()) {
        const auto target = rstd::move(pending_plugin_targets.pop()).unwrap_unchecked();
        if (plugin_targets[target] != u8 {}) continue;
        plugin_targets[target] = u8(1);
        for (const auto& input : package_plan.link_inputs[target]) {
            if (input.is_Target()) pending_plugin_targets.emplace_back(input.as_Target().target);
        }
    }

    auto plugin_roots   = Vec<cpp::UnitId>::make();
    auto provider_roots = Vec<cpp::UnitId>::make();
    for (auto target : package_plan.target_order) {
        if (plugin_targets[target] != u8 {}) {
            for (auto unit : target_units[target]) plugin_roots.emplace_back(unit);
        }
        if (package.targets[target].artifact_kind == cpp::ArtifactKind::ProcMacroProvider) {
            for (auto unit : target_units[target]) provider_roots.emplace_back(unit);
        }
    }
    auto plugin_selection    = compile_plan_prerequisite_closure(actions.compile, plugin_roots);
    auto completed_selection = prerequisite_selection.clone();
    for (auto unit = cpp::UnitId {}; unit < completed_selection.len(); ++unit) {
        if (plugin_selection[unit] != u8 {}) completed_selection[unit] = u8(1);
    }
    if (! plugin_roots.is_empty()) {
        auto plugin_started = rstd::time::Instant::now();
        auto plugin_compile = execute_compile_plan_selection(package,
                                                             units,
                                                             actions.compile,
                                                             plugin_selection,
                                                             rstd::move(object_identities),
                                                             cache,
                                                             toolchain.compile_executor(),
                                                             request.observer,
                                                             *compile_execution);
        if (plugin_compile.is_err()) {
            return Err(rstd::move(plugin_compile).unwrap_err());
        }
        auto plugin_result = rstd::move(plugin_compile).unwrap();
        object_identities  = rstd::move(plugin_result.object_identities);
        compiled += plugin_result.compiled;
        reused += plugin_result.reused;
        for (auto target : package_plan.target_order) {
            if (plugin_targets[target] == u8 {}) continue;
            const auto kind = package.targets[target].artifact_kind;
            if (kind == cpp::ArtifactKind::SharedLibrary) {
                return build_failure<BuildSummary>(
                    "compiler plugin host dependency cannot be a shared library"_str);
            }
            if (kind != cpp::ArtifactKind::StaticLibrary &&
                kind != cpp::ArtifactKind::CompilerPlugin) {
                continue;
            }
            auto archived = build_archive_target(request,
                                                 layout,
                                                 toolchain,
                                                 archive_cache,
                                                 package.targets[target],
                                                 units,
                                                 target_units[target],
                                                 object_identities,
                                                 build_timing);
            if (archived.is_err()) return Err(rstd::move(archived).unwrap_err());
            library_paths[target] = Some(rstd::move(archived).unwrap());
        }
        auto built_plugins = build_compiler_plugins(project.configuration,
                                                    project.platform,
                                                    layout,
                                                    toolchain,
                                                    plugin_sdk->sdk,
                                                    package,
                                                    package_plan,
                                                    library_paths);
        if (built_plugins.is_err()) return Err(rstd::move(built_plugins).unwrap_err());
        compiler_plugins = rstd::move(built_plugins).unwrap();
        if (! provider_roots.is_empty()) {
            auto provider_prerequisites =
                compile_plan_prerequisite_closure(actions.compile, provider_roots);
            for (auto root : provider_roots) provider_prerequisites[root] = u8 {};
            for (auto unit = cpp::UnitId {}; unit < provider_prerequisites.len(); ++unit) {
                if (completed_selection[unit] != u8 {}) provider_prerequisites[unit] = u8 {};
            }
            auto provider_dependency_compile =
                execute_compile_plan_selection(package,
                                               units,
                                               actions.compile,
                                               provider_prerequisites,
                                               rstd::move(object_identities),
                                               cache,
                                               toolchain.compile_executor(),
                                               request.observer,
                                               *compile_execution);
            if (provider_dependency_compile.is_err()) {
                return Err(rstd::move(provider_dependency_compile).unwrap_err());
            }
            auto provider_dependency_result = rstd::move(provider_dependency_compile).unwrap();
            object_identities = rstd::move(provider_dependency_result.object_identities);
            compiled += provider_dependency_result.compiled;
            reused += provider_dependency_result.reused;
            for (auto unit = cpp::UnitId {}; unit < provider_prerequisites.len(); ++unit) {
                if (provider_prerequisites[unit] != u8 {}) completed_selection[unit] = u8(1);
            }
            auto provider_transformed = transform_proc_macro_provider_sources(
                layout, toolchain, package, package_plan, units, target_units, compiler_plugins);
            if (provider_transformed.is_err()) {
                return Err(rstd::move(provider_transformed).unwrap_err());
            }
            if (*provider_transformed) {
                auto expected_units = actions.compile.nodes.len();
                rstd_try(rebuild_after_source_transformation());
                if (actions.compile.nodes.len() != expected_units ||
                    object_identities.len() != actions.compile.nodes.len()) {
                    return build_failure<BuildSummary>(
                        "proc-macro provider transformation changed compile unit alignment"_str);
                }
                completed_selection.clear();
                for (const auto& identity : object_identities) {
                    completed_selection.push(identity.is_some() ? u8(1) : u8 {});
                }
            }
        }
        auto attached = attach_target_compiler_plugins(
            package, package_plan, target_units, compiler_plugins, toolchain, actions.compile);
        if (attached.is_err()) return Err(rstd::move(attached).unwrap_err());
        stage_timing.record(BuildStage::Plugin, plugin_started.elapsed());
    } else if (! compiler_plugins.is_empty()) {
        auto attached = attach_target_compiler_plugins(
            package, package_plan, target_units, compiler_plugins, toolchain, actions.compile);
        if (attached.is_err()) return Err(rstd::move(attached).unwrap_err());
    }

    auto remaining = compile_plan_remaining_selection(actions.compile, completed_selection);
    auto executed  = stage_timing.measure(BuildStage::CompileExecute, [&] {
        return execute_compile_plan_selection(package,
                                              units,
                                              actions.compile,
                                              remaining,
                                              rstd::move(object_identities),
                                              cache,
                                              toolchain.compile_executor(),
                                              request.observer,
                                              *compile_execution);
    });
    if (executed.is_err()) return Err(rstd::move(executed).unwrap_err());
    auto compile_result = rstd::move(executed).unwrap();
    object_identities   = rstd::move(compile_result.object_identities);
    compiled += compile_result.compiled;
    reused += compile_result.reused;
    compile_tests                                   = rstd::move(compile_result.compile_tests);
    compile_statistics                              = compile_result.statistics;
    compile_statistics.documentation_units          = documentation_units.len();
    compile_statistics.documentation_retained_bytes = documentation_retained;
    const auto& compile_timing = compile_result.timing.timing(BuildOperation::Compile);
    if (compile_timing.count != usize {}) {
        build_timing.record(BuildOperation::Compile, compile_timing.total);
    }

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
        if (package.targets[target].artifact_kind == cpp::ArtifactKind::StaticLibrary ||
            package.targets[target].artifact_kind == cpp::ArtifactKind::CompilerPlugin ||
            package.targets[target].artifact_kind == cpp::ArtifactKind::ProcMacroProvider ||
            package.targets[target].artifact_kind == cpp::ArtifactKind::TestAttachmentArchive) {
            records.push(layout.cache_archive(package.targets[target].id));
        }
        auto finished = cache.finish_target(layout, package.targets[target].id, records);
        if (finished.is_err()) {
            return Err(rstd::into<BuildError>(rstd::move(finished).unwrap_err()));
        }
    }
    stage_timing.record(BuildStage::CompileCacheFinish, cache_finish_started.elapsed());

    auto artifacts = Vec<BuiltArtifact>::make();
    for (auto target : package_plan.target_order) {
        const auto& target_spec = package.targets[target];
        if (library_paths[target].is_some()) continue;
        if (target_spec.artifact_kind != cpp::ArtifactKind::StaticLibrary &&
            target_spec.artifact_kind != cpp::ArtifactKind::CompilerPlugin &&
            target_spec.artifact_kind != cpp::ArtifactKind::ProcMacroProvider &&
            target_spec.artifact_kind != cpp::ArtifactKind::TestAttachmentArchive) {
            continue;
        }
        auto archive_started = rstd::time::Instant::now();
        auto archived        = build_archive_target(request,
                                                    layout,
                                                    toolchain,
                                                    archive_cache,
                                                    target_spec,
                                                    units,
                                                    target_units[target],
                                                    object_identities,
                                                    build_timing);
        if (archived.is_err()) return Err(rstd::move(archived).unwrap_err());
        auto archive_path     = rstd::move(archived).unwrap();
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
            target_spec.artifact_kind == cpp::ArtifactKind::CompilerPlugin ||
            target_spec.artifact_kind == cpp::ArtifactKind::ProcMacroProvider ||
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
        if (artifact_link_variants != nullptr)
            for (const auto& requested : *artifact_link_variants) {
                if (requested.target != target_spec.id) continue;
                if (install_variant != nullptr) {
                    return build_failure<BuildSummary>(rstd::format(
                        "target '{}' has more than one requested install link variant",
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
            .standard_library_runtime  = project.configuration.standard_library_runtime,
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
            project.configuration.standard_library_runtime));
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

    auto proc_macro_products = build_proc_macro_aggregates(project.configuration,
                                                           project.platform,
                                                           layout,
                                                           toolchain,
                                                           package,
                                                           package_plan,
                                                           library_paths,
                                                           compiler_plugins,
                                                           project.proc_macro_aggregates);
    if (proc_macro_products.is_err()) {
        return Err(rstd::move(proc_macro_products).unwrap_err());
    }
    auto built_proc_macro_products = rstd::move(proc_macro_products).unwrap();
    auto proc_macro_providers      = rstd::move(built_proc_macro_products.providers);
    auto proc_macro_aggregates     = rstd::move(built_proc_macro_products.aggregates);
    for (const auto& provider : proc_macro_providers) {
        auto recorded = false;
        for (auto target = cpp::TargetId {}; target < package.targets.len(); ++target) {
            if (package.targets[target].id == provider.target) {
                if (library_paths[target].is_none() ||
                    library_paths[target]->as_path() != provider.archive.as_path()) {
                    return build_failure<BuildSummary>(
                        "proc-macro provider product does not match its target archive"_str);
                }
                recorded = true;
                break;
            }
        }
        if (! recorded) {
            return build_failure<BuildSummary>(
                "proc-macro provider product has no package target"_str);
        }
    }
    if (plugin_host.is_some()) {
        proc_macro_providers =
            Vec<BuiltProcMacroProvider>::with_capacity(plugin_host->proc_macro_providers.len());
        for (const auto& provider : plugin_host->proc_macro_providers) {
            proc_macro_providers.push(provider.clone());
        }
        proc_macro_aggregates =
            Vec<BuiltProcMacroAggregate>::with_capacity(plugin_host->proc_macro_aggregates.len());
        for (const auto& aggregate : plugin_host->proc_macro_aggregates) {
            proc_macro_aggregates.push(aggregate.clone());
        }
    }
    auto product_proc_macro_providers =
        Vec<BuiltProcMacroProvider>::with_capacity(proc_macro_providers.len());
    for (const auto& provider : proc_macro_providers) {
        product_proc_macro_providers.push(provider.clone());
    }
    auto product_proc_macro_aggregates =
        Vec<BuiltProcMacroAggregate>::with_capacity(proc_macro_aggregates.len());
    for (const auto& aggregate : proc_macro_aggregates) {
        product_proc_macro_aggregates.push(aggregate.clone());
    }
    auto product_compiler_plugins = Vec<BuiltCompilerPlugin>::with_capacity(compiler_plugins.len());
    for (const auto& plugin : compiler_plugins) product_compiler_plugins.push(plugin.clone());

    auto product = CompletedBuildProduct {
        .profile = package_plan.profile->name.clone(),
        .target  = project.platform.effective_target.triple.clone(),
        .target_kind =
            String::make(project.configuration.target.is_Android() ? "android"_str : "default"_str),
        .android_abi           = project.platform.android_abi.is_some()
                                     ? project.platform.android_abi->clone()
                                     : String::make(),
        .android_minimum_api   = project.platform.android_minimum_api.is_some()
                                     ? *project.platform.android_minimum_api
                                     : u32 {},
        .base_directory        = PathBuf::from(layout.base_directory()),
        .build_directory       = PathBuf::from(layout.output()),
        .artifacts             = rstd::move(artifacts),
        .compiler_plugins      = rstd::move(product_compiler_plugins),
        .proc_macro_providers  = rstd::move(product_proc_macro_providers),
        .proc_macro_aggregates = rstd::move(product_proc_macro_aggregates),
        .target_runtimes       = rstd::move(project.target_runtimes),
        .external_assets       = rstd::move(project.external_assets),
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
        .language_standard          = project.configuration.language_standard.clone(),
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
        .compiler_plugins           = rstd::move(compiler_plugins),
        .proc_macro_providers       = rstd::move(proc_macro_providers),
        .proc_macro_aggregates      = rstd::move(proc_macro_aggregates),
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
