#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.test.cpp;
import lito.core;
import lito.cpp;
import lito.toolchain;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito;
using namespace lito_test;

TEST(Bmi, SeparatesBuildRecipeFromConsumerCompatibility) {
    auto provider = cpp_options(
        "c++20"_str, lito::CppOptimization::None, lito::CppDebugInfo::Full, strings("-Wall"_str));
    auto consumer            = cpp_options("c++20"_str,
                                           lito::CppOptimization::Level3,
                                           lito::CppDebugInfo::None,
                                           strings("-Wpedantic"_str));
    auto provider_format     = format();
    auto consumer_format     = format();
    auto public_requirements = cpp::cpp_public_requirements(provider);
    EXPECT_TRUE(cpp::check_bmi_compatibility(
                    provider_format, provider, public_requirements, consumer_format, consumer)
                    .compatible());
    EXPECT_NE(cpp::cpp_compile_identity(provider).as_str(),
              cpp::cpp_compile_identity(consumer).as_str());

    auto pthread = cpp_options("c++20"_str,
                               lito::CppOptimization::None,
                               lito::CppDebugInfo::None,
                               strings("-pthread"_str));
    EXPECT_FALSE(cpp::check_bmi_compatibility(
                     provider_format, provider, public_requirements, consumer_format, pthread)
                     .compatible());
    EXPECT_NE(cpp::cpp_scan_identity(provider).as_str(), cpp::cpp_scan_identity(pthread).as_str());

    auto incompatible =
        cpp_options("c++23"_str, lito::CppOptimization::Level3, lito::CppDebugInfo::None);
    auto result = cpp::check_bmi_compatibility(
        provider_format, provider, public_requirements, consumer_format, incompatible);
    ASSERT_FALSE(result.compatible());
    EXPECT_EQ(result.differences[usize {}].field, cpp::BmiCompatibilityField::LanguageStandard);

    auto different_format = format("clang-build-b"_str);
    result                = cpp::check_bmi_compatibility(
        provider_format, provider, public_requirements, different_format, provider);
    ASSERT_FALSE(result.compatible());
    EXPECT_EQ(result.differences[usize {}].field, cpp::BmiCompatibilityField::Format);
}

TEST(Bmi, ReportsConservativeSemanticDifferencesByField) {
    auto provider = cpp_options("c++20"_str, lito::CppOptimization::None, lito::CppDebugInfo::None);
    const auto expect_field = [&](const cpp::CppCompileOptions& consumer,
                                  cpp::BmiCompatibilityField    expected) {
        auto identity     = format();
        auto requirements = cpp::cpp_public_requirements(provider);
        auto result =
            cpp::check_bmi_compatibility(identity, provider, requirements, identity, consumer);
        ASSERT_FALSE(result.compatible());
        EXPECT_EQ(result.differences[usize {}].field, expected);
    };

    auto standard_library = cpp::make_cpp_options("c++20"_str,
                                                  StandardLibrary::Libstdcxx,
                                                  false,
                                                  false,
                                                  lito::CppOptimization::None,
                                                  lito::CppDebugInfo::None);
    auto exceptions       = cpp::make_cpp_options("c++20"_str,
                                                  StandardLibrary::Libcxx,
                                                  true,
                                                  false,
                                                  lito::CppOptimization::None,
                                                  lito::CppDebugInfo::None);
    auto rtti             = cpp::make_cpp_options("c++20"_str,
                                                  StandardLibrary::Libcxx,
                                                  false,
                                                  true,
                                                  lito::CppOptimization::None,
                                                  lito::CppDebugInfo::None);
    ASSERT_TRUE(standard_library.is_ok());
    ASSERT_TRUE(exceptions.is_ok());
    ASSERT_TRUE(rtti.is_ok());
    expect_field(*standard_library, cpp::BmiCompatibilityField::StandardLibrary);
    expect_field(*exceptions, cpp::BmiCompatibilityField::Exceptions);
    expect_field(*rtti, cpp::BmiCompatibilityField::Rtti);
    expect_field(cpp_options("c++20"_str,
                             lito::CppOptimization::None,
                             lito::CppDebugInfo::None,
                             strings("-fno-sized-deallocation"_str)),
                 cpp::BmiCompatibilityField::SizedDeallocation);
    expect_field(cpp_options("c++20"_str,
                             lito::CppOptimization::None,
                             lito::CppDebugInfo::None,
                             strings("-ffreestanding"_str)),
                 cpp::BmiCompatibilityField::LanguageModes);
    expect_field(cpp_options("c++20"_str,
                             lito::CppOptimization::None,
                             lito::CppDebugInfo::None,
                             strings("-fshort-enums"_str)),
                 cpp::BmiCompatibilityField::AbiModes);
    expect_field(cpp_options("c++20"_str,
                             lito::CppOptimization::None,
                             lito::CppDebugInfo::None,
                             strings("--target=other-target"_str)),
                 cpp::BmiCompatibilityField::Target);
    expect_field(cpp_options("c++20"_str,
                             lito::CppOptimization::None,
                             lito::CppDebugInfo::None,
                             strings("--sysroot=/other-sysroot"_str)),
                 cpp::BmiCompatibilityField::Sysroot);
    expect_field(cpp_options("c++20"_str,
                             lito::CppOptimization::None,
                             lito::CppDebugInfo::None,
                             strings("-msse2"_str)),
                 cpp::BmiCompatibilityField::TargetFeatures);
    expect_field(cpp_options("c++20"_str,
                             lito::CppOptimization::None,
                             lito::CppDebugInfo::None,
                             strings("-funknown-lito-option"_str)),
                 cpp::BmiCompatibilityField::VendorSemantics);
}

