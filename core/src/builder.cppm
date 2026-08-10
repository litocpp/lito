export module lito.builder;

import rstd;
import lito.model;
import lito.source_discovery;
import lito.project;
import lito.package;
import lito.toolchain;
import lito.modules;
import lito.cache;
import lito.build_layout;
import lito.frontend;
import lito.frontend_analysis;
import lito.frontend_observer;
import lito.environment;
import lito.compile_test;
import lito.compile_executor;
import lito.profiling;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

template<typename T>
auto failure(ErrorKind kind, String message) -> Result<T> {
    return Err(Error::make(kind, rstd::move(message)));
}

template<typename T>
auto failure(ErrorKind kind, ref<str> message) -> Result<T> {
    return Err(Error::make(kind, message));
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

auto resolve_scan_execution(const ScanExecutionPolicy& policy) -> Result<ResolvedScanExecution> {
    auto jobs = usize(1);
    if (policy.jobs.is_some()) {
        jobs = *policy.jobs;
    } else {
        auto available = rstd::thread::available_parallelism();
        if (available.is_ok()) jobs = available->get();
    }
    if (jobs == usize {}) {
        return failure<ResolvedScanExecution>(ErrorKind::InvalidRequest,
                                              "scan jobs must be greater than zero"_str);
    }
    auto max_in_flight = policy.max_in_flight.is_some() ? *policy.max_in_flight : jobs;
    if (max_in_flight == usize {}) {
        return failure<ResolvedScanExecution>(ErrorKind::InvalidRequest,
                                              "scan task capacity must be greater than zero"_str);
    }
    return Ok(ResolvedScanExecution {
        .jobs          = jobs,
        .max_in_flight = max_in_flight,
    });
}

} // namespace lito

