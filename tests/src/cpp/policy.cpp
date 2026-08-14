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

TEST(Cpp, MaterializesTypedDefaultWarnings) {
    auto defaults = cpp_options("c++20"_str, CppOptimization::None, CppDebugInfo::None);
    ASSERT_EQ(defaults.diagnostics.warnings.len(), usize(5));
    EXPECT_EQ(defaults.diagnostics.warnings[usize {}].warning, CppWarning::All);
    EXPECT_TRUE(defaults.diagnostics.warnings[usize {}].enabled);
    EXPECT_EQ(defaults.diagnostics.warnings[usize(1)].warning, CppWarning::Pedantic);
    EXPECT_TRUE(defaults.diagnostics.warnings[usize(1)].enabled);
    EXPECT_EQ(defaults.diagnostics.warnings[usize(2)].warning, CppWarning::GnuStatementExpression);
    EXPECT_FALSE(defaults.diagnostics.warnings[usize(2)].enabled);
    EXPECT_EQ(defaults.diagnostics.warnings[usize(3)].warning, CppWarning::DeprecatedDeclarations);
    EXPECT_FALSE(defaults.diagnostics.warnings[usize(3)].enabled);
    EXPECT_EQ(defaults.diagnostics.warnings[usize(4)].warning, CppWarning::UnknownAttributes);
    EXPECT_TRUE(defaults.diagnostics.warnings[usize(4)].enabled);

    auto parsed = argument_layer(strings("-Wall"_str, "-Wno-deprecated-declarations"_str));
    ASSERT_EQ(parsed.occurrences.len(), usize(2));
    EXPECT_TRUE(parsed.occurrences[usize {}].argument.is_Warning());
    EXPECT_TRUE(parsed.occurrences[usize(1)].argument.is_Warning());
}

TEST(Cpp, MaterializesTypedDefaultPositionIndependentCode) {
    auto defaults = cpp_options("c++20"_str, CppOptimization::None, CppDebugInfo::None);
    EXPECT_TRUE(defaults.codegen.position_independent_code);

    auto parsed = argument_layer(strings("-fno-PIC"_str));
    ASSERT_EQ(parsed.occurrences.len(), usize(1));
    EXPECT_TRUE(parsed.occurrences[usize {}].argument.is_PositionIndependentCode());

    auto disabled = cpp_options(
        "c++20"_str, CppOptimization::None, CppDebugInfo::None, strings("-fno-PIC"_str));
    EXPECT_FALSE(disabled.codegen.position_independent_code);
}

TEST(Cpp, MaterializesTypedSizedDeallocationPolicy) {
    auto automatic = cpp_options("c++20"_str, CppOptimization::None, CppDebugInfo::None);
    EXPECT_EQ(automatic.language.sized_deallocation, CppSizedDeallocation::Auto);

    auto enabled  = cpp_options("c++20"_str,
                                CppOptimization::None,
                                CppDebugInfo::None,
                                strings("-fsized-deallocation"_str));
    auto disabled = cpp_options("c++20"_str,
                                CppOptimization::None,
                                CppDebugInfo::None,
                                strings("-fno-sized-deallocation"_str));
    EXPECT_EQ(enabled.language.sized_deallocation, CppSizedDeallocation::Enabled);
    EXPECT_EQ(disabled.language.sized_deallocation, CppSizedDeallocation::Disabled);
    EXPECT_NE(cpp_compile_identity(automatic).as_str(), cpp_compile_identity(enabled).as_str());
    EXPECT_NE(cpp_scan_identity(automatic).as_str(), cpp_scan_identity(enabled).as_str());
    EXPECT_NE(cpp_bmi_compatibility_identity(automatic).as_str(),
              cpp_bmi_compatibility_identity(enabled).as_str());

    auto parsed =
        argument_layer(strings("-fsized-deallocation"_str, "-fno-sized-deallocation"_str));
    ASSERT_EQ(parsed.occurrences.len(), usize(2));
    EXPECT_TRUE(parsed.occurrences[usize {}].argument.is_SizedDeallocation());
    EXPECT_TRUE(parsed.occurrences[usize(1)].argument.is_SizedDeallocation());
}

TEST(Cpp, NormalizesCommonOptionsBeforeToolchainMapping) {
    auto first  = cpp_options("c++20"_str,
                              CppOptimization::None,
                              CppDebugInfo::Full,
                              strings("-Wall"_str,
                                      "-fPIC"_str,
                                      "--target=x86_64-unknown-linux-gnu"_str,
                                      "-msse2"_str,
                                      "-mno-sse2"_str));
    auto second = cpp_options("c++20"_str,
                              CppOptimization::None,
                              CppDebugInfo::Full,
                              strings("-Wall"_str,
                                      "--target"_str,
                                      "x86_64-unknown-linux-gnu"_str,
                                      "-mno-sse2"_str,
                                      "-fPIC"_str));
    EXPECT_EQ(cpp_compile_identity(first).as_str(), cpp_compile_identity(second).as_str());

    auto ordered  = make_cpp_options("c++20"_str,
                                     StandardLibrary::Libcxx,
                                     false,
                                     false,
                                     CppOptimization::None,
                                     CppDebugInfo::None,
                                     CppOptionLayer {
                                         .include_directories = paths("first"_str, "second"_str),
                                     });
    auto reversed = make_cpp_options("c++20"_str,
                                     StandardLibrary::Libcxx,
                                     false,
                                     false,
                                     CppOptimization::None,
                                     CppDebugInfo::None,
                                     CppOptionLayer {
                                         .include_directories = paths("second"_str, "first"_str),
                                     });
    ASSERT_TRUE(ordered.is_ok());
    ASSERT_TRUE(reversed.is_ok());
    EXPECT_NE(cpp_compile_identity(*ordered).as_str(), cpp_compile_identity(*reversed).as_str());

    auto draft_standard     = cpp_options("c++2b"_str, CppOptimization::None, CppDebugInfo::None);
    auto published_standard = cpp_options("c++23"_str, CppOptimization::None, CppDebugInfo::None);
    EXPECT_EQ(cpp_compile_identity(draft_standard).as_str(),
              cpp_compile_identity(published_standard).as_str());
}

TEST(Cpp, RejectsRawOverridesOfTypedSettings) {
    auto standard     = make_cpp_options("c++20"_str,
                                         StandardLibrary::Libcxx,
                                         false,
                                         false,
                                         CppOptimization::None,
                                         CppDebugInfo::Full,
                                         CppOptionLayer {
                                             .arguments = argument_layer(strings("-std=c++23"_str)),
                                         });
    auto optimization = make_cpp_options("c++20"_str,
                                         StandardLibrary::Libcxx,
                                         false,
                                         false,
                                         CppOptimization::None,
                                         CppDebugInfo::Full,
                                         CppOptionLayer {
                                             .arguments = argument_layer(strings("-O3"_str)),
                                         });
    auto rtti         = make_cpp_options("c++20"_str,
                                         StandardLibrary::Libcxx,
                                         false,
                                         false,
                                         CppOptimization::None,
                                         CppDebugInfo::Full,
                                         CppOptionLayer {
                                             .arguments = argument_layer(strings("-frtti"_str)),
                                         });
    EXPECT_TRUE(standard.is_err());
    EXPECT_TRUE(optimization.is_err());
    EXPECT_TRUE(rtti.is_err());
    auto optimization_error = rstd::move(optimization).unwrap_err();
    ASSERT_TRUE(optimization_error.is_Message());
    EXPECT_TRUE(optimization_error.as_Message().message.as_str().contains("optimization"_str));
}
