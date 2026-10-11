module;
#include <rstd/macro.hpp>

module lito.driver;

import rstd;
import lito.core;
import lito.tools;
import lito.system;
import :command.tree;
import :project;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::package;

namespace lito
{

auto tree_purpose_name(PackageSelectionPurpose purpose) noexcept -> ref<str> {
    switch (purpose) {
    case PackageSelectionPurpose::Production: return "build"_str;
    case PackageSelectionPurpose::Test: return "test"_str;
    case PackageSelectionPurpose::Benchmark: return "bench"_str;
    case PackageSelectionPurpose::Example: return "example"_str;
    case PackageSelectionPurpose::Install: return "install"_str;
    default: return {};
    }
}

auto parse_tree_purpose(ref<str> name) -> CommandResult<PackageSelectionPurpose> {
    constexpr PackageSelectionPurpose values[] = {
        PackageSelectionPurpose::Production, PackageSelectionPurpose::Test,
        PackageSelectionPurpose::Benchmark,  PackageSelectionPurpose::Example,
        PackageSelectionPurpose::Install,
    };
    for (auto value : values)
        if (tree_purpose_name(value) == name) return Ok(value);
    return Err(CommandError::Message(rstd::format(
        "unknown tree purpose '{}'; expected build, test, bench, example, or install", name)));
}

auto parse_tree_charset(ref<str> name) -> CommandResult<TreeCharset> {
    if (name == "utf8"_str) return Ok(TreeCharset::Utf8);
    if (name == "ascii"_str) return Ok(TreeCharset::Ascii);
    return Err(CommandError::Message(
        rstd::format("unknown tree charset '{}'; expected utf8 or ascii", name)));
}

auto tree(const TreeRequest& request) -> CommandResult<TreeReport> {
    if (request.selection.root.is_empty())
        return Err(CommandError::Message("tree project root is required"_Str));
    if (tree_purpose_name(request.purpose).is_empty())
        return Err(CommandError::Message("unsupported tree selection purpose"_Str));
    auto environment = rstd_try(ResolvedProcessEnvironment::resolve(request.environment));
    auto resolver  = tools::ToolResolver(environment, request.tools.clone(), request.tool_reporter);
    auto selection = request.selection.clone();
    selection.root_selection = PackageRootSelection::Metadata;
    auto profile             = request.profile.is_some() ? request.profile->clone()
                                                         : default_build_profile(request.purpose);
    auto project             = rstd_try(resolve_inspection_project(
        selection,
        profile,
        request.configuration,
        request.sources,
        request.lock,
        resolver,
        environment,
        request.locked,
        request.purpose,
        request.observer,
        request.registries.is_some() ? rstd::addressof(*request.registries) : nullptr));
    return Ok(TreeReport { rstd::move(project.selection), project.lock });
}

struct TreeRenderTask {
    usize                  node {};
    usize                  depth {};
    String                 prefix;
    bool                   last { true };
    bool                   root { false };
    Option<usize>          edge;
    Option<DependencyKind> group;
};

auto render_tree(const TreeReport& report, const TreeRenderOptions& options)
    -> CommandResult<String> {
    const auto& packages = report.selection.graph.packages;
    const auto& graph    = report.selection.active_graph;
    const auto  count    = packages.len() * usize(2);
    const auto  key      = [](PackageInstance instance) {
        return instance.package * usize(2) + usize(instance.host ? 1 : 0);
    };
    auto present   = Vec<bool>::make();
    auto incoming  = Vec<usize>::make();
    auto adjacency = Vec<Vec<usize>>::make();
    for (usize i {}; i < count; ++i) {
        present.push(false);
        incoming.push(usize {});
        adjacency.push(Vec<usize>::make());
    }
    for (const auto& instance : graph.instances) {
        if (instance.package >= packages.len() || present[key(instance)])
            return Err(CommandError::Message("invalid package instance in tree report"_Str));
        present[key(instance)] = true;
    }
    const auto valid = [&](PackageInstance instance) {
        return instance.package < packages.len() && present[key(instance)];
    };
    auto roots = Vec<usize>::make();
    for (usize i {}; i < graph.roots.len(); ++i) {
        if (! valid(graph.roots[i].instance))
            return Err(CommandError::Message("invalid root in tree report"_Str));
        roots.emplace_back(i);
    }
    for (usize i {}; i < graph.dependencies.len(); ++i) {
        const auto& edge = graph.dependencies[i];
        if (! valid(edge.consumer) || ! valid(edge.provider))
            return Err(CommandError::Message("invalid dependency in tree report"_Str));
        adjacency[key(edge.consumer)].emplace_back(i);
        ++incoming[key(edge.provider)];
    }
    auto ready = Vec<usize>::make();
    for (usize i {}; i < count; ++i)
        if (present[i] && incoming[i] == usize {}) ready.emplace_back(i);
    usize visited {};
    while (! ready.is_empty()) {
        const auto current = ready.pop().unwrap();
        ++visited;
        for (auto edge : adjacency[current]) {
            auto provider = key(graph.dependencies[edge].provider);
            if (--incoming[provider] == usize {}) ready.emplace_back(provider);
        }
    }
    if (visited != graph.instances.len())
        return Err(CommandError::Message("dependency cycle in tree report"_Str));
    const auto instance_less = [&](PackageInstance left, PackageInstance right) {
        const auto& a = packages[left.package];
        const auto& b = packages[right.package];
        if (a.manifest.name != b.manifest.name.as_str())
            return a.manifest.name < b.manifest.name.as_str();
        if (left.host != right.host) return ! left.host;
        return a.source_identity < b.source_identity.as_str();
    };
    rstd::slice_::sort_unstable_by(roots.as_mut_slice().as_mut_ref(), [&](usize a, usize b) {
        return instance_less(graph.roots[a].instance, graph.roots[b].instance);
    });
    for (auto& edges : adjacency)
        rstd::slice_::sort_unstable_by(edges.as_mut_slice().as_mut_ref(), [&](usize a, usize b) {
            const auto& left  = graph.dependencies[a];
            const auto& right = graph.dependencies[b];
            if (left.kind != right.kind) return left.kind < right.kind;
            if (left.provider != right.provider)
                return instance_less(left.provider, right.provider);
            if (left.type != right.type) return left.type < right.type;
            return left.declaration < right.declaration;
        });
    const auto ascii  = options.charset == TreeCharset::Ascii;
    auto       result = String::make();
    for (auto root : roots) {
        if (! result.is_empty()) result.push_ascii('\n');
        auto expanded = Vec<Option<usize>>::make();
        for (usize i {}; i < count; ++i) expanded.push(None());
        auto tasks = Vec<TreeRenderTask>::make();
        tasks.push(TreeRenderTask { .node = key(graph.roots[root].instance), .root = true });
        while (! tasks.is_empty()) {
            auto task = tasks.pop().unwrap();
            result.push_str(task.prefix.as_str());
            if (! task.root)
                result.push_str(task.last ? (ascii ? "`-- "_str : "└── "_str)
                                          : (ascii ? "|-- "_str : "├── "_str));
            if (task.group.is_some()) {
                result.push_str(*task.group == DependencyKind::Development
                                    ? "[dev-dependencies]\n"_str
                                    : "[runtime-dependencies]\n"_str);
            } else {
                const auto& package = packages[task.node / usize(2)];
                result.push_str(package.manifest.name.as_str());
                if (package.manifest.version.value.is_some())
                    result.push_str(
                        rstd::format(" v{}", package.manifest.version.value->as_str()).as_str());
                if (package.source.kind == source::PackageSourceKind::Path &&
                    ! package.manifest.root.is_empty())
                    result.push_str(
                        rstd::format(" ({})", package.manifest.root.as_path()).as_str());
                if (task.edge.is_some()) {
                    switch (graph.dependencies[*task.edge].type) {
                    case DependencyType::Script: result.push_str(" [script]"_str); break;
                    case DependencyType::Plugin: result.push_str(" [plugin]"_str); break;
                    case DependencyType::Pmacro: result.push_str(" [pmacro]"_str); break;
                    default: break;
                    }
                }
                if (task.node % usize(2) != usize {}) result.push_str(" [host]"_str);
                auto expand = ! adjacency[task.node].is_empty() &&
                              (options.depth.is_none() || task.depth < *options.depth);
                if (expand) {
                    const auto remaining =
                        options.depth.is_some() ? *options.depth - task.depth : count;
                    if (! options.no_dedupe && expanded[task.node].is_some() &&
                        *expanded[task.node] >= remaining) {
                        result.push_str(" (*)"_str);
                        expand = false;
                    } else {
                        expanded[task.node] = Some(usize(remaining));
                    }
                }
                result.push_ascii('\n');
                if (! expand) continue;
            }
            auto children = Vec<TreeRenderTask>::make();
            auto prefix   = task.prefix.clone();
            if (! task.root)
                prefix.push_str(task.last ? "    "_str : (ascii ? "|   "_str : "│   "_str));
            const auto child = [&](usize edge) {
                children.push(TreeRenderTask {
                    .node   = key(graph.dependencies[edge].provider),
                    .depth  = task.group.is_some() ? task.depth : task.depth + usize(1),
                    .prefix = prefix.clone(),
                    .edge   = Some(edge),
                });
            };
            if (task.group.is_some()) {
                for (auto edge : adjacency[task.node])
                    if (graph.dependencies[edge].kind == *task.group) child(edge);
            } else {
                for (auto edge : adjacency[task.node])
                    if (graph.dependencies[edge].kind == DependencyKind::Normal) child(edge);
                constexpr DependencyKind groups[] = { DependencyKind::Development,
                                                      DependencyKind::Runtime };
                for (auto group : groups) {
                    auto any = false;
                    for (auto edge : adjacency[task.node])
                        if (graph.dependencies[edge].kind == group) any = true;
                    if (any)
                        children.push(TreeRenderTask {
                            .node   = task.node,
                            .depth  = task.depth + usize(1),
                            .prefix = prefix.clone(),
                            .group  = Some(group),
                        });
                }
            }
            for (usize i {}; i < children.len(); ++i)
                children[i].last = i + usize(1) == children.len();
            while (! children.is_empty()) tasks.push(children.pop().unwrap());
        }
    }
    return Ok(rstd::move(result));
}

} // namespace lito
