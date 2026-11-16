#include <rstd/test/gtest.hpp>

import rstd;
import lito.core;
import lito.cpp;
import lito.toolchain;
import lito.test.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito_test;

auto feature(ref<str> name, bool default_enabled = false) -> lito::manifest::FeatureDeclaration {
    return {
        .name            = String::make(name),
        .macro_name      = lito::manifest::normalized_feature_macro(name),
        .default_enabled = default_enabled,
    };
}

auto dependency(ref<str> name, Vec<String> features = {}, bool default_features = true)
    -> lito::package::ResolvedRequiredDependency {
    return lito::package::ResolvedRequiredDependency::Cpp(lito::package::ResolvedCppDependency {
        .name             = String::make(name),
        .features         = rstd::move(features),
        .default_features = default_features,
    });
}

auto dev_dependency(ref<str> name, Vec<String> features = {}, bool default_features = true)
    -> lito::package::ResolvedRequiredDependency {
    return lito::package::ResolvedRequiredDependency::Cpp(lito::package::ResolvedCppDependency {
        .name             = String::make(name),
        .features         = rstd::move(features),
        .default_features = default_features,
    });
}

auto package(ref<str>                                       name,
             Vec<lito::manifest::FeatureDeclaration>        features         = {},
             Vec<lito::package::ResolvedRequiredDependency> dependencies     = {},
             Vec<lito::package::ResolvedRequiredDependency> dev_dependencies = {})
    -> lito::package::ResolvedPackage {
    return {
        .manifest =
            lito::manifest::PackageManifest {
                .name     = String::make(name),
                .features = rstd::move(features),
            },
        .dependencies     = rstd::move(dependencies),
        .dev_dependencies = rstd::move(dev_dependencies),
    };
}

template<typename... Values>
auto names(Values... values) -> Vec<String> {
    auto result = Vec<String>::make();
    (result.push(String::make(values)), ...);
    return result;
}

template<typename... Values>
auto feature_declarations(Values... values) -> Vec<lito::manifest::FeatureDeclaration> {
    auto result = Vec<lito::manifest::FeatureDeclaration>::make();
    (result.push(feature(values)), ...);
    return result;
}

template<typename... Packages>
auto library_targets(Packages... packages) -> Vec<lito::package::PackageTargetId> {
    auto       result = Vec<lito::package::PackageTargetId>::make();
    const auto append = [&](ref<str> package) {
        result.push({
            .package = String::make(package),
            .kind    = lito::package::PackageTargetKind::Library,
            .name    = String::make(package),
        });
    };
    (append(packages), ...);
    return result;
}

auto feature_enabled(const lito::package::ResolvedPackage& package, ref<str> name) -> bool {
    for (const auto& value : package.features) {
        if (value.name.as_str() == name) return value.enabled;
    }
    return false;
}

auto selected_name(const Vec<String>& names, ref<str> name) -> bool {
    for (const auto& value : names)
        if (value == name) return true;
    return false;
}

auto external_condition(ref<str> source) -> lito::dependency::ExternalDependencyCondition {
    return lito::dependency::ExternalDependencyCondition {
        .source     = String::make(source),
        .expression = lito::condition::parse(source).unwrap(),
    };
}

auto package_with_external_backends() -> lito::package::ResolvedPackage {
    auto declarations = Vec<lito::manifest::FeatureDeclaration>::make();
    declarations.push(feature("qt"_str));
    auto result = package("provider"_str, rstd::move(declarations));
    result.manifest.pkg_config_external_dependencies.push(
        lito::dependency::PkgConfigExternalDependency {
            .alias     = "curl"_Str,
            .condition = Some(external_condition("!feature.qt"_str)),
        });
    result.manifest.cmake_external_dependencies.push(lito::dependency::CMakeDependencyRequirement {
        .alias     = "qt"_Str,
        .package   = "Qt6"_Str,
        .condition = Some(external_condition("feature.qt"_str)),
    });
    return result;
}

TEST(PackageFeatures, ResolvesRootDefaultsAndCommandLineRequests) {
    auto declarations = Vec<lito::manifest::FeatureDeclaration>::make();
    declarations.push(feature("default-on"_str, true));
    declarations.push(feature("explicit"_str));
    auto packages = Vec<lito::package::ResolvedPackage>::make();
    packages.push(package("provider"_str, rstd::move(declarations)));
    auto graph     = lito::package::ResolvedPackageGraph { .packages = rstd::move(packages) };
    auto roots     = names("provider"_str);
    auto targets   = library_targets("provider"_str);
    auto selection = lito::package::FeatureSelection {
        .enabled          = names("explicit"_str),
        .default_features = true,
    };

    auto resolved = lito::package::resolve_features(graph, roots, roots, targets, selection);
    ASSERT_TRUE(resolved.is_ok());
    EXPECT_TRUE(feature_enabled(graph.packages[usize {}], "default-on"_str));
    EXPECT_TRUE(feature_enabled(graph.packages[usize {}], "explicit"_str));
    ASSERT_EQ(graph.packages[usize {}].features[usize {}].activation_sources.len(), usize(1));

    selection.enabled.clear();
    selection.default_features = false;
    resolved = lito::package::resolve_features(graph, roots, roots, targets, selection);
    ASSERT_TRUE(resolved.is_ok());
    EXPECT_FALSE(feature_enabled(graph.packages[usize {}], "default-on"_str));
    EXPECT_FALSE(feature_enabled(graph.packages[usize {}], "explicit"_str));

    selection.all_features = true;
    resolved = lito::package::resolve_features(graph, roots, roots, targets, selection);
    ASSERT_TRUE(resolved.is_ok());
    EXPECT_TRUE(feature_enabled(graph.packages[usize {}], "default-on"_str));
    EXPECT_TRUE(feature_enabled(graph.packages[usize {}], "explicit"_str));
}

