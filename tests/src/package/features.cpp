#include <rstd/test/gtest.hpp>

import rstd;
import lito.core;

using namespace rstd::prelude;
using namespace rstd::literals;

auto feature(ref<str> name, bool default_enabled = false)
    -> lito::manifest::FeatureDeclaration {
    return {
        .name = String::make(name),
        .macro_name = lito::manifest::normalized_feature_macro(name),
        .default_enabled = default_enabled,
    };
}

auto dependency(ref<str> name,
                Vec<String> features = {},
                bool default_features = true) -> lito::package::ResolvedDependency {
    return {
        .name = String::make(name),
        .features = rstd::move(features),
        .default_features = default_features,
    };
}

auto package(ref<str> name,
             Vec<lito::manifest::FeatureDeclaration> features = {},
             Vec<lito::package::ResolvedDependency> dependencies = {},
             Vec<lito::package::ResolvedDependency> dev_dependencies = {})
    -> lito::package::ResolvedPackage {
    return {
        .manifest =
            lito::manifest::PackageManifest {
                .name = String::make(name),
                .features = rstd::move(features),
            },
        .dependencies = rstd::move(dependencies),
        .dev_dependencies = rstd::move(dev_dependencies),
    };
}

template<typename... Values>
auto names(Values... values) -> Vec<String> {
    auto result = Vec<String>::make();
    (result.push(String::make(values)), ...);
    return result;
}

template<typename... Packages>
auto library_targets(Packages... packages) -> Vec<lito::package::PackageTargetId> {
    auto result = Vec<lito::package::PackageTargetId>::make();
    const auto append = [&](ref<str> package) {
        result.push({
            .package = String::make(package),
            .kind = lito::package::PackageTargetKind::Library,
            .name = String::make(package),
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

TEST(PackageFeatures, ResolvesRootDefaultsAndCommandLineRequests) {
    auto declarations = Vec<lito::manifest::FeatureDeclaration>::make();
    declarations.push(feature("default-on"_str, true));
    declarations.push(feature("explicit"_str));
    auto packages = Vec<lito::package::ResolvedPackage>::make();
    packages.push(package("provider"_str, rstd::move(declarations)));
    auto graph = lito::package::ResolvedPackageGraph { .packages = rstd::move(packages) };
    auto roots = names("provider"_str);
    auto targets = library_targets("provider"_str);
    auto selection = lito::package::FeatureSelection {
        .enabled = names("explicit"_str),
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
}

TEST(PackageFeatures, UnifiesDependencyRequestsAndIgnoresInactiveDevEdges) {
    auto declarations = Vec<lito::manifest::FeatureDeclaration>::make();
    declarations.push(feature("first"_str));
    declarations.push(feature("second"_str));

    auto first_dependencies = Vec<lito::package::ResolvedDependency>::make();
    first_dependencies.push(dependency("provider"_str, names("first"_str), false));
    auto first_dev_dependencies = Vec<lito::package::ResolvedDependency>::make();
    first_dev_dependencies.push(dependency("provider"_str, names("second"_str), false));
    auto second_dependencies = Vec<lito::package::ResolvedDependency>::make();
    second_dependencies.push(dependency("provider"_str, names("second"_str), false));

    auto packages = Vec<lito::package::ResolvedPackage>::make();
    packages.push(package("first-consumer"_str,
                          {},
                          rstd::move(first_dependencies),
                          rstd::move(first_dev_dependencies)));
    packages.push(package("provider"_str, rstd::move(declarations)));
    packages.push(package("second-consumer"_str, {}, rstd::move(second_dependencies)));
    auto graph = lito::package::ResolvedPackageGraph { .packages = rstd::move(packages) };
    auto roots = names("first-consumer"_str);
    auto selected = names("first-consumer"_str, "provider"_str);
    auto targets = library_targets("first-consumer"_str);

    auto resolved = lito::package::resolve_features(
        graph, roots, selected, targets, lito::package::FeatureSelection {});
    ASSERT_TRUE(resolved.is_ok());
    EXPECT_TRUE(feature_enabled(graph.packages[usize(1)], "first"_str));
    EXPECT_FALSE(feature_enabled(graph.packages[usize(1)], "second"_str));

    roots = names("first-consumer"_str, "second-consumer"_str);
    selected = names("first-consumer"_str, "provider"_str, "second-consumer"_str);
    targets = library_targets("first-consumer"_str, "second-consumer"_str);
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
    auto graph = lito::package::ResolvedPackageGraph { .packages = rstd::move(packages) };
    auto roots = names("provider"_str);
    auto targets = library_targets("provider"_str);
    auto selection = lito::package::FeatureSelection {
        .enabled = names("missing"_str),
    };

    EXPECT_TRUE(
        lito::package::resolve_features(graph, roots, roots, targets, selection).is_err());
}
