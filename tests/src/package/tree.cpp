#include <rstd/test/gtest.hpp>

import rstd;
import lito.core;
import lito.driver;
import lito.test.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::package;
using namespace lito_test;

auto tree_package(ref<str> name) -> ResolvedPackage {
    return { .source_identity = String::make(name),
             .manifest        = lito::manifest::PackageManifest { .name = String::make(name) } };
}

auto tree_edge(usize          consumer,
               usize          provider,
               DependencyKind kind          = DependencyKind::Normal,
               bool           provider_host = false,
               bool           consumer_host = false) -> SelectedPackageDependency {
    return { { consumer, consumer_host }, { provider, provider_host }, kind, DependencyType::Cpp };
}

auto tree_report(Vec<ResolvedPackage>           packages,
                 Vec<SelectedPackageDependency> edges,
                 Vec<SelectedPackageRoot>       roots = {}) -> lito::TreeReport {
    auto selected         = SelectedPackageGraph {};
    selected.dependencies = rstd::move(edges);
    selected.roots        = rstd::move(roots);
    if (selected.roots.is_empty())
        selected.roots.push({ { usize {}, false }, ProjectRootRole::PrimaryPackage });
    for (usize i {}; i < packages.len(); ++i) {
        selected.instances.push({ i, false });
        selected.instances.push({ i, true });
    }
    return { .selection = {
                 .graph        = { .packages = rstd::move(packages) },
                 .active_graph = rstd::move(selected),
             } };
}

TEST(PackageTree, DiamondOrderingDepthAndCharacterSets) {
    auto               packages = Vec<ResolvedPackage>::make();
    constexpr ref<str> names[]  = { "root"_str, "a"_str, "b"_str, "shared"_str, "leaf"_str };
    for (auto name : names) packages.push(tree_package(name));
    auto edges = Vec<SelectedPackageDependency>::make();
    edges.push(tree_edge(usize(0), usize(2)));
    edges.push(tree_edge(usize(0), usize(1)));
    edges.push(tree_edge(usize(2), usize(3)));
    edges.push(tree_edge(usize(1), usize(3)));
    edges.push(tree_edge(usize(3), usize(4)));
    auto report = tree_report(rstd::move(packages), rstd::move(edges));
    auto text   = lito::render_tree(report, { .charset = lito::TreeCharset::Ascii });
    ASSERT_TRUE(text.is_ok());
    EXPECT_EQ(text->as_str(),
              "root\n|-- a\n|   `-- shared\n|       `-- leaf\n`-- b\n    `-- shared (*)\n"_str);
    auto shallow = lito::render_tree(report, { .depth = Some(usize(1)) });
    ASSERT_TRUE(shallow.is_ok());
    EXPECT_EQ(shallow->as_str(), "root\n├── a\n└── b\n"_str);
    auto roots = lito::render_tree(report, { .depth = Some(usize {}) });
    ASSERT_TRUE(roots.is_ok());
    EXPECT_EQ(roots->as_str(), "root\n"_str);
    auto repeated = lito::render_tree(report, { .no_dedupe = true });
    ASSERT_TRUE(repeated.is_ok());
    EXPECT_FALSE(repeated->as_str().contains("(*)"_str));
}

TEST(PackageTree, TruncatedDeepPathDoesNotHideShallowerSubtree) {
    auto               packages = Vec<ResolvedPackage>::make();
    constexpr ref<str> names[]  = { "root"_str, "a"_str, "b"_str, "c"_str, "d"_str };
    for (auto name : names) packages.push(tree_package(name));
    auto edges = Vec<SelectedPackageDependency>::make();
    edges.push(tree_edge(usize(0), usize(1)));
    edges.push(tree_edge(usize(1), usize(2)));
    edges.push(tree_edge(usize(0), usize(2)));
    edges.push(tree_edge(usize(2), usize(3)));
    edges.push(tree_edge(usize(3), usize(4)));
    auto report = tree_report(rstd::move(packages), rstd::move(edges));
    auto text =
        lito::render_tree(report, { .depth = Some(usize(3)), .charset = lito::TreeCharset::Ascii });
    ASSERT_TRUE(text.is_ok());
    EXPECT_EQ(text->as_str(),
              "root\n|-- a\n|   `-- b\n|       `-- c\n`-- b\n    `-- c\n        `-- d\n"_str);
}