TEST(PackageFeatures, UnifiesDependencyRequestsAndIgnoresInactiveDevEdges) {
    auto declarations = Vec<lito::manifest::FeatureDeclaration>::make();
    declarations.push(feature("first"_str));
    declarations.push(feature("second"_str));

    auto first_dependencies = Vec<lito::package::ResolvedRequiredDependency>::make();
    first_dependencies.push(dependency("provider"_str, names("first"_str), false));
    auto first_dev_dependencies = Vec<lito::package::ResolvedRequiredDependency>::make();
    first_dev_dependencies.push(dev_dependency("provider"_str, names("second"_str), false));
    auto second_dependencies = Vec<lito::package::ResolvedRequiredDependency>::make();
    second_dependencies.push(dependency("provider"_str, names("second"_str), false));

    auto packages = Vec<lito::package::ResolvedPackage>::make();
    packages.push(package("first-consumer"_str,
                          {},
                          rstd::move(first_dependencies),
                          rstd::move(first_dev_dependencies)));
    packages.push(package("provider"_str, rstd::move(declarations)));
    packages.push(package("second-consumer"_str, {}, rstd::move(second_dependencies)));
    auto graph    = lito::package::ResolvedPackageGraph { .packages = rstd::move(packages) };
    auto roots    = names("first-consumer"_str);
    auto selected = names("first-consumer"_str, "provider"_str);
    auto targets  = library_targets("first-consumer"_str);

    auto resolved = lito::package::resolve_features(
        graph, roots, selected, targets, lito::package::FeatureSelection {});
    ASSERT_TRUE(resolved.is_ok());
    EXPECT_TRUE(feature_enabled(graph.packages[usize(1)], "first"_str));
    EXPECT_FALSE(feature_enabled(graph.packages[usize(1)], "second"_str));

    roots    = names("first-consumer"_str, "second-consumer"_str);
    selected = names("first-consumer"_str, "provider"_str, "second-consumer"_str);
    targets  = library_targets("first-consumer"_str, "second-consumer"_str);
    resolved = lito::package::resolve_features(
        graph, roots, selected, targets, lito::package::FeatureSelection {});
    ASSERT_TRUE(resolved.is_ok());
    EXPECT_TRUE(feature_enabled(graph.packages[usize(1)], "first"_str));
    EXPECT_TRUE(feature_enabled(graph.packages[usize(1)], "second"_str));
    ASSERT_EQ(graph.packages[usize(1)].features[usize(1)].activation_sources.len(), usize(1));
}

TEST(PackageFeatures, RejectsUnknownFeatureRequests) {
    auto packages = Vec<lito::package::ResolvedPackage>::make();
    packages.push(package("provider"_str));
    auto graph     = lito::package::ResolvedPackageGraph { .packages = rstd::move(packages) };
    auto roots     = names("provider"_str);
    auto targets   = library_targets("provider"_str);
    auto selection = lito::package::FeatureSelection {
        .enabled = names("missing"_str),
    };

    EXPECT_TRUE(lito::package::resolve_features(graph, roots, roots, targets, selection).is_err());
}

TEST(PackageFeatures, QualifiedRootsStayIsolatedAndResetBetweenSelections) {
    auto graph = lito::package::ResolvedPackageGraph {};
    graph.packages.push(package("first"_str, feature_declarations("vendored"_str)));
    graph.packages.push(package("second"_str, feature_declarations("vendored"_str)));
    graph.packages.push(package("plain"_str));
    graph.packages.push(package("unselected"_str, feature_declarations("vendored"_str)));
    auto roots     = names("first"_str, "second"_str, "plain"_str);
    auto targets   = library_targets("first"_str, "second"_str, "plain"_str);
    auto selection = lito::package::FeatureSelection {
        .enabled          = names(" first/vendored,,\t"_str, "first/vendored\n"_str),
        .default_features = false,
    };
    ASSERT_TRUE(lito::package::resolve_features(graph, roots, roots, targets, selection).is_ok());
    EXPECT_TRUE(feature_enabled(graph.packages[usize {}], "vendored"_str));
    EXPECT_FALSE(feature_enabled(graph.packages[usize(1)], "vendored"_str));
    EXPECT_FALSE(feature_enabled(graph.packages[usize(3)], "vendored"_str));
    const auto& sources = graph.packages[usize {}].features[usize {}].activation_sources;
    ASSERT_EQ(sources.len(), usize(1));
    EXPECT_TRUE(sources[usize {}].as_str().contains("first/vendored"_str));
    ASSERT_TRUE(lito::package::select_conditioned_packages(
                    graph, roots, targets, nullptr, None(), selection, {}, false)
                    .is_ok());
    EXPECT_TRUE(feature_enabled(graph.packages[usize {}], "vendored"_str));
    EXPECT_FALSE(feature_enabled(graph.packages[usize(1)], "vendored"_str));
    selection.enabled.clear();
    ASSERT_TRUE(lito::package::select_conditioned_packages(
                    graph, roots, targets, nullptr, None(), selection, {}, false)
                    .is_ok());
    EXPECT_FALSE(feature_enabled(graph.packages[usize {}], "vendored"_str));
}

TEST(PackageFeatures, BareFeaturesMatchDeclaringRootsWithoutRequiringEveryRoot) {
    auto graph = lito::package::ResolvedPackageGraph {};
    graph.packages.push(package("first"_str, feature_declarations("vendored"_str)));
    graph.packages.push(package("second"_str, feature_declarations("vendored"_str, "extra"_str)));
    graph.packages.push(package("plain"_str));
    auto roots     = names("first"_str, "second"_str, "plain"_str);
    auto targets   = library_targets("first"_str, "second"_str, "plain"_str);
    auto selection = lito::package::FeatureSelection {
        .enabled = names("vendored second/extra"_str),
    };
    ASSERT_TRUE(lito::package::resolve_features(graph, roots, roots, targets, selection).is_ok());
    EXPECT_TRUE(feature_enabled(graph.packages[usize {}], "vendored"_str));
    EXPECT_TRUE(feature_enabled(graph.packages[usize(1)], "vendored"_str));
    EXPECT_TRUE(feature_enabled(graph.packages[usize(1)], "extra"_str));
    ASSERT_TRUE(lito::package::select_conditioned_packages(
                    graph, roots, targets, nullptr, None(), selection, {}, false)
                    .is_ok());
    EXPECT_TRUE(feature_enabled(graph.packages[usize {}], "vendored"_str));
    EXPECT_TRUE(feature_enabled(graph.packages[usize(1)], "extra"_str));
}

