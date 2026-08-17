module;
#include <rstd/macro.hpp>

export module lito.core:package.features;

import rstd;
import :package.graph;
import :package.error;
import :package.identity;
import :manifest.conditional;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito::package
{
struct FeatureRequest {
    String      name;
    Vec<String> sources;
};

} // namespace lito::package

export namespace lito::package
{

struct FeatureSelection {
    Vec<String> enabled;
    bool        default_features { true };
};

auto resolve_features(ResolvedPackageGraph& graph,
                      const Vec<String>& selected_roots,
                      const Vec<String>& selected_packages,
                      const Vec<PackageTargetId>& selected_targets,
                      const FeatureSelection& selection) -> PackageResult<empty> {
    auto indices = rstd::collections::BTreeMap<String, usize>::make();
    for (usize index {}; index < graph.packages.len(); ++index) {
        indices.insert(graph.packages[index].manifest.name.clone(), index);
    }
    auto defaults = Vec<bool>::with_capacity(graph.packages.len());
    auto requests = Vec<Vec<FeatureRequest>>::with_capacity(graph.packages.len());
    auto default_sources = Vec<Vec<String>>::with_capacity(graph.packages.len());
    for (usize index {}; index < graph.packages.len(); ++index) {
        defaults.push(false);
        requests.emplace_back();
        default_sources.emplace_back();
    }
    const auto append_unique = [](Vec<String>& values, ref<str> value) -> void {
        for (const auto& existing : values) {
            if (existing.as_str() == value) return;
        }
        values.push(String::make(value));
    };
    const auto append_request = [&](Vec<FeatureRequest>& values,
                                    ref<str>             name,
                                    String               source) -> void {
        for (auto& existing : values) {
            if (existing.name.as_str() != name) continue;
            append_unique(existing.sources, source.as_str());
            return;
        }
        auto sources = Vec<String>::make();
        sources.push(rstd::move(source));
        values.push(FeatureRequest { .name = String::make(name), .sources = rstd::move(sources) });
    };
    const auto selected = [&](ref<str> name) -> bool {
        for (const auto& package : selected_packages) {
            if (package.as_str() == name) return true;
        }
        return false;
    };
    const auto development = [&](ref<str> name) -> bool {
        for (const auto& target : selected_targets) {
            if (target.package.as_str() != name) continue;
            if (target.kind == PackageTargetKind::Test ||
                target.kind == PackageTargetKind::Benchmark ||
                target.kind == PackageTargetKind::CompileTest) {
                return true;
            }
        }
        return false;
    };
    for (const auto& root : selected_roots) {
        auto index = indices.get(root.as_str());
        if (index.is_none()) {
            return Err(PackageError::Message(
                rstd::format("selected root package '{}' is missing", root.as_str())));
        }
        if (selection.default_features) {
            defaults[**index] = true;
            append_unique(default_sources[**index],
                          rstd::format("root package '{}' default features", root.as_str()).as_str());
        }
        for (const auto& feature : selection.enabled) {
            append_request(requests[**index],
                           feature.as_str(),
                           rstd::format("command line for root package '{}'", root.as_str()));
        }
    }
    for (const auto& package : graph.packages) {
        if (! selected(package.manifest.name.as_str())) continue;
        const auto collect = [&](const Vec<ResolvedDependency>& dependencies)
            -> PackageResult<empty> {
            for (const auto& dependency : dependencies) {
                if (! selected(dependency.name.as_str())) continue;
                auto index = indices.get(dependency.name.as_str());
                if (index.is_none()) {
                    return Err(PackageError::Message(rstd::format(
                        "feature request targets missing package '{}'", dependency.name.as_str())));
                }
                if (dependency.default_features) {
                    defaults[**index] = true;
                    append_unique(default_sources[**index],
                                  rstd::format(
                                      "dependency '{}' from package '{}' default features",
                                      dependency.name.as_str(),
                                      package.manifest.name.as_str()).as_str());
                }
                for (const auto& feature : dependency.features) {
                    append_request(requests[**index],
                                   feature.as_str(),
                                   rstd::format("dependency '{}' from package '{}'",
                                                dependency.name.as_str(),
                                                package.manifest.name.as_str()));
                }
            }
            return Ok(empty {});
        };
        rstd_try(collect(package.dependencies));
        if (development(package.manifest.name.as_str())) {
            rstd_try(collect(package.dev_dependencies));
        }
    }
    for (usize index {}; index < graph.packages.len(); ++index) {
        auto& package = graph.packages[index];
        for (const auto& request : requests[index]) {
            auto found = false;
            for (const auto& declaration : package.manifest.features) {
                if (declaration.name.as_str() == request.name.as_str()) {
                    found = true;
                    break;
                }
            }
            if (! found) {
                return Err(PackageError::Message(rstd::format(
                    "package '{}' has no feature '{}'",
                    package.manifest.name.as_str(),
                    request.name.as_str())));
            }
        }
        package.features.clear();
        for (const auto& declaration : package.manifest.features) {
            auto enabled = defaults[index] && declaration.default_enabled;
            auto activation_sources = Vec<String>::make();
            if (enabled) {
                for (const auto& source : default_sources[index]) {
                    append_unique(activation_sources, source.as_str());
                }
            }
            for (const auto& request : requests[index]) {
                if (request.name.as_str() == declaration.name.as_str()) {
                    enabled = true;
                    for (const auto& source : request.sources) {
                        append_unique(activation_sources, source.as_str());
                    }
                    break;
                }
            }
            package.features.push(ResolvedFeature {
                .name = declaration.name.clone(),
                .macro_name = declaration.macro_name.clone(),
                .enabled = enabled,
                .activation_sources = rstd::move(activation_sources),
            });
        }
    }
    return Ok(empty {});
}

} // namespace lito::package
