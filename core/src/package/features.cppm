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

struct CommandLineFeature {
    String         selector;
    Option<String> package;
    String         feature;
};

auto parse_command_line_features(const Vec<String>& values)
    -> lito::package::PackageResult<Vec<CommandLineFeature>> {
    auto       result    = Vec<CommandLineFeature>::make();
    const auto separator = [](u8 byte) {
        return byte == u8(',') || byte == u8(' ') || byte == u8('\t') || byte == u8('\n') ||
               byte == u8('\r') || byte == u8('\v') || byte == u8('\f');
    };
    for (const auto& value : values) {
        auto begin = usize {};
        for (usize cursor {}; cursor <= value.len(); ++cursor) {
            if (cursor != value.len() && ! separator(value.as_str().as_bytes()[cursor])) continue;
            const auto start = begin;
            begin            = cursor + usize(1);
            if (start == cursor) continue;
            auto item = value.as_str().get(start, cursor).unwrap();
            if (item.starts_with("dep:"_str) || item.contains("?"_str))
                return Err(lito::package::PackageError::Message(rstd::format(
                    "unsupported command line feature '{}'; expected feature or package/feature",
                    item)));
            auto duplicate = false;
            for (const auto& existing : result)
                if (existing.selector == item) duplicate = true;
            if (duplicate) continue;
            auto entry = CommandLineFeature { .selector = item.into(), .feature = item.into() };
            if (auto parts = item.split_once("/"_str); parts.is_some()) {
                auto package = parts->get<0>();
                auto feature = parts->get<1>();
                if (package.is_empty() || feature.is_empty() || feature.contains("/"_str))
                    return Err(lito::package::PackageError::Message(rstd::format(
                        "invalid command line feature '{}'; expected package/feature with one '/'",
                        item)));
                entry.package = Some(String::make(package));
                entry.feature = feature.into();
            }
            result.push(rstd::move(entry));
        }
    }
    return Ok(rstd::move(result));
}

export namespace lito::package
{

struct PackageFeatureRequest {
    String      package;
    Vec<String> features;
    String      source;

    auto clone() const -> PackageFeatureRequest {
        return { package.clone(), features.clone(), source.clone() };
    }
};

struct FeatureSelection {
    Vec<String>                enabled;
    bool                       default_features { true };
    bool                       all_features { false };
    Vec<PackageFeatureRequest> configured;

    auto clone() const -> FeatureSelection {
        return {
            .enabled          = enabled.clone(),
            .default_features = default_features,
            .all_features     = all_features,
            .configured       = configured.iter()
                                    .map([](auto value) {
                                  return value->clone();
                              })
                              .collect<Vec<PackageFeatureRequest>>(),
        };
    }
};

class FeatureRequests {
    struct Request {
        String package;
        String feature;
        String source;
    };
    Vec<Request> requests_;
    Vec<Request> defaults_;
    struct DependencyRequest {
        String  owner;
        Request value;
    };
    Vec<DependencyRequest> dependency_requests_;

    template<typename Dependency>
    auto collect(const ResolvedPackage& owner, const Dependency& value, ref<str> kind) -> void {
        auto source = rstd::format(
            "{} '{}' from package '{}'", kind, value.name.as_str(), owner.manifest.name.as_str());
        for (const auto& feature : value.features)
            request(value.name.as_str(), feature.as_str(), source.as_str());
        if (value.default_features) defaults(value.name.as_str(), source.as_str());
        for (const auto& entry : dependency_requests_) {
            if (entry.owner == owner.manifest.name && entry.value.package == value.name)
                request(
                    value.name.as_str(), entry.value.feature.as_str(), entry.value.source.as_str());
        }
        for (const auto& declaration : owner.manifest.features) {
            auto enabled = false;
            for (const auto& feature : owner.features)
                if (feature.name == declaration.name && feature.enabled) enabled = true;
            if (! enabled) continue;
            for (const auto& forwarded : declaration.enables) {
                if (forwarded.dependency != value.name) continue;
                auto reason = rstd::format("feature '{}/{}' enables '{}/{}'",
                                           owner.manifest.name.as_str(),
                                           declaration.name.as_str(),
                                           value.name.as_str(),
                                           forwarded.feature.as_str());
                request(value.name.as_str(), forwarded.feature.as_str(), reason.as_str());
            }
        }
    }

public:
    auto request(ref<str> package, ref<str> feature, ref<str> source) -> void {
        for (const auto& entry : requests_) {
            if (entry.package == package && entry.feature == feature && entry.source == source)
                return;
        }
        requests_.push(Request { package.into(), feature.into(), source.into() });
    }

    auto defaults(ref<str> package, ref<str> source) -> void {
        for (const auto& entry : defaults_) {
            if (entry.package == package && entry.source == source) return;
        }
        defaults_.push(Request { package.into(), String::make(), source.into() });
    }

