module;
#include <rstd/macro.hpp>

export module lito.dependency:catalog;

import rstd;
import lito.error;
import lito.cpp;
import lito.dependency.contract;
import lito.build.configuration;
import lito.build.profile_contract;
import lito.build.contract;
import lito.platform.contract;
import lito.package.graph_contract;
import lito.system.environment;
import lito.dependency.cmake;
import :external_source;
import :pkg_config;
import :pkg_config_support;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

struct ExternalPackageUsage {
    String                          package;
    Vec<ResolvedExternalDependency> dependencies;
    bool                            consumed { false };
};

struct ExternalUsageCatalog {
    Vec<ExternalPackageUsage> packages;

    auto take(ref<str> package) -> Result<Vec<ResolvedExternalDependency>> {
        for (auto& entry : packages) {
            if (entry.package.as_str() != package) continue;
            if (entry.consumed) {
                return dependency_failure<Vec<ResolvedExternalDependency>>(rstd::format(
                    "external usage for package '{}' was consumed more than once", package));
            }
            entry.consumed = true;
            return Ok(rstd::move(entry.dependencies));
        }
        return dependency_failure<Vec<ResolvedExternalDependency>>(
            rstd::format("external usage catalog has no package '{}'", package));
    }

    auto all_consumed() const noexcept -> bool {
        for (const auto& entry : packages) {
            if (! entry.consumed) return false;
        }
        return true;
    }
};

auto resolve_external_usage_catalog(const ResolvedPackageGraph&       graph,
                                    const Vec<String>&                selected_package_names,
                                    const PkgConfigProviderConfig&    pkg_config,
                                    const CMakeProviderConfig&        cmake_config,
                                    const BuildConfiguration&         configuration,
                                    const ProfileSpec&                profile,
                                    const BuildPlatform&              platform,
                                    const CppArgumentParser&          parser,
                                    ToolResolver&                     tool_resolver,
                                    const ResolvedProcessEnvironment& process_environment,
                                    usize                             jobs,
                                    const Option<BuildObserver>&      observer)
    -> Result<ExternalUsageCatalog> {
    if (jobs == usize {}) {
        return dependency_failure<ExternalUsageCatalog>(
            "external dependency jobs must be greater than zero"_str);
    }
    auto selected = rstd::collections::BTreeMap<String, empty>::make();
    for (const auto& name : selected_package_names) selected.insert(name.clone(), empty {});

    struct CMakeBinding {
        usize                              catalog {};
        String                             owner;
        ResolvedCMakeDependencyRequirement requirement;
    };
    auto result   = ExternalUsageCatalog {};
    auto bindings = Vec<CMakeBinding>::make();
    for (const auto& package : graph.packages) {
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
        result.packages.push(ExternalPackageUsage {
            .package      = package.manifest.name.clone(),
            .dependencies = rstd::move(dependencies).unwrap(),
        });
        for (const auto& declaration : package.cmake_external_dependencies) {
            auto requirement = resolve_cmake_requirement_for_platform(declaration, platform);
            if (requirement.is_err()) return Err(rstd::move(requirement).unwrap_err());
            bindings.push(CMakeBinding {
                .catalog     = catalog_index,
                .owner       = package.manifest.name.clone(),
                .requirement = rstd::move(requirement).unwrap(),
            });
        }
    }
    if (bindings.is_empty()) return Ok(rstd::move(result));
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
        auto error = rstd::move(resolved_tool).unwrap_err();
        return dependency_failure<ExternalUsageCatalog>(rstd::move(error.message));
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
        auto fetched = acquire_archive_frontier(rstd::move(archive_requests),
                                                jobs,
                                                resolved_cmake.executable.as_path(),
                                                process_environment);
        if (fetched.is_err()) return Err(rstd::move(fetched).unwrap_err());
        for (usize index {}; index < fetched->len(); ++index) {
            auto acquired    = rstd::move((*fetched)[index]);
            auto cmake_lists = acquired.root.join(PathBuf::from("CMakeLists.txt"_str).as_path());
            auto has_project = rstd::fs::exists(cmake_lists.as_path());
            if (has_project.is_err()) {
                return dependency_failure<ExternalUsageCatalog>(
                    rstd::format("cannot inspect archive source CMake project '{}': {}",
                                 cmake_lists.as_path(),
                                 rstd::move(has_project).unwrap_err()));
            }
            bindings[archive_bindings[index]].requirement.source =
                ResolvedCMakeDependencySource::Directory(
                    rstd::move(acquired.root), rstd::move(acquired.identity), *has_project, true);
        }
    }

    auto snapshots = rstd::collections::BTreeMap<String, CMakeUsageSnapshot>::make();
    for (auto& binding : bindings) {
        auto plan = plan_cmake_package(binding.requirement,
                                       resolved_cmake,
                                       configuration,
                                       profile,
                                       platform.compiler_default,
                                       platform.effective_target.triple.as_str(),
                                       jobs);
        if (plan.is_err()) return Err(rstd::move(plan).unwrap_err());
        auto key_text = plan->area.query_root.as_path().to_str();
        if (key_text.is_none()) {
            return dependency_failure<ExternalUsageCatalog>(rstd::format(
                "CMake query path '{}' is not valid UTF-8", plan->area.query_root.as_path()));
        }
        auto cached = snapshots.get(*key_text);
        auto usage  = [&]() -> Result<ResolvedExternalDependency> {
            if (cached.is_some()) return materialize_cmake_usage(*plan, **cached, parser);
            auto executed = execute_cmake_package(*plan, process_environment, observer);
            if (executed.is_err()) return Err(rstd::move(executed).unwrap_err());
            auto materialized = materialize_cmake_usage(*plan, *executed, parser);
            if (materialized.is_err()) return Err(rstd::move(materialized).unwrap_err());
            snapshots.insert(String::make(*key_text), rstd::move(executed).unwrap());
            return materialized;
        }();
        if (usage.is_err()) return Err(rstd::move(usage).unwrap_err());
        result.packages[binding.catalog].dependencies.push(rstd::move(usage).unwrap());
    }
    return Ok(rstd::move(result));
}

} // namespace lito