TEST(PackageFeatures, QualifiedDirectDependenciesUnifyWithManifestRequests) {
    auto dependencies = Vec<lito::package::ResolvedRequiredDependency>::make();
    dependencies.push(dependency("lua"_str, names("extra"_str), false));
    dependencies.push(dependency("sqlite"_str, {}, false));
    auto graph = lito::package::ResolvedPackageGraph {};
    graph.packages.push(package("app"_str, {}, rstd::move(dependencies)));
    graph.packages.push(package("lua"_str, feature_declarations("vendored"_str, "extra"_str)));
    graph.packages.push(package("sqlite"_str, feature_declarations("vendored"_str)));
    auto selection = lito::package::FeatureSelection {
        .enabled = names("lua/vendored"_str, "lua/vendored"_str),
    };
    auto roots    = names("app"_str);
    auto targets  = library_targets("app"_str);
    auto selected = names("app"_str, "lua"_str, "sqlite"_str);
    ASSERT_TRUE(
        lito::package::resolve_features(graph, roots, selected, targets, selection).is_ok());
    EXPECT_TRUE(feature_enabled(graph.packages[usize(1)], "vendored"_str));
    EXPECT_TRUE(feature_enabled(graph.packages[usize(1)], "extra"_str));
    EXPECT_FALSE(feature_enabled(graph.packages[usize(2)], "vendored"_str));
    ASSERT_EQ(graph.packages[usize(1)].features[usize {}].activation_sources.len(), usize(1));
    ASSERT_TRUE(lito::package::select_conditioned_packages(
                    graph, roots, targets, nullptr, None(), selection, {}, false)
                    .is_ok());
    EXPECT_TRUE(feature_enabled(graph.packages[usize(1)], "vendored"_str));
    EXPECT_FALSE(feature_enabled(graph.packages[usize(2)], "vendored"_str));
    selection.enabled.clear();
    ASSERT_TRUE(lito::package::select_conditioned_packages(
                    graph, roots, targets, nullptr, None(), selection, {}, false)
                    .is_ok());
    EXPECT_FALSE(feature_enabled(graph.packages[usize(1)], "vendored"_str));
    EXPECT_TRUE(feature_enabled(graph.packages[usize(1)], "extra"_str));
}

TEST(PackageFeatures, QualifiedInactiveEdgesDoNotLeakToSharedProviders) {
    auto inactive                 = dependency("lua"_str, {}, false);
    inactive.activation.condition = Some(external_condition("feature.enable"_str));
    auto dependencies             = Vec<lito::package::ResolvedRequiredDependency>::make();
    dependencies.push(rstd::move(inactive));
    dependencies.push(dependency("middle"_str));
    auto middle_dependencies = Vec<lito::package::ResolvedRequiredDependency>::make();
    middle_dependencies.push(dependency("lua"_str, {}, false));
    auto graph = lito::package::ResolvedPackageGraph {};
    graph.packages.push(
        package("app"_str, feature_declarations("enable"_str), rstd::move(dependencies)));
    graph.packages.push(package("middle"_str, {}, rstd::move(middle_dependencies)));
    graph.packages.push(package("lua"_str, feature_declarations("vendored"_str)));
    auto roots     = names("app"_str);
    auto targets   = library_targets("app"_str);
    auto selection = lito::package::FeatureSelection { .enabled = names("lua/vendored"_str) };
    auto result    = lito::package::select_conditioned_packages(
        graph, roots, targets, nullptr, None(), selection, {}, false);
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(selected_name(result->target, "lua"_str));
    EXPECT_FALSE(feature_enabled(graph.packages[usize(2)], "vendored"_str));
    ASSERT_TRUE(
        lito::package::resolve_features(graph, roots, result->target, targets, selection).is_ok());
    EXPECT_FALSE(feature_enabled(graph.packages[usize(2)], "vendored"_str));
    selection.enabled.push("enable"_Str);
    ASSERT_TRUE(lito::package::select_conditioned_packages(
                    graph, roots, targets, nullptr, None(), selection, {}, false)
                    .is_ok());
    EXPECT_TRUE(feature_enabled(graph.packages[usize(2)], "vendored"_str));
}

TEST(PackageFeatures, QualifiedRequestsUnifyAcrossRootConsumers) {
    auto first = Vec<lito::package::ResolvedRequiredDependency>::make();
    first.push(dependency("lua"_str, {}, false));
    auto second = Vec<lito::package::ResolvedRequiredDependency>::make();
    second.push(dependency("lua"_str, {}, false));
    auto graph = lito::package::ResolvedPackageGraph {};
    graph.packages.push(package("first"_str, {}, rstd::move(first)));
    graph.packages.push(package("second"_str, {}, rstd::move(second)));
    graph.packages.push(package("lua"_str, feature_declarations("vendored"_str)));
    auto selection = lito::package::FeatureSelection { .enabled = names("lua/vendored"_str) };
    ASSERT_TRUE(
        lito::package::select_conditioned_packages(graph,
                                                   names("first"_str, "second"_str),
                                                   library_targets("first"_str, "second"_str),
                                                   nullptr,
                                                   None(),
                                                   selection,
                                                   {},
                                                   false)
            .is_ok());
    EXPECT_TRUE(feature_enabled(graph.packages[usize(2)], "vendored"_str));
    EXPECT_EQ(graph.packages[usize(2)].features[usize {}].activation_sources.len(), usize(2));
}

