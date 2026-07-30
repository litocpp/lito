export module tenon.builder;

import rstd;
import tenon.model;
import tenon.source_discovery;
import tenon.package_resolver;
import tenon.lock_store;
import tenon.package_adapter;
import tenon.package;
import tenon.clang_toolchain;
import tenon.module_convention;
import tenon.module_graph;
import tenon.artifact_store;

namespace tenon::builder_detail
{

using namespace rstd::literals;

template<typename T>
auto failure(ErrorKind kind, String message) -> Result<T> {
    return rstd::Err(Error::make(kind, rstd::move(message)));
}

template<typename T>
auto failure(ErrorKind kind, rstd::ref<rstd::str> message) -> Result<T> {
    return rstd::Err(Error::make(kind, message));
}

auto join(rstd::ref<rstd::path::Path> base, rstd::ref<rstd::str> component) -> PathBuf {
    auto value = PathBuf::from(component);
    return PathBuf::from(base).join(value.as_path());
}

auto hex_digit(rstd::u8 value) noexcept -> char {
    return value < rstd::u8(10) ? static_cast<char>('0' + value.to_primitive())
                                : static_cast<char>('A' + (value - rstd::u8(10)).to_primitive());
}

auto artifact_filename(rstd::ref<rstd::str> relative,
                       rstd::ref<rstd::str> suffix) -> String {
    auto result = String::make();
    for (rstd::usize index {}; index < relative.size(); ++index) {
        const auto byte = relative[index];
        const bool plain = (byte >= rstd::u8('a') && byte <= rstd::u8('z')) ||
                           (byte >= rstd::u8('A') && byte <= rstd::u8('Z')) ||
                           (byte >= rstd::u8('0') && byte <= rstd::u8('9')) ||
                           byte == rstd::u8('_') || byte == rstd::u8('-') ||
                           byte == rstd::u8('.');
        if (plain) {
            result.push_ascii(static_cast<char>(byte.to_primitive()));
        } else {
            const auto raw = byte.to_primitive();
            result.push_ascii('%');
            result.push_ascii(hex_digit(rstd::u8(raw >> 4u)));
            result.push_ascii(hex_digit(rstd::u8(raw & 0x0fu)));
        }
    }
    result.push_str(suffix);
    return result;
}

auto source_artifact(rstd::ref<rstd::path::Path> output,
                     rstd::ref<rstd::str> category,
                     rstd::ref<rstd::str> target,
                     rstd::ref<rstd::path::Path> source,
                     rstd::ref<rstd::path::Path> root,
                     rstd::ref<rstd::str> suffix) -> Result<PathBuf> {
    auto relative = source.strip_prefix(root);
    if (relative.is_none() || (*relative).is_empty()) {
        return failure<PathBuf>(
            ErrorKind::Artifact, rstd::format("source '{}' is outside package root", source));
    }
    auto text = (*relative).to_str();
    if (text.is_none()) {
        return failure<PathBuf>(
            ErrorKind::Artifact, rstd::format("source path '{}' is not valid UTF-8", source));
    }

    auto name = artifact_filename(*text, suffix);
    auto category_dir = join(output, category);
    auto target_dir   = join(category_dir.as_path(), target);
    auto relative_output = PathBuf::from(rstd::move(name));
    return rstd::Ok(target_dir.join(relative_output.as_path()));
}

auto module_filename(rstd::ref<rstd::str> logical_name) -> String {
    auto result = String::make();
    for (auto byte : logical_name) {
        const auto ascii = (byte >= rstd::u8('a') && byte <= rstd::u8('z')) ||
                           (byte >= rstd::u8('A') && byte <= rstd::u8('Z')) ||
                           (byte >= rstd::u8('0') && byte <= rstd::u8('9')) ||
                           byte == rstd::u8('_');
        result.push_ascii(ascii ? static_cast<char>(byte.to_primitive()) : '-');
    }
    result.push_str(".pcm"_str);
    return result;
}

auto bmi_artifact(rstd::ref<rstd::path::Path> output,
                  rstd::ref<rstd::str> target,
                  rstd::ref<rstd::str> logical_name) -> PathBuf {
    auto bmi_dir    = join(output, "bmi"_str);
    auto target_dir = join(bmi_dir.as_path(), target);
    auto filename   = PathBuf::from(module_filename(logical_name));
    return target_dir.join(filename.as_path());
}

auto output_directory(const BuildRequest& request,
                      const PackageSpec& package,
                      const ProfileSpec& profile) -> Result<PathBuf> {
    auto parent = package.manifest_path.as_path().parent();
    if (parent.is_none()) {
        return failure<PathBuf>(ErrorKind::Filesystem,
                                "manifest path has no parent directory"_str);
    }

    auto output = PathBuf::make();
    if (request.output.is_empty()) {
        auto build = join(*parent, "build"_str);
        output     = join(build.as_path(), profile.name.as_str());
    } else if (request.output.as_path().is_absolute()) {
        output = request.output.clone();
    } else {
        output = PathBuf::from(*parent).join(request.output.as_path());
    }

    auto created = rstd::fs::create_dir_all(output.as_path());
    if (created.is_err()) {
        return failure<PathBuf>(ErrorKind::Filesystem, rstd::format(
            "cannot create output directory '{}': {}", output.as_path(), rstd::move(created).unwrap_err()));
    }
    auto canonical = rstd::fs::canonicalize(output.as_path());
    if (canonical.is_err()) {
        return failure<PathBuf>(ErrorKind::Filesystem, rstd::format(
            "cannot resolve output directory '{}': {}", output.as_path(), rstd::move(canonical).unwrap_err()));
    }
    return rstd::Ok(rstd::move(canonical).unwrap());
}

auto emit(const BuildRequest& request,
          BuildEventKind kind,
          rstd::ref<rstd::str> target,
          rstd::ref<rstd::path::Path> path) noexcept -> void {
    if (request.observer.is_none()) return;
    const auto& observer = *request.observer;
    if (observer.notify == nullptr) return;
    observer.notify(observer.context, BuildEvent { kind, target, path });
}

auto load_build_package(const BuildRequest& request) -> Result<PackageSpec> {
    auto extension = request.manifest.as_path().extension();
    if (extension.is_none()) {
        return failure<PackageSpec>(ErrorKind::Manifest, "manifest path must end in .toml"_str);
    }
    auto text = (*extension).to_str();
    if (text.is_none()) {
        return failure<PackageSpec>(ErrorKind::Manifest,
                                    "manifest extension is not valid UTF-8"_str);
    }
    if (*text != "toml"_str) {
        return failure<PackageSpec>(ErrorKind::Manifest, "manifest path must end in .toml"_str);
    }

    auto resolved = resolve_package_graph(request.manifest.as_path());
    if (resolved.is_err()) return rstd::Err(rstd::move(resolved).unwrap_err());
    auto graph = rstd::move(resolved).unwrap();
    auto locked = sync_lock(graph, request.locked);
    if (locked.is_err()) return rstd::Err(rstd::move(locked).unwrap_err());

    auto source_sets = Vec<ResolvedSourceSet>::with_capacity(graph.packages.len());
    for (const auto& package : graph.packages) {
        auto discovered = discover_sources(package.manifest);
        if (discovered.is_err()) return rstd::Err(rstd::move(discovered).unwrap_err());
        source_sets.push(rstd::move(discovered).unwrap());
    }
    return adapt_package_graph(
        rstd::move(graph), rstd::move(source_sets), request.configuration);
}

} // namespace tenon::builder_detail

