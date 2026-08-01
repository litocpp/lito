export module tenon.builder;

import rstd;
import tenon.model;
import tenon.source_discovery;
import tenon.workspace_resolver;
import tenon.lock_store;
import tenon.package;
import tenon.toolchain.clang;
import tenon.modules;
import tenon.artifact_store;
import tenon.build_layout;

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

auto load_build_metadata(const BuildRequest& request) -> Result<PackageMetadata> {
    auto lock = load_lock_session(request.selection.root.as_path(), request.locked);
    if (lock.is_err()) return Err(rstd::move(lock).unwrap_err());
    auto lock_session  = rstd::move(lock).unwrap();
    auto resolution    = lock_session.take_resolution_options();
    resolution.sources = request.sources.clone();
    auto resolved      = resolve_package_selection(request.selection, rstd::move(resolution));
    if (resolved.is_err()) return Err(rstd::move(resolved).unwrap_err());
    auto build  = rstd::move(resolved).unwrap();
    auto locked = sync_lock(build.graph, rstd::move(lock_session));
    if (locked.is_err()) return Err(rstd::move(locked).unwrap_err());

    return adapt_package_graph_metadata(rstd::move(build.graph),
                                        build.selected_package_names,
                                        build.selected_root_names,
                                        request.configuration);
}

} // namespace tenon