TEST(PackageFeatures, QualifiedDevelopmentFeaturesRequireDevelopmentTargets) {
    auto development = Vec<lito::package::ResolvedRequiredDependency>::make();
    development.push(dev_dependency("lua"_str, {}, false));
    auto graph = lito::package::ResolvedPackageGraph {};
    graph.packages.push(package("app"_str, {}, {}, rstd::move(development)));
    graph.packages.push(package("lua"_str, feature_declarations("vendored"_str)));
    auto roots     = names("app"_str);
    auto targets   = library_targets("app"_str);
    auto selection = lito::package::FeatureSelection { .enabled = names("lua/vendored"_str) };
    auto normal    = lito::package::select_conditioned_packages(
        graph, roots, targets, nullptr, None(), selection, {}, false);
    ASSERT_TRUE(normal.is_ok());
    EXPECT_FALSE(selected_name(normal->target, "lua"_str));
    EXPECT_FALSE(feature_enabled(graph.packages[usize(1)], "vendored"_str));
    targets[usize {}].kind = lito::package::PackageTargetKind::Test;
    ASSERT_TRUE(lito::package::select_conditioned_packages(
                    graph, roots, targets, nullptr, None(), selection, {}, false)
                    .is_ok());
    EXPECT_TRUE(feature_enabled(graph.packages[usize(1)], "vendored"_str));
}

TEST(PackageFeatures, RejectsMalformedAndOutOfScopeSelectors) {
    auto dependencies = Vec<lito::package::ResolvedRequiredDependency>::make();
    dependencies.push(dependency("middle"_str));
    auto middle_dependencies = Vec<lito::package::ResolvedRequiredDependency>::make();
    middle_dependencies.push(dependency("lua"_str));
    auto graph = lito::package::ResolvedPackageGraph {};
    graph.packages.push(
        package("app"_str, feature_declarations("vendored"_str), rstd::move(dependencies)));
    graph.packages.push(package("middle"_str, {}, rstd::move(middle_dependencies)));
    graph.packages.push(package("lua"_str, feature_declarations("vendored"_str)));
    graph.packages.push(package("unselected"_str, feature_declarations("vendored"_str)));
    const ref<str> invalid[] = {
        "/vendored"_str,           "app/"_str,
        "app/lua/vendored"_str,    "dep:lua"_str,
        "lua?/vendored"_str,       "unknown/vendored"_str,
        "unselected/vendored"_str, "lua/vendored"_str,
        "app/missing"_str,         "missing"_str,
        "middle/missing"_str,
    };
    for (auto value : invalid) {
        auto selection = lito::package::FeatureSelection { .enabled = names(value) };
        auto result    = lito::package::select_conditioned_packages(graph,
                                                                    names("app"_str),
                                                                    library_targets("app"_str),
                                                                    nullptr,
                                                                    None(),
                                                                    selection,
                                                                    {},
                                                                    false);
        ASSERT_TRUE(result.is_err());
        EXPECT_TRUE(error_chain_text(result.unwrap_err()).as_str().contains(value));
    }
}

TEST(PackageFeatures, ConfiguredFeaturesReachActiveTransitivePackagesBeforeConditions) {
    auto app_edges = Vec<lito::package::ResolvedRequiredDependency>::make();
    app_edges.push(dependency("middle"_str));
    auto middle_edges = Vec<lito::package::ResolvedRequiredDependency>::make();
    middle_edges.push(dependency("lua"_str, names("manifest"_str), false));
    auto lua_edges              = Vec<lito::package::ResolvedRequiredDependency>::make();
    auto marker                 = dependency("marker"_str);
    marker.activation.condition = Some(external_condition("feature.vendored"_str));
    lua_edges.push(rstd::move(marker));
    auto graph = lito::package::ResolvedPackageGraph {};
    graph.packages.push(
        package("app"_str, feature_declarations("command"_str), rstd::move(app_edges)));
    graph.packages.push(package("middle"_str, {}, rstd::move(middle_edges)));
    graph.packages.push(package(
        "lua"_str, feature_declarations("vendored"_str, "manifest"_str), rstd::move(lua_edges)));
    graph.packages.push(package("marker"_str));
    graph.packages.push(package("inactive"_str));
    auto selection = lito::package::FeatureSelection { .enabled = names("command"_str) };
    selection.configured.push(
        { "lua"_Str, names("vendored"_str, "vendored"_str), "config.packages.lua.features"_Str });
    selection.configured.push(
        { "inactive"_Str, names("missing"_str), "config.packages.inactive.features"_Str });
    selection.configured.push(
        { "absent"_Str, names("missing"_str), "config.packages.absent.features"_Str });
    auto roots    = names("app"_str);
    auto targets  = library_targets("app"_str);
    auto selected = lito::package::select_conditioned_packages(
        graph, roots, targets, nullptr, None(), selection, {}, false);
    ASSERT_TRUE(selected.is_ok());
    EXPECT_TRUE(feature_enabled(graph.packages[usize {}], "command"_str));
    EXPECT_TRUE(feature_enabled(graph.packages[usize(2)], "vendored"_str));
    EXPECT_TRUE(feature_enabled(graph.packages[usize(2)], "manifest"_str));
    EXPECT_TRUE(selected_name(selected->target, "marker"_str));
    EXPECT_FALSE(selected_name(selected->target, "inactive"_str));
    ASSERT_EQ(graph.packages[usize(2)].features[usize {}].activation_sources.len(), usize(1));
    EXPECT_EQ(graph.packages[usize(2)].features[usize {}].activation_sources[usize {}].as_str(),
              "config.packages.lua.features"_str);
    ASSERT_TRUE(lito::package::resolve_features(graph, roots, selected->target, targets, selection)
                    .is_ok());
    EXPECT_TRUE(feature_enabled(graph.packages[usize(2)], "vendored"_str));
    selection.configured.clear();
    selected = lito::package::select_conditioned_packages(
        graph, roots, targets, nullptr, None(), selection, {}, false);
    ASSERT_TRUE(selected.is_ok());
    EXPECT_FALSE(feature_enabled(graph.packages[usize(2)], "vendored"_str));
    EXPECT_TRUE(feature_enabled(graph.packages[usize(2)], "manifest"_str));
    EXPECT_FALSE(selected_name(selected->target, "marker"_str));
}

