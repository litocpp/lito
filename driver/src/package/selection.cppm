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
    Sources,
    Production,
    Documentation,
    Install,
    Test,
    Benchmark,
    Example,
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
        case lito::package::PackageSelectionPurpose::Sources: name = "sources"_str; break;
        case lito::package::PackageSelectionPurpose::Production: name = "production"_str; break;
        case lito::package::PackageSelectionPurpose::Documentation:
            name = "documentation"_str;
            break;
        case lito::package::PackageSelectionPurpose::Install: name = "install"_str; break;
        case lito::package::PackageSelectionPurpose::Test: name = "test"_str; break;
        case lito::package::PackageSelectionPurpose::Benchmark: name = "benchmark"_str; break;
        case lito::package::PackageSelectionPurpose::Example: name = "example"_str; break;
        }
        return formatter.write_str(name);
    }
};

} // namespace rstd

using namespace lito::package;

auto copy_strings(const Vec<String>& values) -> Vec<String> {
    auto result = values.iter()
                      .map([](auto value) {
                          return value->clone();
                      })
                      .collect<Vec<String>>();
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
    if (purpose == PackageSelectionPurpose::Example) return kind == PackageTargetKind::Example;
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
    Option<ref<str>>                          artifact_processor = None(),
    const PackageConditionEnvironment*        conditions         = nullptr)
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
    auto graph = rstd::move(resolved).unwrap();
    if (purpose == PackageSelectionPurpose::Sources) {
        return Ok(ResolvedPackageSelection { .graph = rstd::move(graph) });
    }
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
                return Err(PackageSelectionError::Message(
                    rstd::format("package selection '{}' must contain only ASCII "
                                 "letters, digits, '-' or '_'",
                                 name.as_str())));
            }
            if (selected_names.contains_key(name.as_str())) {
                return Err(PackageSelectionError::Message(rstd::format(
                    "project package '{}' was selected more than once", name.as_str())));
            }
            auto role = root_roles.get(name.as_str());
            if (role.is_none()) {
                return Err(PackageSelectionError::Message(
                    rstd::format("project has no root package named '{}'", name.as_str())));
            }
            const ResolvedPackage* selected_package = nullptr;
            if (target != nullptr) {
                for (const auto& package : graph.packages) {
                    if (package.manifest.name.as_str() != name.as_str()) continue;
                    selected_package = rstd::addressof(package);
                    if (! package.manifest.target.matches(*target)) {
                        return Err(PackageSelectionError::Message(
                            rstd::format("package '{}' does not support target '{}'",
                                         name.as_str(),
                                         target->triple.as_str())));
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
                return Err(PackageSelectionError::Message(
                    rstd::format("project package '{}' has no {} target", name.as_str(), purpose)));
            }
            selected_names.insert(name.clone(), empty {});
            selected_roots.push(name.clone());
        }
    }
    if (selected_roots.is_empty()) {
        return Err(PackageSelectionError::Message(
            rstd::format("project has no selected {} package", purpose)));
    }
    rstd::slice_::sort_unstable(selected_roots.as_mut_slice().as_mut_ref());

    auto empty_conditions = PackageConditionEnvironment {};
    if (conditions == nullptr && target != nullptr) {
        auto host = detect_host_info();
        if (host.is_err())
            return Err(PackageSelectionError::Message(
                rstd::format("cannot resolve condition host: {}", host.unwrap_err())));
        auto platform = BuildPlatform { .host             = host->clone(),
                                        .effective_target = target->clone(),
                                        .cross = host->architecture != target->architecture ||
                                                 host->os != target->platform_name() };
        empty_conditions.target = make_target_condition_environment(platform);
    }
    auto selected_packages =
        select_conditioned_packages(graph,
                                    selected_roots,
                                    selected_targets,
                                    target,
                                    artifact_processor,
                                    selection.features,
                                    conditions != nullptr ? *conditions : empty_conditions,
                                    purpose == PackageSelectionPurpose::Install);
    if (selected_packages.is_err())
        return Err(rstd::into<PackageSelectionError>(rstd::move(selected_packages).unwrap_err()));

    auto install_packages = Vec<String>::make();
    if (purpose == PackageSelectionPurpose::Install) {
        install_packages      = rstd::move(selected_packages->install);
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
                return Err(PackageSelectionError::Message(
                    rstd::format("runtime package '{}' has no install target", name.as_str())));
            }
            for (auto& selected_target : appended) {
                auto key = package_target_id_text(selected_target);
                if (existing_targets.contains_key(key.as_str())) continue;
                existing_targets.insert(rstd::move(key), empty {});
                selected_targets.push(rstd::move(selected_target));
            }
        }
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
        return Err(PackageSelectionError::Message("plugin host selection has no packages"_Str));
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
        return selected_targets.iter().any([&](auto target) {
            return target->package == name && target->kind == kind;
        });
    };
    for (const auto& name : selection.plugin_package_names) {
        if (! has_selected(name.as_str(), PackageTargetKind::Plugin)) {
            return Err(PackageSelectionError::Message(rstd::format(
                "host selection is missing plugin target for package '{}'", name.as_str())));
        }
    }
    for (const auto& name : selection.proc_macro_provider_names) {
        if (! has_selected(name.as_str(), PackageTargetKind::ProcMacro)) {
            return Err(PackageSelectionError::Message(rstd::format(
                "host selection is missing pmacro target for package '{}'", name.as_str())));
        }
    }
    for (const auto& name : selection.artifact_processor_package_names) {
        if (! has_selected(name.as_str(), PackageTargetKind::Binary)) {
            return Err(PackageSelectionError::Message(
                rstd::format("host selection is missing artifact processor target for package '{}'",
                             name.as_str())));
        }
    }
    for (const auto& name : selection.host_tool_package_names) {
        if (! host.contains_key(name.as_str())) continue;
        if (! has_selected(name.as_str(), PackageTargetKind::Binary)) {
            return Err(PackageSelectionError::Message(rstd::format(
                "host selection is missing host-tool target for package '{}'", name.as_str())));
        }
    }
    for (auto& package : selection.graph.packages) package.host_view = true;
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
    Option<ref<str>>                          artifact_processor = None(),
    const PackageConditionEnvironment*        conditions         = nullptr)
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
                                                           artifact_processor,
                                                           conditions);
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
    Option<ref<str>>                          artifact_processor = None(),
    const PackageConditionEnvironment*        conditions         = nullptr)
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
                                                           artifact_processor,
                                                           conditions);
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
