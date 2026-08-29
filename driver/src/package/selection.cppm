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
    Vec<String>                host_package_names;
    Vec<String>                plugin_package_names;
    Vec<String>                proc_macro_provider_names;
    Vec<String>                artifact_processor_package_names;
    Vec<String>                host_tool_package_names;
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
    return kind == PackageTargetKind::Library || kind == PackageTargetKind::Binary ||
           kind == PackageTargetKind::Plugin || kind == PackageTargetKind::ProcMacro;
}

auto append_selected_targets(Vec<PackageTargetId>&   output,
                             const ResolvedPackage&  package,
                             ProjectRootRole         role,
                             PackageSelectionPurpose purpose) -> bool {
    auto selected = false;
    for (const auto& target : package.manifest.targets) {
        if (lito::manifest::package_target_is_host_tool(target)) continue;
        const auto kind = lito::manifest::package_target_kind(target);
        if (! selected_by_purpose(role, kind, purpose)) continue;
        output.push(PackageTargetId {
            .package = package.manifest.name.clone(),
            .kind    = kind,
            .name    = String::make(lito::manifest::package_target_name(target)),
        });
        selected = true;
    }
    if (! package.manifest.compile_tests.is_empty() &&
        selected_by_purpose(role, PackageTargetKind::CompileTest, purpose)) {
        output.push(PackageTargetId {
            .package = package.manifest.name.clone(),
            .kind    = PackageTargetKind::CompileTest,
            .name    = package.manifest.name.clone(),
        });
        selected = true;
    }
    if (purpose == PackageSelectionPurpose::Install && package.manifest.install_script.is_some()) {
        selected = true;
    }
    return selected;
}

struct SelectedPackageClosure {
    Vec<String> target;
    Vec<String> host;
    Vec<String> plugins;
    Vec<String> providers;
    Vec<String> processors;
    Vec<String> tools;
};

