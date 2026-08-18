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

TEST(Cpp, MaterializesTypedDefaultWarnings) {
    auto defaults = cpp_options(
        "c++20"_str, lito::manifest::Optimization::None, lito::manifest::DebugInfo::None);
    ASSERT_EQ(defaults.diagnostics.warnings.len(), usize(5));
    EXPECT_EQ(defaults.diagnostics.warnings[usize {}].warning, cpp::CppWarning::All);
    EXPECT_TRUE(defaults.diagnostics.warnings[usize {}].enabled);
    EXPECT_EQ(defaults.diagnostics.warnings[usize(1)].warning, cpp::CppWarning::Pedantic);
    EXPECT_TRUE(defaults.diagnostics.warnings[usize(1)].enabled);
    EXPECT_EQ(defaults.diagnostics.warnings[usize(2)].warning,
              cpp::CppWarning::GnuStatementExpression);
    EXPECT_FALSE(defaults.diagnostics.warnings[usize(2)].enabled);
    EXPECT_EQ(defaults.diagnostics.warnings[usize(3)].warning,
              cpp::CppWarning::DeprecatedDeclarations);
    EXPECT_FALSE(defaults.diagnostics.warnings[usize(3)].enabled);
    EXPECT_EQ(defaults.diagnostics.warnings[usize(4)].warning, cpp::CppWarning::UnknownAttributes);
    EXPECT_TRUE(defaults.diagnostics.warnings[usize(4)].enabled);

    auto parsed = argument_layer(strings("-Wall"_str, "-Wno-deprecated-declarations"_str));
    ASSERT_EQ(parsed.occurrences.len(), usize(2));
    EXPECT_TRUE(parsed.occurrences[usize {}].argument.is_Warning());
    EXPECT_TRUE(parsed.occurrences[usize(1)].argument.is_Warning());
}

TEST(Cpp, MaterializesTypedDefaultPositionIndependentCode) {
    auto defaults = cpp_options(
        "c++20"_str, lito::manifest::Optimization::None, lito::manifest::DebugInfo::None);
    EXPECT_TRUE(defaults.common.codegen.position_independent_code);

    auto parsed = argument_layer(strings("-fno-PIC"_str));
    ASSERT_EQ(parsed.occurrences.len(), usize(1));
    EXPECT_TRUE(parsed.occurrences[usize {}].argument.is_PositionIndependentCode());

    auto disabled = cpp_options("c++20"_str,
                                lito::manifest::Optimization::None,
                                lito::manifest::DebugInfo::None,
                                strings("-fno-PIC"_str));
    EXPECT_FALSE(disabled.common.codegen.position_independent_code);
}

TEST(Cpp, AppliesTargetLocalVisibilityWithLastValueWins) {
    auto defaults = cpp_options(
        "c++20"_str, lito::manifest::Optimization::None, lito::manifest::DebugInfo::None);
    EXPECT_EQ(defaults.codegen.visibility.symbols, cpp::CppSymbolVisibility::Hidden);
    EXPECT_TRUE(defaults.codegen.visibility.types.is_none());
    EXPECT_FALSE(defaults.codegen.visibility.inlines_hidden);

    auto configured = cpp_options("c++20"_str,
                                  lito::manifest::Optimization::None,
                                  lito::manifest::DebugInfo::None,
                                  strings("-fvisibility=internal"_str,
                                          "-fvisibility=default"_str,
                                          "-ftype-visibility=protected"_str,
                                          "-fvisibility-inlines-hidden"_str,
                                          "-fno-visibility-inlines-hidden"_str));
    EXPECT_EQ(configured.codegen.visibility.symbols, cpp::CppSymbolVisibility::Default);
    ASSERT_TRUE(configured.codegen.visibility.types.is_some());
    EXPECT_EQ(*configured.codegen.visibility.types, cpp::CppSymbolVisibility::Protected);
    EXPECT_FALSE(configured.codegen.visibility.inlines_hidden);

    auto cloned = as<Clone>(configured).clone();
    ASSERT_TRUE(cloned.codegen.visibility.types.is_some());
    EXPECT_EQ(*cloned.codegen.visibility.types, cpp::CppSymbolVisibility::Protected);
    auto merged = cpp::merge_cpp_options(as<Clone>(defaults).clone(), configured);
    ASSERT_TRUE(merged.is_ok());
    EXPECT_EQ(merged->codegen.visibility.symbols, cpp::CppSymbolVisibility::Default);
    ASSERT_TRUE(merged->codegen.visibility.types.is_some());
    EXPECT_EQ(*merged->codegen.visibility.types, cpp::CppSymbolVisibility::Protected);
    auto reset = cpp::merge_cpp_options(rstd::move(cloned), defaults);
    ASSERT_TRUE(reset.is_ok());
    EXPECT_EQ(reset->codegen.visibility.symbols, cpp::CppSymbolVisibility::Hidden);
    EXPECT_TRUE(reset->codegen.visibility.types.is_none());

    EXPECT_NE(cpp::cpp_compile_identity(defaults).as_str(),
              cpp::cpp_compile_identity(configured).as_str());
    EXPECT_EQ(cpp::cpp_scan_identity(defaults).as_str(),
              cpp::cpp_scan_identity(configured).as_str());
    EXPECT_EQ(cpp::cpp_bmi_compatibility_identity(defaults).as_str(),
              cpp::cpp_bmi_compatibility_identity(configured).as_str());
    EXPECT_EQ(cpp::cpp_abi_compatibility_identity(defaults).as_str(),
              cpp::cpp_abi_compatibility_identity(configured).as_str());
    EXPECT_EQ(
        cpp::cpp_public_requirements_identity(cpp::cpp_public_requirements(defaults)).as_str(),
        cpp::cpp_public_requirements_identity(cpp::cpp_public_requirements(configured)).as_str());
}

