export module lito.driver:package.selection;

import rstd;
import lito.core;
import lito.tools;
import lito.system;
import :package.resolver;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace lito::system;
using namespace lito::tools;
using namespace rstd::literals;
using IndexMap  = rstd::collections::BTreeMap<String, usize>;
using StringSet = rstd::collections::BTreeMap<String, empty>;
using namespace lito;

export namespace lito::package
{

enum class PackageSelectionPurpose
{
    All,
    Production,
    Documentation,
    Install,
    Test,
    Benchmark,
};

struct PackageSelection {
    PathBuf          root;
    Vec<String>      packages;
    FeatureSelection features;
};

struct ResolvedPackageSelection {
    ResolvedPackageGraph       graph;
    Vec<String>                selected_root_names;
    Vec<String>                install_package_names;
    Vec<String>                selected_package_names;
    Vec<PackageTargetId>       selected_targets;
    Vec<PackageTargetId>       effective_targets;
    EffectiveLanguageStandards standards;
};

} // namespace lito::package

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::package::PackageSelectionPurpose>
    : ImplBase<lito::package::PackageSelectionPurpose> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        auto name = "unknown"_str;
        switch (this->self()) {
        case lito::package::PackageSelectionPurpose::All: name = "all"_str; break;
        case lito::package::PackageSelectionPurpose::Production: name = "production"_str; break;
        case lito::package::PackageSelectionPurpose::Documentation:
            name = "documentation"_str;
            break;
        case lito::package::PackageSelectionPurpose::Install: name = "install"_str; break;
        case lito::package::PackageSelectionPurpose::Test: name = "test"_str; break;
        case lito::package::PackageSelectionPurpose::Benchmark: name = "benchmark"_str; break;
        }
        return formatter.write_str(name);
    }
};

} // namespace rstd

using namespace lito::package;

template<typename T>
auto package_selection_failure(String message) -> PackageSelectionResult<T> {
    return Err(PackageSelectionError::Message(rstd::move(message)));
}

template<typename T>
auto package_selection_failure(ref<str> message) -> PackageSelectionResult<T> {
    return Err(PackageSelectionError::Message(String::make(message)));
}

auto copy_strings(const Vec<String>& values) -> Vec<String> {
    auto result = Vec<String>::with_capacity(values.len());
    for (const auto& value : values) result.push(value.clone());
    return result;
}

auto selected_by_purpose(ProjectRootRole         role,
                         PackageTargetKind       kind,
                         PackageSelectionPurpose purpose) -> bool {
    if (purpose == PackageSelectionPurpose::All) return true;
    if (role == ProjectRootRole::AssociatedTest) {
        return purpose == PackageSelectionPurpose::Test &&
               (kind == PackageTargetKind::Test || kind == PackageTargetKind::CompileTest);
    }
    if (purpose == PackageSelectionPurpose::Test) {
        return kind == PackageTargetKind::Test || kind == PackageTargetKind::CompileTest;
    }
    if (purpose == PackageSelectionPurpose::Benchmark) {
        return kind == PackageTargetKind::Benchmark;
    }
    if (purpose == PackageSelectionPurpose::Documentation) {
        return kind == PackageTargetKind::Library;
    }
    if (purpose == PackageSelectionPurpose::Install) {
        return kind == PackageTargetKind::Binary;
    }
    return kind == PackageTargetKind::Library || kind == PackageTargetKind::Binary;
}

auto append_selected_targets(Vec<PackageTargetId>&                  output,
                             const lito::manifest::PackageManifest& package,
                             ProjectRootRole                        role,
                             PackageSelectionPurpose                purpose) -> bool {
    auto selected = false;
    for (const auto& target : package.targets) {
        const auto kind = lito::manifest::package_target_kind(target);
        if (! selected_by_purpose(role, kind, purpose)) continue;
        output.push(PackageTargetId {
            .package = package.name.clone(),
            .kind    = kind,
            .name    = String::make(lito::manifest::package_target_name(target)),
        });
        selected = true;
    }
    if (! package.compile_tests.is_empty() &&
        selected_by_purpose(role, PackageTargetKind::CompileTest, purpose)) {
        output.push(PackageTargetId {
            .package = package.name.clone(),
            .kind    = PackageTargetKind::CompileTest,
            .name    = package.name.clone(),
        });
        selected = true;
    }
    if (purpose == PackageSelectionPurpose::Install && package.install_script.is_some()) {
        selected = true;
    }
    return selected;
}