    auto select_roots(const ResolvedPackageGraph& graph,
                      const Vec<String>&          selected_roots,
                      const FeatureSelection&     selection) -> PackageResult<empty> {
        auto selectors = rstd_try(parse_command_line_features(selection.enabled));
        auto matched   = Vec<bool>::make();
        for (usize index {}; index < selectors.len(); ++index) matched.push(false);
        for (const auto& root : selected_roots) {
            const ResolvedPackage* package = nullptr;
            for (const auto& candidate : graph.packages)
                if (candidate.manifest.name == root) package = rstd::addressof(candidate);
            if (package == nullptr)
                return Err(PackageError::Message(
                    rstd::format("selected root package '{}' is missing", root.as_str())));
            if (selection.default_features) {
                auto reason = rstd::format("root package '{}' default features", root.as_str());
                defaults(root.as_str(), reason.as_str());
            }
            for (usize index {}; index < selectors.len(); ++index) {
                const auto& selector = selectors[index];
                auto        source = rstd::format("command line feature '{}' for root package '{}'",
                                                  selector.selector.as_str(),
                                                  root.as_str());
                if (selector.package.is_some()) {
                    auto       dependency = false;
                    const auto bind       = [&](const auto& edges) {
                        for (const auto& edge : edges) {
                            if (edge.is_Script() ||
                                resolved_dependency_name(edge) != selector.package->as_str())
                                continue;
                            dependency = true;
                        }
                    };
                    bind(package->dependencies);
                    bind(package->dev_dependencies);
                    for (const auto& edge : package->runtime_dependencies)
                        if (edge.name == selector.package->as_str()) dependency = true;
                    if (dependency) {
                        dependency_requests_.push({ root.clone(),
                                                    { selector.package->clone(),
                                                      selector.feature.clone(),
                                                      rstd::move(source) } });
                        matched[index] = true;
                    } else if (*selector.package == root) {
                        request(root.as_str(), selector.feature.as_str(), source.as_str());
                        matched[index] = true;
                    }
                } else {
                    for (const auto& declaration : package->manifest.features) {
                        if (declaration.name != selector.feature) continue;
                        request(root.as_str(), selector.feature.as_str(), source.as_str());
                        matched[index] = true;
                    }
                }
            }
        }
        for (usize index {}; index < selectors.len(); ++index)
            if (! matched[index])
                return Err(PackageError::Message(
                    rstd::format("command line feature '{}' does not match a selected root package "
                                 "feature or direct dependency",
                                 selectors[index].selector.as_str())));
        return Ok(empty {});
    }

    auto dependency(const ResolvedPackage& owner, const ResolvedRequiredDependency& edge) -> void {
        if (edge.is_Cpp())
            collect(owner, edge.as_Cpp().value, "dependency"_str);
        else if (edge.is_Plugin())
            collect(owner, edge.as_Plugin().value, "dependency"_str);
        else if (edge.is_Pmacro())
            collect(owner, edge.as_Pmacro().value, "dependency"_str);
    }

    auto dependency(const ResolvedPackage& owner, const ResolvedRuntimeDependency& edge) -> void {
        collect(owner, edge, "runtime dependency"_str);
    }

    auto configure(ref<str> package, const FeatureSelection& selection) -> void {
        for (const auto& value : selection.configured) {
            if (value.package != package) continue;
            for (const auto& feature : value.features)
                request(package, feature.as_str(), value.source.as_str());
        }
    }