TEST(PackageFeatures, ConfiguredUnknownFeaturesAreCheckedOnlyForActivePackages) {
    auto edges                = Vec<lito::package::ResolvedRequiredDependency>::make();
    auto edge                 = dependency("lua"_str);
    edge.activation.condition = Some(external_condition("feature.enable"_str));
    edges.push(rstd::move(edge));
    auto graph = lito::package::ResolvedPackageGraph {};
    graph.packages.push(package("app"_str, feature_declarations("enable"_str), rstd::move(edges)));
    graph.packages.push(package("lua"_str));
    auto selection = lito::package::FeatureSelection {};
    selection.configured.push(
        { "lua"_Str, names("missing"_str), "config.packages.lua.features"_Str });
    auto roots    = names("app"_str);
    auto targets  = library_targets("app"_str);
    auto selected = lito::package::select_conditioned_packages(
        graph, roots, targets, nullptr, None(), selection, {}, false);
    ASSERT_TRUE(selected.is_ok());
    EXPECT_FALSE(selected_name(selected->target, "lua"_str));
    selection.enabled.push("enable"_Str);
    auto active = lito::package::select_conditioned_packages(
        graph, roots, targets, nullptr, None(), selection, {}, false);
    ASSERT_TRUE(active.is_err());
    auto error = error_chain_text(active.unwrap_err());
    EXPECT_TRUE(error.as_str().contains("has no feature 'missing'"_str));
    EXPECT_TRUE(error.as_str().contains("config.packages.lua.features"_str));
}

TEST(PackageFeatures, ClearingConfiguredRequestsDoesNotClearCommandLineFeatures) {
    auto edges = Vec<lito::package::ResolvedRequiredDependency>::make();
    edges.push(dependency("lua"_str, names("manifest"_str), false));
    auto graph = lito::package::ResolvedPackageGraph {};
    graph.packages.push(package("app"_str, {}, rstd::move(edges)));
    graph.packages.push(package("lua"_str, feature_declarations("vendored"_str, "manifest"_str)));
    auto selection = lito::package::FeatureSelection { .enabled = names("lua/vendored"_str) };
    selection.configured.push(
        { "lua"_Str, names("vendored"_str), "config.packages.lua.features"_Str });
    auto roots   = names("app"_str);
    auto targets = library_targets("app"_str);
    ASSERT_TRUE(lito::package::select_conditioned_packages(
                    graph, roots, targets, nullptr, None(), selection, {}, false)
                    .is_ok());
    EXPECT_TRUE(feature_enabled(graph.packages[usize(1)], "manifest"_str));
    EXPECT_EQ(graph.packages[usize(1)].features[usize {}].activation_sources.len(), usize(2));
    selection.configured.clear();
    ASSERT_TRUE(lito::package::select_conditioned_packages(
                    graph, roots, targets, nullptr, None(), selection, {}, false)
                    .is_ok());
    EXPECT_TRUE(feature_enabled(graph.packages[usize(1)], "vendored"_str));
    EXPECT_TRUE(feature_enabled(graph.packages[usize(1)], "manifest"_str));
    EXPECT_EQ(graph.packages[usize(1)].features[usize {}].activation_sources.len(), usize(1));
}

TEST(PackageFeatures, ConfiguredFeaturesApplyToActiveHostProviders) {
    auto edges                            = Vec<lito::package::ResolvedRequiredDependency>::make();
    auto tool                             = dependency("generator"_str);
    tool.as_Cpp().value.consumption.usage = lito::dependency::DependencyUsage::tool_only();
    edges.push(rstd::move(tool));
    auto graph = lito::package::ResolvedPackageGraph {};
    graph.packages.push(package("app"_str, {}, rstd::move(edges)));
    graph.packages.push(package("generator"_str, feature_declarations("vendored"_str)));
    auto selection = lito::package::FeatureSelection {};
    selection.configured.push(
        { "generator"_Str, names("vendored"_str), "config.packages.generator.features"_Str });
    auto selected = lito::package::select_conditioned_packages(
        graph, names("app"_str), library_targets("app"_str), nullptr, None(), selection, {}, false);
    ASSERT_TRUE(selected.is_ok());
    EXPECT_TRUE(selected_name(selected->host, "generator"_str));
    EXPECT_FALSE(selected_name(selected->target, "generator"_str));
    EXPECT_TRUE(feature_enabled(graph.packages[usize(1)], "vendored"_str));
}

TEST(PackageFeatures, SelectsExternalDependenciesAfterGraphFeatureUnion) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto profile = default_profile(*parser);

    auto packages = Vec<lito::package::ResolvedPackage>::make();
    packages.push(package_with_external_backends());
    auto graph   = lito::package::ResolvedPackageGraph { .packages = rstd::move(packages) };
    auto roots   = names("provider"_str);
    auto targets = library_targets("provider"_str);
    ASSERT_TRUE(lito::package::resolve_features(
                    graph, roots, roots, targets, lito::package::FeatureSelection {})
                    .is_ok());
    ASSERT_TRUE(lito::cpp::apply_package_configuration(
                    graph.packages[usize {}], configuration(), profile, native_platform(), false)
                    .is_ok());
    EXPECT_EQ(graph.packages[usize {}].effective_pkg_config_dependencies().len(), usize(1));
    EXPECT_TRUE(graph.packages[usize {}].effective_cmake_dependencies().is_empty());

    auto consumer_dependencies = Vec<lito::package::ResolvedRequiredDependency>::make();
    consumer_dependencies.push(dependency("provider"_str, names("qt"_str), false));
    auto enabled_packages = Vec<lito::package::ResolvedPackage>::make();
    enabled_packages.push(package("consumer"_str, {}, rstd::move(consumer_dependencies)));
    enabled_packages.push(package_with_external_backends());
    auto enabled_graph =
        lito::package::ResolvedPackageGraph { .packages = rstd::move(enabled_packages) };
    auto enabled_roots    = names("consumer"_str);
    auto enabled_selected = names("consumer"_str, "provider"_str);
    auto enabled_targets  = library_targets("consumer"_str);
    ASSERT_TRUE(lito::package::resolve_features(enabled_graph,
                                                enabled_roots,
                                                enabled_selected,
                                                enabled_targets,
                                                lito::package::FeatureSelection {})
                    .is_ok());
    ASSERT_TRUE(
        lito::cpp::apply_package_configuration(
            enabled_graph.packages[usize(1)], configuration(), profile, native_platform(), false)
            .is_ok());
    EXPECT_TRUE(enabled_graph.packages[usize(1)].effective_pkg_config_dependencies().is_empty());
    ASSERT_EQ(enabled_graph.packages[usize(1)].effective_cmake_dependencies().len(), usize(1));
    EXPECT_EQ(
        enabled_graph.packages[usize(1)].effective_cmake_dependencies()[usize {}].package.as_str(),
        "Qt6"_str);
}

