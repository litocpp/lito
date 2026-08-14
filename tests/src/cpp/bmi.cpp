#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.test.cpp;
import lito.error;
import lito.compiler.arguments;
import lito.cpp;
import lito.cpp.bmi;
import lito.package.target_contract;
import lito.build.plan_contract;
import lito.toolchain;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito;
using namespace lito_test;

TEST(Bmi, SeparatesBuildRecipeFromConsumerCompatibility) {
    auto provider =
        cpp_options("c++20"_str, CppOptimization::None, CppDebugInfo::Full, strings("-Wall"_str));
    auto consumer = cpp_options(
        "c++20"_str, CppOptimization::Level3, CppDebugInfo::None, strings("-Wpedantic"_str));
    auto provider_format     = format();
    auto consumer_format     = format();
    auto public_requirements = cpp_public_requirements(provider);
    EXPECT_TRUE(check_bmi_compatibility(
                    provider_format, provider, public_requirements, consumer_format, consumer)
                    .compatible());
    EXPECT_NE(cpp_compile_identity(provider).as_str(), cpp_compile_identity(consumer).as_str());

    auto incompatible = cpp_options("c++23"_str, CppOptimization::Level3, CppDebugInfo::None);
    auto result       = check_bmi_compatibility(
        provider_format, provider, public_requirements, consumer_format, incompatible);
    ASSERT_FALSE(result.compatible());
    EXPECT_EQ(result.differences[usize {}].field, BmiCompatibilityField::LanguageStandard);

    auto different_format = format("clang-build-b"_str);
    result                = check_bmi_compatibility(
        provider_format, provider, public_requirements, different_format, provider);
    ASSERT_FALSE(result.compatible());
    EXPECT_EQ(result.differences[usize {}].field, BmiCompatibilityField::Format);
}

TEST(Bmi, ReportsConservativeSemanticDifferencesByField) {
    auto       provider     = cpp_options("c++20"_str, CppOptimization::None, CppDebugInfo::None);
    const auto expect_field = [&](const CppCompileOptions& consumer,
                                  BmiCompatibilityField    expected) {
        auto identity     = format();
        auto requirements = cpp_public_requirements(provider);
        auto result = check_bmi_compatibility(identity, provider, requirements, identity, consumer);
        ASSERT_FALSE(result.compatible());
        EXPECT_EQ(result.differences[usize {}].field, expected);
    };

    auto standard_library = make_cpp_options("c++20"_str,
                                             StandardLibrary::Libstdcxx,
                                             false,
                                             false,
                                             CppOptimization::None,
                                             CppDebugInfo::None);
    auto exceptions       = make_cpp_options("c++20"_str,
                                             StandardLibrary::Libcxx,
                                             true,
                                             false,
                                             CppOptimization::None,
                                             CppDebugInfo::None);
    auto rtti             = make_cpp_options("c++20"_str,
                                             StandardLibrary::Libcxx,
                                             false,
                                             true,
                                             CppOptimization::None,
                                             CppDebugInfo::None);
    ASSERT_TRUE(standard_library.is_ok());
    ASSERT_TRUE(exceptions.is_ok());
    ASSERT_TRUE(rtti.is_ok());
    expect_field(*standard_library, BmiCompatibilityField::StandardLibrary);
    expect_field(*exceptions, BmiCompatibilityField::Exceptions);
    expect_field(*rtti, BmiCompatibilityField::Rtti);
    expect_field(cpp_options("c++20"_str,
                             CppOptimization::None,
                             CppDebugInfo::None,
                             strings("-fno-sized-deallocation"_str)),
                 BmiCompatibilityField::SizedDeallocation);
    expect_field(
        cpp_options(
            "c++20"_str, CppOptimization::None, CppDebugInfo::None, strings("-ffreestanding"_str)),
        BmiCompatibilityField::LanguageModes);
    expect_field(
        cpp_options(
            "c++20"_str, CppOptimization::None, CppDebugInfo::None, strings("-fshort-enums"_str)),
        BmiCompatibilityField::AbiModes);
    expect_field(cpp_options("c++20"_str,
                             CppOptimization::None,
                             CppDebugInfo::None,
                             strings("--target=other-target"_str)),
                 BmiCompatibilityField::Target);
    expect_field(cpp_options("c++20"_str,
                             CppOptimization::None,
                             CppDebugInfo::None,
                             strings("--sysroot=/other-sysroot"_str)),
                 BmiCompatibilityField::Sysroot);
    expect_field(
        cpp_options("c++20"_str, CppOptimization::None, CppDebugInfo::None, strings("-msse2"_str)),
        BmiCompatibilityField::TargetFeatures);
    expect_field(cpp_options("c++20"_str,
                             CppOptimization::None,
                             CppDebugInfo::None,
                             strings("-funknown-lito-option"_str)),
                 BmiCompatibilityField::VendorSemantics);
}

