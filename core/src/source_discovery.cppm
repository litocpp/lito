export module tenon.source_discovery;

import rstd;
import tenon.model;
import tenon.modules;
import tenon.frontend_analysis;
import tenon.frontend;
import tenon.profiling;

using namespace rstd::prelude;
using namespace rstd::literals;
using StringSet = rstd::collections::BTreeMap<String, empty>;
using StringMap = rstd::collections::BTreeMap<String, String>;

namespace tenon
{

template<typename T>
auto discovery_failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Manifest, rstd::move(message)));
}

auto supported_extension(ref<rstd::path::Path> path) -> bool {
    auto extension = path.extension();
    if (extension.is_none()) return false;
    auto text = (*extension).to_str();
    if (text.is_none()) return false;
    return *text == "cppm"_str || *text == "cpp"_str || *text == "cc"_str || *text == "cxx"_str;
}

auto path_text(ref<rstd::path::Path> path) -> Result<String> {
    auto text = path.to_str();
    if (text.is_none()) {
        return discovery_failure<String>(rstd::format("source path '{}' is not valid UTF-8", path));
    }
    return Ok(String::make(*text));
}

struct SourceEntry {
    String         key;
    ResolvedSource source;
};

auto resolve_declared_source(const PackageManifest& manifest, ref<rstd::path::Path> declared)
    -> Result<ResolvedSource> {
    auto requested = manifest.root.join(declared);
    auto canonical = rstd::fs::canonicalize(requested.as_path());
    if (canonical.is_err()) {
        return discovery_failure<ResolvedSource>(
            rstd::format("cannot resolve declared source '{}': {}",
                         declared,
                         rstd::move(canonical).unwrap_err()));
    }
    auto resolved = rstd::move(canonical).unwrap();
    auto relative = resolved.as_path().strip_prefix(manifest.root.as_path());
    if (relative.is_none() || (*relative).is_empty()) {
        return discovery_failure<ResolvedSource>(
            rstd::format("declared source '{}' resolves outside package root '{}'",
                         declared,
                         manifest.root.as_path()));
    }
    auto metadata = rstd::fs::metadata(resolved.as_path());
    if (metadata.is_err()) {
        return discovery_failure<ResolvedSource>(
            rstd::format("cannot inspect declared source '{}': {}",
                         declared,
                         rstd::move(metadata).unwrap_err()));
    }
    if (! metadata->is_file()) {
        return discovery_failure<ResolvedSource>(
            rstd::format("declared source '{}' is not a file", declared));
    }
    if (! supported_extension(resolved.as_path())) {
        return discovery_failure<ResolvedSource>(
            rstd::format("unsupported C++ source extension: {}", declared));
    }
    return Ok(ResolvedSource {
        .relative_path  = PathBuf::from(*relative),
        .canonical_path = rstd::move(resolved),
        .origin         = SourceOrigin::Explicit,
    });
}

auto collect_format_directory(const PackageManifest& manifest,
                              ref<rstd::path::Path>  directory,
                              Vec<SourceEntry>&      entries) -> Result<empty> {
    auto opened = rstd::fs::read_dir(directory);
    if (opened.is_err()) {
        return discovery_failure<empty>(rstd::format("cannot enumerate source directory '{}': {}",
                                                     directory,
                                                     rstd::move(opened).unwrap_err()));
    }
    auto stream = rstd::move(opened).unwrap();
    for (auto next = stream.next(); next.is_some(); next = stream.next()) {
        auto item = rstd::move(next).unwrap();
        if (item.is_err()) {
            return discovery_failure<empty>(
                rstd::format("cannot enumerate source directory '{}': {}",
                             directory,
                             rstd::move(item).unwrap_err()));
        }
        auto entry = rstd::move(item).unwrap();
        auto type  = entry.file_type();
        if (type.is_err()) {
            return discovery_failure<empty>(rstd::format("cannot inspect source entry '{}': {}",
                                                         entry.path().as_path(),
                                                         rstd::move(type).unwrap_err()));
        }
        auto path = entry.path();
        if (type->is_dir()) {
            auto nested = collect_format_directory(manifest, path.as_path(), entries);
            if (nested.is_err()) return nested;
            continue;
        }
        if (! type->is_file() || ! supported_extension(path.as_path())) continue;
        auto canonical = rstd::fs::canonicalize(path.as_path());
        if (canonical.is_err()) {
            return discovery_failure<empty>(rstd::format("cannot resolve source candidate '{}': {}",
                                                         path.as_path(),
                                                         rstd::move(canonical).unwrap_err()));
        }
        auto resolved = rstd::move(canonical).unwrap();
        auto relative = resolved.as_path().strip_prefix(manifest.root.as_path());
        if (relative.is_none() || (*relative).is_empty()) {
            return discovery_failure<empty>(rstd::format(
                "source candidate '{}' resolves outside package root", path.as_path()));
        }
        auto key = path_text(*relative);
        if (key.is_err()) return Err(rstd::move(key).unwrap_err());
        entries.push(SourceEntry {
            .key = rstd::move(key).unwrap(),
            .source =
                ResolvedSource {
                    .relative_path  = PathBuf::from(*relative),
                    .canonical_path = rstd::move(resolved),
                    .origin         = SourceOrigin::Convention,
                },
        });
    }
    return Ok(empty {});
}