export namespace lito
{

auto build_with_environment(const BuildRequest&               request,
                            const ResolvedProcessEnvironment& process_environment)
    -> Result<BuildSummary> {
    if (request.selection.root.is_empty()) {
        return failure<BuildSummary>(ErrorKind::InvalidRequest, "build directory is required"_str);
    }
    auto tool_resolver = ToolResolver(process_environment);
    auto created_toolchain =
        ClangToolchain::create(request.configuration.toolchain, tool_resolver, process_environment);
    if (created_toolchain.is_err()) {
        return Err(rstd::move(created_toolchain).unwrap_err());
    }
    auto toolchain = rstd::move(created_toolchain).unwrap();
    auto loaded    = resolve_project_metadata(request.selection,
                                              request.configuration,
                                              request.sources,
                                              request.pkg_config,
                                              request.cmake,
                                              toolchain,
                                              tool_resolver,
                                              process_environment,
                                              request.locked,
                                              request.purpose);
    if (loaded.is_err()) return Err(rstd::move(loaded).unwrap_err());
    auto metadata         = rstd::move(loaded).unwrap();
    auto created_profiler = ScanProfiler::create();
    if (created_profiler.is_err()) {
        return failure<BuildSummary>(ErrorKind::Artifact,
                                     rstd::move(created_profiler).unwrap_err_unchecked());
    }
    auto profiler          = rstd::move(created_profiler).unwrap_unchecked();
    auto frontend_observer = FrontendProfileObserver::make(profiler);
    auto frontend_service  = frontend::FrontendService::make(Some(frontend_observer.observer()));
    auto scan_span         = profiler.span(ScanProbe::Total);

    auto resolved = profiler.measure(ScanProbe::Plan, [&] {
        return resolve_source_discovery(
            metadata, metadata.default_profile.as_str(), request.targets);
    });
    if (resolved.is_err()) return Err(rstd::move(resolved).unwrap_err());
    auto discovery_plan = rstd::move(resolved).unwrap();
    auto stripper       = Option<PathBuf> {};
    if (metadata.profiles[discovery_plan.profile].strip != StripMode::None) {
        auto resolved_stripper = tool_resolver.resolve(
            request.configuration.toolchain.stripper.as_path(), "LLVM strip executable"_str);
        if (resolved_stripper.is_err()) {
            return Err(rstd::move(resolved_stripper).unwrap_err());
        }
        stripper = Some(rstd::move(resolved_stripper).unwrap().executable);
    }
    auto execution = resolve_scan_execution(request.execution.scan);
    if (execution.is_err()) return Err(rstd::move(execution).unwrap_err());
    auto compile_execution = resolve_compile_execution(request.execution.compile);
    if (compile_execution.is_err()) return Err(rstd::move(compile_execution).unwrap_err());

    auto selected_layout =
        BuildLayout::create(metadata.root.as_path(),
                            request.output.as_path(),
                            metadata.profiles[discovery_plan.profile].name.as_str());
    if (selected_layout.is_err()) return Err(rstd::move(selected_layout).unwrap_err());
    auto layout = rstd::move(selected_layout).unwrap();

    auto created_environment =
        CacheEnvironment::create(layout,
                                 metadata.root.as_path(),
                                 metadata.profiles[discovery_plan.profile].name.as_str(),
                                 toolchain.compiler_identity());
    if (created_environment.is_err()) {
        return Err(rstd::move(created_environment).unwrap_err());
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
    if (finalized.is_err()) return Err(rstd::move(finalized).unwrap_err());
    auto package          = rstd::move(finalized).unwrap();
    auto resolved_package = finalize_package_plan(package, rstd::move(discovery_plan));
    if (resolved_package.is_err()) return Err(rstd::move(resolved_package).unwrap_err());
    auto package_plan = rstd::move(resolved_package).unwrap();

    auto target_units = Vec<Vec<UnitId>>::with_capacity(package.targets.len());
    for (auto target = TargetId {}; target < package.targets.len(); ++target) {
        target_units.emplace_back();
    }
    auto compile_contexts = Vec<Box<CompileContext>>::make();
    auto units            = Vec<PreparedUnit>::make();
    auto prepare_span     = profiler.span(ScanProbe::PrepareUnits);
    for (auto target : package_plan.target_order) {
        const auto& target_spec = package.targets[target];
        for (const auto& source : target_spec.sources) {
            const auto* compile_test = static_cast<const CompileTestCase*>(nullptr);
            const auto* context      = rstd::addressof(package_plan.contexts[target]);
            if (target_spec.artifact_kind == ArtifactKind::CompileTest) {
                auto selected =
                    compile_test_for_source(target_spec, source.relative_path.as_path());
                if (selected.is_none()) {
                    return failure<BuildSummary>(
                        ErrorKind::Manifest,
                        rstd::format("compile-test package '{}' has no case for source '{}'",
                                     target_spec.name.as_str(),
                                     source.relative_path.as_path()));
                }
                compile_test = *selected;
                auto selected_context =
                    compile_test_context(package_plan.contexts[target], *compile_test);
                if (selected_context.is_err()) {
                    return Err(rstd::move(selected_context).unwrap_err());
                }
                compile_contexts.push(
                    Box<CompileContext>::make(rstd::move(selected_context).unwrap()));
                context = compile_contexts[compile_contexts.len() - usize(1)].get();
            }
            auto object =
                target_spec.test_attachment.is_some()
                    ? layout.test_attachment_object(
                          target_spec.test_attachment->test_target.as_str(),
                          target_spec.test_attachment->library_target.as_str(),
                          source.relative_path.as_path())
                    : layout.object(target_spec.name.as_str(), source.relative_path.as_path());
            auto cache_record =
                target_spec.test_attachment.is_some()
                    ? layout.test_attachment_cache_unit(
                          target_spec.test_attachment->test_target.as_str(),
                          target_spec.test_attachment->library_target.as_str(),
                          source.relative_path.as_path())
                    : layout.cache_unit(target_spec.name.as_str(), source.relative_path.as_path());
            if (object.is_err()) return Err(rstd::move(object).unwrap_err());
            if (cache_record.is_err()) return Err(rstd::move(cache_record).unwrap_err());
            auto compile_test_record = Option<PathBuf> {};
            if (compile_test != nullptr) {
                auto record = layout.cache_compile_test(target_spec.name.as_str(),
                                                        source.relative_path.as_path());
                if (record.is_err()) return Err(rstd::move(record).unwrap_err());
                compile_test_record = Some(rstd::move(record).unwrap());
            }

            auto id       = units.len();
            auto prepared = toolchain.prepare(
                UnitSpec {
                    .id                  = id,
                    .target              = target,
                    .relative_source     = source.relative_path.clone(),
                    .source              = source.path.clone(),
                    .object              = rstd::move(object).unwrap(),
                    .cache_record        = rstd::move(cache_record).unwrap(),
                    .compile_test_record = rstd::move(compile_test_record),
                    .context             = context,
                    .compile_test        = compile_test,
                },
                target_spec.root.as_path());
            if (prepared.is_err()) return Err(rstd::move(prepared).unwrap_err());
            auto unit = rstd::move(prepared).unwrap();
            if (source.frontend_analysis.is_some() &&
                source.frontend_analysis->context_identity.as_str() == context->scan_id.as_str()) {
                unit.frontend_analysis =
                    Some(as<rstd::clone::Clone>(*source.frontend_analysis).clone());
            }
            units.push(rstd::move(unit));
            target_units[target].emplace_back(id);
        }
    }
    auto prepared_units = profiler.complete(prepare_span);
    if (prepared_units.is_err()) {
        return failure<BuildSummary>(ErrorKind::Artifact,
                                     rstd::move(prepared_units).unwrap_err_unchecked());
    }

    auto scans         = Vec<ScanResult>::with_capacity(units.len());
    auto classify_span = profiler.span(ScanProbe::ClassifyUnits);
    for (auto unit = UnitId {}; unit < units.len(); ++unit) {
        if (units[unit].frontend_analysis.is_none()) {
            return failure<BuildSummary>(
                ErrorKind::Artifact,
                rstd::format("source '{}' reached classification without frontend analysis",
                             units[unit].unit.source.as_path()));
        }
        analysis_service.record_in_build_reuse();
        auto scanned = toolchain.scan(units[unit]);
        if (scanned.is_err()) return Err(rstd::move(scanned).unwrap_err());
        auto result = rstd::move(scanned).unwrap();
        scans.push(rstd::move(result));
    }
    auto classified_units = profiler.complete(classify_span);
    if (classified_units.is_err()) {
        return failure<BuildSummary>(ErrorKind::Artifact,
                                     rstd::move(classified_units).unwrap_err_unchecked());
    }

    auto convention_valid = profiler.measure(ScanProbe::Conventions, [&] {
        return validate_module_conventions(package, units, scans);
    });
    if (convention_valid.is_err()) {
        return Err(rstd::move(convention_valid).unwrap_err());
    }

    auto resolved_modules = profiler.measure(ScanProbe::ModuleGraph, [&] {
        return resolve_modules(package_plan, units, scans, toolchain.bmi_format());
    });
    if (resolved_modules.is_err()) {
        return Err(rstd::move(resolved_modules).unwrap_err());
    }
    auto module_plan    = rstd::move(resolved_modules).unwrap();
    auto completed_scan = profiler.complete(scan_span);
    if (completed_scan.is_err()) {
        return failure<BuildSummary>(ErrorKind::Artifact,
                                     rstd::move(completed_scan).unwrap_err_unchecked());
    }
    auto finished_profile = profiler.finish();
    if (finished_profile.is_err()) {
        return failure<BuildSummary>(ErrorKind::Artifact,
                                     rstd::move(finished_profile).unwrap_err_unchecked());
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
    if (materialized.is_err()) return Err(rstd::move(materialized).unwrap_err());
    auto executed = execute_compile_plan(package,
                                         units,
                                         rstd::move(materialized).unwrap(),
                                         cache,
                                         toolchain.compile_executor(),
                                         request.observer,
                                         *compile_execution);
    if (executed.is_err()) return Err(rstd::move(executed).unwrap_err());
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
        const auto& attachment = package.targets[target].test_attachment;
        if (attachment.is_some()) {
            auto directory = layout.test_attachment_cache_directory(
                attachment->test_target.as_str(), attachment->library_target.as_str());
            auto finished = cache.finish_directory(directory.as_path(), records);
            if (finished.is_err()) return Err(rstd::move(finished).unwrap_err());
        } else {
            auto finished =
                cache.finish_target(layout, package.targets[target].name.as_str(), records);
            if (finished.is_err()) return Err(rstd::move(finished).unwrap_err());
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
                ? layout.test_attachment_archive(
                      target_spec.test_attachment->test_target.as_str(),
                      target_spec.test_attachment->library_target.as_str(),
                      target_spec.archive_stem.as_str())
                : layout.archive(target_spec.name.as_str(), target_spec.artifact_name.as_str());
        auto objects = Vec<PathBuf>::with_capacity(target_units[target].len());
        for (auto unit : target_units[target]) objects.push(units[unit].unit.object.clone());
        emit(request, BuildEventKind::Archive, target_spec.name.as_str(), archive_path.as_path());
        auto archived =
            toolchain.archive(archive_path.as_path(), objects, target_spec.root.as_path());
        if (archived.is_err()) return Err(rstd::move(archived).unwrap_err());
        build_timing.record(BuildOperation::Archive, *archived);
        archive_paths[target] = Some(archive_path.clone());
        artifacts.push(BuiltArtifact {
            .package      = target_spec.test_attachment.is_some()
                                ? target_spec.test_attachment->test_target.clone()
                                : target_spec.name.clone(),
            .target       = target_spec.test_attachment.is_some()
                                ? target_spec.test_attachment->library_target.clone()
                                : target_spec.name.clone(),
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
                    candidate_spec.test_attachment->test_target != target_spec.name.as_str()) {
                    continue;
                }
                if (archive_paths[candidate].is_none()) {
                    return failure<BuildSummary>(
                        ErrorKind::Artifact,
                        rstd::format("test target '{}' has no attachment archive for '{}'",
                                     target_spec.name.as_str(),
                                     candidate_spec.test_attachment->library_target.as_str()));
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
                    ErrorKind::Artifact,
                    rstd::format("executable target '{}' depends on unavailable "
                                 "library target '{}'",
                                 target_spec.name.as_str(),
                                 dependency_spec.name.as_str()));
            }
            link_inputs.push(ResolvedLinkInput::Archive(LinkArchive {
                .path = (*archive_paths[dependency]).clone(),
                .mode = LinkArchiveMode::Normal,
            }));
        }
        auto executable_path =
            target_spec.artifact_kind == ArtifactKind::TestExecutable
                ? layout.test(target_spec.name.as_str(), target_spec.artifact_name.as_str())
                : layout.executable(target_spec.name.as_str(), target_spec.artifact_name.as_str());
        emit(request, BuildEventKind::Link, target_spec.name.as_str(), executable_path.as_path());
        auto linked = toolchain.link_executable(executable_path.as_path(),
                                                objects,
                                                link_inputs,
                                                package_plan.profile->cpp.abi.standard_library,
                                                package_plan.profile->cpp.codegen.lto,
                                                package_plan.linker_options[target],
                                                target_spec.root.as_path());
        if (linked.is_err()) return Err(rstd::move(linked).unwrap_err());
        build_timing.record(BuildOperation::Link, *linked);
        if (package_plan.profile->strip != StripMode::None) {
            if (stripper.is_none()) {
                return failure<BuildSummary>(ErrorKind::Toolchain,
                                             "strip tool was not resolved"_str);
            }
            emit(request,
                 BuildEventKind::Strip,
                 target_spec.name.as_str(),
                 executable_path.as_path());
            auto stripped = toolchain.strip_artifact(executable_path.as_path(),
                                                     stripper->as_path(),
                                                     package_plan.profile->strip,
                                                     target_spec.root.as_path());
            if (stripped.is_err()) return Err(rstd::move(stripped).unwrap_err());
            build_timing.record(BuildOperation::Strip, *stripped);
        }
        artifacts.push(BuiltArtifact {
            .package      = target_spec.name.clone(),
            .target       = target_spec.name.clone(),
            .kind         = target_spec.artifact_kind,
            .path         = rstd::move(executable_path),
            .package_root = target_spec.root.clone(),
        });
    }

    return Ok(BuildSummary {
        .package           = package.name.clone(),
        .profile           = package_plan.profile->name.clone(),
        .output            = PathBuf::from(layout.output()),
        .scanned           = scans.len(),
        .compiled          = compiled,
        .reused            = reused,
        .artifacts         = rstd::move(artifacts),
        .frontend          = frontend_statistics,
        .toolchain         = toolchain.statistics(),
        .scan_profile      = rstd::move(scan_profile),
        .compile_execution = compile_statistics,
        .build_timing      = rstd::move(build_timing),
        .compile_tests     = rstd::move(compile_tests),
    });
}

auto build(const BuildRequest& request) -> Result<BuildSummary> {
    auto environment = ResolvedProcessEnvironment::resolve(request.environment);
    if (environment.is_err()) return Err(rstd::move(environment).unwrap_err());
    return build_with_environment(request, *environment);
}

} // namespace lito
