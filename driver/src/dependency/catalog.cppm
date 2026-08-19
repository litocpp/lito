module;
#include <rstd/macro.hpp>

export module lito.driver:dependency.catalog;

import rstd;
import lito.core;
import lito.cpp;
import :build.event;
import :build.layout;
import lito.system;
import lito.toolchain.clang;
import lito.toolchain.cmake;
import :dependency.external_source;
import :dependency.preparation;
import :dependency.pkg_config;
import lito.toolchain.pkg_config;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;

export namespace lito
{

struct PreparedExternalCatalog {
    cpp::ExternalUsageCatalog      usage;
    cpp::ExternalSourceRootCatalog sources;
    ExternalAssetCatalog           assets;
};

auto resolve_external_usage_catalog(const lito::package::ResolvedPackageGraph& graph,
                                    const Vec<String>&                       selected_package_names,
                                    const PreparedExternalDependencySources& external_sources,
                                    const lito::dependency::PkgConfigProviderConfig& pkg_config,
                                    const lito::dependency::CMakeProviderConfig&     cmake_config,
                                    const cpp::BuildConfiguration&                   configuration,
                                    const cpp::ProfileSpec&                          profile,
                                    const BuildLayout&                               layout,
                                    const BuildPlatform&                             platform,
                                    ToolResolver&                                    tool_resolver,
                                    const ResolvedProcessEnvironment&        process_environment,
                                    usize                                    jobs,
                                    const Option<BuildEventSink>&            observer,
                                    const lito::source::PackageSourceConfig& source_config = {})
    -> lito::dependency::DependencyResult<PreparedExternalCatalog> {
    if (jobs == usize {}) {
        return lito::dependency::dependency_failure<PreparedExternalCatalog>(
            "external dependency jobs must be greater than zero"_str);
    }
    auto selected = rstd::collections::BTreeMap<String, empty>::make();
    for (const auto& name : selected_package_names) selected.insert(name.clone(), empty {});

    struct CMakeBinding {
        usize                              catalog {};
        usize                              package {};
        String                             owner;
        Option<String>                     source_name;
        bool                               installed_override { false };
        SelectedCMakeDependencyRequirement requirement;
    };
    auto result          = cpp::ExternalUsageCatalog {};
    auto bindings        = Vec<CMakeBinding>::make();
    auto catalog_indices = Vec<Option<usize>>::with_capacity(graph.packages.len());
    for (usize package_index {}; package_index < graph.packages.len(); ++package_index) {
        const auto& package = graph.packages[package_index];
        if (! selected.contains_key(package.manifest.name.as_str())) continue;
        auto dependencies =
            resolve_pkg_config_dependencies(package.manifest.pkg_config_external_dependencies,
                                            pkg_config,
                                            package.manifest.name.as_str(),
                                            platform,
                                            tool_resolver,
                                            process_environment);
        if (dependencies.is_err()) return Err(rstd::move(dependencies).unwrap_err());
        auto catalog_index = result.packages.len();
        result.packages.push(cpp::ExternalPackageUsage {
            .package      = package.manifest.name.clone(),
            .dependencies = rstd::move(dependencies).unwrap(),
        });
        while (catalog_indices.len() <= package_index) catalog_indices.push(None());
        catalog_indices[package_index] = Some(catalog_index);
    }
    while (catalog_indices.len() < graph.packages.len()) catalog_indices.push(None());
    for (const auto& declaration : external_sources.dependencies) {
        if (declaration.package >= graph.packages.len() ||
            catalog_indices[declaration.package].is_none()) {
            continue;
        }
        const auto& package = graph.packages[declaration.package];
        auto        requirement =
            resolve_cmake_requirement_for_platform(declaration.requirement, platform);
        if (requirement.is_err()) return Err(rstd::move(requirement).unwrap_err());
        bindings.push(CMakeBinding {
            .catalog            = *catalog_indices[declaration.package],
            .package            = declaration.package,
            .owner              = package.manifest.name.clone(),
            .source_name        = as<Clone>(declaration.requirement.source_name).clone(),
            .installed_override = declaration.installed_override,
            .requirement        = rstd::move(requirement).unwrap(),
        });
    }
    rstd::slice_::sort_unstable_by(bindings.as_mut_slice().as_mut_ref(),
                                   [](const CMakeBinding& left, const CMakeBinding& right) {
                                       if (left.owner != right.owner)
                                           return left.owner < right.owner;
                                       if (left.requirement.alias != right.requirement.alias) {
                                           return left.requirement.alias < right.requirement.alias;
                                       }
                                       return left.requirement.package < right.requirement.package;
                                   });

    auto needs_archive_source = false;
    for (const auto& source : external_sources.sources) {
        if (source.acquired.is_none()) needs_archive_source = true;
    }
    if (bindings.is_empty() && ! needs_archive_source) {
        auto source_catalog = cpp::ExternalSourceRootCatalog {};
        for (const auto& source : external_sources.sources) {
            if (source.acquired.is_none()) continue;
            source_catalog.sources.push(cpp::ExternalSourceRoot {
                .package  = source.package,
                .name     = source.name.clone(),
                .root     = source.acquired->root.clone(),
                .identity = source.acquired->identity.clone(),
            });
        }
        return Ok(PreparedExternalCatalog {
            .usage   = rstd::move(result),
            .sources = rstd::move(source_catalog),
        });
    }
    auto source_catalog  = cpp::ExternalSourceRootCatalog {};
    auto archive_sources = Vec<usize>::make();
    auto source_archives = Vec<lito::source::ArchiveSourceFetchRequest>::make();
    for (usize index {}; index < external_sources.sources.len(); ++index) {
        const auto& source = external_sources.sources[index];
        if (source.acquired.is_some()) {
            source_catalog.sources.push(cpp::ExternalSourceRoot {
                .package  = source.package,
                .name     = source.name.clone(),
                .root     = source.acquired->root.clone(),
                .identity = source.acquired->identity.clone(),
            });
            continue;
        }
        auto url    = String::make();
        auto sha256 = String::make();
        if (source.source.is_Archive()) {
            url    = source.source.as_Archive().url.clone();
            sha256 = source.source.as_Archive().sha256.clone();
        } else if (source.source.is_ArchitectureArchives()) {
            const lito::dependency::ExternalArchiveVariant* selected_variant = nullptr;
            for (const auto& variant : source.source.as_ArchitectureArchives().variants) {
                if (variant.architecture == platform.effective_target.architecture) {
                    selected_variant = rstd::addressof(variant);
                    break;
                }
            }
            if (selected_variant == nullptr) {
                return lito::dependency::dependency_failure<PreparedExternalCatalog>(
                    rstd::format("external source '{}:{}' has no archive for architecture '{}'",
                                 graph.packages[source.package].manifest.name.as_str(),
                                 source.name.as_str(),
                                 platform.effective_target.architecture.as_str()));
            }
            url    = selected_variant->url.clone();
            sha256 = selected_variant->sha256.clone();
        } else {
            return lito::dependency::dependency_failure<PreparedExternalCatalog>(
                rstd::format("external source '{}:{}' was not acquired",
                             graph.packages[source.package].manifest.name.as_str(),
                             source.name.as_str()));
        }
        archive_sources.push(usize(index));
        source_archives.push(lito::source::ArchiveSourceFetchRequest {
            .owner  = graph.packages[source.package].manifest.name.clone(),
            .name   = source.name.clone(),
            .url    = rstd::move(url),
            .sha256 = rstd::move(sha256),
        });
    }
    if (! source_archives.is_empty()) {
        auto materialization_root = layout.source_materialization_root();
        auto fetched = lito::source::acquire_archive_frontier(rstd::move(source_archives),
                                                              jobs,
                                                              materialization_root.as_path(),
                                                              tool_resolver,
                                                              process_environment,
                                                              source_config,
                                                              source_observer(observer));
        if (fetched.is_err()) {
            return Err(
                rstd::into<lito::dependency::DependencyError>(rstd::move(fetched).unwrap_err()));
        }
        for (usize index {}; index < fetched->len(); ++index) {
            const auto& source = external_sources.sources[archive_sources[index]];
            source_catalog.sources.push(cpp::ExternalSourceRoot {
                .package  = source.package,
                .name     = source.name.clone(),
                .root     = (*fetched)[index].root.clone(),
                .identity = (*fetched)[index].identity.clone(),
            });
        }
    }
    for (auto& binding : bindings) {
        if (binding.source_name.is_none()) continue;
        const cpp::ExternalSourceRoot* prepared = nullptr;
        for (const auto& source : source_catalog.sources) {
            if (source.package == binding.package && source.name == binding.source_name->as_str()) {
                prepared = rstd::addressof(source);
                break;
            }
        }
        if (prepared == nullptr) {
            return lito::dependency::dependency_failure<PreparedExternalCatalog>(
                rstd::format("CMake dependency '{}:{}' external source '{}' was not materialized",
                             binding.owner.as_str(),
                             binding.requirement.alias.as_str(),
                             binding.source_name->as_str()));
        }
        binding.requirement.source = SelectedCMakeDependencySource::Directory(
            prepared->root.clone(), prepared->identity.clone(), true);
    }

    auto archive_requests = Vec<lito::source::ArchiveSourceFetchRequest>::make();
    auto archive_bindings = Vec<usize>::make();
    for (usize index {}; index < bindings.len(); ++index) {
        const auto& source = bindings[index].requirement.source;
        if (! source.is_Archive()) continue;
        archive_bindings.push(usize(index));
        archive_requests.push(lito::source::ArchiveSourceFetchRequest {
            .owner  = bindings[index].owner.clone(),
            .name   = bindings[index].requirement.alias.clone(),
            .url    = source.as_Archive().url.clone(),
            .sha256 = source.as_Archive().sha256.clone(),
        });
    }
    if (! archive_requests.is_empty()) {
        auto fetch_observer       = source_observer(observer);
        auto materialization_root = layout.source_materialization_root();
        auto fetched = lito::source::acquire_archive_frontier(rstd::move(archive_requests),
                                                              jobs,
                                                              materialization_root.as_path(),
                                                              tool_resolver,
                                                              process_environment,
                                                              source_config,
                                                              fetch_observer);
        if (fetched.is_err()) {
            return Err(
                rstd::into<lito::dependency::DependencyError>(rstd::move(fetched).unwrap_err()));
        }
        for (usize index {}; index < fetched->len(); ++index) {
            auto acquired = rstd::move((*fetched)[index]);
            bindings[archive_bindings[index]].requirement.source =
                SelectedCMakeDependencySource::Directory(
                    rstd::move(acquired.root), rstd::move(acquired.identity), true);
        }
    }

    if (bindings.is_empty()) {
        return Ok(PreparedExternalCatalog {
            .usage   = rstd::move(result),
            .sources = rstd::move(source_catalog),
        });
    }

    const auto cmake_requirement =
        external_dependency_tool_requirement(HostToolCapability::CMakeProject,
                                             bindings[usize {}].owner,
                                             bindings[usize {}].requirement.alias);
    auto resolved_tool =
        cmake_config.executable.is_empty()
            ? tool_resolver.require(Tool::CMake, cmake_requirement)
            : tool_resolver.resolve(cmake_config.executable.as_path(), "CMake executable"_str);
    if (resolved_tool.is_err()) {
        return Err(
            rstd::into<lito::dependency::DependencyError>(rstd::move(resolved_tool).unwrap_err()));
    }
    auto resolved_cmake       = cmake_config.clone();
    resolved_cmake.executable = rstd::move(resolved_tool).unwrap().executable;
    auto identified = identify_cmake_provider(rstd::move(resolved_cmake), process_environment);
    if (identified.is_err()) return Err(rstd::move(identified).unwrap_err());
    resolved_cmake = rstd::move(identified).unwrap();

    auto snapshots       = rstd::collections::BTreeMap<String, CMakeUsageSnapshot>::make();
    auto assets          = ExternalAssetCatalog {};
    auto cmake_work_root = layout.cmake_work_root();
    for (auto& binding : bindings) {
        auto requirement = materialize_cmake_requirement(binding.requirement);
        if (requirement.is_err()) return Err(rstd::move(requirement).unwrap_err());
        auto contextualize = [&](lito::dependency::DependencyError error) {
            if (! binding.installed_override) return error;
            return lito::dependency::DependencyError::CMakeOverride(
                binding.requirement.package.clone(),
                Box<lito::dependency::DependencyError>::make(rstd::move(error)));
        };
        auto plan = plan_cmake_package(*requirement,
                                       resolved_cmake,
                                       configuration,
                                       profile,
                                       platform.compiler_default,
                                       platform.effective_target.triple.as_str(),
                                       cmake_work_root.as_path(),
                                       jobs);
        if (plan.is_err()) return Err(contextualize(rstd::move(plan).unwrap_err()));
        auto key_text = plan->area.query_root.as_path().to_str();
        if (key_text.is_none()) {
            return lito::dependency::dependency_failure<PreparedExternalCatalog>(rstd::format(
                "CMake query path '{}' is not valid UTF-8", plan->area.query_root.as_path()));
        }
        auto cached = snapshots.get(*key_text);
        auto usage  = [&]() -> lito::dependency::DependencyResult<cpp::ExternalDependencyUsage> {
            if (cached.is_some()) return materialize_cmake_usage(*plan, **cached);
            auto tool_observer = cmake_observer(observer);
            auto executed      = execute_cmake_package(*plan, process_environment, tool_observer);
            if (executed.is_err()) return Err(rstd::move(executed).unwrap_err());
            auto materialized = materialize_cmake_usage(*plan, *executed);
            if (materialized.is_err()) return Err(rstd::move(materialized).unwrap_err());
            snapshots.insert(String::make(*key_text), rstd::move(executed).unwrap());
            return materialized;
        }();
        if (usage.is_err()) return Err(contextualize(rstd::move(usage).unwrap_err()));
        auto dependency = rstd::move(usage).unwrap();
        auto normalized = normalize_clang_link_arguments(rstd::move(dependency.link_arguments));
        if (normalized.is_err()) {
            return lito::dependency::dependency_failure<PreparedExternalCatalog>(
                rstd::format("{}", rstd::move(normalized).unwrap_err()));
        }
        dependency.link_arguments    = rstd::move(normalized->arguments);
        dependency.link_requirements = rstd::move(normalized->requirements);
        result.packages[binding.catalog].dependencies.push(rstd::move(dependency));
        auto snapshot = snapshots.get(*key_text);
        if (snapshot.is_none()) {
            return lito::dependency::dependency_failure<PreparedExternalCatalog>(
                String::make("CMake usage snapshot was not retained"_str));
        }
        for (const auto& set : (**snapshot).assets) {
            auto copied    = set.clone();
            copied.alias   = binding.requirement.alias.clone();
            auto duplicate = false;
            for (const auto& prior : assets.sets) {
                if (prior.alias == copied.alias.as_str() && prior.name == copied.name.as_str()) {
                    if (prior.disposition != copied.disposition ||
                        prior.entries.len() != copied.entries.len()) {
                        return lito::dependency::dependency_failure<PreparedExternalCatalog>(
                            rstd::format("external asset set '{}:{}' has conflicting definitions",
                                         copied.alias.as_str(),
                                         copied.name.as_str()));
                    }
                    for (usize index {}; index < prior.entries.len(); ++index) {
                        if (prior.entries[index].logical_path.as_path() !=
                                copied.entries[index].logical_path.as_path() ||
                            prior.entries[index].source.as_path() !=
                                copied.entries[index].source.as_path()) {
                            return lito::dependency::dependency_failure<PreparedExternalCatalog>(
                                rstd::format(
                                    "external asset set '{}:{}' has conflicting definitions",
                                    copied.alias.as_str(),
                                    copied.name.as_str()));
                        }
                    }
                    duplicate = true;
                    break;
                }
            }
            if (! duplicate) assets.sets.push(rstd::move(copied));
        }
    }
    return Ok(PreparedExternalCatalog {
        .usage   = rstd::move(result),
        .sources = rstd::move(source_catalog),
        .assets  = rstd::move(assets),
    });
}

} // namespace lito