auto sort_sources(Vec<ResolvedSource> sources) -> Result<Vec<ResolvedSource>> {
    auto entries = Vec<SourceEntry>::with_capacity(sources.len());
    for (auto& source : sources) {
        auto key = path_text(source.relative_path.as_path());
        if (key.is_err()) return Err(rstd::move(key).unwrap_err());
        entries.push(SourceEntry {
            .key    = rstd::move(key).unwrap(),
            .source = rstd::move(source),
        });
    }
    rstd::slice_::sort_unstable_by(entries.as_mut_slice().as_mut_ref(),
                                   [](const SourceEntry& left, const SourceEntry& right) {
                                       return left.key < right.key;
                                   });
    auto result = Vec<ResolvedSource>::with_capacity(entries.len());
    for (auto& entry : entries) result.push(rstd::move(entry.source));
    return Ok(rstd::move(result));
}

struct DiscoveryCandidate {
    TargetId       target {};
    ResolvedSource source;
};

auto source_key(ref<rstd::path::Path> path) -> Result<String> {
    auto text = path.to_str();
    if (text.is_none()) {
        return discovery_failure<String>(
            rstd::format("module source '{}' is not valid UTF-8", path));
    }
    return Ok(String::make(*text));
}

auto enqueue_candidate(TargetId                 target,
                       ResolvedSource           source,
                       StringMap&               path_names,
                       StringMap&               name_paths,
                       StringSet&               queued,
                       Vec<DiscoveryCandidate>& queue) -> Result<empty> {
    auto path_result = source_key(source.canonical_path.as_path());
    if (path_result.is_err()) return Err(rstd::move(path_result).unwrap_err());
    auto path = rstd::move(path_result).unwrap();
    if (source.expected_module.is_some()) {
        auto existing_name = path_names.get(path.as_str());
        if (existing_name.is_some() &&
            (**existing_name).as_str() != source.expected_module->as_str()) {
            return discovery_failure<empty>(
                rstd::format("module convention maps both '{}' and '{}' to '{}'",
                             (**existing_name).as_str(),
                             source.expected_module->as_str(),
                             source.canonical_path.as_path()));
        }
        auto existing_path = name_paths.get(source.expected_module->as_str());
        if (existing_path.is_some() && (**existing_path).as_str() != path.as_str()) {
            return discovery_failure<empty>(rstd::format("module '{}' maps to both '{}' and '{}'",
                                                         source.expected_module->as_str(),
                                                         (**existing_path).as_str(),
                                                         path.as_str()));
        }
        path_names.insert(path.clone(), source.expected_module->clone());
        name_paths.insert(source.expected_module->clone(), path.clone());
    }
    if (queued.contains_key(path.as_str())) return Ok(empty {});
    queued.insert(rstd::move(path), empty {});
    queue.push(DiscoveryCandidate { .target = target, .source = rstd::move(source) });
    return Ok(empty {});
}

auto import_owner(const PackageMetadata&     package,
                  const SourceDiscoveryPlan& plan,
                  TargetId                   importer,
                  ref<str>                   logical_name) -> Result<Option<TargetId>> {
    auto owner        = Option<TargetId> {};
    auto owner_length = usize {};
    for (auto visible : plan.visible_targets[importer]) {
        const auto& module = package.targets[visible].manifest.root_module;
        if (module.is_none() || ! module_name_belongs(module->as_str(), logical_name)) continue;
        if (owner.is_none() || module->size() > owner_length) {
            owner        = Some(visible);
            owner_length = module->size();
            continue;
        }
        if (module->size() == owner_length && *owner != visible) {
            return discovery_failure<Option<TargetId>>(
                rstd::format("module import '{}' is owned by both '{}' and '{}'",
                             logical_name,
                             package.targets[*owner].manifest.name.as_str(),
                             package.targets[visible].manifest.name.as_str()));
        }
    }
    return Ok(owner);
}

} // namespace tenon