TEST(PackageTree, GroupsDomainsAndMultipleRootsRemainDistinct) {
    auto               packages = Vec<ResolvedPackage>::make();
    constexpr ref<str> names[]  = { "root"_str, "shared"_str, "leaf"_str };
    for (auto name : names) packages.push(tree_package(name));
    auto edges = Vec<SelectedPackageDependency>::make();
    edges.push(tree_edge(usize(0), usize(1)));
    auto plugin = tree_edge(usize(0), usize(1), DependencyKind::Development, true);
    plugin.type = DependencyType::Plugin;
    edges.push(rstd::move(plugin));
    edges.push(tree_edge(usize(1), usize(2), DependencyKind::Normal, false, false));
    edges.push(tree_edge(usize(1), usize(2), DependencyKind::Normal, true, true));
    auto roots = Vec<SelectedPackageRoot>::make();
    roots.push({ { usize(0), false }, ProjectRootRole::PrimaryPackage });
    roots.push({ { usize(1), false }, ProjectRootRole::WorkspaceMember });
    auto report = tree_report(rstd::move(packages), rstd::move(edges), rstd::move(roots));
    auto text   = lito::render_tree(report);
    ASSERT_TRUE(text.is_ok());
    EXPECT_TRUE(text->as_str().contains("[dev-dependencies]"_str));
    EXPECT_TRUE(text->as_str().contains("shared [plugin] [host]"_str));
    EXPECT_TRUE(text->as_str().contains("leaf [host]"_str));
    EXPECT_TRUE(text->as_str().contains("\n\nshared\n└── leaf\n"_str));
    EXPECT_FALSE(text->as_str().contains("(*)"_str));
}

TEST(PackageTree, InvalidReportsFailBeforeRendering) {
    auto packages = Vec<ResolvedPackage>::make();
    packages.push(tree_package("root"_str));
    packages.push(tree_package("a"_str));
    auto edges = Vec<SelectedPackageDependency>::make();
    edges.push(tree_edge(usize(0), usize(1)));
    edges.push(tree_edge(usize(1), usize(0)));
    auto report = tree_report(rstd::move(packages), rstd::move(edges));
    EXPECT_TRUE(lito::render_tree(report).is_err());
    report.selection.active_graph.dependencies.clear();
    report.selection.active_graph.dependencies.push(tree_edge(usize(0), usize(20)));
    EXPECT_TRUE(lito::render_tree(report).is_err());
}

TEST(PackageTree, ActivationRecordsActualDomainsAndResetsFalseEdges) {
    auto graph = ResolvedPackageGraph {};
    auto root  = tree_package("root"_str);
    root.manifest.features.push({ .name = "enabled"_Str, .macro_name = "LITO_FEAT_ENABLED"_Str });
    auto normal   = ResolvedRequiredDependency::Cpp(ResolvedCppDependency { .name = "shared"_Str });
    auto disabled = ResolvedRequiredDependency::Cpp(ResolvedCppDependency { .name = "shared"_Str });
    disabled.activation.condition = Some(lito::dependency::DependencyCondition {
        .source     = "feature.enabled"_Str,
        .expression = lito::condition::parse("feature.enabled"_str).unwrap(),
    });
    root.dependencies.push(rstd::move(normal));
    root.dependencies.push(rstd::move(disabled));
    root.dev_dependencies.push(
        ResolvedRequiredDependency::Plugin(ResolvedPluginDependency { .name = "plugin"_Str }));
    auto plugin = tree_package("plugin"_str);
    plugin.dependencies.push(
        ResolvedRequiredDependency::Cpp(ResolvedCppDependency { .name = "shared"_Str }));
    graph.packages.push(rstd::move(root));
    graph.packages.push(tree_package("shared"_str));
    graph.packages.push(rstd::move(plugin));
    auto roots   = strings("root"_str);
    auto targets = Vec<PackageTargetId>::make();
    targets.push({ "root"_Str, PackageTargetKind::Test, "root-test"_Str });
    auto features = FeatureSelection {};
    auto selected =
        select_conditioned_packages(graph, roots, targets, nullptr, None(), features, {}, false);
    ASSERT_TRUE(selected.is_ok());
    EXPECT_EQ(selected->graph.dependencies.len(), usize(3));
    EXPECT_TRUE(selected->graph.dependencies[usize(1)].provider.host);
    EXPECT_TRUE(selected->graph.dependencies[usize(2)].consumer.host);
    EXPECT_TRUE(selected->graph.dependencies[usize(2)].provider.host);
    features.enabled.push("enabled"_Str);
    auto enabled =
        select_conditioned_packages(graph, roots, targets, nullptr, None(), features, {}, false);
    ASSERT_TRUE(enabled.is_ok());
    EXPECT_EQ(enabled->graph.dependencies.len(), usize(4));
    features.enabled.clear();
    auto again =
        select_conditioned_packages(graph, roots, targets, nullptr, None(), features, {}, false);
    ASSERT_TRUE(again.is_ok());
    EXPECT_EQ(again->graph.dependencies.len(), usize(3));
}