TEST(Bmi, TreatsStandardLibraryModesAsAnExplicitConsistencyDomain) {
    auto provider = cpp_options("c++20"_str,
                                lito::CppOptimization::None,
                                lito::CppDebugInfo::None,
                                strings("-D_GLIBCXX_USE_CXX11_ABI=0"_str));
    auto consumer = cpp_options("c++20"_str,
                                lito::CppOptimization::None,
                                lito::CppDebugInfo::None,
                                strings("-D_GLIBCXX_USE_CXX11_ABI=1"_str));
    auto identity     = format();
    auto requirements = cpp::cpp_public_requirements(provider);
    auto result =
        cpp::check_bmi_compatibility(identity, provider, requirements, identity, consumer);
    ASSERT_FALSE(result.compatible());
    EXPECT_EQ(result.differences[usize {}].field,
              cpp::BmiCompatibilityField::StandardLibraryModes);
    EXPECT_NE(cpp::cpp_abi_compatibility_identity(provider).as_str(),
              cpp::cpp_abi_compatibility_identity(consumer).as_str());
}

TEST(Bmi, ArtifactIdentityIncludesRepresentationEmbeddingAndDependencies) {
    auto reduced            = artifact_key(cpp::BmiRepresentation::Reduced,
                                           cpp::BmiSourceEmbeddingPolicy::ExternalSources,
                                           "dependency-a"_str);
    auto full               = artifact_key(cpp::BmiRepresentation::Full,
                                           cpp::BmiSourceEmbeddingPolicy::ExternalSources,
                                           "dependency-a"_str);
    auto embedded           = artifact_key(cpp::BmiRepresentation::Reduced,
                                           cpp::BmiSourceEmbeddingPolicy::EmbedAll,
                                           "dependency-a"_str);
    auto changed_dependency = artifact_key(cpp::BmiRepresentation::Reduced,
                                           cpp::BmiSourceEmbeddingPolicy::ExternalSources,
                                           "dependency-b"_str);
    auto changed_source     = artifact_key(cpp::BmiRepresentation::Reduced,
                                           cpp::BmiSourceEmbeddingPolicy::ExternalSources,
                                           "dependency-a"_str,
                                           "source-content-b"_str);
    EXPECT_NE(reduced.value.as_str(), full.value.as_str());
    EXPECT_NE(reduced.value.as_str(), embedded.value.as_str());
    EXPECT_NE(reduced.value.as_str(), changed_dependency.value.as_str());
    EXPECT_NE(reduced.value.as_str(), changed_source.value.as_str());
    EXPECT_TRUE(cpp::bmi_supports_use(cpp::BmiRepresentation::Reduced, cpp::BmiUse::Import));
    EXPECT_FALSE(
        cpp::bmi_supports_use(cpp::BmiRepresentation::Reduced, cpp::BmiUse::GenerateObject));
    EXPECT_TRUE(cpp::bmi_supports_use(cpp::BmiRepresentation::Full, cpp::BmiUse::GenerateObject));
}

TEST(Bmi, RequiresPublicPreprocessorSemanticsWithoutLeakingPrivateInputs) {
    auto provider = cpp::make_cpp_options(
        "c++20"_str,
        StandardLibrary::Libcxx,
        false,
        false,
        lito::CppOptimization::None,
        lito::CppDebugInfo::None,
        cpp::CppOptionLayer {
            .definitions = strings("PRIVATE_VALUE=1"_str, "PUBLIC_VALUE=1"_str),
        });
    auto public_context = cpp::make_cpp_options("c++20"_str,
                                                StandardLibrary::Libcxx,
                                                false,
                                                false,
                                                lito::CppOptimization::None,
                                                lito::CppDebugInfo::None,
                                                cpp::CppOptionLayer {
                                                    .definitions = strings("PUBLIC_VALUE=1"_str),
                                                });
    auto consumer       = cpp::make_cpp_options("c++20"_str,
                                                StandardLibrary::Libcxx,
                                                false,
                                                false,
                                                lito::CppOptimization::None,
                                                lito::CppDebugInfo::None,
                                                cpp::CppOptionLayer {
                                                    .definitions = strings("PUBLIC_VALUE=1"_str),
                                                });
    ASSERT_TRUE(provider.is_ok());
    ASSERT_TRUE(public_context.is_ok());
    ASSERT_TRUE(consumer.is_ok());
    auto required = cpp::cpp_public_requirements(*public_context);
    auto identity = format();
    EXPECT_TRUE(cpp::check_bmi_compatibility(identity, *provider, required, identity, *consumer)
                    .compatible());

    auto incompatible = cpp::make_cpp_options("c++20"_str,
                                              StandardLibrary::Libcxx,
                                              false,
                                              false,
                                              lito::CppOptimization::None,
                                              lito::CppDebugInfo::None,
                                              cpp::CppOptionLayer {
                                                  .definitions = strings("PUBLIC_VALUE=2"_str),
                                              });
    ASSERT_TRUE(incompatible.is_ok());
    auto result =
        cpp::check_bmi_compatibility(identity, *provider, required, identity, *incompatible);
    ASSERT_FALSE(result.compatible());
    EXPECT_EQ(result.differences[usize {}].field,
              cpp::BmiCompatibilityField::PublicPreprocessorRequirements);
}
