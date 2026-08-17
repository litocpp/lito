module;
#include <rstd/macro.hpp>

export module lito.core:package.runtime;

import rstd;
import :package.graph;
import :package.error;
import lito.system;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;
using IndexMap = rstd::collections::BTreeMap<String, usize>;
using namespace lito;

export namespace lito::package
{

struct RuntimePackageEdge {
    String package;
    String dependency;
};

struct RuntimePackageClosure {
    Vec<String>             direct_packages;
    Vec<String>             packages;
    Vec<RuntimePackageEdge> edges;
};

} // namespace lito::package

using namespace lito::package;

template<typename T>
auto runtime_failure(String message) -> PackageResult<T> {
    return Err(PackageError::Message(rstd::move(message)));
}

class RuntimeClosureResolver {
public:
    RuntimeClosureResolver(const ResolvedPackageGraph& graph, const TargetInfo* target)
        : graph_(graph), target_(target), colors_(Vec<u8>::with_capacity(graph.packages.len())) {
        for (usize index {}; index < graph.packages.len(); ++index) {
            indices_.insert(graph.packages[index].manifest.name.clone(), index);
            colors_.push(u8 {});
        }
    }

    auto resolve(const Vec<String>& direct) -> PackageResult<RuntimePackageClosure> {
        auto result = RuntimePackageClosure {};
        for (const auto& package : direct) result.direct_packages.push(package.clone());
        for (const auto& package : direct) rstd_try(visit(package.as_str(), result));
        return Ok(rstd::move(result));
    }

private:
    auto visit(ref<str> name, RuntimePackageClosure& result) -> PackageResult<empty> {
        auto index = indices_.get(name);
        if (index.is_none()) {
            return runtime_failure<empty>(
                rstd::format("runtime package '{}' is missing from the resolved graph", name));
        }
        if (colors_[**index] == u8(2)) return Ok(empty {});
        if (colors_[**index] == u8(1)) {
            auto cycle = String::make();
            auto found = false;
            for (const auto& package : active_) {
                if (package.as_str() == name) found = true;
                if (! found) continue;
                if (! cycle.is_empty()) cycle.push_str(" -> "_str);
                cycle.push_str(package.as_str());
            }
            if (! cycle.is_empty()) cycle.push_str(" -> "_str);
            cycle.push_str(name);
            return runtime_failure<empty>(
                rstd::format("runtime dependency cycle: {}", cycle.as_str()));
        }

        const auto& package = graph_.packages[**index];
        if (target_ != nullptr && ! package.manifest.target.matches(*target_)) {
            return runtime_failure<empty>(
                rstd::format("runtime package '{}' does not support target '{}'",
                             name,
                             target_->triple.as_str()));
        }
        colors_[**index] = u8(1);
        active_.push(String::make(name));
        for (const auto& dependency : package.runtime_dependencies) {
            result.edges.push(RuntimePackageEdge {
                .package    = String::make(name),
                .dependency = dependency.name.clone(),
            });
            rstd_try(visit(dependency.name.as_str(), result));
        }
        active_.pop();
        colors_[**index] = u8(2);
        result.packages.push(String::make(name));
        return Ok(empty {});
    }

    const ResolvedPackageGraph& graph_;
    const TargetInfo*           target_ {};
    IndexMap                    indices_ { IndexMap::make() };
    Vec<u8>                     colors_;
    Vec<String>                 active_;
};

export namespace lito::package
{

auto resolve_runtime_package_closure(const ResolvedPackageGraph& graph,
                                     const Vec<String>&          direct_packages,
                                     const TargetInfo*           target = nullptr)
    -> PackageResult<RuntimePackageClosure> {
    return RuntimeClosureResolver(graph, target).resolve(direct_packages);
}

} // namespace lito::package
