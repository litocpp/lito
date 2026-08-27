module;
#include <rstd/macro.hpp>

module lito.driver;

import rstd;
import lito.core;
import lito.tools;
import lito.tools.cargo;
import lito.system;
import :command.fetch;
import :project;
import :dependency.external_source;
import :dependency.cargo;
import :build.host_tool;
import :build.event;
import :registry.blob;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::system;

namespace lito
{

template<typename T>
auto fetch_failure(String message) -> CommandResult<T> {
    return Err(CommandError::Message(rstd::move(message)));
}

template<typename T>
auto fetch_failure(ref<str> message) -> CommandResult<T> {
    return fetch_failure<T>(String::make(message));
}

auto command_source_result(
    lito::source::SourceResult<Vec<lito::tools::acquisition::VerifiedFile>> result)
    -> CommandResult<Vec<lito::tools::acquisition::VerifiedFile>> {
    if (result.is_err()) {
        return fetch_failure<Vec<lito::tools::acquisition::VerifiedFile>>(
            rstd::format("source acquisition failed: {}", rstd::move(result).unwrap_err()));
    }
    return Ok(rstd::move(result).unwrap());
}

auto selected_package(const Vec<String>& selected, ref<str> package) noexcept -> bool {
    for (const auto& name : selected) {
        if (name == package) return true;
    }
    return false;
}

auto source_for(const PreparedExternalDependencySources& sources, usize package, ref<str> name)
    -> Option<ref<PreparedPackageExternalSource>> {
    for (const auto& source : sources.sources) {
        if (source.package == package && source.name == name) {
            return Some(
                ref<PreparedPackageExternalSource>::from_raw_parts(rstd::addressof(source)));
        }
    }
    return None();
}

struct CargoBundleEntry {
    String  alias;
    String  source_identity;
    PathBuf source_root;
    PathBuf manifest;
    PathBuf manifest_path;
    String  target;
};

struct CargoFetchOutcome {
    Option<lito::tools::cargo::Provider> provider;
    Vec<CargoBundleEntry>                entries;
};

auto fetch_cargo_provider_dependencies(const lito::package::ResolvedPackageGraph& graph,
                                       const Vec<String>&                         selected,
                                       const PreparedExternalDependencySources&   sources,
                                       const BuildPlatform&                       platform,
                                       const lito::tools::cargo::Configuration&   configuration,
                                       lito::tools::ToolResolver&                 resolver,
                                       const ResolvedProcessEnvironment&          environment,
                                       const Option<BuildEventSink>&              observer)
    -> CommandResult<CargoFetchOutcome> {
    auto provider = Option<lito::tools::cargo::Provider> {};
    auto target   = Option<String> {};
    auto requests = rstd::collections::BTreeMap<String, empty>::make();
    auto entries  = Vec<CargoBundleEntry>::make();
    for (usize package_index {}; package_index < graph.packages.len(); ++package_index) {
        const auto& package = graph.packages[package_index];
        if (! selected_package(selected, package.manifest.name.as_str())) continue;
        for (const auto& dependency : package.manifest.cargo_external_dependencies) {
            auto source = source_for(sources, package_index, dependency.recipe.source.as_str());
            if (source.is_none() || (*source)->acquired.is_none()) {
                return fetch_failure<CargoFetchOutcome>(
                    rstd::format("Cargo dependency '{}:{}' requires an external source directory",
                                 package.manifest.name.as_str(),
                                 dependency.alias.as_str()));
            }
            if (provider.is_none()) {
                auto requirement = lito::tools::external_dependency_tool_requirement(
                    lito::tools::HostToolCapability::CargoBuild,
                    package.manifest.name.as_str(),
                    dependency.alias.as_str());
                auto cargo = resolver.require(lito::tools::Tool::Cargo, requirement);
                if (cargo.is_err()) {
                    return Err(rstd::into<CommandError>(rstd::move(cargo).unwrap_err()));
                }
                auto identified = lito::tools::cargo::identify_provider(
                    rstd::move(cargo).unwrap().executable, environment);
                if (identified.is_err()) {
                    return Err(rstd::into<CommandError>(rstd::move(identified).unwrap_err()));
                }
                provider             = Some(rstd::move(identified).unwrap());
                auto selected_target = lito::cargo_target(*provider, platform);
                if (selected_target.is_err()) {
                    return fetch_failure<CargoFetchOutcome>(
                        rstd::format("Cargo target resolution failed: {}",
                                     rstd::move(selected_target).unwrap_err()));
                }
                target = Some(rstd::move(selected_target).unwrap());
            }
            const auto& acquired = *(*source)->acquired;
            auto        manifest = acquired.root.join(dependency.recipe.manifest_path.as_path());
            auto        key      = rstd::format(
                "{}\n{}\n{}", acquired.identity.as_str(), manifest.as_path(), target->as_str());
            if (requests.contains_key(key.as_str())) continue;
            auto fetched =
                lito::tools::cargo::fetch_dependencies(*provider,
                                                       lito::tools::cargo::FetchRequest {
                                                           .alias       = dependency.alias.clone(),
                                                           .source_root = acquired.root.clone(),
                                                           .manifest    = rstd::move(manifest),
                                                           .target      = target->clone(),
                                                           .locked      = true,
                                                           .offline     = configuration.offline,
                                                       },
                                                       environment,
                                                       cargo_observer(observer));
            if (fetched.is_err()) {
                return Err(rstd::into<CommandError>(rstd::move(fetched).unwrap_err()));
            }
            entries.push(CargoBundleEntry {
                .alias           = dependency.alias.clone(),
                .source_identity = acquired.identity.clone(),
                .source_root     = acquired.root.clone(),
                .manifest        = acquired.root.join(dependency.recipe.manifest_path.as_path()),
                .manifest_path   = dependency.recipe.manifest_path.clone(),
                .target          = target->clone(),
            });
            requests.insert(rstd::move(key), empty {});
        }
    }
    return Ok(CargoFetchOutcome {
        .provider = rstd::move(provider),
        .entries  = rstd::move(entries),
    });
}

struct GitBundleEntry {
    lito::source::FetchIdentity identity;
    PathBuf                     source;
    String                      commit;
};

auto clone_archive_request(const lito::source::ArchiveSourceFetchRequest& request)
    -> lito::source::ArchiveSourceFetchRequest {
    return lito::source::ArchiveSourceFetchRequest {
        .owner  = request.owner.clone(),
        .name   = request.name.clone(),
        .url    = request.url.clone(),
        .sha256 = request.sha256.clone(),
    };
}

auto append_git_bundle_entry(Vec<GitBundleEntry>&                       entries,
                             const lito::source::ResolvedPackageSource& source) -> void {
    if (source.kind != lito::source::PackageSourceKind::Git) return;
    auto identity = lito::source::git_fetch_identity(source.git.as_str(), source.commit.as_str());
    auto key      = lito::source::fetch_identity_stable_key(identity);
    for (const auto& entry : entries) {
        if (lito::source::fetch_identity_stable_key(entry.identity) == key.as_str()) return;
    }
    entries.push(GitBundleEntry {
        .identity = rstd::move(identity),
        .source   = source.root_directory.clone(),
        .commit   = source.commit.clone(),
    });
}

auto fetch_entry_count(const lito::package::ResolvedPackageGraph&          graph,
                       const PreparedExternalDependencySources&            externals,
                       const Vec<lito::source::ArchiveSourceFetchRequest>& archive_requests,
                       usize cargo_entries) -> CommandResult<usize> {
    auto       entries = rstd::collections::BTreeMap<String, empty>::make();
    auto       bundle  = lito::source::SourceBundleLayout(PathBuf::make());
    const auto append_package_source =
        [&entries, &bundle](const lito::source::ResolvedPackageSource& source) {
            if (source.kind == lito::source::PackageSourceKind::Git) {
                auto identity =
                    lito::source::git_fetch_identity(source.git.as_str(), source.commit.as_str());
                entries.insert(
                    rstd::format("git:{}", lito::source::fetch_identity_stable_key(identity)),
                    empty {});
                return;
            }
            if (source.kind != lito::source::PackageSourceKind::Registry ||
                source.registry_package.is_none() || source.registry_version.is_none() ||
                source.package_checksum.is_none())
                return;
            entries.insert(
                bundle.registry_package(*source.package_checksum).as_path().to_string_lossy(),
                empty {});
        };
    for (const auto& source : graph.sources) append_package_source(source);
    for (const auto& package : graph.packages) append_package_source(package.source);
    for (const auto& external : externals.sources) {
        if (external.acquired.is_none() || ! external.source.is_Git()) continue;
        auto identity = lito::source::acquired_git_fetch_identity(
            *external.acquired, external.source.as_Git().url.as_str());
        if (identity.is_err()) {
            return fetch_failure<usize>(rstd::format("cannot identify external Git source '{}': {}",
                                                     external.name.as_str(),
                                                     rstd::move(identity).unwrap_err()));
        }
        if (identity->is_none()) continue;
        entries.insert(rstd::format("git:{}", lito::source::fetch_identity_stable_key(**identity)),
                       empty {});
    }
    for (const auto& archive : archive_requests) {
        auto identity =
            lito::source::archive_fetch_identity(archive.url.clone(), archive.sha256.clone());
        entries.insert(
            rstd::format("archive:{}", lito::source::fetch_identity_stable_key(identity)),
            empty {});
    }
    return Ok(entries.len() + cargo_entries);
}

auto registry_configuration(const config::LitoBootstrapConfig& config,
                            const lito::registry::RegistryId&  identity)
    -> Option<ref<config::NamedRegistryConfig>> {
    for (const auto& registry : *config.registries()) {
        if (registry.identity == identity) {
            return Some(
                ref<config::NamedRegistryConfig>::from_raw_parts(rstd::addressof(registry)));
        }
    }
    return None();
}

auto copy_bundle_file(ref<rstd::path::Path> source, ref<rstd::path::Path> destination)
    -> CommandResult<empty> {
    auto parent = destination.parent();
    if (parent.is_none()) return fetch_failure<empty>("source bundle path has no parent"_str);
    auto created = rstd::fs::create_dir_all(*parent);
    if (created.is_err()) {
        return fetch_failure<empty>(rstd::format(
            "cannot create source bundle directory '{}': {}", *parent, created.unwrap_err()));
    }
    auto copied = rstd::fs::copy(source, destination);
    if (copied.is_err()) {
        return fetch_failure<empty>(rstd::format("cannot copy source bundle entry '{}' to '{}': {}",
                                                 source,
                                                 destination,
                                                 copied.unwrap_err()));
    }
    return Ok(empty {});
}

auto staging_directory(ref<rstd::path::Path> destination) -> CommandResult<PathBuf> {
    auto parent = destination.parent();
    auto name   = destination.file_name();
    if (parent.is_none() || name.is_none()) {
        return fetch_failure<PathBuf>("source bundle destination must have a parent and name"_str);
    }
    auto exists = rstd::fs::exists(destination);
    if (exists.is_err()) {
        return fetch_failure<PathBuf>(rstd::format(
            "cannot inspect source bundle destination '{}': {}", destination, exists.unwrap_err()));
    }
    if (*exists) {
        return fetch_failure<PathBuf>(
            rstd::format("source bundle destination '{}' already exists", destination));
    }
    auto created_parent = rstd::fs::create_dir_all(*parent);
    if (created_parent.is_err()) {
        return fetch_failure<PathBuf>(rstd::format(
            "cannot create source bundle parent '{}': {}", *parent, created_parent.unwrap_err()));
    }
    for (usize attempt {}; attempt < usize(64); ++attempt) {
        auto path =
            PathBuf::from(*parent).join(PathBuf::from(rstd::format(".{}.lito-staging-{}-{}",
                                                                   name->to_string_lossy().as_str(),
                                                                   rstd::process::id(),
                                                                   attempt))
                                            .as_path());
        auto created = rstd::fs::create_dir(path.as_path());
        if (created.is_ok()) return Ok(rstd::move(path));
        auto error = rstd::move(created).unwrap_err();
        if (error.kind() !=
            rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::AlreadyExists }) {
            return fetch_failure<PathBuf>(rstd::format(
                "cannot create source bundle staging directory '{}': {}", path.as_path(), error));
        }
    }
    return fetch_failure<PathBuf>("cannot reserve source bundle staging directory"_str);
}