auto selected_closure(const ResolvedPackageGraph& graph,
                      const Vec<String>&          selected_roots,
                      const Vec<PackageTargetId>& selected_targets,
                      const TargetInfo*           target) -> PackageSelectionResult<Vec<String>> {
    auto indices = IndexMap::make();
    for (usize index {}; index < graph.packages.len(); ++index) {
        indices.insert(graph.packages[index].manifest.name.clone(), index);
    }

    auto development = StringSet::make();
    for (const auto& selected_target : selected_targets) {
        if (selected_target.kind == PackageTargetKind::Test ||
            selected_target.kind == PackageTargetKind::Benchmark ||
            selected_target.kind == PackageTargetKind::CompileTest) {
            development.insert(selected_target.package.clone(), empty {});
        }
    }

    auto pending  = copy_strings(selected_roots);
    auto selected = StringSet::make();
    while (! pending.is_empty()) {
        auto current = rstd::move(pending.pop()).unwrap();
        if (selected.contains_key(current.as_str())) continue;
        auto index = indices.get(current.as_str());
        if (index.is_none()) {
            return package_selection_failure<Vec<String>>(rstd::format(
                "selected package '{}' is missing from resolved graph", current.as_str()));
        }
        if (target != nullptr && ! graph.packages[**index].manifest.target.matches(*target)) {
            return package_selection_failure<Vec<String>>(
                rstd::format("package '{}' does not support target '{}'",
                             current.as_str(),
                             target->triple.as_str()));
        }
        selected.insert(current.clone(), empty {});
        for (const auto& dependency : graph.packages[**index].dependencies) {
            pending.push(dependency.name.clone());
        }
        if (development.contains_key(current.as_str())) {
            for (const auto& dependency : graph.packages[**index].dev_dependencies) {
                pending.push(dependency.name.clone());
            }
        }
    }

    auto result = Vec<String>::make();
    for (const auto& package : graph.packages) {
        if (selected.contains_key(package.manifest.name.as_str())) {
            result.push(package.manifest.name.clone());
        }
    }
    return Ok(rstd::move(result));
}

auto effective_compile_targets(const ResolvedPackageGraph& graph,
                               const Vec<String>&          selected_packages,
                               const Vec<PackageTargetId>& selected_targets)
    -> Vec<PackageTargetId> {
    auto result =
        Vec<PackageTargetId>::with_capacity(selected_targets.len() + selected_packages.len());
    const auto append = [&](PackageTargetId target) -> void {
        for (const auto& existing : result) {
            if (existing == target) return;
        }
        result.push(rstd::move(target));
    };
    for (const auto& target : selected_targets) append(target.clone());
    for (const auto& package : graph.packages) {
        auto selected = false;
        for (const auto& name : selected_packages) {
            if (name == package.manifest.name.as_str()) {
                selected = true;
                break;
            }
        }
        if (! selected) continue;
        for (const auto& target : package.manifest.targets) {
            if (! target.is_Library()) continue;
            append(PackageTargetId {
                .package = package.manifest.name.clone(),
                .kind    = PackageTargetKind::Library,
                .name    = String::make(lito::manifest::package_target_name(target)),
            });
            break;
        }
    }
    return result;
}

