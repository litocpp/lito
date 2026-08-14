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

TEST(ClangToolchain, EmitsExactResolvedModuleMapping) {
    auto created = ClangToolchain::create(ToolchainSpec {
        .cxx = PathBuf::from("clang++"_str),
        .ar  = PathBuf::from("llvm-ar"_str),
    });
    ASSERT_TRUE(created.is_ok());
    auto toolchain = rstd::move(created).unwrap();
    EXPECT_TRUE(toolchain.capabilities().reduced_bmi);
    EXPECT_TRUE(toolchain.capabilities().one_phase_bmi);
    EXPECT_TRUE(toolchain.capabilities().exact_module_mapping);
    auto cpp     = cpp_options("c++20"_str, CppOptimization::None, CppDebugInfo::None);
    auto context = CompileContext {
        .id  = String::make("context"_str),
        .cpp = rstd::move(cpp),
    };
    EXPECT_TRUE(toolchain.validate(context.cpp, context.bmi).is_ok());
    auto prepared = PreparedUnit {
        .unit =
            UnitSpec {
                .source  = PathBuf::from("/tmp/lito-bmi-consumer.cpp"_str),
                .object  = PathBuf::from("/tmp/lito-bmi-consumer.o"_str),
                .context = rstd::addressof(context),
            },
        .working_directory = PathBuf::from("/tmp"_str),
    };
    auto dependencies = lito::Vec<ModuleArtifactDependency>::make();
    dependencies.push(ModuleArtifactDependency {
        .logical_name = String::make("sample.module"_str),
        .artifact_key = BmiArtifactKey { .value = String::make("artifact-key"_str) },
        .path         = PathBuf::from("/tmp/sample.module.pcm"_str),
    });
    auto invocation = toolchain.prepare_compile(prepared, ScanResult {}, dependencies);
    ASSERT_TRUE(invocation.is_ok());
    EXPECT_TRUE(has_argument(invocation->arguments, "-fPIC"_str));
    EXPECT_FALSE(has_argument(invocation->arguments, "-fsized-deallocation"_str));
    EXPECT_FALSE(has_argument(invocation->arguments, "-fno-sized-deallocation"_str));
    EXPECT_TRUE(has_argument(invocation->arguments, "-Wall"_str));
    EXPECT_TRUE(has_argument(invocation->arguments, "-Wpedantic"_str));
    EXPECT_TRUE(has_argument(invocation->arguments, "-Wno-gnu-statement-expression"_str));
    EXPECT_TRUE(has_argument(invocation->arguments, "-Wno-deprecated-declarations"_str));
    EXPECT_TRUE(has_argument(invocation->arguments, "-Wunknown-attributes"_str));
    EXPECT_TRUE(has_argument(invocation->arguments,
                             "-fmodule-file=sample.module=/tmp/sample.module.pcm"_str));
    EXPECT_FALSE(has_prefix(invocation->arguments, "-fprebuilt-module-path="_str));

    context.cpp.language.sized_deallocation = CppSizedDeallocation::Disabled;
    auto disabled_invocation = toolchain.prepare_compile(prepared, ScanResult {}, dependencies);
    ASSERT_TRUE(disabled_invocation.is_ok());
    EXPECT_TRUE(has_argument(disabled_invocation->arguments, "-fno-sized-deallocation"_str));
}

TEST(ClangToolchain, MapsStandardLibraryLinkPolicy) {
    EXPECT_EQ(
        toolchain::clang_options::standard_library_linker_option(StandardLibrary::Libcxx, false),
        "-nostdlib++"_str);
    EXPECT_EQ(
        toolchain::clang_options::standard_library_linker_option(StandardLibrary::Libstdcxx, true),
        "-stdlib=libstdc++"_str);
}

TEST(ClangToolchain, RejectsNonLldLinkers) {
    auto created = ClangToolchain::create(ToolchainSpec {
        .cxx = PathBuf::from("clang++"_str),
        .ld  = PathBuf::from("ld"_str),
        .ar  = PathBuf::from("llvm-ar"_str),
    });
    ASSERT_TRUE(created.is_err());
    auto error = rstd::move(created).unwrap_err();
    ASSERT_TRUE(error.is_Message());
    EXPECT_TRUE(error.as_Message().message.as_str().contains("not LLD"_str));
}

TEST(ClangToolchain, DoesNotPublishOneOutputWhenAnotherIsMissing) {
    auto created = ClangToolchain::create(ToolchainSpec {
        .cxx = PathBuf::from("clang++"_str),
        .ar  = PathBuf::from("llvm-ar"_str),
    });
    ASSERT_TRUE(created.is_ok());
    auto toolchain = rstd::move(created).unwrap();
    auto directory = rstd::env::temp_dir().join(
        PathBuf::from(rstd::format("lito-bmi-toolchain-{}", rstd::process::id()).as_str())
            .as_path());
    auto removed = rstd::fs::exists(directory.as_path());
    ASSERT_TRUE(removed.is_ok());
    if (*removed) ASSERT_TRUE(rstd::fs::remove_dir_all(directory.as_path()).is_ok());
    ASSERT_TRUE(rstd::fs::create_dir_all(directory.as_path()).is_ok());

    auto staged_object = directory.join(PathBuf::from("object.building"_str).as_path());
    auto final_object  = directory.join(PathBuf::from("object.o"_str).as_path());
    auto staged_bmi    = directory.join(PathBuf::from("module.building"_str).as_path());
    auto final_bmi     = directory.join(PathBuf::from("module.pcm"_str).as_path());
    auto script        = rstd::format("printf bmi > '{}'", staged_bmi.as_path());
    auto invocation    = CompileInvocation {
        .arguments         = strings("/bin/sh"_str, "-c"_str, script.as_str()),
        .working_directory = directory.clone(),
        .identity          = String::make("partial-output"_str),
        .staged_object     = staged_object.clone(),
        .final_object      = final_object.clone(),
        .staged_bmi        = Some(staged_bmi.clone()),
        .final_bmi         = Some(final_bmi.clone()),
    };
    auto result = toolchain.execute_compile_capture(invocation);
    EXPECT_TRUE(result.is_err());
    auto object_exists = rstd::fs::exists(final_object.as_path());
    auto bmi_exists    = rstd::fs::exists(final_bmi.as_path());
    ASSERT_TRUE(object_exists.is_ok());
    ASSERT_TRUE(bmi_exists.is_ok());
    EXPECT_FALSE(*object_exists);
    EXPECT_FALSE(*bmi_exists);
    EXPECT_TRUE(rstd::fs::remove_dir_all(directory.as_path()).is_ok());
}
