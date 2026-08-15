module;
#include <rstd/macro.hpp>

export module lito.build;

import rstd;
import lito.error;
import lito.manifest.contract;
import lito.workspace.contract;
import lito.workspace;
import lito.build.profile_contract;
import lito.build.identity;
import lito.build.plan_contract;
export import lito.build.contract;
import lito.package.target_contract;
import lito.toolchain.contract;
import lito.build.discovery;
import lito.project;
import lito.build.error_contract;
import lito.package;
import lito.toolchain;
import lito.modules;
import lito.cache;
import lito.build.layout;
import lito.build.script;
import lito.build.resource;
import lito.frontend;
import lito.build.frontend_analysis;
import lito.build.frontend_observer;
import lito.system.environment;
import lito.build.compile_test;
import lito.build.compile_executor;
import lito.build.unit_plan;
import lito.build.profiling;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

template<typename T>
auto failure(String message) -> BuildResult<T> {
    return Err(BuildError::Message(rstd::move(message)));
}

template<typename T>
auto failure(ref<str> message) -> BuildResult<T> {
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
    const Option<BuildObserver>*     downstream {};
};

auto external_preparation_operation(BuildEventKind kind) -> Option<ExternalPreparationOperation> {
    switch (kind) {
    case BuildEventKind::CMakeConfigure: return Some(ExternalPreparationOperation::CMakeConfigure);
    case BuildEventKind::CMakeBuild: return Some(ExternalPreparationOperation::CMakeBuild);
    case BuildEventKind::CMakeInstall: return Some(ExternalPreparationOperation::CMakeInstall);
    case BuildEventKind::CMakeQuery: return Some(ExternalPreparationOperation::CMakeQuery);
    case BuildEventKind::CMakeQueryBuild:
        return Some(ExternalPreparationOperation::CMakeQueryBuild);
    case BuildEventKind::CMakeSnapshot: return Some(ExternalPreparationOperation::CMakeSnapshot);
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
        return failure<ResolvedScanExecution>("scan jobs must be greater than zero"_str);
    }
    auto max_in_flight = policy.max_in_flight.is_some() ? *policy.max_in_flight : jobs;
    if (max_in_flight == usize {}) {
        return failure<ResolvedScanExecution>("scan task capacity must be greater than zero"_str);
    }
    return Ok(ResolvedScanExecution {
        .jobs          = jobs,
        .max_in_flight = max_in_flight,
    });
}

} // namespace lito