TEST(Cpp, MaterializesTypedSizedDeallocationPolicy) {
    auto automatic = cpp_options(
        "c++20"_str, lito::manifest::Optimization::None, lito::manifest::DebugInfo::None);
    EXPECT_EQ(automatic.language.sized_deallocation, cpp::CppSizedDeallocation::Auto);

    auto enabled  = cpp_options("c++20"_str,
                                lito::manifest::Optimization::None,
                                lito::manifest::DebugInfo::None,
                                strings("-fsized-deallocation"_str));
    auto disabled = cpp_options("c++20"_str,
                                lito::manifest::Optimization::None,
                                lito::manifest::DebugInfo::None,
                                strings("-fno-sized-deallocation"_str));
    EXPECT_EQ(enabled.language.sized_deallocation, cpp::CppSizedDeallocation::Enabled);
    EXPECT_EQ(disabled.language.sized_deallocation, cpp::CppSizedDeallocation::Disabled);
    EXPECT_NE(cpp::cpp_compile_identity(automatic).as_str(),
              cpp::cpp_compile_identity(enabled).as_str());
    EXPECT_NE(cpp::cpp_scan_identity(automatic).as_str(), cpp::cpp_scan_identity(enabled).as_str());
    EXPECT_NE(cpp::cpp_bmi_compatibility_identity(automatic).as_str(),
              cpp::cpp_bmi_compatibility_identity(enabled).as_str());

    auto parsed =
        argument_layer(strings("-fsized-deallocation"_str, "-fno-sized-deallocation"_str));
    ASSERT_EQ(parsed.occurrences.len(), usize(2));
    EXPECT_TRUE(parsed.occurrences[usize {}].argument.is_SizedDeallocation());
    EXPECT_TRUE(parsed.occurrences[usize(1)].argument.is_SizedDeallocation());
}

TEST(Cpp, NormalizesCommonOptionsBeforeToolchainMapping) {
    auto first  = cpp_options("c++20"_str,
                              lito::manifest::Optimization::None,
                              lito::manifest::DebugInfo::Full,
                              strings("-Wall"_str,
                                      "-fPIC"_str,
                                      "--target=x86_64-unknown-linux-gnu"_str,
                                      "-msse2"_str,
                                      "-mno-sse2"_str));
    auto second = cpp_options("c++20"_str,
                              lito::manifest::Optimization::None,
                              lito::manifest::DebugInfo::Full,
                              strings("-Wall"_str,
                                      "--target"_str,
                                      "x86_64-unknown-linux-gnu"_str,
                                      "-mno-sse2"_str,
                                      "-fPIC"_str));
    EXPECT_EQ(cpp::cpp_compile_identity(first).as_str(),
              cpp::cpp_compile_identity(second).as_str());

    auto ordered =
        cpp::make_cpp_options("c++20"_str,
                              lito::config::StandardLibrary::Libcxx,
                              false,
                              false,
                              lito::manifest::Optimization::None,
                              lito::manifest::DebugInfo::None,
                              cpp::CppOptionLayer {
                                  .include_directories = paths("first"_str, "second"_str),
                              });
    auto reversed =
        cpp::make_cpp_options("c++20"_str,
                              lito::config::StandardLibrary::Libcxx,
                              false,
                              false,
                              lito::manifest::Optimization::None,
                              lito::manifest::DebugInfo::None,
                              cpp::CppOptionLayer {
                                  .include_directories = paths("second"_str, "first"_str),
                              });
    ASSERT_TRUE(ordered.is_ok());
    ASSERT_TRUE(reversed.is_ok());
    EXPECT_NE(cpp::cpp_compile_identity(*ordered).as_str(),
              cpp::cpp_compile_identity(*reversed).as_str());

    auto draft_standard = cpp_options(
        "c++2b"_str, lito::manifest::Optimization::None, lito::manifest::DebugInfo::None);
    auto published_standard = cpp_options(
        "c++23"_str, lito::manifest::Optimization::None, lito::manifest::DebugInfo::None);
    EXPECT_EQ(cpp::cpp_compile_identity(draft_standard).as_str(),
              cpp::cpp_compile_identity(published_standard).as_str());
}

TEST(Cpp, RejectsRawOverridesOfTypedSettings) {
    auto standard =
        cpp::make_cpp_options("c++20"_str,
                              lito::config::StandardLibrary::Libcxx,
                              false,
                              false,
                              lito::manifest::Optimization::None,
                              lito::manifest::DebugInfo::Full,
                              cpp::CppOptionLayer {
                                  .arguments = argument_layer(strings("-std=c++23"_str)),
                              });
    auto optimization = cpp::make_cpp_options("c++20"_str,
                                              lito::config::StandardLibrary::Libcxx,
                                              false,
                                              false,
                                              lito::manifest::Optimization::None,
                                              lito::manifest::DebugInfo::Full,
                                              cpp::CppOptionLayer {
                                                  .arguments = argument_layer(strings("-O3"_str)),
                                              });
    auto rtti = cpp::make_cpp_options("c++20"_str,
                                      lito::config::StandardLibrary::Libcxx,
                                      false,
                                      false,
                                      lito::manifest::Optimization::None,
                                      lito::manifest::DebugInfo::Full,
                                      cpp::CppOptionLayer {
                                          .arguments = argument_layer(strings("-frtti"_str)),
                                      });
    EXPECT_TRUE(standard.is_err());
    EXPECT_TRUE(optimization.is_err());
    EXPECT_TRUE(rtti.is_err());
    auto optimization_error = rstd::move(optimization).unwrap_err();
    ASSERT_TRUE(optimization_error.is_Message());
    EXPECT_TRUE(optimization_error.as_Message().message.as_str().contains("optimization"_str));
}