    auto resolve(ResolvedPackage& package, bool all_features) const -> PackageResult<empty> {
        const auto name = package.manifest.name.as_str();
        for (const auto& declaration : package.manifest.features) {
            for (const auto& forwarded : declaration.enables) {
                const auto validate = [&](const auto& edges) -> PackageResult<empty> {
                    for (const auto& edge : edges) {
                        if (resolved_dependency_name(edge) == forwarded.dependency &&
                            edge.is_Script())
                            return Err(PackageError::Message(
                                rstd::format("feature '{}/{}' enables '{}/{}', but script "
                                             "dependencies do not support features",
                                             name,
                                             declaration.name.as_str(),
                                             forwarded.dependency.as_str(),
                                             forwarded.feature.as_str())));
                    }
                    return Ok(empty {});
                };
                rstd_try(validate(package.dependencies));
                rstd_try(validate(package.dev_dependencies));
            }
        }
        for (const auto& request : requests_) {
            if (request.package != name) continue;
            auto found = false;
            for (const auto& declaration : package.manifest.features)
                if (declaration.name == request.feature.as_str()) found = true;
            if (! found)
                return Err(PackageError::Message(
                    rstd::format("package '{}' has no feature '{}' requested by {}",
                                 name,
                                 request.feature.as_str(),
                                 request.source.as_str())));
        }
        package.features.clear();
        for (const auto& declaration : package.manifest.features) {
            auto feature = ResolvedFeature {
                .name       = declaration.name.clone(),
                .macro_name = declaration.macro_name.clone(),
                .enabled    = all_features,
            };
            const auto append = [&](ref<str> source) {
                for (const auto& existing : feature.activation_sources)
                    if (existing == source) return;
                feature.activation_sources.push(source.into());
                feature.enabled = true;
            };
            if (all_features) append("command line --all-features"_str);
            if (declaration.default_enabled) {
                for (const auto& request : defaults_)
                    if (request.package == name) append(request.source.as_str());
            }
            for (const auto& request : requests_)
                if (request.package == name && request.feature == declaration.name.as_str())
                    append(request.source.as_str());
            package.features.push(rstd::move(feature));
        }
        return Ok(empty {});
    }
};

auto resolve_features(ResolvedPackageGraph&       graph,
                      const Vec<String>&          selected_roots,
                      const Vec<String>&          selected_packages,
                      const Vec<PackageTargetId>& selected_targets,
                      const FeatureSelection&     selection,
                      const Vec<String>*          host_packages = nullptr,
                      const Vec<String>* install_packages       = nullptr) -> PackageResult<empty> {
    auto       requests = FeatureRequests {};
    const auto contains = [](const Vec<String>& names, ref<str> name) {
        for (const auto& value : names)
            if (value == name) return true;
        return false;
    };
    const auto selected = [&](ref<str> name) {
        return contains(selected_packages, name) ||
               (host_packages != nullptr && contains(*host_packages, name));
    };
    const auto dependencies = [&](const ResolvedPackage& package,
                                  const auto&            visit) -> PackageResult<empty> {
        const auto name   = package.manifest.name.as_str();
        const auto target = contains(selected_packages, name);
        const auto host   = host_packages != nullptr && contains(*host_packages, name);
        const auto active = [&](const auto& edge) {
            return ((target && edge.activation.target) || (host && edge.activation.host)) &&
                   selected(resolved_dependency_name(edge));
        };
        for (const auto& edge : package.dependencies)
            if (active(edge)) rstd_try(visit(edge));
        for (const auto& target : selected_targets) {
            if (target.package != name) continue;
            if (target.kind == PackageTargetKind::Test ||
                target.kind == PackageTargetKind::Benchmark ||
                target.kind == PackageTargetKind::Example ||
                target.kind == PackageTargetKind::CompileTest) {
                for (const auto& edge : package.dev_dependencies)
                    if (active(edge)) rstd_try(visit(edge));
                break;
            }
        }
        if (target && install_packages != nullptr && contains(*install_packages, name))
            for (const auto& edge : package.runtime_dependencies)
                if (edge.activation.target && contains(selected_packages, edge.name.as_str()))
                    rstd_try(visit(edge));
        return Ok(empty {});
    };
    rstd_try(requests.select_roots(graph, selected_roots, selection));
    auto indices  = rstd::collections::BTreeMap<String, usize>::make();
    auto incoming = Vec<usize>::make();
    for (usize index {}; index < graph.packages.len(); ++index) {
        indices.insert(graph.packages[index].manifest.name.clone(), index);
        incoming.push(usize {});
    }
    for (const auto& package : graph.packages) {
        rstd_try(dependencies(package, [&](const auto& edge) -> PackageResult<empty> {
            auto index = indices.get(resolved_dependency_name(edge));
            if (index.is_none())
                return Err(PackageError::Message(
                    rstd::format("dependency '{}' is missing from resolved graph",
                                 resolved_dependency_name(edge))));
            ++incoming[**index];
            return Ok(empty {});
        }));
    }
    auto ready = Vec<usize>::make();
    for (usize index {}; index < incoming.len(); ++index)
        if (incoming[index] == usize {}) ready.emplace_back(index);
    usize visited {};
    while (! ready.is_empty()) {
        auto&      package = graph.packages[*ready.pop()];
        const auto active  = selected(package.manifest.name.as_str());
        if (active) requests.configure(package.manifest.name.as_str(), selection);
        rstd_try(requests.resolve(package, selection.all_features && active));
        rstd_try(dependencies(package, [&](const auto& edge) -> PackageResult<empty> {
            requests.dependency(package, edge);
            auto index = **indices.get(resolved_dependency_name(edge));
            if (--incoming[index] == usize {}) ready.emplace_back(index);
            return Ok(empty {});
        }));
        ++visited;
    }
    if (visited != graph.packages.len())
        return Err(PackageError::Message("package dependency cycle in feature resolution"_Str));
    return Ok(empty {});
}

} // namespace lito::package
