module;
#include <rstd/macro.hpp>

export module lito.driver:dependency.catalog;

import rstd;
import licrypto;
import lito.tools;
import lito.tools.cargo;
import lito.core;
import lito.cpp;
import :build.event;
import :build.layout;
import lito.system;
import lito.toolchain;
import :dependency.cmake;
import :dependency.cargo;
import :dependency.external_source;
import :dependency.preparation;
import :dependency.pkg_config;
import :source;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;

export namespace lito
{

struct PreparedExternalCatalog {
    cpp::ExternalUsageCatalog      usage;
    cpp::ExternalSourceRootCatalog sources;
    ExternalAssetCatalog           assets;
    Vec<ExternalSourceProvenance>  provenance;
};

auto project_external_source_provenance(const lito::package::ResolvedPackageGraph& graph,
                                        const cpp::ExternalSourceRootCatalog&      sources)
    -> lito::dependency::DependencyResult<Vec<ExternalSourceProvenance>> {
    auto result = Vec<ExternalSourceProvenance>::with_capacity(sources.sources.len());
    for (const auto& source : sources.sources) {
        if (source.package >= graph.packages.len()) {
            return lito::dependency::dependency_failure<Vec<ExternalSourceProvenance>>(
                rstd::format("external source '{}' refers to unavailable package index {}",
                             source.name.as_str(),
                             source.package));
        }
        result.push(ExternalSourceProvenance {
            .package                = graph.packages[source.package].manifest.name.clone(),
            .name                   = source.name.clone(),
            .materialized_root      = source.root.clone(),
            .stable_source_identity = source.identity.clone(),
        });
    }
    return Ok(rstd::move(result));
}

auto resolve_external_usage_catalog(const lito::package::ResolvedPackageGraph& graph,
                                    const Vec<String>&                       selected_package_names,
                                    const PreparedExternalDependencySources& external_sources,
                                    const lito::tools::cargo::Configuration& cargo_config,
                                    const lito::dependency::PkgConfigProviderConfig& pkg_config,
                                    const lito::dependency::CMakeProviderConfig&     cmake_config,
                                    const cpp::BuildConfiguration&                   configuration,
                                    const cpp::ProfileSpec&                          profile,
                                    const LinkerIdentity&                            linker,
                                    const BuildLayout&                               layout,
                                    const BuildPlatform&                             platform,
                                    lito::tools::ToolResolver&                       tool_resolver,
                                    const ResolvedProcessEnvironment&        process_environment,
                                    usize                                    jobs,
                                    const Option<BuildEventSink>&            observer,
                                    const lito::source::PackageSourceConfig& source_config = {},
                                    const Option<AndroidCmakeProjection>&    android_cmake = None(),
                                    const Option<PathBuf>& cmake_find_install_prefix       = None())
    -> lito::dependency::DependencyResult<PreparedExternalCatalog> {
    if (jobs == usize {}) {
        return lito::dependency::dependency_failure<PreparedExternalCatalog>(
            "external dependency jobs must be greater than zero"_str);
    }
    auto selected = rstd::collections::BTreeMap<String, empty>::make();
    for (const auto& name : selected_package_names) selected.insert(name.clone(), empty {});
    auto acquisition_plan =
        rstd_try(resolve_external_acquisition_plan(graph, external_sources, platform));

    struct CMakeBinding {
        usize                              prepared {};
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
    for (usize prepared_index {}; prepared_index < external_sources.cmake_dependencies.len();
         ++prepared_index) {
        const auto& declaration = external_sources.cmake_dependencies[prepared_index];
        if (declaration.package >= graph.packages.len() ||
            catalog_indices[declaration.package].is_none()) {
            continue;
        }
        const auto& package = graph.packages[declaration.package];
        auto        requirement =
            resolve_cmake_requirement_for_platform(declaration.requirement, platform);
        if (requirement.is_err()) return Err(rstd::move(requirement).unwrap_err());
        bindings.push(CMakeBinding {
            .prepared           = prepared_index,
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
                                       if (left.requirement.package != right.requirement.package) {
                                           return left.requirement.package <
                                                  right.requirement.package;
                                       }
                                       if (left.owner != right.owner)
                                           return left.owner < right.owner;
                                       if (left.requirement.alias != right.requirement.alias) {
                                           return left.requirement.alias < right.requirement.alias;
                                       }
                                       return left.prepared < right.prepared;
                                   });

    auto source_catalog = cpp::ExternalSourceRootCatalog {};
    for (const auto& source : external_sources.sources) {
        if (source.acquired.is_none()) continue;
        source_catalog.sources.push(cpp::ExternalSourceRoot {
            .package      = source.package,
            .package_name = graph.packages[source.package].manifest.name.clone(),
            .name         = source.name.clone(),
            .root         = source.acquired->root.clone(),
            .identity     = source.acquired->identity.clone(),
            .cacheable    = source.acquired->cacheable,
        });
    }
    if (! acquisition_plan.archives.is_empty()) {
        auto requests = Vec<lito::source::ArchiveSourceFetchRequest>::with_capacity(
            acquisition_plan.archives.len());
        for (auto& acquisition : acquisition_plan.archives) {
            requests.push(rstd::move(acquisition.request));
        }
        auto materialization_root = layout.source_materialization_root();
        auto fetched = lito::source::acquire_archive_frontier(rstd::move(requests),
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
        for (usize index {}; index < acquisition_plan.archives.len(); ++index) {
            const auto& owner = acquisition_plan.archives[index].owner;
            if (owner.is_PackageExternal()) {
                const auto& source = external_sources.sources[owner.as_PackageExternal().index];
                source_catalog.sources.push(cpp::ExternalSourceRoot {
                    .package      = source.package,
                    .package_name = graph.packages[source.package].manifest.name.clone(),
                    .name         = source.name.clone(),
                    .root         = (*fetched)[index].root.clone(),
                    .identity     = (*fetched)[index].identity.clone(),
                    .cacheable    = (*fetched)[index].cacheable,
                });
                continue;
            }
            const auto    prepared = owner.as_CMakeExternal().index;
            CMakeBinding* binding  = nullptr;
            for (auto& candidate : bindings) {
                if (candidate.prepared == prepared) {
                    binding = rstd::addressof(candidate);
                    break;
                }
            }
            if (binding == nullptr) {
                return lito::dependency::dependency_failure<PreparedExternalCatalog>(
                    "external acquisition plan refers to an unavailable CMake dependency"_str);
            }
            binding->requirement.source = SelectedCMakeDependencySource::Directory(
                (*fetched)[index].root.clone(), (*fetched)[index].identity.clone(), true);
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
            prepared->root.clone(), prepared->identity.clone(), prepared->cacheable);
    }

    auto cargo_provider = Option<lito::tools::cargo::Provider> {};
    auto assets         = ExternalAssetCatalog {};
    for (usize package_index {}; package_index < graph.packages.len(); ++package_index) {
        if (catalog_indices[package_index].is_none()) continue;
        const auto& package = graph.packages[package_index];
        auto        dependencies =
            resolve_cargo_dependencies(package.manifest.cargo_external_dependencies,
                                       package_index,
                                       package.manifest.name.as_str(),
                                       lito::manifest::package_manifest_language(package.manifest),
                                       source_catalog,
                                       profile,
                                       platform,
                                       layout,
                                       cargo_config,
                                       source_config,
                                       tool_resolver,
                                       process_environment,
                                       jobs,
                                       cargo_provider,
                                       observer);
        if (dependencies.is_err()) return Err(rstd::move(dependencies).unwrap_err());
        auto& destination = result.packages[*catalog_indices[package_index]].dependencies;
        for (auto& dependency : dependencies->usage) destination.push(rstd::move(dependency));
        for (auto& asset : dependencies->assets) {
            auto inserted = assets.insert(rstd::move(asset));
            if (inserted.is_err()) {
                return lito::dependency::dependency_failure<PreparedExternalCatalog>(
                    rstd::move(inserted).unwrap_err());
            }
        }
    }

    if (bindings.is_empty()) {
        auto provenance = rstd_try(project_external_source_provenance(graph, source_catalog));
        return Ok(PreparedExternalCatalog {
            .usage      = rstd::move(result),
            .sources    = rstd::move(source_catalog),
            .assets     = rstd::move(assets),
            .provenance = rstd::move(provenance),
        });
    }

    const auto cmake_requirement = lito::tools::external_dependency_tool_requirement(
        lito::tools::HostToolCapability::CMakeProject,
        bindings[usize {}].owner,
        bindings[usize {}].requirement.alias);
    auto resolved_tool =
        cmake_config.executable.is_empty()
            ? tool_resolver.require(lito::tools::Tool::CMake, cmake_requirement)
            : tool_resolver.resolve(cmake_config.executable.as_path(), "CMake executable"_str);
    if (resolved_tool.is_err()) {
        return Err(
            cmake_error("resolve CMake executable"_str, rstd::move(resolved_tool).unwrap_err()));
    }
    auto resolved_cmake       = cmake_config.clone();
    resolved_cmake.executable = rstd::move(resolved_tool).unwrap().executable;
    auto identified = identify_cmake_provider(rstd::move(resolved_cmake), process_environment);
    if (identified.is_err()) return Err(rstd::move(identified).unwrap_err());
    resolved_cmake = rstd::move(identified).unwrap();

    auto cmake_work_root = layout.cmake_work_root();
    for (usize index {}; index < bindings.len(); ++index) {
        auto folded = bindings[index].requirement.package.clone();
        folded.as_mut_str().make_ascii_lowercase();
        for (usize prior {}; prior < index; ++prior) {
            auto prior_folded = bindings[prior].requirement.package.clone();
            prior_folded.as_mut_str().make_ascii_lowercase();
            if (folded != prior_folded.as_str() ||
                bindings[index].requirement.package ==
                    bindings[prior].requirement.package.as_str()) {
                continue;
            }
            return lito::dependency::dependency_failure<PreparedExternalCatalog>(rstd::format(
                "CMake packages '{}' and '{}' have colliding portable work directory names",
                bindings[prior].requirement.package.as_str(),
                bindings[index].requirement.package.as_str()));
        }
    }
    for (usize left {}; left < bindings.len();) {
        auto right = left + usize(1);
        while (right < bindings.len() &&
               bindings[right].requirement.package == bindings[left].requirement.package.as_str()) {
            ++right;
        }
        auto requirements = Vec<ResolvedCMakeDependencyRequirement>::with_capacity(right - left);
        for (auto index = left; index < right; ++index) {
            auto requirement = materialize_cmake_requirement(bindings[index].requirement);
            if (requirement.is_err()) return Err(rstd::move(requirement).unwrap_err());
            requirements.push(rstd::move(requirement).unwrap());
        }
        auto package = resolve_cmake_package(requirements);
        if (package.is_err()) return Err(rstd::move(package).unwrap_err());
        const auto contextualize = [&](lito::dependency::DependencyError error) {
            for (auto index = left; index < right; ++index) {
                if (! bindings[index].installed_override) continue;
                return lito::dependency::DependencyError::CMakeOverride(
                    bindings[index].requirement.package.clone(),
                    Box<lito::dependency::DependencyError>::make(rstd::move(error)));
            }
            return error;
        };
        auto plan = plan_cmake_package(package->requirement,
                                       resolved_cmake,
                                       configuration,
                                       profile,
                                       linker,
                                       platform.compiler_default,
                                       platform.effective_target.triple.as_str(),
                                       cmake_work_root.as_path(),
                                       jobs,
                                       android_cmake,
                                       cmake_find_install_prefix);
        if (plan.is_err()) return Err(contextualize(rstd::move(plan).unwrap_err()));
        auto tool_observer = cmake_observer(observer);
        auto snapshot      = execute_cmake_package(*plan, process_environment, tool_observer);
        if (snapshot.is_err()) return Err(contextualize(rstd::move(snapshot).unwrap_err()));
        for (auto index = left; index < right; ++index) {
            auto usage = materialize_cmake_usage(*plan, *snapshot, requirements[index - left]);
            if (usage.is_err()) return Err(contextualize(rstd::move(usage).unwrap_err()));
            auto dependency = rstd::move(usage).unwrap();
            auto normalized = normalize_clang_link_arguments(rstd::move(dependency.link_arguments));
            if (normalized.is_err()) {
                return lito::dependency::dependency_failure<PreparedExternalCatalog>(
                    rstd::format("{}", rstd::move(normalized).unwrap_err()));
            }
            dependency.link_arguments    = rstd::move(normalized->arguments);
            dependency.link_requirements = rstd::move(normalized->requirements);
            result.packages[bindings[index].catalog].dependencies.push(rstd::move(dependency));
            for (const auto& set : snapshot->assets) {
                auto copied   = set.clone();
                copied.alias  = bindings[index].requirement.alias.clone();
                auto inserted = assets.insert(rstd::move(copied));
                if (inserted.is_err()) {
                    return lito::dependency::dependency_failure<PreparedExternalCatalog>(
                        rstd::move(inserted).unwrap_err());
                }
            }
        }
        left = right;
    }
    auto provenance = rstd_try(project_external_source_provenance(graph, source_catalog));
    return Ok(PreparedExternalCatalog {
        .usage      = rstd::move(result),
        .sources    = rstd::move(source_catalog),
        .assets     = rstd::move(assets),
        .provenance = rstd::move(provenance),
    });
}

} // namespace lito
