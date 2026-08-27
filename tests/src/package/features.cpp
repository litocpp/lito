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
    -> lito::package::ResolvedCppDependency {
    return {
        .name             = String::make(name),
        .features         = rstd::move(features),
        .default_features = default_features,
    };
}

auto package(ref<str>                                       name,
             Vec<lito::manifest::FeatureDeclaration>        features         = {},
             Vec<lito::package::ResolvedRequiredDependency> dependencies     = {},
             Vec<lito::package::ResolvedCppDependency>      dev_dependencies = {})
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
            .alias     = String::make("curl"_str),
            .condition = Some(external_condition("!feature.qt"_str)),
        });
    result.manifest.cmake_external_dependencies.push(lito::dependency::CMakeDependencyRequirement {
        .alias     = String::make("qt"_str),
        .package   = String::make("Qt6"_str),
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
    auto first_dev_dependencies = Vec<lito::package::ResolvedCppDependency>::make();
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
    EXPECT_EQ(graph.packages[usize {}].manifest.pkg_config_external_dependencies.len(), usize(1));
    EXPECT_TRUE(graph.packages[usize {}].manifest.cmake_external_dependencies.is_empty());

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
    EXPECT_TRUE(
        enabled_graph.packages[usize(1)].manifest.pkg_config_external_dependencies.is_empty());
    ASSERT_EQ(enabled_graph.packages[usize(1)].manifest.cmake_external_dependencies.len(),
              usize(1));
    EXPECT_EQ(enabled_graph.packages[usize(1)]
                  .manifest.cmake_external_dependencies[usize {}]
                  .package.as_str(),
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