TEST(Bmi, ArtifactIdentityIncludesRepresentationEmbeddingAndDependencies) {
    auto reduced = artifact_key(
        BmiRepresentation::Reduced, BmiSourceEmbeddingPolicy::ExternalSources, "dependency-a"_str);
    auto full = artifact_key(
        BmiRepresentation::Full, BmiSourceEmbeddingPolicy::ExternalSources, "dependency-a"_str);
    auto embedded = artifact_key(
        BmiRepresentation::Reduced, BmiSourceEmbeddingPolicy::EmbedAll, "dependency-a"_str);
    auto changed_dependency = artifact_key(
        BmiRepresentation::Reduced, BmiSourceEmbeddingPolicy::ExternalSources, "dependency-b"_str);
    auto changed_source = artifact_key(BmiRepresentation::Reduced,
                                       BmiSourceEmbeddingPolicy::ExternalSources,
                                       "dependency-a"_str,
                                       "source-content-b"_str);
    EXPECT_NE(reduced.value.as_str(), full.value.as_str());
    EXPECT_NE(reduced.value.as_str(), embedded.value.as_str());
    EXPECT_NE(reduced.value.as_str(), changed_dependency.value.as_str());
    EXPECT_NE(reduced.value.as_str(), changed_source.value.as_str());
    EXPECT_TRUE(bmi_supports_use(BmiRepresentation::Reduced, BmiUse::Import));
    EXPECT_FALSE(bmi_supports_use(BmiRepresentation::Reduced, BmiUse::GenerateObject));
    EXPECT_TRUE(bmi_supports_use(BmiRepresentation::Full, BmiUse::GenerateObject));
}

TEST(Bmi, RequiresPublicPreprocessorSemanticsWithoutLeakingPrivateInputs) {
    auto provider =
        make_cpp_options("c++20"_str,
                         StandardLibrary::Libcxx,
                         false,
                         false,
                         CppOptimization::None,
                         CppDebugInfo::None,
                         CppOptionLayer {
                             .definitions = strings("PRIVATE_VALUE=1"_str, "PUBLIC_VALUE=1"_str),
                         });
    auto public_context = make_cpp_options("c++20"_str,
                                           StandardLibrary::Libcxx,
                                           false,
                                           false,
                                           CppOptimization::None,
                                           CppDebugInfo::None,
                                           CppOptionLayer {
                                               .definitions = strings("PUBLIC_VALUE=1"_str),
                                           });
    auto consumer       = make_cpp_options("c++20"_str,
                                           StandardLibrary::Libcxx,
                                           false,
                                           false,
                                           CppOptimization::None,
                                           CppDebugInfo::None,
                                           CppOptionLayer {
                                               .definitions = strings("PUBLIC_VALUE=1"_str),
                                           });
    ASSERT_TRUE(provider.is_ok());
    ASSERT_TRUE(public_context.is_ok());
    ASSERT_TRUE(consumer.is_ok());
    auto required = cpp_public_requirements(*public_context);
    auto identity = format();
    EXPECT_TRUE(
        check_bmi_compatibility(identity, *provider, required, identity, *consumer).compatible());

    auto incompatible = make_cpp_options("c++20"_str,
                                         StandardLibrary::Libcxx,
                                         false,
                                         false,
                                         CppOptimization::None,
                                         CppDebugInfo::None,
                                         CppOptionLayer {
                                             .definitions = strings("PUBLIC_VALUE=2"_str),
                                         });
    ASSERT_TRUE(incompatible.is_ok());
    auto result = check_bmi_compatibility(identity, *provider, required, identity, *incompatible);
    ASSERT_FALSE(result.compatible());
    EXPECT_EQ(result.differences[usize {}].field,
              BmiCompatibilityField::PublicPreprocessorRequirements);
}
