#include <rstd/test/gtest.hpp>

import rstd;
import lito.core;

using namespace rstd::prelude;
using namespace rstd::literals;

auto language_package(ref<str>                                       name,
                      lito::manifest::PackageStandardRequirement     standard,
                      Vec<lito::package::ResolvedRequiredDependency> dependencies = {})
    -> lito::package::ResolvedPackage {
    return lito::package::ResolvedPackage {
        .manifest =
            lito::manifest::PackageManifest {
                .name     = String::make(name),
                .standard = Some(rstd::move(standard)),
            },
        .dependencies = rstd::move(dependencies),
    };
}

template<typename... Values>
auto language_names(Values... values) -> Vec<String> {
    auto result = Vec<String>::make();
    (result.push(String::make(values)), ...);
    return result;
}

TEST(PackageLanguage, ResolvesIndependentEffectiveStandards) {
    auto packages = Vec<lito::package::ResolvedPackage>::make();
    packages.push(language_package(
        "c99"_str, lito::manifest::PackageStandardRequirement::C(lito::manifest::CStandard::C99)));
    packages.push(language_package(
        "c17"_str, lito::manifest::PackageStandardRequirement::C(lito::manifest::CStandard::C17)));
    packages.push(language_package(
        "cpp20"_str,
        lito::manifest::PackageStandardRequirement::Cpp(lito::manifest::CppStandard::Cpp20)));
    packages.push(language_package(
        "cpp23"_str,
        lito::manifest::PackageStandardRequirement::Cpp(lito::manifest::CppStandard::Cpp23)));
    auto graph    = lito::package::ResolvedPackageGraph { .packages = rstd::move(packages) };
    auto selected = language_names("c99"_str, "c17"_str, "cpp20"_str, "cpp23"_str);

    auto standards = lito::package::resolve_effective_language_standards(graph, selected);
    ASSERT_TRUE(standards.is_ok());
    ASSERT_TRUE(standards->c.is_some());
    ASSERT_TRUE(standards->cpp.is_some());
    EXPECT_EQ(*standards->c, lito::manifest::CStandard::C17);
    EXPECT_EQ(*standards->cpp, lito::manifest::CppStandard::Cpp23);
    ASSERT_EQ(standards->c_provenance.len(), usize(1));
    EXPECT_EQ(standards->c_provenance[usize {}].as_str(), "c17"_str);
    ASSERT_EQ(standards->cpp_provenance.len(), usize(1));
    EXPECT_EQ(standards->cpp_provenance[usize {}].as_str(), "cpp23"_str);
}

TEST(PackageLanguage, RejectsCDependencyOnCppPackage) {
    auto dependencies = Vec<lito::package::ResolvedRequiredDependency>::make();
    dependencies.push(
        lito::package::ResolvedRequiredDependency::Cpp(lito::package::ResolvedCppDependency {
            .name = String::make("cpp-provider"_str),
        }));
    auto packages = Vec<lito::package::ResolvedPackage>::make();
    packages.push(language_package(
        "c-consumer"_str,
        lito::manifest::PackageStandardRequirement::C(lito::manifest::CStandard::C99),
        rstd::move(dependencies)));
    packages.push(language_package(
        "cpp-provider"_str,
        lito::manifest::PackageStandardRequirement::Cpp(lito::manifest::CppStandard::Cpp20)));
    auto graph    = lito::package::ResolvedPackageGraph { .packages = rstd::move(packages) };
    auto selected = language_names("c-consumer"_str, "cpp-provider"_str);

    auto standards = lito::package::resolve_effective_language_standards(graph, selected);
    ASSERT_TRUE(standards.is_err());
    auto error = rstd::move(standards).unwrap_err();
    EXPECT_TRUE(rstd::format("{}", error).as_str().contains("cannot depend on C++"_str));
}
