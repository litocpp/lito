export module lito.source_discovery;

import rstd;
import lito.model;
import lito.modules;
import lito.frontend_analysis;
import lito.frontend;
import lito.profiling;
import lito.scan_executor;
import lito.package;

using namespace rstd::prelude;
using namespace rstd::literals;
using StringSet = rstd::collections::BTreeMap<String, empty>;
using StringMap = rstd::collections::BTreeMap<String, String>;

namespace lito
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
    auto requested = manifest.source_root.join(declared);
    auto canonical = rstd::fs::canonicalize(requested.as_path());
    if (canonical.is_err()) {
        return discovery_failure<ResolvedSource>(
            rstd::format("cannot resolve declared source '{}': {}",
                         declared,
                         rstd::move(canonical).unwrap_err()));
    }
    auto resolved = rstd::move(canonical).unwrap();
    auto relative = resolved.as_path().strip_prefix(manifest.source_root.as_path());
    if (relative.is_none() || (*relative).is_empty()) {
        return discovery_failure<ResolvedSource>(
            rstd::format("declared source '{}' resolves outside package source root '{}'",
                         declared,
                         manifest.source_root.as_path()));
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
        auto relative = resolved.as_path().strip_prefix(manifest.source_root.as_path());
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
    String         key;
    ResolvedSource source;
    bool           expand_imports { true };
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
                       bool                     expand_imports,
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
    auto work_key = rstd::format("{}:{}", target, path.as_str());
    if (queued.contains_key(work_key.as_str())) return Ok(empty {});
    queued.insert(rstd::move(work_key), empty {});
    queue.push(DiscoveryCandidate {
        .target         = target,
        .key            = rstd::move(path),
        .source         = rstd::move(source),
        .expand_imports = expand_imports,
    });
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

} // namespace lito