TEST(PackageFeatures, ReportsExternalDependencyConditionContext) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto profile                   = default_profile(*parser);
    auto package                   = package_with_external_backends();
    package.manifest.manifest_path = rstd::path::PathBuf::from("fixture/lito.toml"_str);
    package.manifest.pkg_config_external_dependencies[usize {}].condition =
        Some(external_condition("feature.unknown"_str));

    auto resolved = lito::cpp::apply_package_configuration(
        package, configuration(), profile, native_platform(), false);
    ASSERT_TRUE(resolved.is_err());
    auto message = error_chain_text(resolved.unwrap_err());
    EXPECT_TRUE(message.as_str().contains("fixture/lito.toml"_str));
    EXPECT_TRUE(message.as_str().contains("curl"_str));
    EXPECT_TRUE(message.as_str().contains("feature.unknown"_str));
}

auto conditioned_dependency(ref<str> name, ref<str> condition, Vec<String> requested = {})
    -> lito::package::ResolvedRequiredDependency {
    auto result                 = dependency(name, rstd::move(requested), false);
    result.activation.condition = Some(external_condition(condition));
    return result;
}

TEST(PackageFeatures, ConditionsWaitForAllConsumersAndResetBetweenSelections) {
    auto a_edges = Vec<lito::package::ResolvedRequiredDependency>::make();
    a_edges.push(dependency("b"_str, {}, false));
    a_edges.push(conditioned_dependency("c"_str, "feature.use-c"_str));
    auto b_edges = Vec<lito::package::ResolvedRequiredDependency>::make();
    b_edges.push(conditioned_dependency("slow"_str, "!feature.fast"_str));
    auto c_edges = Vec<lito::package::ResolvedRequiredDependency>::make();
    c_edges.push(dependency("b"_str, names("fast"_str), false));
    auto a_features = Vec<lito::manifest::FeatureDeclaration>::make();
    a_features.push(feature("use-c"_str));
    auto b_features = Vec<lito::manifest::FeatureDeclaration>::make();
    b_features.push(feature("fast"_str));
    auto graph = lito::package::ResolvedPackageGraph {};
    graph.packages.push(package("b"_str, rstd::move(b_features), rstd::move(b_edges)));
    graph.packages.push(package("a"_str, rstd::move(a_features), rstd::move(a_edges)));
    graph.packages.push(package("slow"_str));
    graph.packages.push(package("c"_str, {}, rstd::move(c_edges)));
    auto roots       = names("a"_str);
    auto targets     = library_targets("a"_str);
    auto environment = lito::package::PackageConditionEnvironment {};
    auto selection   = lito::package::FeatureSelection { .enabled = names("use-c"_str) };
    for (int iteration = 0; iteration < 3; ++iteration) {
        if (iteration == 1) selection.enabled.clear();
        if (iteration == 2) selection.enabled = names("use-c"_str);
        auto resolved = lito::package::select_conditioned_packages(
            graph, roots, targets, nullptr, None(), selection, environment, false);
        ASSERT_TRUE(resolved.is_ok());
        EXPECT_EQ(selected_name(resolved->target, "slow"_str), iteration == 1);
        EXPECT_EQ(selected_name(resolved->target, "c"_str), iteration != 1);
        EXPECT_EQ(feature_enabled(graph.packages[usize {}], "fast"_str), iteration != 1);
        EXPECT_EQ(graph.packages[usize {}].dependencies.len(), usize(1));
    }
}

TEST(PackageFeatures, InactiveEdgesDoNotRequestFeaturesFromAnActiveProvider) {
    auto a_edges = Vec<lito::package::ResolvedRequiredDependency>::make();
    a_edges.push(conditioned_dependency("shared"_str, "false"_str, names("missing"_str)));
    auto b_edges = Vec<lito::package::ResolvedRequiredDependency>::make();
    b_edges.push(dependency("shared"_str, {}, false));
    auto graph = lito::package::ResolvedPackageGraph {};
    graph.packages.push(package("a"_str, {}, rstd::move(a_edges)));
    graph.packages.push(package("shared"_str));
    graph.packages.push(package("b"_str, {}, rstd::move(b_edges)));
    auto result = lito::package::select_conditioned_packages(graph,
                                                             names("a"_str, "b"_str),
                                                             library_targets("a"_str, "b"_str),
                                                             nullptr,
                                                             None(),
                                                             {},
                                                             {},
                                                             false);
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(selected_name(result->target, "shared"_str));
    EXPECT_FALSE(graph.packages[usize {}].dependencies[usize {}].activation.target);
}

TEST(PackageFeatures, ConditionsDistinguishHostAndTargetConsumers) {
    auto edges = Vec<lito::package::ResolvedRequiredDependency>::make();
    edges.push(conditioned_dependency("platform-only"_str, "target.os == \"linux\""_str));
    auto graph = lito::package::ResolvedPackageGraph {};
    graph.packages.push(package("root"_str, {}, rstd::move(edges)));
    graph.packages.push(package("platform-only"_str));
    auto targets = library_targets("root"_str);
    targets.push({ .package = "root"_Str,
                   .kind    = lito::package::PackageTargetKind::Plugin,
                   .name    = "root-plugin"_Str });
    auto environment = lito::package::PackageConditionEnvironment {};
    environment.target.set_string("target.os"_Str, "windows"_Str);
    environment.host.set_string("target.os"_Str, "linux"_Str);
    auto result = lito::package::select_conditioned_packages(
        graph, names("root"_str), targets, nullptr, None(), {}, environment, false);
    ASSERT_TRUE(result.is_ok());
    EXPECT_FALSE(selected_name(result->target, "platform-only"_str));
    EXPECT_TRUE(selected_name(result->host, "platform-only"_str));
    EXPECT_FALSE(graph.packages[usize {}].dependencies[usize {}].activation.target);
    EXPECT_TRUE(graph.packages[usize {}].dependencies[usize {}].activation.host);
}

