export module lito.core:package.language;

import rstd;
import :manifest.language;
import :package.graph;
import :package.error;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::package
{

struct EffectiveLanguageStandards {
    Option<lito::manifest::CStandard>   c;
    Option<lito::manifest::CppStandard> cpp;
    Vec<String>                         c_provenance;
    Vec<String>                         cpp_provenance;
};

auto resolve_effective_language_standards(const ResolvedPackageGraph& graph,
                                          const Vec<String>&          selected_packages)
    -> PackageResult<EffectiveLanguageStandards> {
    auto selected = rstd::collections::BTreeMap<String, empty>::make();
    for (const auto& name : selected_packages) selected.insert(name.clone(), empty {});

    auto result = EffectiveLanguageStandards {};
    for (const auto& package : graph.packages) {
        if (! selected.contains_key(package.manifest.name.as_str()) ||
            package.manifest.standard.is_none()) {
            continue;
        }
        const auto& requirement = *package.manifest.standard;
        if (requirement.is_C()) {
            const auto minimum = requirement.as_C().minimum;
            if (result.c.is_none() || lito::manifest::c_standard_rank(minimum) >
                                          lito::manifest::c_standard_rank(*result.c)) {
                result.c = Some<lito::manifest::CStandard>(minimum);
                result.c_provenance.clear();
                result.c_provenance.push(package.manifest.name.clone());
            } else if (minimum == *result.c) {
                result.c_provenance.push(package.manifest.name.clone());
            }
            continue;
        }
        const auto minimum = requirement.as_Cpp().minimum;
        if (result.cpp.is_none() || lito::manifest::cpp_standard_rank(minimum) >
                                        lito::manifest::cpp_standard_rank(*result.cpp)) {
            result.cpp = Some<lito::manifest::CppStandard>(minimum);
            result.cpp_provenance.clear();
            result.cpp_provenance.push(package.manifest.name.clone());
        } else if (minimum == *result.cpp) {
            result.cpp_provenance.push(package.manifest.name.clone());
        }
    }

    for (const auto& package : graph.packages) {
        if (! selected.contains_key(package.manifest.name.as_str()) ||
            package.manifest.standard.is_none() || ! package.manifest.standard->is_C()) {
            continue;
        }
        for (const auto& dependency : package.dependencies) {
            if (! selected.contains_key(dependency.name.as_str())) continue;
            const lito::manifest::PackageManifest* dependency_manifest = nullptr;
            for (const auto& candidate : graph.packages) {
                if (candidate.manifest.name == dependency.name.as_str()) {
                    dependency_manifest = rstd::addressof(candidate.manifest);
                    break;
                }
            }
            if (dependency_manifest == nullptr || dependency_manifest->standard.is_none()) continue;
            if (dependency_manifest->standard->is_Cpp()) {
                return Err(PackageError::Message(
                    rstd::format("C package '{}' cannot depend on C++ package '{}'",
                                 package.manifest.name.as_str(),
                                 dependency.name.as_str())));
            }
        }
    }
    return Ok(rstd::move(result));
}

} // namespace lito::package