export namespace tenon
{

auto build(const BuildRequest& request) -> Result<BuildSummary> {
    using namespace builder_detail;
    using namespace rstd::literals;

    if (request.manifest.is_empty()) {
        return failure<BuildSummary>(ErrorKind::InvalidRequest, "manifest path is required"_str);
    }
    auto loaded = load_build_package(request);
    if (loaded.is_err()) return rstd::Err(rstd::move(loaded).unwrap_err());
    auto package = rstd::move(loaded).unwrap();

    auto created_toolchain = ClangToolchain::create(package.toolchain);
    if (created_toolchain.is_err()) {
        return rstd::Err(rstd::move(created_toolchain).unwrap_err());
    }
    auto toolchain = rstd::move(created_toolchain).unwrap();

    auto resolved = resolve_package(
        package, request.profile.as_str(), request.targets, toolchain.identity());
    if (resolved.is_err()) return rstd::Err(rstd::move(resolved).unwrap_err());
    auto package_plan = rstd::move(resolved).unwrap();

    auto selected_output = output_directory(request, package, *package_plan.profile);
    if (selected_output.is_err()) return rstd::Err(rstd::move(selected_output).unwrap_err());
    auto output = rstd::move(selected_output).unwrap();

    auto target_units = Vec<Vec<UnitId>>::with_capacity(package.targets.len());
    for (auto target = TargetId {}; target < package.targets.len(); ++target) {
        target_units.emplace_back();
    }
    auto units = Vec<PreparedUnit>::make();
    for (auto target : package_plan.target_order) {
        const auto& target_spec = package.targets[target];
        for (const auto& source : target_spec.sources) {
            auto object = source_artifact(output.as_path(),
                                          "obj"_str,
                                          target_spec.name.as_str(),
                                          source.as_path(),
                                          target_spec.root.as_path(),
                                          ".o"_str);
            auto depfile = source_artifact(output.as_path(),
                                           "dep"_str,
                                           target_spec.name.as_str(),
                                           source.as_path(),
                                           target_spec.root.as_path(),
                                           ".d"_str);
            auto fingerprint = source_artifact(output.as_path(),
                                               "fingerprint"_str,
                                               target_spec.name.as_str(),
                                               source.as_path(),
                                               target_spec.root.as_path(),
                                               ".txt"_str);
            if (object.is_err()) return rstd::Err(rstd::move(object).unwrap_err());
            if (depfile.is_err()) return rstd::Err(rstd::move(depfile).unwrap_err());
            if (fingerprint.is_err()) return rstd::Err(rstd::move(fingerprint).unwrap_err());

            auto id = units.len();
            auto prepared = toolchain.prepare(
                UnitSpec {
                    .id = id,
                    .target = target,
                    .source = source.clone(),
                    .object = rstd::move(object).unwrap(),
                    .depfile = rstd::move(depfile).unwrap(),
                    .fingerprint = rstd::move(fingerprint).unwrap(),
                    .context = rstd::addressof(package_plan.contexts[target]),
                },
                target_spec.root.as_path());
            if (prepared.is_err()) return rstd::Err(rstd::move(prepared).unwrap_err());
            units.push(rstd::move(prepared).unwrap());
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
        if (scanned.is_err()) return rstd::Err(rstd::move(scanned).unwrap_err());
        auto result = rstd::move(scanned).unwrap();
        if (result.provided.is_some()) {
            units[unit].unit.bmi = rstd::Some(bmi_artifact(
                output.as_path(),
                package.targets[target].name.as_str(),
                (*result.provided).logical_name.as_str()));
        }
        scans.push(rstd::move(result));
    }

    auto convention_valid = validate_module_conventions(package, units, scans);
    if (convention_valid.is_err()) {
        return rstd::Err(rstd::move(convention_valid).unwrap_err());
    }

    auto resolved_modules = resolve_modules(package_plan, units, scans);
    if (resolved_modules.is_err()) {
        return rstd::Err(rstd::move(resolved_modules).unwrap_err());
    }
    auto module_plan = rstd::move(resolved_modules).unwrap();

    auto store = ArtifactStore {};
    auto keys  = Vec<String>::with_capacity(units.len());
    for (auto unit = UnitId {}; unit < units.len(); ++unit) keys.emplace_back();
    auto compiled = rstd::usize {};
    auto reused   = rstd::usize {};
    for (auto unit : module_plan.compile_order) {
        auto dependency_keys = Vec<String>::make();
        for (const auto& input : module_plan.direct_inputs[unit]) {
            dependency_keys.push(keys[input.provider].clone());
        }
        auto key = store.key_for(units[unit], scans[unit], dependency_keys);
        if (key.is_err()) return rstd::Err(rstd::move(key).unwrap_err());
        auto artifact_key = rstd::move(key).unwrap();
        const auto target = units[unit].unit.target;
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
            auto result = toolchain.compile(
                units[unit], scans[unit], module_plan.transitive_inputs[unit]);
            if (result.is_err()) return rstd::Err(rstd::move(result).unwrap_err());
            auto committed = store.commit(units[unit], artifact_key.as_str());
            if (committed.is_err()) return rstd::Err(rstd::move(committed).unwrap_err());
            ++compiled;
        }
        keys[unit] = rstd::move(artifact_key);
    }

    auto archives = Vec<PathBuf>::make();
    auto archive_paths =
        Vec<rstd::Option<PathBuf>>::with_capacity(package.targets.len());
    for (auto target = TargetId {}; target < package.targets.len(); ++target) {
        archive_paths.emplace_back(rstd::None());
    }
    auto library_directory = join(output.as_path(), "lib"_str);
    for (auto target : package_plan.target_order) {
        const auto& target_spec = package.targets[target];
        if (target_spec.artifact_kind != ArtifactKind::StaticLibrary) continue;
        auto archive_name = PathBuf::from(target_spec.artifact_name.as_str());
        auto archive_path = library_directory.join(archive_name.as_path());
        auto objects      = Vec<PathBuf>::with_capacity(target_units[target].len());
        for (auto unit : target_units[target]) objects.push(units[unit].unit.object.clone());
        emit(request,
             BuildEventKind::Archive,
             target_spec.name.as_str(),
             archive_path.as_path());
        auto archived = toolchain.archive(archive_path.as_path(), objects, target_spec.root.as_path());
        if (archived.is_err()) return rstd::Err(rstd::move(archived).unwrap_err());
        archive_paths[target] = rstd::Some(archive_path.clone());
        archives.push(rstd::move(archive_path));
    }

    auto executables = Vec<PathBuf>::make();
    auto binary_directory = join(output.as_path(), "bin"_str);
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
                    rstd::format(
                        "executable target '{}' depends on unavailable library target '{}'",
                        target_spec.name.as_str(),
                        dependency_spec.name.as_str()));
            }
            linked_archives.push((*archive_paths[dependency]).clone());
        }
        auto executable_name = PathBuf::from(target_spec.artifact_name.as_str());
        auto executable_path = binary_directory.join(executable_name.as_path());
        emit(request,
             BuildEventKind::Link,
             target_spec.name.as_str(),
             executable_path.as_path());
        auto linked = toolchain.link_executable(
            executable_path.as_path(),
            objects,
            linked_archives,
            package_plan.profile->standard_library,
            target_spec.root.as_path());
        if (linked.is_err()) return rstd::Err(rstd::move(linked).unwrap_err());
        executables.push(rstd::move(executable_path));
    }

    return rstd::Ok(BuildSummary {
        .package = package.name.clone(),
        .profile = package_plan.profile->name.clone(),
        .output = rstd::move(output),
        .scanned = scans.len(),
        .compiled = compiled,
        .reused = reused,
        .archives = rstd::move(archives),
        .executables = rstd::move(executables),
    });
}

} // namespace tenon