TEST(PackageFeatures, ConditionsRespectDevelopmentAndRuntimePurposes) {
    auto dev = Vec<lito::package::ResolvedRequiredDependency>::make();
    dev.push(conditioned_dependency("dev"_str, "true"_str));
    auto graph = lito::package::ResolvedPackageGraph {};
    graph.packages.push(package("root"_str, {}, {}, rstd::move(dev)));
    graph.packages[usize {}].runtime_dependencies.push({
        .name       = "runtime"_Str,
        .activation = { .condition = Some(external_condition("feature.runtime"_str)) },
    });
    graph.packages[usize {}].manifest.features.push(feature("runtime"_str));
    graph.packages.push(package("dev"_str));
    graph.packages.push(package("runtime"_str));
    auto roots   = names("root"_str);
    auto targets = library_targets("root"_str);
    auto result  = lito::package::select_conditioned_packages(
        graph, roots, targets, nullptr, None(), {}, {}, false);
    ASSERT_TRUE(result.is_ok());
    EXPECT_FALSE(selected_name(result->target, "dev"_str));
    EXPECT_FALSE(selected_name(result->target, "runtime"_str));
    targets[usize {}].kind = lito::package::PackageTargetKind::Test;
    result                 = lito::package::select_conditioned_packages(
        graph, roots, targets, nullptr, None(), {}, {}, false);
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(selected_name(result->target, "dev"_str));
    auto selection         = lito::package::FeatureSelection { .enabled = names("runtime"_str) };
    targets[usize {}].kind = lito::package::PackageTargetKind::Binary;
    result                 = lito::package::select_conditioned_packages(
        graph, roots, targets, nullptr, None(), selection, {}, true);
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(selected_name(result->install, "runtime"_str));
    EXPECT_FALSE(selected_name(result->target, "dev"_str));
}

TEST(PackageFeatures, ToolUsageKeepsHostAndTargetInstancesSeparate) {
    auto tool                             = dependency("generator"_str);
    tool.as_Cpp().value.consumption.usage = lito::dependency::DependencyUsage::tool_only();
    auto root_edges                       = Vec<lito::package::ResolvedRequiredDependency>::make();
    root_edges.push(rstd::move(tool));
    root_edges.push(dependency("library-user"_str));
    auto library_edges = Vec<lito::package::ResolvedRequiredDependency>::make();
    library_edges.push(dependency("generator"_str));
    auto generator_edges = Vec<lito::package::ResolvedRequiredDependency>::make();
    generator_edges.push(conditioned_dependency("host-library"_str, "target.os == \"linux\""_str));
    auto graph = lito::package::ResolvedPackageGraph {};
    graph.packages.push(package("root"_str, {}, rstd::move(root_edges)));
    graph.packages.push(package("library-user"_str, {}, rstd::move(library_edges)));
    graph.packages.push(package("generator"_str, {}, rstd::move(generator_edges)));
    graph.packages.push(package("host-library"_str));
    auto environment = lito::package::PackageConditionEnvironment {};
    environment.target.set_string("target.os"_Str, "unknown"_Str);
    environment.host.set_string("target.os"_Str, "linux"_Str);
    auto selected = lito::package::select_conditioned_packages(graph,
                                                               names("root"_str),
                                                               library_targets("root"_str),
                                                               nullptr,
                                                               None(),
                                                               {},
                                                               environment,
                                                               false);
    ASSERT_TRUE(selected.is_ok());
    EXPECT_TRUE(selected_name(selected->target, "generator"_str));
    EXPECT_TRUE(selected_name(selected->host, "generator"_str));
    EXPECT_TRUE(selected_name(selected->tools, "generator"_str));
    EXPECT_FALSE(selected_name(selected->target, "host-library"_str));
    EXPECT_TRUE(selected_name(selected->host, "host-library"_str));
}

TEST(PackageFeatures, CombinedToolUsageSelectsBothDomainsForNormalAndDevelopmentEdges) {
    const bool kinds[] = { false, true };
    for (bool development : kinds) {
        auto edge = dependency("generator"_str);
        edge.as_Cpp().value.consumption.usage =
            lito::dependency::DependencyUsage::from_facets(true, true, false, true).unwrap();
        auto edges = Vec<lito::package::ResolvedRequiredDependency>::make();
        edges.push(rstd::move(edge));
        auto graph = lito::package::ResolvedPackageGraph {};
        graph.packages.push(development ? package("root"_str, {}, {}, rstd::move(edges))
                                        : package("root"_str, {}, rstd::move(edges)));
        auto dependencies = Vec<lito::package::ResolvedRequiredDependency>::make();
        dependencies.push(conditioned_dependency("host-library"_str, "target.os == \"linux\""_str));
        graph.packages.push(package("generator"_str, {}, rstd::move(dependencies)));
        graph.packages.push(package("host-library"_str));
        auto environment = lito::package::PackageConditionEnvironment {};
        environment.target.set_string("target.os"_Str, "unknown"_Str);
        environment.host.set_string("target.os"_Str, "linux"_Str);
        auto targets = library_targets("root"_str);
        if (development) {
            auto inactive = lito::package::select_conditioned_packages(
                graph, names("root"_str), targets, nullptr, None(), {}, environment, false);
            ASSERT_TRUE(inactive.is_ok());
            EXPECT_FALSE(selected_name(inactive->target, "generator"_str));
            EXPECT_FALSE(selected_name(inactive->host, "generator"_str));
            targets[usize {}].kind = lito::package::PackageTargetKind::Test;
        }
        auto selected = lito::package::select_conditioned_packages(
            graph, names("root"_str), targets, nullptr, None(), {}, environment, false);
        ASSERT_TRUE(selected.is_ok());
        EXPECT_TRUE(selected_name(selected->target, "generator"_str));
        EXPECT_TRUE(selected_name(selected->host, "generator"_str));
        EXPECT_TRUE(selected_name(selected->tools, "generator"_str));
        EXPECT_FALSE(selected_name(selected->target, "host-library"_str));
        EXPECT_TRUE(selected_name(selected->host, "host-library"_str));
        auto target_edges = usize {}, host_edges = usize {};
        for (const auto& selected_edge : selected->graph.dependencies) {
            if (selected_edge.consumer.package != usize {}) continue;
            EXPECT_EQ(selected_edge.declaration, usize {});
            EXPECT_EQ(selected_edge.kind,
                      development ? lito::package::DependencyKind::Development
                                  : lito::package::DependencyKind::Normal);
            if (selected_edge.provider.host)
                ++host_edges;
            else
                ++target_edges;
        }
        EXPECT_EQ(target_edges, usize(1));
        EXPECT_EQ(host_edges, usize(1));
    }
}