namespace lito
{

auto build_with_environment_impl(const BuildRequest&               request,
                                 const ResolvedProcessEnvironment& process_environment,
                                 Option<WorkspaceCatalog>          catalog,
                                 Option<PreparedBuildProject>      prepared = None())
    -> BuildResult<BuildSummary> {
    if (request.selection.root.is_empty()) {
        return failure<BuildSummary>("build directory is required"_str);
    }
    auto tool_resolver = ToolResolver(process_environment);
    auto profile =
        request.profile.is_some()
            ? request.profile->clone()
            : BuildProfileName {
                  .value = String::make(request.purpose == PackageSelectionPurpose::Benchmark
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
    auto preparation_observer = Some(BuildObserver {
        .context = rstd::addressof(preparation_context),
        .notify  = observe_preparation,
    });
    auto supplied_prepared    = prepared.is_some();
    auto resolved_project     = [&]() -> BuildResult<PreparedBuildProject> {
        if (prepared.is_some()) return Ok(rstd::move(prepared).unwrap());
        auto project = prepare_build_project(request.selection,
                                             request.configuration,
                                             profile,
                                             request.output.as_path(),
                                             request.sources,
                                             request.lock,
                                             request.pkg_config,
                                             request.cmake,
                                             tool_resolver,
                                             process_environment,
                                             request.locked,
                                             request.purpose,
                                             execution->jobs,
                                             preparation_observer,
                                             rstd::move(catalog));
        if (project.is_err()) {
            return Err(rstd::into<BuildError>(rstd::move(project).unwrap_err()));
        }
        return Ok(rstd::move(project).unwrap());
    }();
    if (resolved_project.is_err()) return Err(rstd::move(resolved_project).unwrap_err());
    auto  project   = rstd::move(resolved_project).unwrap();
    auto& toolchain = project.toolchain;
    auto& metadata  = project.metadata;
    auto& layout    = project.layout;
    if (supplied_prepared) {
        if (profile.as_str() != metadata.default_profile.as_str()) {
            return failure<BuildSummary>(
                rstd::format("prepared project profile '{}' cannot satisfy requested profile '{}'",
                             metadata.default_profile.as_str(),
                             profile.as_str()));
        }
        auto requested_layout = BuildLayout::resolve(
            metadata.root.as_path(), request.output.as_path(), metadata.default_profile.as_str());
        auto canonical_output = rstd::fs::canonicalize(requested_layout.output());
        if (canonical_output.is_err()) {
            return Err(BuildError::System(
                SystemError::Io(String::make("resolve requested prepared build output"_str),
                                PathBuf::from(requested_layout.output()),
                                rstd::move(canonical_output).unwrap_err())));
        }
        if (canonical_output->as_path() != layout.output()) {
            return failure<BuildSummary>(
                rstd::format("prepared project output '{}' cannot satisfy requested output '{}'",
                             layout.output(),
                             canonical_output->as_path()));
        }
    }
    auto created_profiler = ScanProfiler::create();
    if (created_profiler.is_err()) {
        return failure<BuildSummary>(rstd::move(created_profiler).unwrap_err_unchecked());
    }
    auto profiler          = rstd::move(created_profiler).unwrap_unchecked();
    auto frontend_observer = FrontendProfileObserver::make(profiler);
    auto frontend_service  = frontend::FrontendService::make(Some(frontend_observer.observer()));

    auto selected = resolve_source_selection(
        metadata, metadata.default_profile.as_str(), request.targets, request.exact_targets);
    if (selected.is_err()) {
        return Err(rstd::into<BuildError>(rstd::move(selected).unwrap_err()));
    }
    auto selected_targets = Vec<PackageTargetId>::with_capacity(selected->selected_targets.len());
    for (auto target : selected->selected_targets) {
        selected_targets.push(metadata.targets[target].id.clone());
    }
    auto selected_packages = Vec<SelectedPackageMetadata>::make();
    for (const auto& package : metadata.selected_packages) {
        auto used = false;
        for (const auto& target : selected_targets) {
            if (target.package == package.name.as_str()) {
                used = true;
                break;
            }
        }
        if (! used) continue;
        auto version = Option<String> {};
        if (package.version.is_some()) version = Some(package.version->clone());
        selected_packages.push(SelectedPackageMetadata {
            .name            = package.name.clone(),
            .version         = rstd::move(version),
            .source_identity = package.source_identity.clone(),
            .root            = package.root.clone(),
        });
    }
    auto script_packages = resolve_build_script_packages(metadata, *selected);
    if (script_packages.is_err()) {
        return Err(rstd::into<BuildError>(rstd::move(script_packages).unwrap_err()));
    }

    auto script_report = rstd_try(execute_build_script(metadata,
                                                       layout,
                                                       metadata.default_profile.as_str(),
                                                       *script_packages,
                                                       *selected,
                                                       request.observer,
                                                       project.platform.host,
                                                       request.cmake,
                                                       tool_resolver,
                                                       process_environment,
                                                       request.sources,
                                                       execution->jobs));
    auto runtime_resources = rstd_try(resolve_runtime_resources(metadata,
                                                                layout,
                                                                selected_targets,
                                                                request.observer));

    auto scan_span = profiler.span(ScanProbe::Total);

    auto resolved = profiler.measure(ScanProbe::Plan, [&] {
        return resolve_source_discovery(metadata, rstd::move(selected).unwrap());
    });
    if (resolved.is_err()) {
        return Err(rstd::into<BuildError>(rstd::move(resolved).unwrap_err()));
    }
    auto discovery_plan = rstd::move(resolved).unwrap();
    auto stripper       = Option<PathBuf> {};
    if (metadata.profiles[discovery_plan.profile].strip != StripMode::None) {
        auto resolved_stripper = tool_resolver.resolve(
            request.configuration.toolchain.strip.as_path(), "LLVM strip executable"_str);
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
                                 metadata.profiles[discovery_plan.profile].name.as_str(),
                                 toolchain.compiler_identity());
    if (created_environment.is_err()) {
        return Err(rstd::into<BuildError>(rstd::move(created_environment).unwrap_err()));
    }
    auto cache_environment = rstd::move(created_environment).unwrap();
    auto scan_cache        = ScanCacheSession::create(cache_environment);
    auto analysis_service =
        FrontendAnalysisService::make(layout, toolchain, frontend_service, scan_cache, profiler);

    auto discovered = profiler.measure(ScanProbe::Discovery, [&] {
        return discover_package_sources(metadata,
                                        discovery_plan,
                                        analysis_service,
                                        request.observer,
                                        execution->jobs,
                                        execution->max_in_flight);
    });
    if (discovered.is_err()) return Err(rstd::move(discovered).unwrap_err());
    auto source_sets = rstd::move(discovered).unwrap();
    auto finalized   = finalize_package(rstd::move(metadata), rstd::move(source_sets));
    if (finalized.is_err()) {
        return Err(rstd::into<BuildError>(rstd::move(finalized).unwrap_err()));
    }
    auto package          = rstd::move(finalized).unwrap();
    auto resolved_package = finalize_package_plan(package, rstd::move(discovery_plan));
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
    auto  preparation_finished = profiler.complete(prepare_span);
    if (preparation_finished.is_err()) {
        return failure<BuildSummary>(rstd::move(preparation_finished).unwrap_err_unchecked());
    }

    auto scans         = Vec<ScanResult>::with_capacity(units.len());
    auto classify_span = profiler.span(ScanProbe::ClassifyUnits);
    for (auto unit = UnitId {}; unit < units.len(); ++unit) {
        if (units[unit].frontend_analysis.is_none()) {
            return failure<BuildSummary>(
                rstd::format("source '{}' reached classification without frontend analysis",
                             units[unit].unit.source.as_path()));
        }
        analysis_service.record_in_build_reuse();
        auto scanned = toolchain.scan(units[unit]);
        if (scanned.is_err()) {
            return Err(rstd::into<BuildError>(rstd::move(scanned).unwrap_err()));
        }
        auto result = rstd::move(scanned).unwrap();
        scans.push(rstd::move(result));
    }
    auto classified_units = profiler.complete(classify_span);
    if (classified_units.is_err()) {
        return failure<BuildSummary>(rstd::move(classified_units).unwrap_err_unchecked());
    }

    auto convention_valid = profiler.measure(ScanProbe::Conventions, [&] {
        return validate_module_conventions(package, units, scans);
    });
    if (convention_valid.is_err()) {
        return Err(rstd::into<BuildError>(rstd::move(convention_valid).unwrap_err()));
    }

    auto resolved_modules = profiler.measure(ScanProbe::ModuleGraph, [&] {
        return resolve_modules(package_plan, units, scans, toolchain.bmi_format());
    });
    if (resolved_modules.is_err()) {
        return Err(rstd::into<BuildError>(rstd::move(resolved_modules).unwrap_err()));
    }
    auto module_plan    = rstd::move(resolved_modules).unwrap();
    auto completed_scan = profiler.complete(scan_span);
    if (completed_scan.is_err()) {
        return failure<BuildSummary>(rstd::move(completed_scan).unwrap_err_unchecked());
    }
    auto finished_profile = profiler.finish();
    if (finished_profile.is_err()) {
        return failure<BuildSummary>(rstd::move(finished_profile).unwrap_err_unchecked());
    }
    auto scan_profile                          = rstd::move(finished_profile).unwrap_unchecked();
    auto frontend_statistics                   = analysis_service.statistics();
    auto scan_cache_statistics                 = scan_cache.statistics();
    frontend_statistics.persistent_scan_hits   = scan_cache_statistics.hits;
    frontend_statistics.persistent_scan_misses = scan_cache_statistics.misses;
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
    frontend_statistics.persistent_scan_receipt         = scan_cache_statistics.receipt;
    frontend_statistics.persistent_fingerprint_requests =
        scan_cache_statistics.fingerprint_requests;
    frontend_statistics.persistent_fingerprint_hits   = scan_cache_statistics.fingerprint_hits;
    frontend_statistics.persistent_fingerprint_builds = scan_cache_statistics.fingerprint_builds;
    frontend_statistics.persistent_fingerprint_waits  = scan_cache_statistics.fingerprint_waits;
    frontend_statistics.persistent_fingerprint_wait   = scan_cache_statistics.fingerprint_wait;
    analysis_service.release_source_cache();

    auto cache = CompileCacheSession::create(cache_environment, layout.output());
    auto materialized =
        materialize_compile_plan(package, layout, toolchain, units, scans, module_plan);
    if (materialized.is_err()) {
        return Err(rstd::move(materialized).unwrap_err());
    }
    auto documentation_units =
        materialize_documentation_units(package, units, scans, *materialized, selected_targets);
    if (documentation_units.is_err()) {
        return Err(rstd::move(documentation_units).unwrap_err());
    }
    auto executed = execute_compile_plan(package,
                                         units,
                                         rstd::move(materialized).unwrap(),
                                         cache,
                                         toolchain.compile_executor(),
                                         request.observer,
                                         *compile_execution);
    if (executed.is_err()) {
        return Err(rstd::move(executed).unwrap_err());
    }
    auto compile_result     = rstd::move(executed).unwrap();
    auto compiled           = compile_result.compiled;
    auto reused             = compile_result.reused;
    auto compile_tests      = rstd::move(compile_result.compile_tests);
    auto compile_statistics = compile_result.statistics;
    auto build_timing       = rstd::move(compile_result.timing);

    for (auto target : package_plan.target_order) {
        auto records = Vec<PathBuf>::with_capacity(target_units[target].len());
        for (auto unit : target_units[target]) {
            if (units[unit].unit.compile_test == nullptr ||
                units[unit].unit.compile_test->outcome == CompileTestOutcome::Success) {
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

    auto artifacts     = Vec<BuiltArtifact>::make();
    auto archive_paths = Vec<Option<PathBuf>>::with_capacity(package.targets.len());
    for (auto target = TargetId {}; target < package.targets.len(); ++target) {
        archive_paths.emplace_back(None());
    }
    for (auto target : package_plan.target_order) {
        const auto& target_spec = package.targets[target];
        if (target_spec.artifact_kind != ArtifactKind::StaticLibrary &&
            target_spec.artifact_kind != ArtifactKind::TestAttachmentArchive) {
            continue;
        }
        auto archive_path =
            target_spec.test_attachment.is_some()
                ? layout.test_attachment_archive(*target_spec.test_attachment,
                                                 target_spec.archive_stem.as_str())
                : layout.archive(target_spec.id, target_spec.artifact_name.as_str());
        auto objects = Vec<PathBuf>::with_capacity(target_units[target].len());
        for (auto unit : target_units[target]) objects.push(units[unit].unit.object.clone());
        auto target_identity = package_target_id_text(target_spec.id);
        emit(request, BuildEventKind::Archive, target_identity.as_str(), archive_path.as_path());
        auto archived =
            toolchain.archive(archive_path.as_path(), objects, target_spec.root.as_path());
        if (archived.is_err()) {
            return Err(rstd::into<BuildError>(rstd::move(archived).unwrap_err()));
        }
        build_timing.record(BuildOperation::Archive, *archived);
        archive_paths[target] = Some(archive_path.clone());
        artifacts.push(BuiltArtifact {
            .target       = target_spec.id.clone(),
            .kind         = target_spec.artifact_kind,
            .path         = rstd::move(archive_path),
            .package_root = target_spec.root.clone(),
        });
    }

    for (auto target : package_plan.target_order) {
        const auto& target_spec = package.targets[target];
        if (target_spec.artifact_kind == ArtifactKind::StaticLibrary ||
            target_spec.artifact_kind == ArtifactKind::TestAttachmentArchive ||
            target_spec.artifact_kind == ArtifactKind::CompileTest) {
            continue;
        }
        auto objects = Vec<PathBuf>::with_capacity(target_units[target].len());
        for (auto unit : target_units[target]) objects.push(units[unit].unit.object.clone());
        auto link_inputs = Vec<ResolvedLinkInput>::make();
        if (target_spec.artifact_kind == ArtifactKind::TestExecutable) {
            for (auto candidate : package_plan.target_order) {
                const auto& candidate_spec = package.targets[candidate];
                if (candidate_spec.test_attachment.is_none() ||
                    ! (candidate_spec.test_attachment->test_target == target_spec.id)) {
                    continue;
                }
                if (archive_paths[candidate].is_none()) {
                    return failure<BuildSummary>(rstd::format(
                        "test target '{}' has no attachment archive for '{}'",
                        package_target_id_text(target_spec.id).as_str(),
                        package_target_id_text(candidate_spec.test_attachment->library_target)
                            .as_str()));
                }
                link_inputs.push(ResolvedLinkInput::Archive(LinkArchive {
                    .path = (*archive_paths[candidate]).clone(),
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
            if (dependency_spec.artifact_kind != ArtifactKind::StaticLibrary ||
                archive_paths[dependency].is_none()) {
                return failure<BuildSummary>(
                    rstd::format("executable target '{}' depends on unavailable "
                                 "library target '{}'",
                                 package_target_id_text(target_spec.id).as_str(),
                                 package_target_id_text(dependency_spec.id).as_str()));
            }
            link_inputs.push(ResolvedLinkInput::Archive(LinkArchive {
                .path = (*archive_paths[dependency]).clone(),
                .mode = LinkArchiveMode::Normal,
            }));
        }
        auto executable_path =
            layout.executable(target_spec.id, target_spec.artifact_name.as_str());
        if (target_spec.artifact_kind == ArtifactKind::TestExecutable) {
            executable_path = layout.test(target_spec.id, target_spec.artifact_name.as_str());
        } else if (target_spec.artifact_kind == ArtifactKind::BenchmarkExecutable) {
            executable_path = layout.benchmark(target_spec.id, target_spec.artifact_name.as_str());
        }
        auto target_identity = package_target_id_text(target_spec.id);
        emit(request, BuildEventKind::Link, target_identity.as_str(), executable_path.as_path());
        auto linked = toolchain.link_executable(executable_path.as_path(),
                                                objects,
                                                link_inputs,
                                                package_plan.profile->cpp.abi.standard_library,
                                                target_spec.link_stdlib,
                                                package_plan.profile->cpp.codegen.lto,
                                                package_plan.linker_options[target],
                                                target_spec.root.as_path());
        if (linked.is_err()) {
            return Err(rstd::into<BuildError>(rstd::move(linked).unwrap_err()));
        }
        build_timing.record(BuildOperation::Link, *linked);
        if (package_plan.profile->strip != StripMode::None) {
            if (stripper.is_none()) {
                return failure<BuildSummary>("strip tool was not resolved"_str);
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
        artifacts.push(BuiltArtifact {
            .target       = target_spec.id.clone(),
            .kind         = target_spec.artifact_kind,
            .path         = rstd::move(executable_path),
            .package_root = target_spec.root.clone(),
        });
    }

    return Ok(BuildSummary {
        .package              = package.name.clone(),
        .profile              = package_plan.profile->name.clone(),
        .target               = String::make(toolchain.target()),
        .language_standard    = request.configuration.language_standard.clone(),
        .output               = PathBuf::from(layout.output()),
        .scanned              = scans.len(),
        .compiled             = compiled,
        .reused               = reused,
        .artifacts            = rstd::move(artifacts),
        .runtime_resources    = rstd::move(runtime_resources),
        .selected_targets     = rstd::move(selected_targets),
        .selected_packages    = rstd::move(selected_packages),
        .frontend             = frontend_statistics,
        .toolchain            = toolchain.statistics(),
        .scan_profile         = rstd::move(scan_profile),
        .compile_execution    = compile_statistics,
        .external_preparation = rstd::move(preparation_timing),
        .build_timing         = rstd::move(build_timing),
        .compile_tests        = rstd::move(compile_tests),
        .script               = rstd::move(script_report),
        .external_assets      = rstd::move(project.external_assets),
        .compiler             = toolchain.compiler_identity().clone(),
        .documentation_units  = rstd::move(documentation_units).unwrap(),
    });
}

} // namespace lito

export namespace lito
{

auto build_with_environment(const BuildRequest&               request,
                            const ResolvedProcessEnvironment& process_environment)
    -> BuildResult<BuildSummary> {
    return build_with_environment_impl(request, process_environment, None(), None());
}

auto build_resolved_project(BuildRequest request, ResolvedProjectEntry project)
    -> BuildResult<BuildSummary> {
    request.selection.root = rstd::move(project.root);
    auto environment       = ResolvedProcessEnvironment::resolve(request.environment);
    if (environment.is_err()) {
        return Err(rstd::into<BuildError>(rstd::move(environment).unwrap_err()));
    }
    return build_with_environment_impl(
        request, *environment, Some(rstd::move(project.catalog)), None());
}

auto build_prepared_project(const BuildRequest&               request,
                            const ResolvedProcessEnvironment& environment,
                            PreparedBuildProject project) -> BuildResult<BuildSummary> {
    return build_with_environment_impl(request, environment, None(), Some(rstd::move(project)));
}

auto build(const BuildRequest& request) -> BuildResult<BuildSummary> {
    auto environment = ResolvedProcessEnvironment::resolve(request.environment);
    if (environment.is_err()) {
        return Err(rstd::into<BuildError>(rstd::move(environment).unwrap_err()));
    }
    return build_with_environment_impl(request, *environment, None(), None());
}

} // namespace lito