auto selected_closure(const ResolvedPackageGraph& graph,
                      const Vec<String>&          selected_roots,
                      const Vec<PackageTargetId>& selected_targets,
                      const TargetInfo*           target,
                      Option<ref<str>>            artifact_processor)
    -> PackageSelectionResult<SelectedPackageClosure> {
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

    auto       pending_target  = Vec<String>::make();
    auto       pending_host    = Vec<String>::make();
    auto       selected_target = StringSet::make();
    auto       selected_host   = StringSet::make();
    auto       plugins         = StringSet::make();
    auto       providers       = StringSet::make();
    auto       processors      = StringSet::make();
    auto       tools           = StringSet::make();
    const auto has_host_tool   = [&](ref<str> name) noexcept {
        auto index = indices.get(name);
        return index.is_some() &&
               lito::manifest::package_has_host_tool_target(graph.packages[**index].manifest);
    };
    for (const auto& root : selected_roots) {
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
        if (has_target) pending_target.push(root.clone());
        if (has_plugin) {
            pending_host.push(root.clone());
            plugins.insert(root.clone(), empty {});
        }
        if (has_provider) {
            pending_host.push(root.clone());
            providers.insert(root.clone(), empty {});
        }
    }
    const auto consume = [&](String current, bool host) -> PackageSelectionResult<empty> {
        auto& selected = host ? selected_host : selected_target;
        if (selected.contains_key(current.as_str())) return Ok(empty {});
        auto index = indices.get(current.as_str());
        if (index.is_none()) {
            return package_selection_failure<empty>(rstd::format(
                "selected package '{}' is missing from resolved graph", current.as_str()));
        }
        if (! host && target != nullptr &&
            ! graph.packages[**index].manifest.target.matches(*target)) {
            return package_selection_failure<empty>(
                rstd::format("package '{}' does not support target '{}'",
                             current.as_str(),
                             target->triple.as_str()));
        }
        selected.insert(current.clone(), empty {});
        for (const auto& dependency : graph.packages[**index].dependencies) {
            if (dependency.is_Plugin()) {
                if (host && plugins.contains_key(current.as_str())) {
                    return package_selection_failure<empty>(
                        rstd::format("plugin '{}' cannot depend on plugin '{}'",
                                     current.as_str(),
                                     dependency.as_Plugin().value.name.as_str()));
                }
                pending_host.push(dependency.as_Plugin().value.name.clone());
                plugins.insert(dependency.as_Plugin().value.name.clone(), empty {});
            } else if (dependency.is_Pmacro()) {
                if (host) {
                    return package_selection_failure<empty>(
                        rstd::format("pmacro provider '{}' cannot depend on pmacro provider '{}'",
                                     current.as_str(),
                                     dependency.as_Pmacro().value.name.as_str()));
                }
                pending_host.push(dependency.as_Pmacro().value.name.clone());
                providers.insert(dependency.as_Pmacro().value.name.clone(), empty {});
            } else if (! host && artifact_processor.is_some() && dependency.is_Cpp() &&
                       dependency.as_Cpp().value.name.as_str() == **artifact_processor) {
                pending_host.push(dependency.as_Cpp().value.name.clone());
                processors.insert(dependency.as_Cpp().value.name.clone(), empty {});
            } else if (dependency.is_Cpp() &&
                       has_host_tool(dependency.as_Cpp().value.name.as_str())) {
                tools.insert(dependency.as_Cpp().value.name.clone(), empty {});
                if (host)
                    pending_host.push(dependency.as_Cpp().value.name.clone());
                else
                    pending_target.push(dependency.as_Cpp().value.name.clone());
            } else if (host) {
                pending_host.push(String::make(resolved_dependency_name(dependency)));
            } else {
                pending_target.push(String::make(resolved_dependency_name(dependency)));
            }
        }
        if (! host && development.contains_key(current.as_str())) {
            for (const auto& dependency : graph.packages[**index].dev_dependencies) {
                if (dependency.is_Plugin()) {
                    pending_host.push(dependency.as_Plugin().value.name.clone());
                    plugins.insert(dependency.as_Plugin().value.name.clone(), empty {});
                } else if (dependency.is_Pmacro()) {
                    pending_host.push(dependency.as_Pmacro().value.name.clone());
                    providers.insert(dependency.as_Pmacro().value.name.clone(), empty {});
                } else if (dependency.is_Cpp() &&
                           has_host_tool(dependency.as_Cpp().value.name.as_str())) {
                    tools.insert(dependency.as_Cpp().value.name.clone(), empty {});
                    pending_target.push(dependency.as_Cpp().value.name.clone());
                } else {
                    pending_target.push(String::make(resolved_dependency_name(dependency)));
                }
            }
        }
        return Ok(empty {});
    };
    while (! pending_target.is_empty() || ! pending_host.is_empty()) {
        if (! pending_target.is_empty()) {
            auto consumed = consume(rstd::move(pending_target.pop()).unwrap(), false);
            if (consumed.is_err()) return Err(rstd::move(consumed).unwrap_err());
        } else {
            auto consumed = consume(rstd::move(pending_host.pop()).unwrap(), true);
            if (consumed.is_err()) return Err(rstd::move(consumed).unwrap_err());
        }
    }

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
    return Ok(rstd::move(result));
}