auto write_source_bundle(ref<rstd::path::Path>                               destination,
                         const lito::package::ResolvedPackageGraph&          graph,
                         const PreparedExternalDependencySources&            externals,
                         const Vec<lito::source::ArchiveSourceFetchRequest>& archive_requests,
                         const Vec<lito::tools::acquisition::VerifiedFile>&  archive_files,
                         const CargoFetchOutcome&                            cargo,
                         bool                                                cargo_offline,
                         const Option<config::LitoBootstrapConfig>&          registries,
                         lito::tools::ToolResolver&                          resolver,
                         const ResolvedProcessEnvironment&                   environment,
                         const Option<BuildEventSink>& observer) -> CommandResult<usize> {
    auto       staging = rstd_try(staging_directory(destination));
    const auto cleanup = [&]() {
        (void)rstd::fs::remove_dir_all(staging.as_path());
    };
    auto layout  = lito::source::SourceBundleLayout(staging.clone());
    auto entries = usize {};

    auto git_entries = Vec<GitBundleEntry>::make();
    for (const auto& source : graph.sources) append_git_bundle_entry(git_entries, source);
    for (const auto& package : graph.packages) append_git_bundle_entry(git_entries, package.source);
    for (const auto& external : externals.sources) {
        if (external.acquired.is_none() || ! external.source.is_Git()) continue;
        auto identity = lito::source::acquired_git_fetch_identity(
            *external.acquired, external.source.as_Git().url.as_str());
        if (identity.is_err()) {
            cleanup();
            return fetch_failure<usize>(rstd::format("cannot export external Git source '{}': {}",
                                                     external.name.as_str(),
                                                     rstd::move(identity).unwrap_err()));
        }
        if (identity->is_none()) continue;
        auto duplicate = false;
        auto key       = lito::source::fetch_identity_stable_key(**identity);
        for (const auto& entry : git_entries) {
            if (lito::source::fetch_identity_stable_key(entry.identity) == key.as_str()) {
                duplicate = true;
                break;
            }
        }
        if (! duplicate) {
            auto resolved_identity = rstd::move(identity).unwrap().unwrap();
            auto commit            = resolved_identity.as_Git().commit.clone();
            git_entries.push(GitBundleEntry {
                .identity = rstd::move(resolved_identity),
                .source   = external.acquired->root.clone(),
                .commit   = rstd::move(commit),
            });
        }
    }
    if (! git_entries.is_empty()) {
        auto resolved = resolver.require(
            lito::tools::Tool::Git,
            lito::tools::command_tool_requirement(lito::tools::HostToolCapability::GitCheckout,
                                                  "source bundle export"_str));
        if (resolved.is_err()) {
            cleanup();
            return Err(rstd::into<CommandError>(rstd::move(resolved).unwrap_err()));
        }
        auto git = lito::tools::GitClient(resolved->executable.clone(), environment);
        for (const auto& entry : git_entries) {
            auto output  = layout.git(entry.identity);
            auto parent  = output.as_path().parent().unwrap();
            auto created = rstd::fs::create_dir_all(parent);
            if (created.is_err()) {
                cleanup();
                return fetch_failure<usize>(rstd::format(
                    "cannot create Git bundle directory '{}': {}", parent, created.unwrap_err()));
            }
            auto exported = git.export_checkout(
                entry.source.as_path(), output.as_path(), entry.commit.as_str());
            if (exported.is_err()) {
                cleanup();
                return Err(rstd::into<CommandError>(rstd::move(exported).unwrap_err()));
            }
            ++entries;
        }
    }

    auto archived = rstd::collections::BTreeMap<String, empty>::make();
    for (usize index {}; index < archive_files.len(); ++index) {
        auto identity = lito::source::archive_fetch_identity(
            archive_requests[index].url.clone(), archive_requests[index].sha256.clone());
        auto key = lito::source::fetch_identity_stable_key(identity);
        if (archived.contains_key(key.as_str())) continue;
        auto copied = copy_bundle_file(archive_files[index].path.as_path(),
                                       layout.archive(identity).as_path());
        if (copied.is_err()) {
            cleanup();
            return Err(rstd::move(copied).unwrap_err());
        }
        archived.insert(rstd::move(key), empty {});
        ++entries;
    }

    if (! cargo.entries.is_empty() && cargo.provider.is_none()) {
        cleanup();
        return fetch_failure<usize>("Cargo source bundle provider is unavailable"_str);
    }
    for (const auto& entry : cargo.entries) {
        auto area = layout.cargo(
            entry.source_identity.as_str(), entry.manifest_path.as_path(), entry.target.as_str());
        auto vendored =
            lito::tools::cargo::vendor_dependencies(*cargo.provider,
                                                    lito::tools::cargo::VendorRequest {
                                                        .alias       = entry.alias.clone(),
                                                        .source_root = entry.source_root.clone(),
                                                        .manifest    = entry.manifest.clone(),
                                                        .destination = rstd::move(area),
                                                        .locked      = true,
                                                        .offline     = cargo_offline,
                                                    },
                                                    environment,
                                                    cargo_observer(observer));
        if (vendored.is_err()) {
            cleanup();
            return Err(rstd::into<CommandError>(rstd::move(vendored).unwrap_err()));
        }
        auto validated = lito::tools::cargo::validate_vendor_config(vendored->config.as_path());
        if (validated.is_err()) {
            cleanup();
            return Err(rstd::into<CommandError>(rstd::move(validated).unwrap_err()));
        }
        ++entries;
    }

    auto registry_packages = rstd::collections::BTreeMap<String, empty>::make();
    for (const auto& source : graph.sources) {
        if (source.kind != lito::source::PackageSourceKind::Registry) continue;
        if (registries.is_none() || source.registry_package.is_none() ||
            source.registry_version.is_none() || source.package_checksum.is_none()) {
            cleanup();
            return fetch_failure<usize>("Registry source bundle metadata is incomplete"_str);
        }
        auto destination = layout.registry_package(*source.package_checksum);
        auto key         = destination.as_path().to_string_lossy();
        if (registry_packages.contains_key(key.as_str())) continue;

        auto configured = registry_configuration(*registries, source.registry_package->registry);
        if (configured.is_none()) {
            cleanup();
            return fetch_failure<usize>(rstd::format("Registry '{}' is not configured",
                                                     source.registry_package->registry.as_str()));
        }
        auto data = lito::system::LitoDataRoot::resolve();
        if (data.is_err()) {
            cleanup();
            return Err(rstd::into<CommandError>(rstd::move(data).unwrap_err()));
        }
        auto blobs =
            lito::registry::RegistryBlobCache(PathBuf::from(data->root()),
                                              (**configured).effective_endpoints()->blob.clone(),
                                              lito::registry::RegistryNetworkPolicy::Offline,
                                              {});
        auto blob = blobs.acquire(*source.registry_package, *source.package_checksum);
        if (blob.is_err()) {
            cleanup();
            return fetch_failure<usize>(rstd::format(
                "cannot read verified Registry package archive: {}", blob.unwrap_err().message));
        }
        auto copied = copy_bundle_file(blob->path.as_path(), destination.as_path());
        if (copied.is_err()) {
            cleanup();
            return Err(rstd::move(copied).unwrap_err());
        }
        registry_packages.insert(rstd::move(key), empty {});
        ++entries;
    }

    auto published = rstd::fs::rename(staging.as_path(), destination);
    if (published.is_err()) {
        cleanup();
        return fetch_failure<usize>(rstd::format(
            "cannot publish source bundle '{}': {}", destination, published.unwrap_err()));
    }
    return Ok(entries);
}