export namespace tenon
{

auto build(const BuildRequest& request) -> Result<BuildSummary> {
    if (request.selection.root.is_empty()) {
        return failure<BuildSummary>(ErrorKind::InvalidRequest, "build directory is required"_str);
    }
    auto loaded = load_build_metadata(request);
    if (loaded.is_err()) return Err(rstd::move(loaded).unwrap_err());
    auto metadata = rstd::move(loaded).unwrap();

    auto created_toolchain = ClangToolchain::create(metadata.toolchain);
    if (created_toolchain.is_err()) {
        return Err(rstd::move(created_toolchain).unwrap_err());
    }
    auto toolchain = rstd::move(created_toolchain).unwrap();

    auto resolved = resolve_source_discovery(
        metadata, metadata.default_profile.as_str(), request.targets, toolchain.identity());
    if (resolved.is_err()) return Err(rstd::move(resolved).unwrap_err());
    auto discovery_plan = rstd::move(resolved).unwrap();

    auto selected_layout =
        BuildLayout::create(metadata.root.as_path(),
                            request.output.as_path(),
                            metadata.profiles[discovery_plan.profile].name.as_str());
    if (selected_layout.is_err()) return Err(rstd::move(selected_layout).unwrap_err());
    auto layout = rstd::move(selected_layout).unwrap();

    auto discovered = discover_package_sources(metadata, discovery_plan, toolchain, layout);
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
    auto units = Vec<PreparedUnit>::make();
    for (auto target : package_plan.target_order) {
        const auto& target_spec = package.targets[target];
        for (const auto& source : target_spec.sources) {
            auto object = layout.object(
                target_spec.name.as_str(), source.path.as_path(), target_spec.root.as_path());
            auto depfile = layout.depfile(
                target_spec.name.as_str(), source.path.as_path(), target_spec.root.as_path());
            auto fingerprint = layout.fingerprint(
                target_spec.name.as_str(), source.path.as_path(), target_spec.root.as_path());
            if (object.is_err()) return Err(rstd::move(object).unwrap_err());
            if (depfile.is_err()) return Err(rstd::move(depfile).unwrap_err());
            if (fingerprint.is_err()) return Err(rstd::move(fingerprint).unwrap_err());

            auto id       = units.len();
            auto prepared = toolchain.prepare(
                UnitSpec {
                    .id          = id,
                    .target      = target,
                    .source      = source.path.clone(),
                    .object      = rstd::move(object).unwrap(),
                    .depfile     = rstd::move(depfile).unwrap(),
                    .fingerprint = rstd::move(fingerprint).unwrap(),
                    .context     = rstd::addressof(package_plan.contexts[target]),
                },
                target_spec.root.as_path());
            if (prepared.is_err()) return Err(rstd::move(prepared).unwrap_err());
            auto unit = rstd::move(prepared).unwrap();
            if (source.preprocessed.is_some()) {
                unit.preprocessed = rstd::addressof(*source.preprocessed);
            }
            units.push(rstd::move(unit));
            target_units[target].emplace_back(id);
        }
    }

    auto scans = Vec<ScanResult>::with_capacity(units.len());
    for (auto unit = UnitId {}; unit < units.len(); ++unit) {
        const auto target = units[unit].unit.target;
        emit(request,
             BuildEventKind::Scan,
             package.targets[target].name.as_str(),
             units[unit].unit.source.as_path());
        auto scanned = toolchain.scan(units[unit]);
        if (scanned.is_err()) return Err(rstd::move(scanned).unwrap_err());
        auto result = rstd::move(scanned).unwrap();
        if (result.provided.is_some()) {
            units[unit].unit.bmi = Some(layout.bmi((*result.provided).logical_name.as_str()));
        }
        scans.push(rstd::move(result));
    }

    auto convention_valid = validate_module_conventions(package, units, scans);
    if (convention_valid.is_err()) {
        return Err(rstd::move(convention_valid).unwrap_err());
    }

    auto resolved_modules = resolve_modules(package_plan, units, scans);
    if (resolved_modules.is_err()) {
        return Err(rstd::move(resolved_modules).unwrap_err());
    }
    auto module_plan = rstd::move(resolved_modules).unwrap();

    auto store = ArtifactStore {};
    auto keys  = Vec<String>::with_capacity(units.len());
    for (auto unit = UnitId {}; unit < units.len(); ++unit) keys.emplace_back();
    auto bmi_directory = layout.bmi_directory();
    auto compiled      = usize {};
    auto reused        = usize {};
    for (auto unit : module_plan.compile_order) {
        auto dependency_keys = Vec<String>::make();
        for (auto input : module_plan.direct_inputs[unit]) {
            dependency_keys.push(keys[input].clone());
        }
        auto key = store.key_for(units[unit], scans[unit], dependency_keys);
        if (key.is_err()) return Err(rstd::move(key).unwrap_err());
        auto       artifact_key = rstd::move(key).unwrap();
        const auto target       = units[unit].unit.target;
        if (store.current(units[unit], scans[unit], artifact_key.as_str())) {
            ++reused;
            emit(request,
                 BuildEventKind::Reuse,
                 package.targets[target].name.as_str(),
                 units[unit].unit.source.as_path());
        } else {
            emit(request,
                 BuildEventKind::Compile,
                 package.targets[target].name.as_str(),
                 units[unit].unit.source.as_path());
            auto prebuilt_module_path = Option<ref<rstd::path::Path>> {};
            if (! module_plan.direct_inputs[unit].is_empty()) {
                prebuilt_module_path = Some(bmi_directory.as_path());
            }
            auto result = toolchain.compile(units[unit], scans[unit], prebuilt_module_path);
            if (result.is_err()) return Err(rstd::move(result).unwrap_err());
            auto committed = store.commit(units[unit], artifact_key.as_str());
            if (committed.is_err()) return Err(rstd::move(committed).unwrap_err());
            ++compiled;
        }
        keys[unit] = rstd::move(artifact_key);
    }

    auto archives      = Vec<PathBuf>::make();
    auto archive_paths = Vec<Option<PathBuf>>::with_capacity(package.targets.len());
    for (auto target = TargetId {}; target < package.targets.len(); ++target) {
        archive_paths.emplace_back(None());
    }
    for (auto target : package_plan.target_order) {
        const auto& target_spec = package.targets[target];
        if (target_spec.artifact_kind != ArtifactKind::StaticLibrary) continue;
        auto archive_path =
            layout.archive(target_spec.name.as_str(), target_spec.artifact_name.as_str());
        auto objects = Vec<PathBuf>::with_capacity(target_units[target].len());
        for (auto unit : target_units[target]) objects.push(units[unit].unit.object.clone());
        emit(request, BuildEventKind::Archive, target_spec.name.as_str(), archive_path.as_path());
        auto archived =
            toolchain.archive(archive_path.as_path(), objects, target_spec.root.as_path());
        if (archived.is_err()) return Err(rstd::move(archived).unwrap_err());
        archive_paths[target] = Some(archive_path.clone());
        archives.push(rstd::move(archive_path));
    }

    auto executables = Vec<PathBuf>::make();
    for (auto target : package_plan.target_order) {
        const auto& target_spec = package.targets[target];
        if (target_spec.artifact_kind != ArtifactKind::Executable) continue;
        auto objects = Vec<PathBuf>::with_capacity(target_units[target].len());
        for (auto unit : target_units[target]) objects.push(units[unit].unit.object.clone());
        auto linked_archives =
            Vec<PathBuf>::with_capacity(package_plan.link_dependencies[target].len());
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
            linked_archives.push((*archive_paths[dependency]).clone());
        }
        auto executable_path =
            layout.executable(target_spec.name.as_str(), target_spec.artifact_name.as_str());
        emit(request, BuildEventKind::Link, target_spec.name.as_str(), executable_path.as_path());
        auto linked = toolchain.link_executable(executable_path.as_path(),
                                                objects,
                                                linked_archives,
                                                package_plan.profile->standard_library,
                                                package_plan.linker_options[target],
                                                target_spec.root.as_path());
        if (linked.is_err()) return Err(rstd::move(linked).unwrap_err());
        executables.push(rstd::move(executable_path));
    }

    return Ok(BuildSummary {
        .package     = package.name.clone(),
        .profile     = package_plan.profile->name.clone(),
        .output      = PathBuf::from(layout.output()),
        .scanned     = scans.len(),
        .compiled    = compiled,
        .reused      = reused,
        .archives    = rstd::move(archives),
        .executables = rstd::move(executables),
    });
}

} // namespace tenon
