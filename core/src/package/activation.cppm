module;
#include <rstd/macro.hpp>

export module lito.core:package.activation;

import rstd;
import lito.system;
import :package.graph;
import :package.error;
import :package.features;
import :package.condition;
import :package.runtime;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::system;
using IndexMap  = rstd::collections::BTreeMap<String, usize>;
using StringSet = rstd::collections::BTreeMap<String, empty>;

export namespace lito::package
{

struct SelectedPackageClosure {
    Vec<String> target;
    Vec<String> host;
    Vec<String> plugins;
    Vec<String> providers;
    Vec<String> processors;
    Vec<String> tools;
    Vec<String> install;
};

auto select_conditioned_packages(ResolvedPackageGraph&              graph,
                                 const Vec<String>&                 selected_roots,
                                 const Vec<PackageTargetId>&        selected_targets,
                                 const TargetInfo*                  target,
                                 Option<ref<str>>                   artifact_processor,
                                 const FeatureSelection&            features,
                                 const PackageConditionEnvironment& environment,
                                 bool install) -> PackageResult<SelectedPackageClosure> {
    auto indices = IndexMap::make();
    for (usize index {}; index < graph.packages.len(); ++index) {
        indices.insert(graph.packages[index].manifest.name.clone(), index);
    }

    auto development = StringSet::make();
    for (const auto& selected_target : selected_targets) {
        if (selected_target.kind == PackageTargetKind::Test ||
            selected_target.kind == PackageTargetKind::Benchmark ||
            selected_target.kind == PackageTargetKind::Example ||
            selected_target.kind == PackageTargetKind::CompileTest) {
            development.insert(selected_target.package.clone(), empty {});
        }
    }

    auto       requests         = FeatureRequests {};
    auto       host_environment = Option<lito::condition::Context> {};
    auto       installing       = StringSet::make();
    auto       selected_target  = StringSet::make();
    auto       selected_host    = StringSet::make();
    auto       plugins          = StringSet::make();
    auto       providers        = StringSet::make();
    auto       processors       = StringSet::make();
    auto       tools            = StringSet::make();
    const auto has_host_tool    = [&](ref<str> name) noexcept {
        auto index = indices.get(name);
        return index.is_some() &&
               lito::manifest::package_has_host_tool_target(graph.packages[**index].manifest);
    };
    for (const auto& root : selected_roots) {
        if (! indices.contains_key(root.as_str()))
            return Err(PackageError::Message(
                rstd::format("selected root package '{}' is missing", root.as_str())));
        requests.root(root.as_str(), features);
        if (install) installing.insert(root.clone(), empty {});
        auto has_target   = false;
        auto has_plugin   = false;
        auto has_provider = false;
        for (const auto& selected : selected_targets) {
            if (selected.package != root.as_str()) continue;
            if (selected.kind == PackageTargetKind::Plugin)
                has_plugin = true;
            else if (selected.kind == PackageTargetKind::ProcMacro)
                has_provider = true;
            else
                has_target = true;
        }
        if (! has_target && ! has_plugin && ! has_provider) has_target = true;
        if (has_target) selected_target.insert(root.clone(), empty {});
        if (has_plugin) {
            selected_host.insert(root.clone(), empty {});
            plugins.insert(root.clone(), empty {});
        }
        if (has_provider) {
            selected_host.insert(root.clone(), empty {});
            providers.insert(root.clone(), empty {});
        }
    }
    const auto consume = [&](String current, bool host) -> PackageResult<empty> {
        auto& selected = host ? selected_host : selected_target;
        if (! selected.contains_key(current.as_str())) return Ok(empty {});
        auto index = indices.get(current.as_str());
        if (index.is_none()) {
            return Err(PackageError::Message(rstd::format(
                "selected package '{}' is missing from resolved graph", current.as_str())));
        }
        if (! host && target != nullptr &&
            ! graph.packages[**index].manifest.target.matches(*target)) {
            return Err(
                PackageError::Message(rstd::format("package '{}' does not support target '{}'",
                                                   current.as_str(),
                                                   target->triple.as_str())));
        }
        auto& package = graph.packages[**index];
        if (host && host_environment.is_none()) {
            host_environment = Some(environment.resolve_host != nullptr
                                        ? rstd_try(environment.resolve_host(environment.host_state))
                                        : environment.host.clone());
        }
        auto context =
            make_package_condition_context(package, host ? *host_environment : environment.target);
        for (auto& dependency : package.dependencies) {
            auto enabled = rstd_try(dependency_condition_matches(
                package, resolved_dependency_name(dependency), dependency.activation, context));
            (host ? dependency.activation.host : dependency.activation.target) = enabled;
            if (! enabled) continue;
            requests.dependency(current.as_str(), dependency);
            if (dependency.is_Plugin()) {
                if (host && plugins.contains_key(current.as_str())) {
                    return Err(PackageError::Message(
                        rstd::format("plugin '{}' cannot depend on plugin '{}'",
                                     current.as_str(),
                                     dependency.as_Plugin().value.name.as_str())));
                }
                selected_host.insert(dependency.as_Plugin().value.name.clone(), empty {});
                plugins.insert(dependency.as_Plugin().value.name.clone(), empty {});
            } else if (dependency.is_Pmacro()) {
                if (host) {
                    return Err(PackageError::Message(
                        rstd::format("pmacro provider '{}' cannot depend on pmacro provider '{}'",
                                     current.as_str(),
                                     dependency.as_Pmacro().value.name.as_str())));
                }
                selected_host.insert(dependency.as_Pmacro().value.name.clone(), empty {});
                providers.insert(dependency.as_Pmacro().value.name.clone(), empty {});
            } else if (! host && artifact_processor.is_some() && dependency.is_Cpp() &&
                       dependency.as_Cpp().value.name.as_str() == **artifact_processor) {
                selected_host.insert(dependency.as_Cpp().value.name.clone(), empty {});
                processors.insert(dependency.as_Cpp().value.name.clone(), empty {});
            } else if (dependency.is_Cpp() &&
                       has_host_tool(dependency.as_Cpp().value.name.as_str())) {
                tools.insert(dependency.as_Cpp().value.name.clone(), empty {});
                if (host)
                    selected_host.insert(dependency.as_Cpp().value.name.clone(), empty {});
                else
                    selected_target.insert(dependency.as_Cpp().value.name.clone(), empty {});
            } else if (host) {
                selected_host.insert(String::make(resolved_dependency_name(dependency)), empty {});
            } else {
                selected_target.insert(String::make(resolved_dependency_name(dependency)),
                                       empty {});
            }
        }
        if (! host && development.contains_key(current.as_str())) {
            for (auto& dependency : package.dev_dependencies) {
                dependency.activation.target = rstd_try(dependency_condition_matches(
                    package, resolved_dependency_name(dependency), dependency.activation, context));
                if (! dependency.activation.target) continue;
                requests.dependency(current.as_str(), dependency);
                if (dependency.is_Plugin()) {
                    selected_host.insert(dependency.as_Plugin().value.name.clone(), empty {});
                    plugins.insert(dependency.as_Plugin().value.name.clone(), empty {});
                } else if (dependency.is_Pmacro()) {
                    selected_host.insert(dependency.as_Pmacro().value.name.clone(), empty {});
                    providers.insert(dependency.as_Pmacro().value.name.clone(), empty {});
                } else if (dependency.is_Cpp() &&
                           has_host_tool(dependency.as_Cpp().value.name.as_str())) {
                    tools.insert(dependency.as_Cpp().value.name.clone(), empty {});
                    selected_target.insert(dependency.as_Cpp().value.name.clone(), empty {});
                } else {
                    selected_target.insert(String::make(resolved_dependency_name(dependency)),
                                           empty {});
                }
            }
        }
        if (! host && installing.contains_key(current.as_str())) {
            for (auto& dependency : package.runtime_dependencies) {
                dependency.activation.target = rstd_try(dependency_condition_matches(
                    package, dependency.name.as_str(), dependency.activation, context));
                if (! dependency.activation.target) continue;
                installing.insert(dependency.name.clone(), empty {});
                selected_target.insert(dependency.name.clone(), empty {});
            }
        }
        return Ok(empty {});
    };
    auto incoming = Vec<usize>::with_capacity(graph.packages.len());
    for (auto& package : graph.packages) {
        incoming.push(usize {});
        package.host_view = false;
        package.features.clear();
        package.selected_pkg_config_dependencies = None();
        package.selected_cmake_dependencies      = None();
        package.selected_cargo_dependencies      = None();
        for (auto& edge : package.dependencies)
            edge.activation.target = edge.activation.host = false;
        for (auto& edge : package.dev_dependencies)
            edge.activation.target = edge.activation.host = false;
        for (auto& edge : package.runtime_dependencies)
            edge.activation.target = edge.activation.host = false;
    }
    const auto count = [&](ref<str> name) -> PackageResult<empty> {
        auto index = indices.get(name);
        if (index.is_none())
            return Err(PackageError::Message(
                rstd::format("dependency '{}' is missing from resolved graph", name)));
        ++incoming[**index];
        return Ok(empty {});
    };
    for (const auto& package : graph.packages) {
        for (const auto& edge : package.dependencies)
            rstd_try(count(resolved_dependency_name(edge)));
        for (const auto& edge : package.dev_dependencies)
            rstd_try(count(resolved_dependency_name(edge)));
        for (const auto& edge : package.runtime_dependencies) rstd_try(count(edge.name.as_str()));
    }
    auto ready = Vec<usize>::make();
    for (usize index {}; index < incoming.len(); ++index)
        if (incoming[index] == usize {}) ready.emplace_back(index);
    usize visited {};
    while (! ready.is_empty()) {
        const auto index   = *ready.pop();
        auto&      package = graph.packages[index];
        const auto name    = package.manifest.name.as_str();
        const auto active  = selected_target.contains_key(name) || selected_host.contains_key(name);
        rstd_try(requests.resolve(package, active && features.all_features));
        if (selected_target.contains_key(name)) {
            auto context = make_package_condition_context(package, environment.target);
            rstd_try(resolve_external_dependency_conditions(package, context));
        }
        if (active) {
            rstd_try(consume(package.manifest.name.clone(), false));
            rstd_try(consume(package.manifest.name.clone(), true));
        }
        const auto release = [&](ref<str> dependency) {
            const auto index = **indices.get(dependency);
            if (--incoming[index] == usize {}) ready.emplace_back(index);
        };
        for (const auto& edge : package.dependencies) release(resolved_dependency_name(edge));
        for (const auto& edge : package.dev_dependencies) release(resolved_dependency_name(edge));
        for (const auto& edge : package.runtime_dependencies) release(edge.name.as_str());
        ++visited;
    }
    if (visited != graph.packages.len())
        return Err(PackageError::Message("package dependency cycle in condition selection"_Str));

    auto result = SelectedPackageClosure {};
    for (const auto& package : graph.packages) {
        if (selected_target.contains_key(package.manifest.name.as_str())) {
            result.target.push(package.manifest.name.clone());
        }
        if (selected_host.contains_key(package.manifest.name.as_str())) {
            result.host.push(package.manifest.name.clone());
        }
        if (plugins.contains_key(package.manifest.name.as_str())) {
            result.plugins.push(package.manifest.name.clone());
        }
        if (providers.contains_key(package.manifest.name.as_str())) {
            result.providers.push(package.manifest.name.clone());
        }
        if (processors.contains_key(package.manifest.name.as_str())) {
            result.processors.push(package.manifest.name.clone());
        }
        if (tools.contains_key(package.manifest.name.as_str())) {
            result.tools.push(package.manifest.name.clone());
        }
    }
    if (install)
        result.install =
            rstd_try(resolve_runtime_package_closure(graph, selected_roots, target)).packages;
    return Ok(rstd::move(result));
}
} // namespace lito::package
