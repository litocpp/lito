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

export namespace lito::package
{

struct FeatureSelection {
    Vec<String> enabled;
    bool        default_features { true };
    bool        all_features { false };
};

class FeatureRequests {
    struct Request {
        String package;
        String feature;
        String source;
    };
    Vec<Request> requests_;
    Vec<Request> defaults_;

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

    auto root(ref<str> package, const FeatureSelection& selection) -> void {
        auto source = rstd::format("command line for root package '{}'", package);
        for (const auto& name : selection.enabled) request(package, name.as_str(), source.as_str());
        if (selection.default_features) {
            auto reason = rstd::format("root package '{}' default features", package);
            defaults(package, reason.as_str());
        }
    }

    auto dependency(ref<str> owner, const ResolvedRequiredDependency& edge) -> void {
        const auto collect = [&](const auto& value) {
            auto source =
                rstd::format("dependency '{}' from package '{}'", value.name.as_str(), owner);
            for (const auto& feature : value.features)
                request(value.name.as_str(), feature.as_str(), source.as_str());
            if (value.default_features) defaults(value.name.as_str(), source.as_str());
        };
        if (edge.is_Cpp())
            collect(edge.as_Cpp().value);
        else if (edge.is_Plugin())
            collect(edge.as_Plugin().value);
        else if (edge.is_Pmacro())
            collect(edge.as_Pmacro().value);
    }

    auto resolve(ResolvedPackage& package, bool all_features) const -> PackageResult<empty> {
        const auto name = package.manifest.name.as_str();
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
                      const Vec<String>*          host_packages = nullptr) -> PackageResult<empty> {
    auto       requests = FeatureRequests {};
    const auto selected = [&](ref<str> name) {
        for (const auto& value : selected_packages)
            if (value == name) return true;
        if (host_packages != nullptr)
            for (const auto& value : *host_packages)
                if (value == name) return true;
        return false;
    };
    for (const auto& root : selected_roots) {
        auto found = false;
        for (const auto& package : graph.packages)
            if (package.manifest.name == root.as_str()) found = true;
        if (! found)
            return Err(PackageError::Message(
                rstd::format("selected root package '{}' is missing", root.as_str())));
        requests.root(root.as_str(), selection);
    }
    for (const auto& package : graph.packages) {
        if (! selected(package.manifest.name.as_str())) continue;
        for (const auto& edge : package.active_dependencies())
            if (selected(resolved_dependency_name(edge)))
                requests.dependency(package.manifest.name.as_str(), edge);
        auto development = false;
        for (const auto& target : selected_targets) {
            if (target.package != package.manifest.name.as_str()) continue;
            if (target.kind == PackageTargetKind::Test ||
                target.kind == PackageTargetKind::Benchmark ||
                target.kind == PackageTargetKind::Example ||
                target.kind == PackageTargetKind::CompileTest)
                development = true;
        }
        if (development)
            for (const auto& edge : package.active_dev_dependencies())
                if (selected(resolved_dependency_name(edge)))
                    requests.dependency(package.manifest.name.as_str(), edge);
    }
    for (auto& package : graph.packages)
        rstd_try(requests.resolve(
            package, selection.all_features && selected(package.manifest.name.as_str())));
    return Ok(empty {});
}

} // namespace lito::package
