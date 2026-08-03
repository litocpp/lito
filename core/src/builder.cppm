export module tenon.builder;

import rstd;
import tenon.model;
import tenon.source_discovery;
import tenon.project;
import tenon.package;
import tenon.toolchain;
import tenon.modules;
import tenon.cache;
import tenon.build_layout;
import tenon.frontend;
import tenon.frontend_analysis;
import tenon.frontend_observer;
import tenon.compile_test;
import tenon.profiling;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace tenon
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

} // namespace tenon

export namespace tenon
{

auto build(const BuildRequest& request) -> Result<BuildSummary> {
    if (request.selection.root.is_empty()) {
        return failure<BuildSummary>(ErrorKind::InvalidRequest, "build directory is required"_str);
    }
    auto created_toolchain = ClangToolchain::create(request.configuration.toolchain);
    if (created_toolchain.is_err()) {
        return Err(rstd::move(created_toolchain).unwrap_err());
    }
    auto toolchain = rstd::move(created_toolchain).unwrap();
    auto loaded    = resolve_project_metadata(request.selection,
                                              request.configuration,
                                              request.sources,
                                              toolchain.target_info(),
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
        return discover_package_sources(
            metadata, discovery_plan, analysis_service, request.observer);
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
                source.frontend_analysis->context_identity.as_str() == context->id.as_str()) {
                unit.frontend_analysis = Some(frontend::clone_analysis(*source.frontend_analysis));
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
        const auto target          = units[unit].unit.target;
        auto       needed_analysis = units[unit].frontend_analysis.is_none();
        if (needed_analysis) {
            auto source_frame = profiler.begin_source_frame(package.targets[target].name.as_str(),
                                                            units[unit].unit.source.as_path(),
                                                            ScanSourceOrigin::Classify);
            if (source_frame.is_err()) {
                return failure<BuildSummary>(ErrorKind::Artifact,
                                             rstd::move(source_frame).unwrap_err_unchecked());
            }
            auto analyzed = analysis_service.analyze(package.targets[target].name.as_str(),
                                                     units[unit].unit.relative_source.as_path(),
                                                     units[unit].unit.source.as_path(),
                                                     *units[unit].unit.context,
                                                     units[unit].working_directory.as_path());
            auto source_finished = profiler.end_source_frame();
            if (analyzed.is_err()) return Err(rstd::move(analyzed).unwrap_err());
            if (source_finished.is_err()) {
                return failure<BuildSummary>(ErrorKind::Artifact,
                                             rstd::move(source_finished).unwrap_err_unchecked());
            }
            auto analysis = rstd::move(analyzed).unwrap();
            emit(request,
                 analysis.origin == frontend::FrontendAnalysisOrigin::PersistentCache
                     ? BuildEventKind::ScanReuse
                     : BuildEventKind::Scan,
                 package.targets[target].name.as_str(),
                 units[unit].unit.source.as_path());
            units[unit].frontend_analysis = Some(rstd::move(analysis));
        } else {
            analysis_service.record_in_build_reuse();
        }
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
    auto        scan_profile                   = rstd::move(finished_profile).unwrap_unchecked();
    auto        frontend_statistics            = frontend_service.statistics();
    const auto& scan_cache_statistics          = scan_cache.statistics();
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
    frontend_service.release_source_cache();

    auto cache = CompileCacheSession::create(cache_environment, layout.output());

    auto format_identity = bmi_format_identity(toolchain.bmi_format());
    auto format_key      = bmi_format_key(toolchain.bmi_format());
    auto compiled        = usize {};
    auto reused          = usize {};
    auto compile_tests   = Vec<CompileTestExecution>::make();
    auto build_timing    = BuildTimingReport {};
    for (auto unit : module_plan.compile_order) {
        auto dependencies        = Vec<DependencyArtifact>::make();
        auto recipe_dependencies = Vec<BmiRecipeDependency>::make();
        for (auto input : module_plan.direct_inputs[unit]) {
            if (scans[input].provided.is_none() || units[input].unit.bmi.is_none()) {
                return failure<BuildSummary>(
                    ErrorKind::Dependency,
                    rstd::format("module dependency '{}' has no resolved BMI artifact",
                                 units[input].unit.source.as_path()));
            }
            const auto& artifact = *units[input].unit.bmi;
            dependencies.push(DependencyArtifact {
                .logical_name = artifact.logical_name.clone(),
                .artifact     = artifact.key.value.clone(),
            });
            recipe_dependencies.push(BmiRecipeDependency {
                .logical_name = artifact.logical_name.clone(),
                .artifact_key = artifact.key.value.clone(),
            });
        }
        auto module_dependencies = Vec<ModuleArtifactDependency>::make();
        for (auto input : module_plan.resolved_inputs[unit]) {
            if (scans[input].provided.is_none() || units[input].unit.bmi.is_none()) {
                return failure<BuildSummary>(
                    ErrorKind::Dependency,
                    rstd::format("module dependency '{}' has no resolved BMI artifact",
                                 units[input].unit.source.as_path()));
            }
            const auto& artifact = *units[input].unit.bmi;
            module_dependencies.push(ModuleArtifactDependency {
                .logical_name = artifact.logical_name.clone(),
                .artifact_key = BmiArtifactKey { .value = artifact.key.value.clone() },
                .path         = artifact.path.clone(),
            });
        }
        const auto target = units[unit].unit.target;
        if (scans[unit].provided.is_some()) {
            auto source_identity   = units[unit].unit.source.as_path().to_str();
            auto relative_identity = units[unit].unit.relative_source.as_path().to_str();
            if (source_identity.is_none() || relative_identity.is_none()) {
                return failure<BuildSummary>(ErrorKind::Artifact,
                                             "BMI provider path is not valid UTF-8"_str);
            }
            auto provider_identity = rstd::format("{}:{}:{}",
                                                  package.targets[target].name.as_str(),
                                                  *relative_identity,
                                                  units[unit].unit.context->id.as_str());
            auto key               = make_bmi_artifact_key(BmiRecipe {
                .request                 = units[unit].unit.context->bmi,
                .logical_name            = scans[unit].provided->logical_name.clone(),
                .provider_identity       = provider_identity.clone(),
                .source_identity         = String::make(*source_identity),
                .source_content_identity = units[unit].frontend_analysis->receipt.clone(),
                .cpp_context_identity    = cpp_compile_identity(units[unit].unit.context->cpp),
                .public_requirements_identity =
                    cpp_public_requirements_identity(units[unit].unit.context->public_requirements),
                .format_identity     = format_identity.clone(),
                .direct_dependencies = rstd::move(recipe_dependencies),
            });
            auto bmi_path          = layout.bmi(format_key.as_str(),
                                                key.value.as_str(),
                                                scans[unit].provided->logical_name.as_str());
            auto direct            = Vec<BmiRecipeDependency>::with_capacity(dependencies.len());
            for (const auto& dependency : dependencies) {
                direct.push(BmiRecipeDependency {
                    .logical_name = dependency.logical_name.clone(),
                    .artifact_key = dependency.artifact.clone(),
                });
            }
            units[unit].unit.bmi = Some(BmiArtifact {
                .logical_name        = scans[unit].provided->logical_name.clone(),
                .provider_identity   = rstd::move(provider_identity),
                .key                 = rstd::move(key),
                .format              = clone_bmi_format_identity(toolchain.bmi_format()),
                .request             = units[unit].unit.context->bmi,
                .path                = rstd::move(bmi_path),
                .direct_dependencies = rstd::move(direct),
                .paired_object       = Some(units[unit].unit.object.clone()),
            });
        }
        auto invocation = toolchain.prepare_compile(units[unit], scans[unit], module_dependencies);
        if (invocation.is_err()) return Err(rstd::move(invocation).unwrap_err());
        auto decision = cache.evaluate(package.targets[target].name.as_str(),
                                       units[unit],
                                       units[unit].frontend_analysis->receipt.as_str(),
                                       *invocation,
                                       dependencies);
        if (decision.is_err()) return Err(rstd::move(decision).unwrap_err());
        auto cache_decision = rstd::move(decision).unwrap();
        if (units[unit].unit.compile_test != nullptr) {
            const auto& test = *units[unit].unit.compile_test;
            if (cache_decision.current() && test.outcome == CompileTestOutcome::Success) {
                ++reused;
                emit(request,
                     BuildEventKind::Reuse,
                     package.targets[target].name.as_str(),
                     units[unit].unit.source.as_path());
                auto execution = evaluate_compile_test(package.targets[target].name.as_str(),
                                                       test,
                                                       units[unit].unit.source.as_path(),
                                                       CompileCommandResult {});
                auto recorded  = cache.record_compile_test(
                    cache_decision, (*units[unit].unit.compile_test_record).as_path(), execution);
                if (recorded.is_err()) return Err(rstd::move(recorded).unwrap_err());
                compile_tests.push(rstd::move(execution));
                continue;
            }
            auto begun = cache.begin_compile_test(
                cache_decision, (*units[unit].unit.compile_test_record).as_path(), test);
            if (begun.is_err()) return Err(rstd::move(begun).unwrap_err());
            emit(request,
                 BuildEventKind::Compile,
                 package.targets[target].name.as_str(),
                 units[unit].unit.source.as_path());
            auto output = toolchain.execute_compile_capture(*invocation);
            if (output.is_err()) return Err(rstd::move(output).unwrap_err());
            auto command_output = rstd::move(output).unwrap();
            auto elapsed        = command_output.elapsed;
            build_timing.record(BuildOperation::Compile, elapsed);
            auto execution = evaluate_compile_test(package.targets[target].name.as_str(),
                                                   test,
                                                   units[unit].unit.source.as_path(),
                                                   rstd::move(command_output));
            if (execution.exit_code == i32 {}) {
                auto committed = cache.commit_success(units[unit], cache_decision);
                if (committed.is_err()) return Err(rstd::move(committed).unwrap_err());
            }
            auto recorded = cache.record_compile_test(
                cache_decision, (*units[unit].unit.compile_test_record).as_path(), execution);
            if (recorded.is_err()) return Err(rstd::move(recorded).unwrap_err());
            compile_tests.push(rstd::move(execution));
            ++compiled;
            continue;
        }
        if (cache_decision.current()) {
            ++reused;
            emit(request,
                 BuildEventKind::Reuse,
                 package.targets[target].name.as_str(),
                 units[unit].unit.source.as_path());
        } else {
            auto begun = cache.begin_compile(cache_decision);
            if (begun.is_err()) return Err(rstd::move(begun).unwrap_err());
            emit(request,
                 BuildEventKind::Compile,
                 package.targets[target].name.as_str(),
                 units[unit].unit.source.as_path());
            auto result = toolchain.execute_compile(*invocation, units[unit].unit.source.as_path());
            if (result.is_err()) return Err(rstd::move(result).unwrap_err());
            build_timing.record(BuildOperation::Compile, *result);
            auto committed = cache.commit_success(units[unit], cache_decision);
            if (committed.is_err()) return Err(rstd::move(committed).unwrap_err());
            ++compiled;
        }
    }

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
        auto linked_archives = Vec<LinkArchive>::make();
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
                linked_archives.push(LinkArchive {
                    .path = (*archive_paths[candidate]).clone(),
                    .mode = LinkArchiveMode::Whole,
                });
            }
        }
        for (auto dependency : package_plan.link_dependencies[target]) {
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
            linked_archives.push(LinkArchive {
                .path = (*archive_paths[dependency]).clone(),
                .mode = LinkArchiveMode::Normal,
            });
        }
        auto executable_path =
            target_spec.artifact_kind == ArtifactKind::TestExecutable
                ? layout.test(target_spec.name.as_str(), target_spec.artifact_name.as_str())
                : layout.executable(target_spec.name.as_str(), target_spec.artifact_name.as_str());
        emit(request, BuildEventKind::Link, target_spec.name.as_str(), executable_path.as_path());
        auto linked = toolchain.link_executable(executable_path.as_path(),
                                                objects,
                                                linked_archives,
                                                package_plan.profile->cpp.abi.standard_library,
                                                package_plan.linker_options[target],
                                                target_spec.root.as_path());
        if (linked.is_err()) return Err(rstd::move(linked).unwrap_err());
        build_timing.record(BuildOperation::Link, *linked);
        artifacts.push(BuiltArtifact {
            .package      = target_spec.name.clone(),
            .target       = target_spec.name.clone(),
            .kind         = target_spec.artifact_kind,
            .path         = rstd::move(executable_path),
            .package_root = target_spec.root.clone(),
        });
    }

    return Ok(BuildSummary {
        .package       = package.name.clone(),
        .profile       = package_plan.profile->name.clone(),
        .output        = PathBuf::from(layout.output()),
        .scanned       = scans.len(),
        .compiled      = compiled,
        .reused        = reused,
        .artifacts     = rstd::move(artifacts),
        .frontend      = frontend_statistics,
        .toolchain     = toolchain.statistics(),
        .scan_profile  = rstd::move(scan_profile),
        .build_timing  = rstd::move(build_timing),
        .compile_tests = rstd::move(compile_tests),
    });
}

} // namespace tenon