export namespace lito::package
{

auto resolve_package_selection_with_environment_impl(
    const PackageSelection&                   selection,
    PackageSelectionPurpose                   purpose,
    lito::source::SourceResolutionOptions     options,
    const TargetInfo*                         target,
    lito::tools::ToolResolver*                tool_resolver,
    const ResolvedProcessEnvironment&         environment,
    usize                                     jobs     = usize(1),
    lito::source::SourceEventSink             observer = {},
    Option<lito::workspace::WorkspaceCatalog> catalog  = None())
    -> PackageSelectionResult<ResolvedPackageSelection> {
    auto resolved = resolve_package_graph_with_environment_impl(selection.root.as_path(),
                                                                rstd::move(options),
                                                                tool_resolver,
                                                                environment,
                                                                jobs,
                                                                observer,
                                                                rstd::move(catalog));
    if (resolved.is_err()) {
        return Err(rstd::into<PackageSelectionError>(rstd::move(resolved).unwrap_err()));
    }
    auto graph      = rstd::move(resolved).unwrap();
    auto root_roles = rstd::collections::BTreeMap<String, ProjectRootRole>::make();
    for (const auto& root : graph.roots) root_roles.insert(root.name.clone(), root.role);
    auto selected_roots   = Vec<String>::make();
    auto selected_targets = Vec<PackageTargetId>::make();
    if (selection.packages.is_empty()) {
        auto defaults = StringSet::make();
        if (purpose != PackageSelectionPurpose::All) {
            for (const auto& name : graph.default_roots) defaults.insert(name.clone(), empty {});
        }
        for (const auto& root : graph.roots) {
            const auto& name = root.name;
            if (! defaults.is_empty() && root.role != ProjectRootRole::AssociatedTest &&
                ! defaults.contains_key(name.as_str()))
                continue;
            auto                                   supported = true;
            const lito::manifest::PackageManifest* manifest  = nullptr;
            if (target != nullptr) {
                for (const auto& package : graph.packages) {
                    if (package.manifest.name.as_str() == name.as_str()) {
                        supported = package.manifest.target.matches(*target);
                        manifest  = rstd::addressof(package.manifest);
                        break;
                    }
                }
            } else {
                for (const auto& package : graph.packages) {
                    if (package.manifest.name.as_str() == name.as_str()) {
                        manifest = rstd::addressof(package.manifest);
                        break;
                    }
                }
            }
            if (manifest != nullptr && supported &&
                append_selected_targets(selected_targets, *manifest, root.role, purpose)) {
                selected_roots.push(name.clone());
            }
        }
    } else {
        auto selected_names = StringSet::make();
        for (const auto& name : selection.packages) {
            if (! lito::manifest::valid_package_name(name.as_str())) {
                return package_selection_failure<ResolvedPackageSelection>(
                    rstd::format("package selection '{}' must contain only ASCII "
                                 "letters, digits, '-' or '_'",
                                 name.as_str()));
            }
            if (selected_names.contains_key(name.as_str())) {
                return package_selection_failure<ResolvedPackageSelection>(rstd::format(
                    "project package '{}' was selected more than once", name.as_str()));
            }
            auto role = root_roles.get(name.as_str());
            if (role.is_none()) {
                return package_selection_failure<ResolvedPackageSelection>(
                    rstd::format("project has no root package named '{}'", name.as_str()));
            }
            const lito::manifest::PackageManifest* manifest = nullptr;
            if (target != nullptr) {
                for (const auto& package : graph.packages) {
                    if (package.manifest.name.as_str() != name.as_str()) continue;
                    manifest = rstd::addressof(package.manifest);
                    if (! package.manifest.target.matches(*target)) {
                        return package_selection_failure<ResolvedPackageSelection>(
                            rstd::format("package '{}' does not support target '{}'",
                                         name.as_str(),
                                         target->triple.as_str()));
                    }
                    break;
                }
            } else {
                for (const auto& package : graph.packages) {
                    if (package.manifest.name.as_str() == name.as_str()) {
                        manifest = rstd::addressof(package.manifest);
                        break;
                    }
                }
            }
            if (manifest == nullptr ||
                ! append_selected_targets(selected_targets, *manifest, **role, purpose)) {
                return package_selection_failure<ResolvedPackageSelection>(
                    rstd::format("project package '{}' has no {} target", name.as_str(), purpose));
            }
            selected_names.insert(name.clone(), empty {});
            selected_roots.push(name.clone());
        }
    }
    if (selected_roots.is_empty()) {
        return package_selection_failure<ResolvedPackageSelection>(
            rstd::format("project has no selected {} package", purpose));
    }
    rstd::slice_::sort_unstable(selected_roots.as_mut_slice().as_mut_ref());

    auto install_packages = Vec<String>::make();
    if (purpose == PackageSelectionPurpose::Install) {
        auto runtime = resolve_runtime_package_closure(graph, selected_roots, target);
        if (runtime.is_err()) {
            return Err(rstd::into<PackageSelectionError>(rstd::move(runtime).unwrap_err()));
        }
        install_packages      = rstd::move(runtime).unwrap().packages;
        auto existing_targets = StringSet::make();
        for (const auto& selected_target : selected_targets) {
            existing_targets.insert(package_target_id_text(selected_target), empty {});
        }
        for (const auto& name : install_packages) {
            auto already_selected = false;
            for (const auto& selected : selected_roots) {
                if (selected == name.as_str()) {
                    already_selected = true;
                    break;
                }
            }
            if (already_selected) continue;
            const lito::manifest::PackageManifest* manifest = nullptr;
            for (const auto& package : graph.packages) {
                if (package.manifest.name == name.as_str()) {
                    manifest = rstd::addressof(package.manifest);
                    break;
                }
            }
            auto appended = Vec<PackageTargetId>::make();
            if (manifest == nullptr ||
                ! append_selected_targets(
                    appended, *manifest, ProjectRootRole::PrimaryPackage, purpose)) {
                return package_selection_failure<ResolvedPackageSelection>(
                    rstd::format("runtime package '{}' has no install target", name.as_str()));
            }
            for (auto& selected_target : appended) {
                auto key = package_target_id_text(selected_target);
                if (existing_targets.contains_key(key.as_str())) continue;
                existing_targets.insert(rstd::move(key), empty {});
                selected_targets.push(rstd::move(selected_target));
            }
        }
    }

    const auto& closure_roots =
        purpose == PackageSelectionPurpose::Install ? install_packages : selected_roots;
    auto selected_packages = selected_closure(graph, closure_roots, selected_targets, target);
    if (selected_packages.is_err()) {
        return Err(rstd::move(selected_packages).unwrap_err());
    }
    auto resolved_features = resolve_features(
        graph, selected_roots, *selected_packages, selected_targets, selection.features);
    if (resolved_features.is_err()) {
        return Err(rstd::into<PackageSelectionError>(rstd::move(resolved_features).unwrap_err()));
    }
    auto standards = resolve_effective_language_standards(graph, *selected_packages);
    if (standards.is_err()) {
        return Err(rstd::into<PackageSelectionError>(rstd::move(standards).unwrap_err()));
    }
    auto effective_targets = effective_compile_targets(graph, *selected_packages, selected_targets);
    return Ok(ResolvedPackageSelection {
        .graph                  = rstd::move(graph),
        .selected_root_names    = rstd::move(selected_roots),
        .install_package_names  = rstd::move(install_packages),
        .selected_package_names = rstd::move(selected_packages).unwrap(),
        .selected_targets       = rstd::move(selected_targets),
        .effective_targets      = rstd::move(effective_targets),
        .standards              = rstd::move(standards).unwrap(),
    });
}

auto resolve_package_selection_with_environment(
    const PackageSelection&                   selection,
    PackageSelectionPurpose                   purpose,
    lito::source::SourceResolutionOptions     options,
    const TargetInfo*                         target,
    lito::tools::ToolResolver&                tool_resolver,
    const ResolvedProcessEnvironment&         environment,
    usize                                     jobs     = usize(1),
    lito::source::SourceEventSink             observer = {},
    Option<lito::workspace::WorkspaceCatalog> catalog  = None())
    -> PackageSelectionResult<ResolvedPackageSelection> {
    return resolve_package_selection_with_environment_impl(selection,
                                                           purpose,
                                                           rstd::move(options),
                                                           target,
                                                           rstd::addressof(tool_resolver),
                                                           environment,
                                                           jobs,
                                                           observer,
                                                           rstd::move(catalog));
}

auto resolve_existing_package_selection_with_environment(
    const PackageSelection&                   selection,
    PackageSelectionPurpose                   purpose,
    lito::source::SourceResolutionOptions     options,
    const TargetInfo&                         target,
    const ResolvedProcessEnvironment&         environment,
    usize                                     jobs     = usize(1),
    lito::source::SourceEventSink             observer = {},
    Option<lito::workspace::WorkspaceCatalog> catalog  = None())
    -> PackageSelectionResult<ResolvedPackageSelection> {
    return resolve_package_selection_with_environment_impl(selection,
                                                           purpose,
                                                           rstd::move(options),
                                                           rstd::addressof(target),
                                                           nullptr,
                                                           environment,
                                                           jobs,
                                                           observer,
                                                           rstd::move(catalog));
}

auto resolve_package_selection(const PackageSelection& selection,
                               PackageSelectionPurpose purpose = PackageSelectionPurpose::All,
                               lito::source::SourceResolutionOptions options = {},
                               const TargetInfo*                     target  = nullptr)
    -> PackageSelectionResult<ResolvedPackageSelection> {
    auto environment = ResolvedProcessEnvironment::resolve(ProcessEnvironmentSpec {});
    if (environment.is_err()) {
        return Err(rstd::into<PackageSelectionError>(rstd::move(environment).unwrap_err()));
    }
    auto resolver = lito::tools::ToolResolver(*environment);
    return resolve_package_selection_with_environment(
        selection, purpose, rstd::move(options), target, resolver, *environment);
}

} // namespace lito::package