auto fetch_dependencies(const FetchRequest& request) -> CommandResult<FetchSummary> {
    if (request.selection.root.as_path().is_empty()) {
        return fetch_failure<FetchSummary>("fetch directory is required"_str);
    }
    if (request.jobs == usize {}) {
        return fetch_failure<FetchSummary>("fetch jobs must be greater than zero"_str);
    }
    if (! request.sources.source_bundles.is_empty()) {
        return fetch_failure<FetchSummary>("fetch does not accept source bundle inputs"_str);
    }
    auto environment = ResolvedProcessEnvironment::resolve(request.environment);
    if (environment.is_err()) {
        return Err(rstd::into<CommandError>(rstd::move(environment).unwrap_err()));
    }
    auto resolver =
        lito::tools::ToolResolver(*environment, request.tools.clone(), request.tool_reporter);
    auto acquisition_platform = resolve_acquisition_platform(request.configuration, *environment);
    if (acquisition_platform.is_err()) {
        return Err(rstd::into<CommandError>(rstd::move(acquisition_platform).unwrap_err()));
    }
    auto platform = rstd::move(acquisition_platform).unwrap();
    auto project  = resolve_project(
        request.selection,
        lito::package::PackageSelectionPurpose::All,
        request.sources,
        request.lock,
        request.locked,
        lito::source::GitResolutionMode::ReuseLocked,
        rstd::addressof(platform.platform.effective_target),
        resolver,
        *environment,
        request.cmake_build_overrides,
        request.jobs,
        request.observer.is_some() ? *request.observer : BuildEventSink {},
        None(),
        lito::lock::InvalidLockPolicy::Reject,
        request.registries.is_some() ? rstd::addressof(*request.registries) : nullptr);
    if (project.is_err()) {
        return Err(rstd::into<CommandError>(rstd::move(project).unwrap_err()));
    }
    auto resolved = rstd::move(project).unwrap();
    auto external = prepare_external_dependency_sources(
        resolved.selection.graph,
        resolved.selection.selected_package_names,
        rstd::move(resolved.external_sources),
        resolved.cmake_build_overrides,
        resolver,
        *environment,
        request.jobs,
        request.observer.is_some() ? *request.observer : BuildEventSink {});
    if (external.is_err()) {
        return fetch_failure<FetchSummary>(rstd::format("external source preparation failed: {}",
                                                        rstd::move(external).unwrap_err()));
    }
    auto prepared = rstd::move(external).unwrap();
    auto external_plan =
        resolve_external_acquisition_plan(resolved.selection.graph, prepared, platform.platform);
    if (external_plan.is_err()) {
        return fetch_failure<FetchSummary>(rstd::format("external acquisition planning failed: {}",
                                                        rstd::move(external_plan).unwrap_err()));
    }
    auto archive_requests = Vec<lito::source::ArchiveSourceFetchRequest>::make();
    for (auto& acquisition : external_plan->archives) {
        archive_requests.push(rstd::move(acquisition.request));
    }
    auto host_archives = resolve_host_build_tool_archives(resolved.selection.graph,
                                                          resolved.selection.selected_package_names,
                                                          platform.platform.host);
    if (host_archives.is_err()) {
        return fetch_failure<FetchSummary>(
            rstd::format("host build-tool acquisition planning failed: {}",
                         rstd::move(host_archives).unwrap_err()));
    }
    for (auto& archive : *host_archives) archive_requests.push(rstd::move(archive));
    auto acquisition_requests =
        Vec<lito::source::ArchiveSourceFetchRequest>::with_capacity(archive_requests.len());
    for (const auto& archive : archive_requests) {
        acquisition_requests.push(clone_archive_request(archive));
    }
    auto archive_files = rstd_try(command_source_result(
        lito::source::cache_archive_frontier(rstd::move(acquisition_requests),
                                             request.jobs,
                                             resolver,
                                             *environment,
                                             request.sources,
                                             source_observer(request.observer))));
    auto cargo =
        rstd_try(fetch_cargo_provider_dependencies(resolved.selection.graph,
                                                   resolved.selection.selected_package_names,
                                                   prepared,
                                                   platform.platform,
                                                   request.cargo,
                                                   resolver,
                                                   *environment,
                                                   request.observer));
    auto entries = rstd_try(fetch_entry_count(
        resolved.selection.graph, prepared, archive_requests, cargo.entries.len()));
    if (request.destination.is_SourceBundle()) {
        entries = rstd_try(write_source_bundle(request.destination.as_SourceBundle().path.as_path(),
                                               resolved.selection.graph,
                                               prepared,
                                               archive_requests,
                                               archive_files,
                                               cargo,
                                               request.cargo.offline,
                                               request.registries,
                                               resolver,
                                               *environment,
                                               request.observer));
    }
    return Ok(FetchSummary {
        .lock = resolved.lock,
        .destination =
            request.destination.is_SourceBundle()
                ? FetchDestination::SourceBundle(request.destination.as_SourceBundle().path.clone())
                : FetchDestination::GlobalCache(),
        .entries = entries,
    });
}

} // namespace lito
