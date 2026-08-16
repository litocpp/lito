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
    cpp::ExternalUsageCatalog usage;
    ExternalAssetCatalog      assets;
};

auto resolve_external_usage_catalog(const ResolvedPackageGraph&              graph,
                                    const Vec<String>&                       selected_package_names,
                                    const PreparedExternalDependencySources& external_sources,
                                    const PkgConfigProviderConfig&           pkg_config,
                                    const CMakeProviderConfig&               cmake_config,
                                    const cpp::BuildConfiguration&           configuration,
                                    const cpp::ProfileSpec&                  profile,
                                    const BuildLayout&                       layout,
                                    const BuildPlatform&                     platform,
                                    const cpp::CppArgumentParser&            parser,
                                    ToolResolver&                            tool_resolver,
                                    const ResolvedProcessEnvironment&        process_environment,
                                    usize                                    jobs,
                                    const Option<BuildEventSink>&            observer,
                                    const PackageSourceConfig&               source_config = {})
    -> DependencyResult<PreparedExternalCatalog> {
    if (jobs == usize {}) {
        return dependency_failure<PreparedExternalCatalog>(
            "external dependency jobs must be greater than zero"_str);
    }
    auto selected = rstd::collections::BTreeMap<String, empty>::make();
    for (const auto& name : selected_package_names) selected.insert(name.clone(), empty {});

    struct CMakeBinding {
        usize                              catalog {};
        String                             owner;
        ResolvedCMakeDependencyRequirement requirement;
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
                                            platform,
                                            parser,
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
            .catalog     = *catalog_indices[declaration.package],
            .owner       = package.manifest.name.clone(),
            .requirement = rstd::move(requirement).unwrap(),
        });
    }
    if (bindings.is_empty()) {
        return Ok(PreparedExternalCatalog { .usage = rstd::move(result) });
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

    auto resolved_tool =
        tool_resolver.resolve(cmake_config.executable.as_path(), "CMake executable"_str);
    if (resolved_tool.is_err()) {
        return Err(rstd::into<DependencyError>(rstd::move(resolved_tool).unwrap_err()));
    }
    auto resolved_cmake       = cmake_config.clone();
    resolved_cmake.executable = rstd::move(resolved_tool).unwrap().executable;
    auto identified = identify_cmake_provider(rstd::move(resolved_cmake), process_environment);
    if (identified.is_err()) return Err(rstd::move(identified).unwrap_err());
    resolved_cmake = rstd::move(identified).unwrap();

    auto archive_requests = Vec<ArchiveSourceFetchRequest>::make();
    auto archive_bindings = Vec<usize>::make();
    for (usize index {}; index < bindings.len(); ++index) {
        const auto& source = bindings[index].requirement.source;
        if (! source.is_Archive()) continue;
        archive_bindings.push(usize(index));
        archive_requests.push(ArchiveSourceFetchRequest {
            .url    = source.as_Archive().url.clone(),
            .sha256 = source.as_Archive().sha256.clone(),
        });
    }
    if (! archive_requests.is_empty()) {
        auto fetch_observer       = source_observer(observer);
        auto materialization_root = layout.source_materialization_root();
        auto fetched              = acquire_archive_frontier(rstd::move(archive_requests),
                                                             jobs,
                                                             materialization_root.as_path(),
                                                             resolved_cmake.executable.as_path(),
                                                             process_environment,
                                                             source_config,
                                                             fetch_observer);
        if (fetched.is_err()) {
            return Err(rstd::into<DependencyError>(rstd::move(fetched).unwrap_err()));
        }
        for (usize index {}; index < fetched->len(); ++index) {
            auto acquired    = rstd::move((*fetched)[index]);
            auto cmake_lists = acquired.root.join(PathBuf::from("CMakeLists.txt"_str).as_path());
            auto has_project = rstd::fs::exists(cmake_lists.as_path());
            if (has_project.is_err()) {
                return Err(DependencyError::Io(String::make("inspect archive CMake project"_str),
                                               rstd::move(cmake_lists),
                                               rstd::move(has_project).unwrap_err()));
            }
            bindings[archive_bindings[index]].requirement.source =
                ResolvedCMakeDependencySource::Directory(
                    rstd::move(acquired.root),
                    rstd::move(acquired.identity),
                    bindings[archive_bindings[index]].requirement.add_subdirectory && *has_project,
                    true);
        }
    }

    auto snapshots       = rstd::collections::BTreeMap<String, CMakeUsageSnapshot>::make();
    auto assets          = ExternalAssetCatalog {};
    auto cmake_work_root = layout.cmake_work_root();
    for (auto& binding : bindings) {
        auto plan = plan_cmake_package(binding.requirement,
                                       resolved_cmake,
                                       configuration,
                                       profile,
                                       platform.compiler_default,
                                       platform.effective_target.triple.as_str(),
                                       cmake_work_root.as_path(),
                                       jobs);
        if (plan.is_err()) return Err(rstd::move(plan).unwrap_err());
        auto key_text = plan->area.query_root.as_path().to_str();
        if (key_text.is_none()) {
            return dependency_failure<PreparedExternalCatalog>(rstd::format(
                "CMake query path '{}' is not valid UTF-8", plan->area.query_root.as_path()));
        }
        auto cached = snapshots.get(*key_text);
        auto usage  = [&]() -> DependencyResult<cpp::ResolvedExternalDependency> {
            if (cached.is_some()) return materialize_cmake_usage(*plan, **cached, parser);
            auto tool_observer = cmake_observer(observer);
            auto executed      = execute_cmake_package(*plan, process_environment, tool_observer);
            if (executed.is_err()) return Err(rstd::move(executed).unwrap_err());
            auto materialized = materialize_cmake_usage(*plan, *executed, parser);
            if (materialized.is_err()) return Err(rstd::move(materialized).unwrap_err());
            snapshots.insert(String::make(*key_text), rstd::move(executed).unwrap());
            return materialized;
        }();
        if (usage.is_err()) return Err(rstd::move(usage).unwrap_err());
        auto dependency = rstd::move(usage).unwrap();
        auto normalized = normalize_clang_link_arguments(rstd::move(dependency.link_arguments));
        dependency.link_arguments    = rstd::move(normalized.arguments);
        dependency.link_requirements = rstd::move(normalized.requirements);
        result.packages[binding.catalog].dependencies.push(rstd::move(dependency));
        auto snapshot = snapshots.get(*key_text);
        if (snapshot.is_none()) {
            return dependency_failure<PreparedExternalCatalog>(
                String::make("CMake usage snapshot was not retained"_str));
        }
        for (const auto& set : (**snapshot).assets) {
            auto copied    = set.clone();
            copied.alias   = binding.requirement.alias.clone();
            auto duplicate = false;
            for (const auto& prior : assets.sets) {
                if (prior.alias == copied.alias.as_str() && prior.name == copied.name.as_str()) {
                    if (prior.entries.len() != copied.entries.len()) {
                        return dependency_failure<PreparedExternalCatalog>(
                            rstd::format("external asset set '{}:{}' has conflicting definitions",
                                         copied.alias.as_str(),
                                         copied.name.as_str()));
                    }
                    for (usize index {}; index < prior.entries.len(); ++index) {
                        if (prior.entries[index].logical_path.as_path() !=
                                copied.entries[index].logical_path.as_path() ||
                            prior.entries[index].source.as_path() !=
                                copied.entries[index].source.as_path()) {
                            return dependency_failure<PreparedExternalCatalog>(rstd::format(
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
        .usage  = rstd::move(result),
        .assets = rstd::move(assets),
    });
}

} // namespace lito
