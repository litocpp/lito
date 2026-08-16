module lito.driver;

import rstd;
import lito.core;
import lito.cpp;
import :build.event;
import :build.frontend_analysis;
import :build.package_discovery;
import lito.frontend;
import :build.profiling;
import :build.scan_executor;
import :build.error;

using namespace rstd::prelude;
using namespace rstd::literals;
using StringSet = rstd::collections::BTreeMap<String, empty>;
using StringMap = rstd::collections::BTreeMap<String, String>;

namespace lito
{

template<typename T>
auto discovery_failure(String message) -> cpp::SourceDiscoveryResult<T> {
    return Err(cpp::SourceDiscoveryError::Message(rstd::move(message)));
}

template<typename T>
auto discovery_io_failure(ref<str>               operation,
                          ref<rstd::path::Path>  path,
                          rstd::io::error::Error source) -> cpp::SourceDiscoveryResult<T> {
    return Err(cpp::SourceDiscoveryError::Io(
        String::make(operation), PathBuf::from(path), rstd::move(source)));
}

auto path_text(ref<rstd::path::Path> path) -> cpp::SourceDiscoveryResult<String> {
    auto text = path.to_str();
    if (text.is_none()) {
        return discovery_failure<String>(rstd::format("source path '{}' is not valid UTF-8", path));
    }
    return Ok(String::make(*text));
}

struct SourceEntry {
    String              key;
    cpp::ResolvedSource source;
};

auto resolve_declared_source(ref<rstd::path::Path> source_root, ref<rstd::path::Path> declared)
    -> cpp::SourceDiscoveryResult<cpp::ResolvedSource> {
    auto requested = PathBuf::from(source_root).join(declared);
    auto canonical = rstd::fs::canonicalize(requested.as_path());
    if (canonical.is_err()) {
        return discovery_io_failure<cpp::ResolvedSource>(
            "resolve declared"_str, requested.as_path(), rstd::move(canonical).unwrap_err());
    }
    auto resolved = rstd::move(canonical).unwrap();
    auto relative = resolved.as_path().strip_prefix(source_root);
    if (relative.is_none() || (*relative).is_empty()) {
        return discovery_failure<cpp::ResolvedSource>(
            rstd::format("declared source '{}' resolves outside package source root '{}'",
                         declared,
                         source_root));
    }
    auto metadata = rstd::fs::metadata(resolved.as_path());
    if (metadata.is_err()) {
        return discovery_io_failure<cpp::ResolvedSource>(
            "inspect declared"_str, resolved.as_path(), rstd::move(metadata).unwrap_err());
    }
    if (! metadata->is_file()) {
        return discovery_failure<cpp::ResolvedSource>(
            rstd::format("declared source '{}' is not a file", declared));
    }
    if (! supported_manifest_source(resolved.as_path())) {
        return discovery_failure<cpp::ResolvedSource>(
            rstd::format("unsupported C++ source extension: {}", declared));
    }
    return Ok(cpp::ResolvedSource {
        .relative_path  = PathBuf::from(*relative),
        .canonical_path = rstd::move(resolved),
        .origin         = cpp::SourceOrigin::Explicit,
    });
}

auto collect_format_directory(ref<rstd::path::Path> source_root,
                              ref<rstd::path::Path> directory,
                              Vec<SourceEntry>&     entries) -> cpp::SourceDiscoveryResult<empty> {
    auto opened = rstd::fs::read_dir(directory);
    if (opened.is_err()) {
        return discovery_io_failure<empty>(
            "enumerate"_str, directory, rstd::move(opened).unwrap_err());
    }
    auto stream = rstd::move(opened).unwrap();
    for (auto next = stream.next(); next.is_some(); next = stream.next()) {
        auto item = rstd::move(next).unwrap();
        if (item.is_err()) {
            return discovery_io_failure<empty>(
                "enumerate"_str, directory, rstd::move(item).unwrap_err());
        }
        auto entry = rstd::move(item).unwrap();
        auto type  = entry.file_type();
        if (type.is_err()) {
            auto path = entry.path();
            return discovery_io_failure<empty>(
                "inspect entry"_str, path.as_path(), rstd::move(type).unwrap_err());
        }
        auto path = entry.path();
        if (type->is_dir()) {
            auto nested = collect_format_directory(source_root, path.as_path(), entries);
            if (nested.is_err()) return nested;
            continue;
        }
        if (! type->is_file() || ! supported_manifest_source(path.as_path())) continue;
        auto canonical = rstd::fs::canonicalize(path.as_path());
        if (canonical.is_err()) {
            return discovery_io_failure<empty>(
                "resolve candidate"_str, path.as_path(), rstd::move(canonical).unwrap_err());
        }
        auto resolved = rstd::move(canonical).unwrap();
        auto relative = resolved.as_path().strip_prefix(source_root);
        if (relative.is_none() || (*relative).is_empty()) {
            return discovery_failure<empty>(rstd::format(
                "source candidate '{}' resolves outside package root", path.as_path()));
        }
        auto key = path_text(*relative);
        if (key.is_err()) return Err(rstd::move(key).unwrap_err());
        entries.push(SourceEntry {
            .key = rstd::move(key).unwrap(),
            .source =
                cpp::ResolvedSource {
                    .relative_path  = PathBuf::from(*relative),
                    .canonical_path = rstd::move(resolved),
                    .origin         = cpp::SourceOrigin::Convention,
                },
        });
    }
    return Ok(empty {});
}

auto sort_sources(Vec<cpp::ResolvedSource> sources)
    -> cpp::SourceDiscoveryResult<Vec<cpp::ResolvedSource>> {
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
    auto result = Vec<cpp::ResolvedSource>::with_capacity(entries.len());
    for (auto& entry : entries) result.push(rstd::move(entry.source));
    return Ok(rstd::move(result));
}

struct DiscoveryCandidate {
    cpp::TargetId       target {};
    String              key;
    cpp::ResolvedSource source;
    bool                expand_imports { true };
};

auto source_key(ref<rstd::path::Path> path) -> cpp::SourceDiscoveryResult<String> {
    auto text = path.to_str();
    if (text.is_none()) {
        return discovery_failure<String>(
            rstd::format("module source '{}' is not valid UTF-8", path));
    }
    return Ok(String::make(*text));
}

auto enqueue_candidate(cpp::TargetId            target,
                       cpp::ResolvedSource      source,
                       bool                     expand_imports,
                       StringMap&               path_names,
                       StringMap&               name_paths,
                       StringSet&               queued,
                       Vec<DiscoveryCandidate>& queue) -> cpp::SourceDiscoveryResult<empty> {
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

struct ImportOwner {
    cpp::TargetId       target {};
    cpp::ResolvedSource source;
};

struct ImportOwnerCandidate {
    cpp::TargetId       target {};
    usize               priority {};
    cpp::ResolvedSource source;
};

auto discovery_compile_context(const cpp::PackageMetadata&     package,
                               const cpp::SourceDiscoveryPlan& plan,
                               cpp::TargetId                   target,
                               ref<rstd::path::Path>           relative_source)
    -> BuildResult<Option<cpp::CompileContext>> {
    const auto& resolved_target = package.targets[target];
    if (resolved_target.artifact_kind != cpp::ArtifactKind::CompileTest) return Ok(None());
    const cpp::ResolvedCompileTestCase* selected = nullptr;
    for (const auto& test : resolved_target.compile_tests) {
        if (test.source.as_path() == relative_source) {
            selected = rstd::addressof(test);
            break;
        }
    }
    if (selected == nullptr) {
        return Err(BuildError::Discovery(cpp::SourceDiscoveryError::Message(
            rstd::format("compile-test package '{}' has no case for source '{}'",
                         resolved_target.id.package.as_str(),
                         relative_source))));
    }
    auto context = compile_test_context(plan.contexts[target], *selected);
    if (context.is_err()) return Err(rstd::into<BuildError>(rstd::move(context).unwrap_err()));
    return Ok(Some(rstd::move(context).unwrap()));
}

auto convention_import_owner(const cpp::PackageMetadata&     package,
                             const cpp::SourceDiscoveryPlan& plan,
                             cpp::TargetId                   importer,
                             ref<str>                        logical_name,
                             FrontendAnalysisService& analysis_service)
    -> BuildResult<Option<ImportOwner>> {
    auto candidates       = Vec<ImportOwnerCandidate>::make();
    auto highest_priority = usize {};
    for (auto visible : plan.visible_targets[importer]) {
        const auto& target = package.targets[visible];
        if (target.source.discovery == SourceDiscoveryMode::Explicit) continue;
        auto exists = cpp::module_source_exists(target, logical_name);
        if (exists.is_err()) {
            return Err(BuildError::Discovery(cpp::SourceDiscoveryError::Message(
                rstd::format("cannot inspect owner of module '{}': {}",
                             logical_name,
                             rstd::move(exists).unwrap_err()))));
        }
        if (! *exists) continue;
        const auto& module = package.targets[visible].source.module;
        auto priority = module.is_some() && cpp::module_name_belongs(module->as_str(), logical_name)
                            ? module->size() + usize(1)
                            : usize {};
        auto source   = cpp::module_source(package.targets[visible], logical_name);
        if (source.is_err()) {
            return Err(rstd::into<BuildError>(rstd::move(source).unwrap_err()));
        }
        if (priority > highest_priority) highest_priority = priority;
        candidates.push(ImportOwnerCandidate {
            .target   = visible,
            .priority = priority,
            .source   = rstd::move(source).unwrap(),
        });
    }
    auto matching = Vec<usize>::make();
    for (usize index {}; index < candidates.len(); ++index) {
        if (candidates[index].priority == highest_priority) matching.emplace_back(index);
    }
    if (matching.is_empty()) return Ok(None());
    if (matching.len() == usize(1)) {
        auto index = matching[usize {}];
        return Ok(Some(ImportOwner {
            .target = candidates[index].target,
            .source = rstd::move(candidates[index].source),
        }));
    }

    auto selected = Option<usize> {};
    for (auto index : matching) {
        auto& candidate = candidates[index];
        auto  context   = discovery_compile_context(
            package, plan, candidate.target, candidate.source.relative_path.as_path());
        if (context.is_err()) return Err(rstd::move(context).unwrap_err());
        const auto& compile_context =
            context->is_some() ? **context : plan.contexts[candidate.target];
        auto analysis =
            analysis_service.analyze(package.targets[candidate.target].id,
                                     candidate.source.relative_path.as_path(),
                                     candidate.source.canonical_path.as_path(),
                                     compile_context,
                                     package.targets[candidate.target].source_root.as_path());
        if (analysis.is_err()) return Err(rstd::move(analysis).unwrap_err());
        auto value = rstd::move(analysis).unwrap();
        if (value.result.provided.is_none() ||
            value.result.provided->logical_name.as_str() != logical_name) {
            continue;
        }
        if (selected.is_some()) {
            return Err(BuildError::Discovery(cpp::SourceDiscoveryError::Message(rstd::format(
                "module import '{}' is provided by both '{}' and '{}'",
                logical_name,
                package_target_id_text(package.targets[candidates[*selected].target].id).as_str(),
                package_target_id_text(package.targets[candidate.target].id).as_str()))));
        }
        candidate.source.frontend_analysis = Some(rstd::move(value));
        selected                           = Some(index);
    }
    if (selected.is_none()) return Ok(None());
    auto index = *selected;
    return Ok(Some(ImportOwner {
        .target = candidates[index].target,
        .source = rstd::move(candidates[index].source),
    }));
}

} // namespace lito

namespace lito
{

static auto discover_explicit_sources_impl(ref<rstd::path::Path>       source_root,
                                           const TargetSourceManifest& source_manifest)
    -> cpp::SourceDiscoveryResult<cpp::ResolvedSourceSet> {
    auto       seen    = StringSet::make();
    auto       entries = Vec<SourceEntry>::make();
    const auto append  = [&](const PathBuf& declared) -> cpp::SourceDiscoveryResult<empty> {
        auto resolved = resolve_declared_source(source_root, declared.as_path());
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
    for (const auto& declared : source_manifest.declared_sources) {
        auto appended = append(declared);
        if (appended.is_err()) return Err(rstd::move(appended).unwrap_err());
    }
    for (const auto& group : source_manifest.conditional_source_groups) {
        for (const auto& declared : group.sources) {
            auto appended = append(declared);
            if (appended.is_err()) return Err(rstd::move(appended).unwrap_err());
        }
    }
    rstd::slice_::sort_unstable_by(entries.as_mut_slice().as_mut_ref(),
                                   [](const SourceEntry& left, const SourceEntry& right) {
                                       return left.key < right.key;
                                   });
    auto sources = Vec<cpp::ResolvedSource>::with_capacity(entries.len());
    for (auto& entry : entries) sources.push(rstd::move(entry.source));
    return Ok(cpp::ResolvedSourceSet { .sources = rstd::move(sources) });
}

} // namespace lito

namespace lito
{

auto discover_explicit_sources(const cpp::ResolvedTarget& target)
    -> cpp::SourceDiscoveryResult<cpp::ResolvedSourceSet> {
    return discover_explicit_sources_impl(target.source_root.as_path(), target.source);
}

auto discover_format_sources(const PackageManifest& manifest)
    -> cpp::SourceDiscoveryResult<cpp::ResolvedSourceSet> {
    auto entries          = Vec<SourceEntry>::make();
    auto module_discovery = false;
    for (const auto& target : manifest.targets) {
        if (package_target_source(target).discovery == SourceDiscoveryMode::Module) {
            module_discovery = true;
            break;
        }
    }
    if (module_discovery) {
        auto source_directory = manifest.source_root.join(PathBuf::from("src"_str).as_path());
        auto collected        = collect_format_directory(
            manifest.source_root.as_path(), source_directory.as_path(), entries);
        if (collected.is_err()) return Err(rstd::move(collected).unwrap_err());
    }
    auto seen = StringSet::make();
    for (const auto& entry : entries) seen.insert(entry.key.clone(), empty {});
    const auto append = [&](const PathBuf& declared) -> cpp::SourceDiscoveryResult<empty> {
        auto resolved = resolve_declared_source(manifest.source_root.as_path(), declared.as_path());
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
    for (const auto& target : manifest.targets) {
        const auto& source = package_target_source(target);
        for (const auto& declared : source.declared_sources) {
            auto appended = append(declared);
            if (appended.is_err()) return Err(rstd::move(appended).unwrap_err());
        }
        for (const auto& group : source.conditional_source_groups) {
            for (const auto& declared : group.sources) {
                auto appended = append(declared);
                if (appended.is_err()) return Err(rstd::move(appended).unwrap_err());
            }
        }
        auto attachments = package_target_attachments(target);
        if (attachments.is_none()) continue;
        for (const auto& attachment : **attachments) {
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
    for (const auto& test : manifest.compile_tests) {
        auto appended = append(test.source);
        if (appended.is_err()) return Err(rstd::move(appended).unwrap_err());
    }
    rstd::slice_::sort_unstable_by(entries.as_mut_slice().as_mut_ref(),
                                   [](const SourceEntry& left, const SourceEntry& right) {
                                       return left.key < right.key;
                                   });
    auto sources = Vec<cpp::ResolvedSource>::with_capacity(entries.len());
    for (auto& entry : entries) sources.push(rstd::move(entry.source));
    return Ok(cpp::ResolvedSourceSet { .sources = rstd::move(sources) });
}

auto resolve_source_target(const cpp::PackageMetadata&     package,
                           const cpp::SourceDiscoveryPlan& discovery,
                           ref<rstd::path::Path>           source)
    -> cpp::SourceDiscoveryResult<cpp::TargetId> {
    auto exact = Option<cpp::TargetId> {};
    for (auto target : discovery.target_order) {
        const auto& resolved_target = package.targets[target];
        if (resolved_target.source.discovery != SourceDiscoveryMode::Explicit) continue;
        auto discovered = discover_explicit_sources(resolved_target);
        if (discovered.is_err()) return Err(rstd::move(discovered).unwrap_err());
        for (const auto& candidate : discovered->sources) {
            if (candidate.canonical_path.as_path() != source) continue;
            if (exact.is_some() && *exact != target) {
                return discovery_failure<cpp::TargetId>(
                    rstd::format("source '{}' belongs to multiple selected targets", source));
            }
            exact = Some(target);
        }
    }
    if (exact.is_some()) return Ok(*exact);

    auto selected             = Option<cpp::TargetId> {};
    auto selected_root_length = usize {};
    for (auto target : discovery.target_order) {
        if (package.targets[target].test_attachment.is_some()) continue;
        const auto root = package.targets[target].source_root.as_path();
        if (source.strip_prefix(root).is_none()) continue;
        auto root_length = root.as_os_str().as_encoded_bytes().len();
        if (selected.is_some() && root_length == selected_root_length) {
            return discovery_failure<cpp::TargetId>(
                rstd::format("source '{}' belongs to multiple selected targets", source));
        }
        if (selected.is_none() || root_length > selected_root_length) {
            selected             = Some(target);
            selected_root_length = root_length;
        }
    }
    if (selected.is_none()) {
        return discovery_failure<cpp::TargetId>(
            rstd::format("source '{}' is outside the selected package targets", source));
    }
    auto relative = source.strip_prefix(package.targets[*selected].source_root.as_path());
    if (relative.is_none() || relative->is_empty()) {
        return discovery_failure<cpp::TargetId>(
            rstd::format("source '{}' does not name a file inside target '{}'",
                         source,
                         package_target_id_text(package.targets[*selected].id).as_str()));
    }
    return Ok(*selected);
}

} // namespace lito

namespace lito
{

auto discover_sources(const cpp::PackageMetadata&     package,
                      const cpp::SourceDiscoveryPlan& plan,
                      FrontendAnalysisService&        analysis_service,
                      const Option<BuildEventSink>&   observer,
                      usize                           jobs,
                      usize max_in_flight) -> BuildResult<Vec<cpp::ResolvedTargetSources>> {
    auto discovered = Vec<Vec<cpp::ResolvedSource>>::with_capacity(package.targets.len());
    for (auto target = cpp::TargetId {}; target < package.targets.len(); ++target) {
        discovered.emplace_back();
    }
    auto queue      = Vec<DiscoveryCandidate>::make();
    auto path_names = StringMap::make();
    auto name_paths = StringMap::make();
    auto queued     = StringSet::make();

    for (auto target : plan.target_order) {
        const auto& resolved_target = package.targets[target];
        if (resolved_target.source.discovery == SourceDiscoveryMode::Explicit) {
            auto explicit_sources = discover_explicit_sources(resolved_target);
            if (explicit_sources.is_err()) {
                return Err(rstd::into<BuildError>(rstd::move(explicit_sources).unwrap_err()));
            }
            auto sources = rstd::move(explicit_sources).unwrap().sources;
            for (auto& source : sources) {
                auto enqueued = enqueue_candidate(
                    target, rstd::move(source), false, path_names, name_paths, queued, queue);
                if (enqueued.is_err()) {
                    return Err(rstd::into<BuildError>(rstd::move(enqueued).unwrap_err()));
                }
            }
            continue;
        }
        auto entry = cpp::module_entry_source(resolved_target);
        if (entry.is_err()) {
            return Err(rstd::into<BuildError>(rstd::move(entry).unwrap_err()));
        }
        auto enqueued = enqueue_candidate(
            target, rstd::move(entry).unwrap(), true, path_names, name_paths, queued, queue);
        if (enqueued.is_err()) {
            return Err(rstd::into<BuildError>(rstd::move(enqueued).unwrap_err()));
        }
        for (const auto& declared : resolved_target.source.declared_sources) {
            auto resolved =
                resolve_declared_source(resolved_target.source_root.as_path(), declared.as_path());
            if (resolved.is_err()) {
                return Err(rstd::into<BuildError>(rstd::move(resolved).unwrap_err()));
            }
            enqueued = enqueue_candidate(
                target, rstd::move(resolved).unwrap(), true, path_names, name_paths, queued, queue);
            if (enqueued.is_err()) {
                return Err(rstd::into<BuildError>(rstd::move(enqueued).unwrap_err()));
            }
        }
    }

    auto executor = FrontendScanExecution::create(jobs, max_in_flight);
    if (executor.is_err()) {
        return Err(rstd::move(executor).unwrap_err());
    }
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
        auto prepared = Vec<Option<FrontendAnalysisTask>>::with_capacity(queue.len());
        auto pending  = usize {};
        for (const auto& candidate : queue) {
            const auto& target = package.targets[candidate.target];
            if (candidate.source.frontend_analysis.is_some()) {
                prepared.push(None());
                continue;
            }
            auto resolved_context = discovery_compile_context(
                package, plan, candidate.target, candidate.source.relative_path.as_path());
            if (resolved_context.is_err()) return Err(rstd::move(resolved_context).unwrap_err());
            const auto& context =
                resolved_context->is_some() ? **resolved_context : plan.contexts[candidate.target];
            auto task = analysis_service.prepare(target.id,
                                                 candidate.source.relative_path.as_path(),
                                                 candidate.source.canonical_path.as_path(),
                                                 context,
                                                 target.source_root.as_path(),
                                                 ScanSourceOrigin::Discovery);
            if (task.is_err()) {
                return Err(rstd::move(task).unwrap_err());
            }
            prepared.push(Some(rstd::move(task).unwrap()));
            ++pending;
        }
        if (pending != usize {}) {
            auto prepared_executor = scan_executor.prepare(pending);
            if (prepared_executor.is_err()) {
                return Err(rstd::move(prepared_executor).unwrap_err());
            }
        }
        auto outcomes =
            Vec<Option<BuildResult<FrontendAnalysisTaskOutcome>>>::with_capacity(queue.len());
        for (auto index = usize {}; index < queue.len(); ++index) outcomes.push(None());
        auto submitted = usize {};
        auto completed = usize {};
        auto active    = usize {};
        while (completed < pending) {
            auto frontier_capacity = max_in_flight < pending ? max_in_flight : pending;
            while (submitted < queue.len() && active < frontier_capacity) {
                if (prepared[submitted].is_none()) {
                    ++submitted;
                    continue;
                }
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
            auto        candidate         = rstd::move(queue[index]);
            const auto& target            = package.targets[candidate.target];
            auto        frontend_analysis = frontend::FrontendAnalysis {};
            if (candidate.source.frontend_analysis.is_some()) {
                frontend_analysis = rstd::move(candidate.source.frontend_analysis).unwrap();
            } else {
                auto task_outcome = rstd::move(outcomes[index]).unwrap();
                if (task_outcome.is_err()) {
                    return Err(rstd::move(task_outcome).unwrap_err());
                }
                auto facts = analysis_service.commit(rstd::move(task_outcome).unwrap());
                if (facts.is_err()) return Err(rstd::move(facts).unwrap_err());
                frontend_analysis = rstd::move(facts).unwrap();
            }
            auto target_identity = package_target_id_text(target.id);
            if (observer.is_some() && observer->notify != nullptr) {
                auto kind =
                    frontend_analysis.origin == frontend::FrontendAnalysisOrigin::PersistentCache
                        ? BuildEventKind::ScanReuse
                        : BuildEventKind::Scan;
                observer->notify(observer->context,
                                 BuildEvent { kind,
                                              target_identity.as_str(),
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
                    auto failed = discovery_failure<Vec<cpp::ResolvedTargetSources>>(
                        rstd::format("module convention expected '{}' to provide '{}', but "
                                     "native preprocessing reported '{}'",
                                     candidate.source.canonical_path.as_path(),
                                     candidate.source.expected_module->as_str(),
                                     actual));
                    return Err(rstd::into<BuildError>(rstd::move(failed).unwrap_err()));
                }
                if (candidate.source.expected_module->as_str() == target.source.module->as_str() &&
                    ! frontend_result.provided->is_interface) {
                    auto failed = discovery_failure<Vec<cpp::ResolvedTargetSources>>(
                        rstd::format("primary module source '{}' is not an interface",
                                     candidate.source.canonical_path.as_path()));
                    return Err(rstd::into<BuildError>(rstd::move(failed).unwrap_err()));
                }
            }

            if (candidate.expand_imports) {
                for (const auto& imported : frontend_result.imports) {
                    auto owner = convention_import_owner(package,
                                                         plan,
                                                         candidate.target,
                                                         imported.logical_name.as_str(),
                                                         analysis_service);
                    if (owner.is_err()) {
                        return Err(rstd::move(owner).unwrap_err());
                    }
                    if (owner->is_none()) continue;
                    auto resolved_owner = rstd::move(owner).unwrap().unwrap();
                    auto enqueued = enqueue_candidate(resolved_owner.target,
                                                      rstd::move(resolved_owner.source),
                                                      true,
                                                      path_names,
                                                      name_paths,
                                                      queued,
                                                      next);
                    if (enqueued.is_err()) {
                        return Err(rstd::into<BuildError>(rstd::move(enqueued).unwrap_err()));
                    }
                }
                auto companion = cpp::module_companion_source(target, candidate.source);
                if (companion.is_err()) {
                    return Err(rstd::into<BuildError>(rstd::move(companion).unwrap_err()));
                }
                if (companion->is_some()) {
                    auto enqueued = enqueue_candidate(candidate.target,
                                                      rstd::move(companion).unwrap().unwrap(),
                                                      true,
                                                      path_names,
                                                      name_paths,
                                                      queued,
                                                      next);
                    if (enqueued.is_err()) {
                        return Err(rstd::into<BuildError>(rstd::move(enqueued).unwrap_err()));
                    }
                }
            }
            candidate.source.frontend_analysis = Some(rstd::move(frontend_analysis));
            discovered[candidate.target].push(rstd::move(candidate.source));
        }
        queue = rstd::move(next);
    }
    scan_executor.finish();
    analysis_service.profiler().record_execution(scan_executor.statistics());

    auto result = Vec<cpp::ResolvedTargetSources>::with_capacity(plan.target_order.len());
    for (auto target : plan.target_order) {
        auto sorted = sort_sources(rstd::move(discovered[target]));
        if (sorted.is_err()) {
            return Err(rstd::into<BuildError>(rstd::move(sorted).unwrap_err()));
        }
        result.push(cpp::ResolvedTargetSources {
            .target  = package.targets[target].id.clone(),
            .sources = cpp::ResolvedSourceSet { .sources = rstd::move(sorted).unwrap() },
        });
    }
    return Ok(rstd::move(result));
}

} // namespace lito

namespace lito
{

auto discover_package_sources(const cpp::PackageMetadata&     package,
                              const cpp::SourceDiscoveryPlan& plan,
                              FrontendAnalysisService&        analysis_service,
                              const Option<BuildEventSink>&   observer,
                              usize                           jobs,
                              usize max_in_flight) -> BuildResult<Vec<cpp::ResolvedTargetSources>> {
    return discover_sources(package, plan, analysis_service, observer, jobs, max_in_flight);
}

} // namespace lito