TEST(PackageFeatures, ProcessorConfigurationRequiresActiveToolUsage) {
    auto edges = Vec<lito::package::ResolvedRequiredDependency>::make();
    edges.push(dependency("processor"_str));
    auto graph = lito::package::ResolvedPackageGraph {};
    graph.packages.push(package("root"_str, {}, rstd::move(edges)));
    graph.packages.push(package("processor"_str));
    auto selected = lito::package::select_conditioned_packages(graph,
                                                               names("root"_str),
                                                               library_targets("root"_str),
                                                               nullptr,
                                                               Some("processor"_str),
                                                               {},
                                                               {},
                                                               false);
    ASSERT_TRUE(selected.is_err());
    EXPECT_TRUE(error_chain_text(selected.unwrap_err()).as_str().contains("usage = 'tool'"_str));
    graph.packages[usize {}].dependencies[usize {}].as_Cpp().value.consumption.usage =
        lito::dependency::DependencyUsage::tool_only();
    selected = lito::package::select_conditioned_packages(graph,
                                                          names("root"_str),
                                                          library_targets("root"_str),
                                                          nullptr,
                                                          Some("processor"_str),
                                                          {},
                                                          {},
                                                          false);
    ASSERT_TRUE(selected.is_ok());
    EXPECT_TRUE(selected_name(selected->processors, "processor"_str));
    EXPECT_FALSE(selected_name(selected->target, "processor"_str));
    graph.packages[usize {}].dependencies[usize {}].activation.condition =
        Some(external_condition("false"_str));
    selected = lito::package::select_conditioned_packages(graph,
                                                          names("root"_str),
                                                          library_targets("root"_str),
                                                          nullptr,
                                                          Some("processor"_str),
                                                          {},
                                                          {},
                                                          false);
    EXPECT_TRUE(selected.is_err());
}

TEST(PackageFeatures, DevelopmentToolUsageIsSelectedOnlyForDevelopmentTargets) {
    auto tool                             = dependency("generator"_str);
    tool.as_Cpp().value.consumption.usage = lito::dependency::DependencyUsage::tool_only();
    auto development                      = Vec<lito::package::ResolvedRequiredDependency>::make();
    development.push(rstd::move(tool));
    auto graph = lito::package::ResolvedPackageGraph {};
    graph.packages.push(package("root"_str, {}, {}, rstd::move(development)));
    graph.packages.push(package("generator"_str));
    auto targets  = library_targets("root"_str);
    auto selected = lito::package::select_conditioned_packages(
        graph, names("root"_str), targets, nullptr, None(), {}, {}, false);
    ASSERT_TRUE(selected.is_ok());
    EXPECT_TRUE(selected->host.is_empty());
    targets[usize {}].kind = lito::package::PackageTargetKind::Test;
    selected               = lito::package::select_conditioned_packages(
        graph, names("root"_str), targets, nullptr, None(), {}, {}, false);
    ASSERT_TRUE(selected.is_ok());
    EXPECT_TRUE(selected_name(selected->host, "generator"_str));
    EXPECT_FALSE(selected_name(selected->target, "generator"_str));
}

TEST(PackageFeatures, ExternalConditionSelectionPreservesDeclarations) {
    auto package = package_with_external_backends();
    auto context = lito::condition::Context {};
    context.set_bool("feature.qt"_Str, false);
    ASSERT_TRUE(lito::cpp::resolve_external_dependency_conditions(package, context).is_ok());
    EXPECT_EQ(package.effective_pkg_config_dependencies().len(), usize(1));
    EXPECT_TRUE(package.effective_cmake_dependencies().is_empty());
    EXPECT_EQ(package.manifest.cmake_external_dependencies.len(), usize(1));
    context.set_bool("feature.qt"_Str, true);
    ASSERT_TRUE(lito::cpp::resolve_external_dependency_conditions(package, context).is_ok());
    EXPECT_TRUE(package.effective_pkg_config_dependencies().is_empty());
    EXPECT_EQ(package.effective_cmake_dependencies().len(), usize(1));
    EXPECT_EQ(package.manifest.pkg_config_external_dependencies.len(), usize(1));
}

TEST(PackageFeatures, SelectionProjectsExternalConditionsBeforeBuildConsumers) {
    auto graph = lito::package::ResolvedPackageGraph {};
    graph.packages.push(package_with_external_backends());
    auto selection = lito::package::FeatureSelection { .enabled = names("provider/qt"_str) };
    auto result    = lito::package::select_conditioned_packages(graph,
                                                                names("provider"_str),
                                                                library_targets("provider"_str),
                                                                nullptr,
                                                                None(),
                                                                selection,
                                                                {},
                                                                false);
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(graph.packages[usize {}].effective_pkg_config_dependencies().is_empty());
    EXPECT_EQ(graph.packages[usize {}].effective_cmake_dependencies().len(), usize(1));
    EXPECT_EQ(graph.packages[usize {}].manifest.pkg_config_external_dependencies.len(), usize(1));
    selection.enabled.clear();
    result = lito::package::select_conditioned_packages(graph,
                                                        names("provider"_str),
                                                        library_targets("provider"_str),
                                                        nullptr,
                                                        None(),
                                                        selection,
                                                        {},
                                                        false);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(graph.packages[usize {}].effective_pkg_config_dependencies().len(), usize(1));
    EXPECT_TRUE(graph.packages[usize {}].effective_cmake_dependencies().is_empty());
}