auto effective_compile_targets(const ResolvedPackageGraph& graph,
                               const Vec<String>&          selected_packages,
                               const Vec<PackageTargetId>& selected_targets,
                               const Vec<String>& host_tool_packages) -> Vec<PackageTargetId> {
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
        auto host_tool_package = false;
        for (const auto& name : host_tool_packages) {
            if (name == package.manifest.name.as_str()) host_tool_package = true;
        }
        if (! host_tool_package) continue;
        for (const auto& target : package.manifest.targets) {
            if (! target.is_Binary() || ! lito::manifest::package_target_is_host_tool(target)) {
                continue;
            }
            append(PackageTargetId {
                .package = package.manifest.name.clone(),
                .kind    = PackageTargetKind::Binary,
                .name    = String::make(lito::manifest::package_target_name(target)),
            });
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
    usize                                     jobs               = usize(1),
    lito::source::SourceEventSink             observer           = {},
    Option<lito::workspace::WorkspaceCatalog> catalog            = None(),
    lito::registry::RegistryGraphProvider     registry           = {},
    Option<ref<str>>                          artifact_processor = None())
    -> PackageSelectionResult<ResolvedPackageSelection> {
    auto resolved = resolve_package_graph_with_environment_impl(selection.root.as_path(),
                                                                rstd::move(options),
                                                                tool_resolver,
                                                                environment,
                                                                jobs,
                                                                observer,
                                                                rstd::move(catalog),
                                                                registry);
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
            auto                   supported        = true;
            const ResolvedPackage* selected_package = nullptr;
            if (target != nullptr) {
                for (const auto& package : graph.packages) {
                    if (package.manifest.name.as_str() == name.as_str()) {
                        supported        = package.manifest.target.matches(*target);
                        selected_package = rstd::addressof(package);
                        break;
                    }
                }
            } else {
                for (const auto& package : graph.packages) {
                    if (package.manifest.name.as_str() == name.as_str()) {
                        selected_package = rstd::addressof(package);
                        break;
                    }
                }
            }
            if (selected_package != nullptr && supported &&
                append_selected_targets(selected_targets, *selected_package, root.role, purpose)) {
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
            const ResolvedPackage* selected_package = nullptr;
            if (target != nullptr) {
                for (const auto& package : graph.packages) {
                    if (package.manifest.name.as_str() != name.as_str()) continue;
                    selected_package = rstd::addressof(package);
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
                        selected_package = rstd::addressof(package);
                        break;
                    }
                }
            }
            if (selected_package == nullptr ||
                ! append_selected_targets(selected_targets, *selected_package, **role, purpose)) {
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
            const ResolvedPackage* selected_package = nullptr;
            for (const auto& package : graph.packages) {
                if (package.manifest.name == name.as_str()) {
                    selected_package = rstd::addressof(package);
                    break;
                }
            }
            auto appended = Vec<PackageTargetId>::make();
            if (selected_package == nullptr ||
                ! append_selected_targets(
                    appended, *selected_package, ProjectRootRole::PrimaryPackage, purpose)) {
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
    auto selected_packages =
        selected_closure(graph, closure_roots, selected_targets, target, artifact_processor);
    if (selected_packages.is_err()) {
        return Err(rstd::move(selected_packages).unwrap_err());
    }
    auto resolved_features = resolve_features(graph,
                                              selected_roots,
                                              selected_packages->target,
                                              selected_targets,
                                              selection.features,
                                              rstd::addressof(selected_packages->host));
    if (resolved_features.is_err()) {
        return Err(rstd::into<PackageSelectionError>(rstd::move(resolved_features).unwrap_err()));
    }
    auto standards = resolve_effective_language_standards(graph, selected_packages->target);
    if (standards.is_err()) {
        return Err(rstd::into<PackageSelectionError>(rstd::move(standards).unwrap_err()));
    }
    auto target_selected_targets = Vec<PackageTargetId>::make();
    for (const auto& selected_target : selected_targets) {
        if (selected_target.kind != PackageTargetKind::Plugin &&
            selected_target.kind != PackageTargetKind::ProcMacro)
            target_selected_targets.push(selected_target.clone());
    }
    auto effective_targets = effective_compile_targets(
        graph, selected_packages->target, target_selected_targets, selected_packages->tools);
    return Ok(ResolvedPackageSelection {
        .graph                            = rstd::move(graph),
        .selected_root_names              = rstd::move(selected_roots),
        .install_package_names            = rstd::move(install_packages),
        .selected_package_names           = rstd::move(selected_packages->target),
        .host_package_names               = rstd::move(selected_packages->host),
        .plugin_package_names             = rstd::move(selected_packages->plugins),
        .proc_macro_provider_names        = rstd::move(selected_packages->providers),
        .artifact_processor_package_names = rstd::move(selected_packages->processors),
        .host_tool_package_names          = rstd::move(selected_packages->tools),
        .selected_targets                 = rstd::move(target_selected_targets),
        .effective_targets                = rstd::move(effective_targets),
        .standards                        = rstd::move(standards).unwrap(),
    });
}

auto resolve_plugin_host_selection(ResolvedPackageSelection selection)
    -> PackageSelectionResult<ResolvedPackageSelection> {
    if (selection.host_package_names.is_empty()) {
        return package_selection_failure<ResolvedPackageSelection>(
            "plugin host selection has no packages"_str);
    }
    auto host = StringSet::make();
    for (const auto& name : selection.host_package_names) host.insert(name.clone(), empty {});
    auto providers = StringSet::make();
    for (const auto& name : selection.proc_macro_provider_names) {
        providers.insert(name.clone(), empty {});
    }
    auto plugins = StringSet::make();
    for (const auto& name : selection.plugin_package_names) {
        plugins.insert(name.clone(), empty {});
    }
    auto processors = StringSet::make();
    for (const auto& name : selection.artifact_processor_package_names) {
        processors.insert(name.clone(), empty {});
    }
    auto tools = StringSet::make();
    for (const auto& name : selection.host_tool_package_names) {
        tools.insert(name.clone(), empty {});
    }
    auto selected_targets  = Vec<PackageTargetId>::make();
    auto effective_targets = Vec<PackageTargetId>::make();
    for (const auto& package : selection.graph.packages) {
        if (! host.contains_key(package.manifest.name.as_str())) continue;
        for (const auto& target : package.manifest.targets) {
            const auto kind         = lito::manifest::package_target_kind(target);
            const auto is_provider  = providers.contains_key(package.manifest.name.as_str()) &&
                                      kind == PackageTargetKind::ProcMacro;
            const auto is_plugin    = plugins.contains_key(package.manifest.name.as_str()) &&
                                      kind == PackageTargetKind::Plugin;
            const auto is_library   = kind == PackageTargetKind::Library;
            const auto is_processor = processors.contains_key(package.manifest.name.as_str()) &&
                                      kind == PackageTargetKind::Binary &&
                                      lito::manifest::package_target_is_host_tool(target);
            const auto is_tool      = tools.contains_key(package.manifest.name.as_str()) &&
                                      kind == PackageTargetKind::Binary &&
                                      lito::manifest::package_target_is_host_tool(target);
            if (! is_provider && ! is_plugin && ! is_processor && ! is_tool && ! is_library)
                continue;
            auto id = PackageTargetId {
                .package = package.manifest.name.clone(),
                .kind    = kind,
                .name    = String::make(lito::manifest::package_target_name(target)),
            };
            if (is_provider || is_plugin || is_processor || is_tool)
                selected_targets.push(id.clone());
            effective_targets.push(rstd::move(id));
        }
    }
    const auto has_selected = [&selected_targets](ref<str> name, PackageTargetKind kind) noexcept {
        for (const auto& target : selected_targets) {
            if (target.package == name && target.kind == kind) return true;
        }
        return false;
    };
    for (const auto& name : selection.plugin_package_names) {
        if (! has_selected(name.as_str(), PackageTargetKind::Plugin)) {
            return package_selection_failure<ResolvedPackageSelection>(rstd::format(
                "host selection is missing plugin target for package '{}'", name.as_str()));
        }
    }
    for (const auto& name : selection.proc_macro_provider_names) {
        if (! has_selected(name.as_str(), PackageTargetKind::ProcMacro)) {
            return package_selection_failure<ResolvedPackageSelection>(rstd::format(
                "host selection is missing pmacro target for package '{}'", name.as_str()));
        }
    }
    for (const auto& name : selection.artifact_processor_package_names) {
        if (! has_selected(name.as_str(), PackageTargetKind::Binary)) {
            return package_selection_failure<ResolvedPackageSelection>(
                rstd::format("host selection is missing artifact processor target for package '{}'",
                             name.as_str()));
        }
    }
    for (const auto& name : selection.host_tool_package_names) {
        if (! host.contains_key(name.as_str())) continue;
        if (! has_selected(name.as_str(), PackageTargetKind::Binary)) {
            return package_selection_failure<ResolvedPackageSelection>(rstd::format(
                "host selection is missing host-tool target for package '{}'", name.as_str()));
        }
    }
    auto standards =
        resolve_effective_language_standards(selection.graph, selection.host_package_names);
    if (standards.is_err()) {
        return Err(rstd::into<PackageSelectionError>(rstd::move(standards).unwrap_err()));
    }
    selection.selected_root_names = selection.plugin_package_names.clone();
    for (const auto& name : selection.proc_macro_provider_names) {
        if (! plugins.contains_key(name.as_str())) selection.selected_root_names.push(name.clone());
    }
    for (const auto& name : selection.artifact_processor_package_names) {
        if (! plugins.contains_key(name.as_str()) && ! providers.contains_key(name.as_str()))
            selection.selected_root_names.push(name.clone());
    }
    for (const auto& name : selection.host_tool_package_names) {
        if (! host.contains_key(name.as_str())) continue;
        if (! plugins.contains_key(name.as_str()) && ! providers.contains_key(name.as_str()) &&
            ! processors.contains_key(name.as_str()))
            selection.selected_root_names.push(name.clone());
    }
    selection.install_package_names.clear();
    selection.selected_package_names = rstd::move(selection.host_package_names);
    selection.selected_targets       = rstd::move(selected_targets);
    selection.effective_targets      = rstd::move(effective_targets);
    selection.standards              = rstd::move(standards).unwrap();
    selection.host_package_names.clear();
    selection.plugin_package_names.clear();
    selection.proc_macro_provider_names.clear();
    selection.artifact_processor_package_names.clear();
    selection.host_tool_package_names.clear();
    return Ok(rstd::move(selection));
}

auto resolve_package_selection_with_environment(
    const PackageSelection&                   selection,
    PackageSelectionPurpose                   purpose,
    lito::source::SourceResolutionOptions     options,
    const TargetInfo*                         target,
    lito::tools::ToolResolver&                tool_resolver,
    const ResolvedProcessEnvironment&         environment,
    usize                                     jobs               = usize(1),
    lito::source::SourceEventSink             observer           = {},
    Option<lito::workspace::WorkspaceCatalog> catalog            = None(),
    lito::registry::RegistryGraphProvider     registry           = {},
    Option<ref<str>>                          artifact_processor = None())
    -> PackageSelectionResult<ResolvedPackageSelection> {
    return resolve_package_selection_with_environment_impl(selection,
                                                           purpose,
                                                           rstd::move(options),
                                                           target,
                                                           rstd::addressof(tool_resolver),
                                                           environment,
                                                           jobs,
                                                           observer,
                                                           rstd::move(catalog),
                                                           registry,
                                                           artifact_processor);
}

auto resolve_existing_package_selection_with_environment(
    const PackageSelection&                   selection,
    PackageSelectionPurpose                   purpose,
    lito::source::SourceResolutionOptions     options,
    const TargetInfo&                         target,
    const ResolvedProcessEnvironment&         environment,
    usize                                     jobs               = usize(1),
    lito::source::SourceEventSink             observer           = {},
    Option<lito::workspace::WorkspaceCatalog> catalog            = None(),
    lito::registry::RegistryGraphProvider     registry           = {},
    Option<ref<str>>                          artifact_processor = None())
    -> PackageSelectionResult<ResolvedPackageSelection> {
    return resolve_package_selection_with_environment_impl(selection,
                                                           purpose,
                                                           rstd::move(options),
                                                           rstd::addressof(target),
                                                           nullptr,
                                                           environment,
                                                           jobs,
                                                           observer,
                                                           rstd::move(catalog),
                                                           registry,
                                                           artifact_processor);
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