export namespace tenon
{

auto discover_explicit_sources(const PackageManifest& manifest) -> Result<ResolvedSourceSet> {
    auto       seen    = StringSet::make();
    auto       entries = Vec<SourceEntry>::make();
    const auto append  = [&](const PathBuf& declared) -> Result<empty> {
        auto resolved = resolve_declared_source(manifest, declared.as_path());
        if (resolved.is_err()) return Err(rstd::move(resolved).unwrap_err());
        auto source = rstd::move(resolved).unwrap();
        auto key    = path_text(source.relative_path.as_path());
        if (key.is_err()) return Err(rstd::move(key).unwrap_err());
        auto source_key = rstd::move(key).unwrap();
        if (seen.contains_key(source_key.as_str())) {
            return discovery_failure<empty>(
                rstd::format("artifact sources repeat source '{}'", source_key.as_str()));
        }
        seen.insert(source_key.clone(), empty {});
        entries.push(SourceEntry { .key = rstd::move(source_key), .source = rstd::move(source) });
        return Ok(empty {});
    };
    for (const auto& declared : manifest.declared_sources) {
        auto appended = append(declared);
        if (appended.is_err()) return Err(rstd::move(appended).unwrap_err());
    }
    for (const auto& group : manifest.conditional_source_groups) {
        for (const auto& declared : group.sources) {
            auto appended = append(declared);
            if (appended.is_err()) return Err(rstd::move(appended).unwrap_err());
        }
    }
    rstd::slice_::sort_unstable_by(entries.as_mut_slice().as_mut_ref(),
                                   [](const SourceEntry& left, const SourceEntry& right) {
                                       return left.key < right.key;
                                   });
    auto sources = Vec<ResolvedSource>::with_capacity(entries.len());
    for (auto& entry : entries) sources.push(rstd::move(entry.source));
    return Ok(ResolvedSourceSet { .sources = rstd::move(sources) });
}

auto discover_format_sources(const PackageManifest& manifest) -> Result<ResolvedSourceSet> {
    if (manifest.discovery == SourceDiscoveryMode::Explicit) {
        return discover_explicit_sources(manifest);
    }
    auto source_root = manifest.root.join(PathBuf::from("src"_str).as_path());
    auto entries     = Vec<SourceEntry>::make();
    auto collected   = collect_format_directory(manifest, source_root.as_path(), entries);
    if (collected.is_err()) return Err(rstd::move(collected).unwrap_err());
    rstd::slice_::sort_unstable_by(entries.as_mut_slice().as_mut_ref(),
                                   [](const SourceEntry& left, const SourceEntry& right) {
                                       return left.key < right.key;
                                   });
    auto sources = Vec<ResolvedSource>::with_capacity(entries.len());
    for (auto& entry : entries) sources.push(rstd::move(entry.source));
    return Ok(ResolvedSourceSet { .sources = rstd::move(sources) });
}

auto discover_package_sources(const PackageMetadata&       package,
                              const SourceDiscoveryPlan&   plan,
                              FrontendAnalysisService&     analysis_service,
                              const Option<BuildObserver>& observer)
    -> Result<Vec<ResolvedPackageSources>> {
    auto discovered = Vec<Vec<ResolvedSource>>::with_capacity(package.targets.len());
    for (auto target = TargetId {}; target < package.targets.len(); ++target) {
        discovered.emplace_back();
    }
    auto queue      = Vec<DiscoveryCandidate>::make();
    auto path_names = StringMap::make();
    auto name_paths = StringMap::make();
    auto queued     = StringSet::make();

    for (auto target : plan.target_order) {
        const auto& manifest = package.targets[target].manifest;
        if (manifest.discovery == SourceDiscoveryMode::Explicit) {
            auto explicit_sources = discover_explicit_sources(manifest);
            if (explicit_sources.is_err()) return Err(rstd::move(explicit_sources).unwrap_err());
            discovered[target] = rstd::move(explicit_sources).unwrap().sources;
            continue;
        }
        auto entry = module_entry_source(manifest);
        if (entry.is_err()) return Err(rstd::move(entry).unwrap_err());
        auto enqueued = enqueue_candidate(
            target, rstd::move(entry).unwrap(), path_names, name_paths, queued, queue);
        if (enqueued.is_err()) return Err(rstd::move(enqueued).unwrap_err());
        for (const auto& declared : manifest.declared_sources) {
            auto resolved = resolve_declared_source(manifest, declared.as_path());
            if (resolved.is_err()) return Err(rstd::move(resolved).unwrap_err());
            enqueued = enqueue_candidate(
                target, rstd::move(resolved).unwrap(), path_names, name_paths, queued, queue);
            if (enqueued.is_err()) return Err(rstd::move(enqueued).unwrap_err());
        }
    }

    for (auto cursor = usize {}; cursor < queue.len(); ++cursor) {
        auto        candidate    = rstd::move(queue[cursor]);
        const auto& target       = package.targets[candidate.target];
        auto        source_frame = analysis_service.profiler().begin_source_frame(
            target.manifest.name.as_str(),
            candidate.source.canonical_path.as_path(),
            ScanSourceOrigin::Discovery);
        if (source_frame.is_err()) {
            return Err(
                Error::make(ErrorKind::Artifact, rstd::move(source_frame).unwrap_err_unchecked()));
        }
        auto facts           = analysis_service.analyze(target.manifest.name.as_str(),
                                                        candidate.source.relative_path.as_path(),
                                                        candidate.source.canonical_path.as_path(),
                                                        plan.contexts[candidate.target],
                                                        target.manifest.root.as_path());
        auto source_finished = analysis_service.profiler().end_source_frame();
        if (facts.is_err()) return Err(rstd::move(facts).unwrap_err());
        if (source_finished.is_err()) {
            return Err(Error::make(ErrorKind::Artifact,
                                   rstd::move(source_finished).unwrap_err_unchecked()));
        }
        auto frontend_analysis = rstd::move(facts).unwrap();
        if (observer.is_some() && observer->notify != nullptr) {
            auto kind =
                frontend_analysis.origin == frontend::FrontendAnalysisOrigin::PersistentCache
                    ? BuildEventKind::ScanReuse
                    : BuildEventKind::Scan;
            observer->notify(observer->context,
                             BuildEvent { kind,
                                          target.manifest.name.as_str(),
                                          candidate.source.canonical_path.as_path() });
        }
        const auto& frontend_result = frontend_analysis.result;
        if (candidate.source.expected_module.is_some()) {
            if (frontend_result.provided.is_none() ||
                frontend_result.provided->logical_name.as_str() !=
                    candidate.source.expected_module->as_str()) {
                auto actual = frontend_result.provided.is_some()
                                  ? frontend_result.provided->logical_name.as_str()
                                  : "<none>"_str;
                return discovery_failure<Vec<ResolvedPackageSources>>(
                    rstd::format("module convention expected '{}' to provide '{}', but "
                                 "native preprocessing reported '{}'",
                                 candidate.source.canonical_path.as_path(),
                                 candidate.source.expected_module->as_str(),
                                 actual));
            }
            if (candidate.source.expected_module->as_str() ==
                    target.manifest.root_module->as_str() &&
                ! frontend_result.provided->is_interface) {
                return discovery_failure<Vec<ResolvedPackageSources>>(
                    rstd::format("primary module source '{}' is not an interface",
                                 candidate.source.canonical_path.as_path()));
            }
        }

        for (const auto& imported : frontend_result.imports) {
            auto owner =
                import_owner(package, plan, candidate.target, imported.logical_name.as_str());
            if (owner.is_err()) return Err(rstd::move(owner).unwrap_err());
            if (owner->is_none()) continue;
            if (package.targets[**owner].manifest.discovery == SourceDiscoveryMode::Explicit) {
                continue;
            }
            auto nested =
                module_source(package.targets[**owner].manifest, imported.logical_name.as_str());
            if (nested.is_err()) return Err(rstd::move(nested).unwrap_err());
            auto enqueued = enqueue_candidate(
                **owner, rstd::move(nested).unwrap(), path_names, name_paths, queued, queue);
            if (enqueued.is_err()) return Err(rstd::move(enqueued).unwrap_err());
        }
        auto companion = module_companion_source(target.manifest, candidate.source);
        if (companion.is_err()) return Err(rstd::move(companion).unwrap_err());
        if (companion->is_some()) {
            auto enqueued = enqueue_candidate(candidate.target,
                                              rstd::move(companion).unwrap().unwrap(),
                                              path_names,
                                              name_paths,
                                              queued,
                                              queue);
            if (enqueued.is_err()) return Err(rstd::move(enqueued).unwrap_err());
        }
        candidate.source.frontend_analysis = Some(rstd::move(frontend_analysis));
        discovered[candidate.target].push(rstd::move(candidate.source));
    }

    auto result = Vec<ResolvedPackageSources>::with_capacity(plan.target_order.len());
    for (auto target : plan.target_order) {
        auto sorted = sort_sources(rstd::move(discovered[target]));
        if (sorted.is_err()) return Err(rstd::move(sorted).unwrap_err());
        result.push(ResolvedPackageSources {
            .package_name = package.targets[target].manifest.name.clone(),
            .sources      = ResolvedSourceSet { .sources = rstd::move(sorted).unwrap() },
        });
    }
    return Ok(rstd::move(result));
}

} // namespace tenon