namespace lito
{

static auto discover_explicit_sources_impl(const PackageManifest& manifest,
                                           bool                   include_test_attachments)
    -> Result<ResolvedSourceSet> {
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
    if (include_test_attachments) {
        for (const auto& attachment : manifest.test_attachments) {
            for (const auto& declared : attachment.sources) {
                auto appended = append(declared);
                if (appended.is_err()) return Err(rstd::move(appended).unwrap_err());
            }
            for (const auto& group : attachment.conditional_source_groups) {
                for (const auto& declared : group.sources) {
                    auto appended = append(declared);
                    if (appended.is_err()) return Err(rstd::move(appended).unwrap_err());
                }
            }
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

} // namespace lito

export namespace lito
{

auto discover_explicit_sources(const PackageManifest& manifest) -> Result<ResolvedSourceSet> {
    return discover_explicit_sources_impl(manifest, false);
}

auto discover_format_sources(const PackageManifest& manifest) -> Result<ResolvedSourceSet> {
    if (manifest.discovery == SourceDiscoveryMode::Explicit) {
        return discover_explicit_sources_impl(manifest, true);
    }
    auto source_root = manifest.source_root.join(PathBuf::from("src"_str).as_path());
    auto entries     = Vec<SourceEntry>::make();
    auto collected   = collect_format_directory(manifest, source_root.as_path(), entries);
    if (collected.is_err()) return Err(rstd::move(collected).unwrap_err());
    auto seen = StringSet::make();
    for (const auto& entry : entries) seen.insert(entry.key.clone(), empty {});
    const auto append_attachment = [&](const PathBuf& declared) -> Result<empty> {
        auto resolved = resolve_declared_source(manifest, declared.as_path());
        if (resolved.is_err()) return Err(rstd::move(resolved).unwrap_err());
        auto source = rstd::move(resolved).unwrap();
        auto key    = path_text(source.relative_path.as_path());
        if (key.is_err()) return Err(rstd::move(key).unwrap_err());
        auto source_key = rstd::move(key).unwrap();
        if (seen.contains_key(source_key.as_str())) return Ok(empty {});
        seen.insert(source_key.clone(), empty {});
        entries.push(SourceEntry { .key = rstd::move(source_key), .source = rstd::move(source) });
        return Ok(empty {});
    };
    for (const auto& attachment : manifest.test_attachments) {
        for (const auto& declared : attachment.sources) {
            auto appended = append_attachment(declared);
            if (appended.is_err()) return Err(rstd::move(appended).unwrap_err());
        }
        for (const auto& group : attachment.conditional_source_groups) {
            for (const auto& declared : group.sources) {
                auto appended = append_attachment(declared);
                if (appended.is_err()) return Err(rstd::move(appended).unwrap_err());
            }
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

auto resolve_source_target(const PackageMetadata&     package,
                           const SourceDiscoveryPlan& discovery,
                           ref<rstd::path::Path>      source) -> Result<TargetId> {
    auto exact = Option<TargetId> {};
    for (auto target : discovery.target_order) {
        const auto& manifest = package.targets[target].manifest;
        if (manifest.discovery != SourceDiscoveryMode::Explicit) continue;
        auto discovered = discover_explicit_sources(manifest);
        if (discovered.is_err()) return Err(rstd::move(discovered).unwrap_err());
        for (const auto& candidate : discovered->sources) {
            if (candidate.canonical_path.as_path() != source) continue;
            if (exact.is_some() && *exact != target) {
                return discovery_failure<TargetId>(
                    rstd::format("source '{}' belongs to multiple selected targets", source));
            }
            exact = Some(target);
        }
    }
    if (exact.is_some()) return Ok(*exact);

    auto selected             = Option<TargetId> {};
    auto selected_root_length = usize {};
    for (auto target : discovery.target_order) {
        if (package.targets[target].test_attachment.is_some()) continue;
        const auto root = package.targets[target].manifest.source_root.as_path();
        if (source.strip_prefix(root).is_none()) continue;
        auto root_length = root.as_os_str().as_encoded_bytes().len();
        if (selected.is_some() && root_length == selected_root_length) {
            return discovery_failure<TargetId>(
                rstd::format("source '{}' belongs to multiple selected targets", source));
        }
        if (selected.is_none() || root_length > selected_root_length) {
            selected             = Some(target);
            selected_root_length = root_length;
        }
    }
    if (selected.is_none()) {
        return discovery_failure<TargetId>(
            rstd::format("source '{}' is outside the selected package targets", source));
    }
    auto relative = source.strip_prefix(package.targets[*selected].manifest.source_root.as_path());
    if (relative.is_none() || relative->is_empty()) {
        return discovery_failure<TargetId>(
            rstd::format("source '{}' does not name a file inside target '{}'",
                         source,
                         package.targets[*selected].manifest.name.as_str()));
    }
    return Ok(*selected);
}

} // namespace lito

namespace lito
{

auto discover_sources(const PackageMetadata&       package,
                      const SourceDiscoveryPlan&   plan,
                      FrontendAnalysisService&     analysis_service,
                      const Option<BuildObserver>& observer,
                      usize                        jobs,
                      usize max_in_flight) -> Result<Vec<ResolvedPackageSources>> {
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
            auto sources = rstd::move(explicit_sources).unwrap().sources;
            for (auto& source : sources) {
                auto enqueued = enqueue_candidate(
                    target, rstd::move(source), false, path_names, name_paths, queued, queue);
                if (enqueued.is_err()) return Err(rstd::move(enqueued).unwrap_err());
            }
            continue;
        }
        auto entry = module_entry_source(manifest);
        if (entry.is_err()) return Err(rstd::move(entry).unwrap_err());
        auto enqueued = enqueue_candidate(
            target, rstd::move(entry).unwrap(), true, path_names, name_paths, queued, queue);
        if (enqueued.is_err()) return Err(rstd::move(enqueued).unwrap_err());
        for (const auto& declared : manifest.declared_sources) {
            auto resolved = resolve_declared_source(manifest, declared.as_path());
            if (resolved.is_err()) return Err(rstd::move(resolved).unwrap_err());
            enqueued = enqueue_candidate(
                target, rstd::move(resolved).unwrap(), true, path_names, name_paths, queued, queue);
            if (enqueued.is_err()) return Err(rstd::move(enqueued).unwrap_err());
        }
    }

    auto executor = FrontendScanExecution::create(jobs, max_in_flight);
    if (executor.is_err()) return Err(rstd::move(executor).unwrap_err());
    auto scan_executor = rstd::move(executor).unwrap();
    while (! queue.is_empty()) {
        rstd::slice_::sort_unstable_by(
            queue.as_mut_slice().as_mut_ref(),
            [](const DiscoveryCandidate& left, const DiscoveryCandidate& right) {
                if (left.key.as_str() != right.key.as_str()) {
                    return left.key < right.key;
                }
                return left.target < right.target;
            });
        auto prepared_executor = scan_executor.prepare(queue.len());
        if (prepared_executor.is_err()) {
            return Err(rstd::move(prepared_executor).unwrap_err());
        }
        auto prepared = Vec<Option<FrontendAnalysisTask>>::with_capacity(queue.len());
        for (const auto& candidate : queue) {
            const auto& target  = package.targets[candidate.target];
            auto        context = static_cast<const CompileContext*>(
                rstd::addressof(plan.contexts[candidate.target]));
            auto compile_test_context_value = Option<CompileContext> {};
            if (target.manifest.artifact_kind == ArtifactKind::CompileTest) {
                const auto* selected = static_cast<const CompileTestCase*>(nullptr);
                for (const auto& test : target.manifest.compile_tests) {
                    if (test.source.as_path() == candidate.source.relative_path.as_path()) {
                        selected = rstd::addressof(test);
                        break;
                    }
                }
                if (selected == nullptr) {
                    return discovery_failure<Vec<ResolvedPackageSources>>(
                        rstd::format("compile-test package '{}' has no case for source '{}'",
                                     target.manifest.name.as_str(),
                                     candidate.source.relative_path.as_path()));
                }
                auto resolved_context = compile_test_context(*context, *selected);
                if (resolved_context.is_err()) {
                    return Err(rstd::move(resolved_context).unwrap_err());
                }
                compile_test_context_value = Some(rstd::move(resolved_context).unwrap());
                context                    = rstd::addressof(*compile_test_context_value);
            }
            auto task = analysis_service.prepare(target.manifest.name.as_str(),
                                                 candidate.source.relative_path.as_path(),
                                                 candidate.source.canonical_path.as_path(),
                                                 *context,
                                                 target.manifest.source_root.as_path(),
                                                 ScanSourceOrigin::Discovery);
            if (task.is_err()) return Err(rstd::move(task).unwrap_err());
            prepared.push(Some(rstd::move(task).unwrap()));
        }
        auto outcomes =
            Vec<Option<Result<FrontendAnalysisTaskOutcome>>>::with_capacity(queue.len());
        for (auto index = usize {}; index < queue.len(); ++index) outcomes.push(None());
        auto submitted = usize {};
        auto completed = usize {};
        auto active    = usize {};
        while (completed < queue.len()) {
            auto frontier_capacity = max_in_flight < queue.len() ? max_in_flight : queue.len();
            while (submitted < queue.len() && active < frontier_capacity) {
                auto submitted_task =
                    scan_executor.submit(submitted, rstd::move(prepared[submitted]).unwrap());
                if (submitted_task.is_err()) {
                    scan_executor.cancel();
                    return Err(rstd::move(submitted_task).unwrap_err());
                }
                ++submitted;
                ++active;
            }
            auto received = scan_executor.recv();
            if (received.is_err()) {
                scan_executor.cancel();
                return Err(rstd::move(received).unwrap_err());
            }
            auto completion           = rstd::move(received).unwrap();
            outcomes[completion.node] = Some(rstd::move(completion.outcome));
            --active;
            ++completed;
        }

        auto next = Vec<DiscoveryCandidate>::make();
        for (auto index = usize {}; index < queue.len(); ++index) {
            auto        candidate    = rstd::move(queue[index]);
            const auto& target       = package.targets[candidate.target];
            auto        task_outcome = rstd::move(outcomes[index]).unwrap();
            if (task_outcome.is_err()) {
                return Err(rstd::move(task_outcome).unwrap_err());
            }
            auto facts = analysis_service.commit(rstd::move(task_outcome).unwrap());
            if (facts.is_err()) return Err(rstd::move(facts).unwrap_err());
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

            if (candidate.expand_imports) {
                for (const auto& imported : frontend_result.imports) {
                    auto owner = import_owner(
                        package, plan, candidate.target, imported.logical_name.as_str());
                    if (owner.is_err()) return Err(rstd::move(owner).unwrap_err());
                    if (owner->is_none()) continue;
                    if (package.targets[**owner].manifest.discovery ==
                        SourceDiscoveryMode::Explicit) {
                        continue;
                    }
                    auto nested = module_source(package.targets[**owner].manifest,
                                                imported.logical_name.as_str());
                    if (nested.is_err()) return Err(rstd::move(nested).unwrap_err());
                    auto enqueued = enqueue_candidate(**owner,
                                                      rstd::move(nested).unwrap(),
                                                      true,
                                                      path_names,
                                                      name_paths,
                                                      queued,
                                                      next);
                    if (enqueued.is_err()) return Err(rstd::move(enqueued).unwrap_err());
                }
                auto companion = module_companion_source(target.manifest, candidate.source);
                if (companion.is_err()) return Err(rstd::move(companion).unwrap_err());
                if (companion->is_some()) {
                    auto enqueued = enqueue_candidate(candidate.target,
                                                      rstd::move(companion).unwrap().unwrap(),
                                                      true,
                                                      path_names,
                                                      name_paths,
                                                      queued,
                                                      next);
                    if (enqueued.is_err()) return Err(rstd::move(enqueued).unwrap_err());
                }
            }
            candidate.source.frontend_analysis = Some(rstd::move(frontend_analysis));
            discovered[candidate.target].push(rstd::move(candidate.source));
        }
        queue = rstd::move(next);
    }
    scan_executor.finish();
    analysis_service.profiler().record_execution(scan_executor.statistics());

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

} // namespace lito

export namespace lito
{

auto discover_package_sources(const PackageMetadata&       package,
                              const SourceDiscoveryPlan&   plan,
                              FrontendAnalysisService&     analysis_service,
                              const Option<BuildObserver>& observer,
                              usize                        jobs,
                              usize max_in_flight) -> Result<Vec<ResolvedPackageSources>> {
    return discover_sources(package, plan, analysis_service, observer, jobs, max_in_flight);
}

} // namespace lito
