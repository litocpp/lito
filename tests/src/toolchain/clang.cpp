#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.test.cpp;
import lito.core;
import lito.cpp;
import lito.frontend.result;
import lito.toolchain;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito;
using namespace lito_test;

TEST(ClangToolchain, ProjectsLanguageSpecificScanFacts) {
    auto facts = frontend::FrontendResult {};
    facts.header_inputs.push(PathBuf::from("/tmp/c-header.h"_str));
    auto c_scan = cpp::scan_from_frontend(facts, usize(7), lito::manifest::PackageLanguage::C);
    ASSERT_TRUE(c_scan.is_ok());
    ASSERT_TRUE(c_scan->language.is_C());
    EXPECT_EQ(c_scan->language.as_C().facts.common.header_inputs.len(), usize(1));

    facts.provided = Some(frontend::ProvidedModule {
        .logical_name = String::make("invalid.c.module"_str),
        .is_interface = true,
    });
    auto invalid   = cpp::scan_from_frontend(facts, usize(7), lito::manifest::PackageLanguage::C);
    ASSERT_TRUE(invalid.is_err());
    EXPECT_TRUE(invalid.unwrap_err().as_str().contains("C++ module facts"_str));
}

TEST(ClangToolchain, ProjectsTypedCCompileOptions) {
    auto created = ClangToolchain::create(lito::config::ToolchainSpec {
        .cxx = PathBuf::from("clang++"_str),
        .ar  = PathBuf::from("llvm-ar"_str),
    });
    ASSERT_TRUE(created.is_ok());
    auto toolchain = rstd::move(created).unwrap();

    auto       layer       = c::CArgumentLayer {};
    const auto push_common = [&](compiler::CommonCompilerArgument argument) {
        layer.occurrences.push(c::CCompilerArgumentOccurrence {
            .argument = c::CCompilerArgument::Common(rstd::move(argument)),
        });
    };
    push_common(compiler::CommonCompilerArgument::Threading(compiler::ThreadingModel::Posix));
    push_common(compiler::CommonCompilerArgument::PositionIndependentCode(false));
    push_common(compiler::CommonCompilerArgument::Warning(compiler::CompilerWarningOption {
        .warning = compiler::CompilerWarning::Pedantic,
        .enabled = false,
    }));
    layer.occurrences.push(c::CCompilerArgumentOccurrence {
        .argument = c::CCompilerArgument::Vendor(c::CVendorOption {
            .value               = String::make("-fno-builtin"_str),
            .raw_tokens          = strings("-fno-builtin"_str),
            .preserve_raw_tokens = true,
        }),
    });
    auto options = c::apply_c_option_layer(
        c::make_c_options(compiler::CommonCompileOptions {}, lito::manifest::CStandard::C23),
        rstd::move(layer));
    ASSERT_TRUE(options.is_ok());
    auto context = cpp::CompileContext {
        .id = String::make("c-context"_str),
        .language =
            cpp::LanguageCompileContext::C(rstd::move(options).unwrap(), c::CPublicRequirements {}),
    };
    auto prepared = cpp::PreparedUnit {
        .unit =
            cpp::UnitSpec {
                .source   = PathBuf::from("/tmp/lito-c-source.c"_str),
                .object   = PathBuf::from("/tmp/lito-c-source.o"_str),
                .language = cpp::LanguageSourceUnit::C(),
                .context  = rstd::addressof(context),
            },
        .working_directory = PathBuf::from("/tmp"_str),
    };
    auto invocation =
        toolchain.prepare_compile(prepared,
                                  cpp::ScanResult {
                                      .language = cpp::LanguageScanResult::C(cpp::CScanResult {}),
                                  },
                                  Vec<cpp::ModuleArtifactDependency>::make());
    ASSERT_TRUE(invocation.is_ok());
    EXPECT_TRUE(has_argument(invocation->arguments, "-std=c23"_str));
    EXPECT_TRUE(has_argument(invocation->arguments, "-pthread"_str));
#if defined(_WIN32)
    EXPECT_FALSE(has_argument(invocation->arguments, "-fno-PIC"_str));
#else
    EXPECT_TRUE(has_argument(invocation->arguments, "-fno-PIC"_str));
#endif
    EXPECT_TRUE(has_argument(invocation->arguments, "-fno-builtin"_str));
    EXPECT_TRUE(has_argument(invocation->arguments, "-Wall"_str));
    EXPECT_TRUE(has_argument(invocation->arguments, "-Wno-pedantic"_str));
    EXPECT_TRUE(has_argument(invocation->arguments, "-Wunknown-attributes"_str));
}

TEST(ClangToolchain, EmitsExactResolvedModuleMapping) {
    auto created = ClangToolchain::create(lito::config::ToolchainSpec {
        .cxx = PathBuf::from("clang++"_str),
        .ar  = PathBuf::from("llvm-ar"_str),
    });
    ASSERT_TRUE(created.is_ok());
    auto toolchain = rstd::move(created).unwrap();
    EXPECT_TRUE(toolchain.capabilities().reduced_bmi);
    EXPECT_TRUE(toolchain.capabilities().one_phase_bmi);
    EXPECT_TRUE(toolchain.capabilities().exact_module_mapping);
    auto cpp = cpp_options(
        "c++20"_str, lito::manifest::Optimization::None, lito::manifest::DebugInfo::None);
    auto context = cpp::CompileContext {
        .id       = String::make("context"_str),
        .language = cpp::LanguageCompileContext::Cpp(
            cpp::BmiRequest {}, rstd::move(cpp), cpp::CppPublicRequirements {}),
    };
    EXPECT_TRUE(toolchain.validate(context.language.as_Cpp().options, context.language.as_Cpp().bmi)
                    .is_ok());
    auto prepared = cpp::PreparedUnit {
        .unit =
            cpp::UnitSpec {
                .source  = PathBuf::from("/tmp/lito-bmi-consumer.cpp"_str),
                .object  = PathBuf::from("/tmp/lito-bmi-consumer.o"_str),
                .context = rstd::addressof(context),
            },
        .working_directory = PathBuf::from("/tmp"_str),
    };
    auto dependencies = Vec<cpp::ModuleArtifactDependency>::make();
    dependencies.push(cpp::ModuleArtifactDependency {
        .logical_name = String::make("sample.module"_str),
        .artifact_key = cpp::BmiArtifactKey { .value = String::make("artifact-key"_str) },
        .path         = PathBuf::from("/tmp/sample.module.pcm"_str),
    });
    auto invocation = toolchain.prepare_compile(prepared, cpp::ScanResult {}, dependencies);
    ASSERT_TRUE(invocation.is_ok());
#if defined(_WIN32)
    EXPECT_FALSE(has_argument(invocation->arguments, "-fPIC"_str));
#else
    EXPECT_TRUE(has_argument(invocation->arguments, "-fPIC"_str));
#endif
    EXPECT_TRUE(has_argument(invocation->arguments, "-fvisibility=hidden"_str));
    EXPECT_FALSE(has_prefix(invocation->arguments, "-ftype-visibility="_str));
    EXPECT_FALSE(has_argument(invocation->arguments, "-fvisibility-inlines-hidden"_str));
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

    context.language.as_Cpp().options.language.sized_deallocation =
        cpp::CppSizedDeallocation::Disabled;
    auto disabled_invocation =
        toolchain.prepare_compile(prepared, cpp::ScanResult {}, dependencies);
    ASSERT_TRUE(disabled_invocation.is_ok());
    EXPECT_TRUE(has_argument(disabled_invocation->arguments, "-fno-sized-deallocation"_str));

    context.language.as_Cpp().options.codegen.visibility.symbols =
        cpp::CppSymbolVisibility::Default;
    context.language.as_Cpp().options.codegen.visibility.types =
        Some(cpp::CppSymbolVisibility::Protected);
    context.language.as_Cpp().options.codegen.visibility.inlines_hidden = true;
    auto visibility_invocation =
        toolchain.prepare_compile(prepared, cpp::ScanResult {}, dependencies);
    ASSERT_TRUE(visibility_invocation.is_ok());
    EXPECT_TRUE(has_argument(visibility_invocation->arguments, "-fvisibility=default"_str));
    EXPECT_TRUE(has_argument(visibility_invocation->arguments, "-ftype-visibility=protected"_str));
    EXPECT_TRUE(has_argument(visibility_invocation->arguments, "-fvisibility-inlines-hidden"_str));

    context.language.as_Cpp().options.common.codegen.optimization = None();
    context.language.as_Cpp().options.common.codegen.debug_info   = None();
    context.language.as_Cpp().options.common.codegen.lto          = None();
    auto plain_invocation = toolchain.prepare_compile(prepared, cpp::ScanResult {}, dependencies);
    ASSERT_TRUE(plain_invocation.is_ok());
    EXPECT_FALSE(has_prefix(plain_invocation->arguments, "-O"_str));
    EXPECT_FALSE(has_prefix(plain_invocation->arguments, "-g"_str));
    EXPECT_FALSE(has_prefix(plain_invocation->arguments, "-flto"_str));
    EXPECT_FALSE(has_argument(plain_invocation->arguments, "-fno-lto"_str));

    context.language.as_Cpp().options.common.codegen.lto =
        Some<lito::manifest::Lto>(lito::manifest::Lto::Thin);
    auto lto_invocation = toolchain.prepare_compile(prepared, cpp::ScanResult {}, dependencies);
    ASSERT_TRUE(lto_invocation.is_ok());
    EXPECT_TRUE(has_argument(lto_invocation->arguments, "-flto=thin"_str));
    EXPECT_TRUE(has_argument(lto_invocation->arguments, "-fvisibility=default"_str));
}

TEST(ClangToolchain, MapsStandardLibraryLinkPolicy) {
    EXPECT_EQ(toolchain::clang_options::standard_library_linker_option(
                  lito::config::StandardLibrary::Libcxx, false),
              "-nostdlib++"_str);
    EXPECT_EQ(toolchain::clang_options::standard_library_linker_option(
                  lito::config::StandardLibrary::Libstdcxx, true),
              "-stdlib=libstdc++"_str);
}

TEST(ClangToolchain, RejectsNonLldLinkers) {
    auto created = ClangToolchain::create(lito::config::ToolchainSpec {
        .cxx = PathBuf::from("clang++"_str),
        .ld  = PathBuf::from("clang++"_str),
        .ar  = PathBuf::from("llvm-ar"_str),
    });
    ASSERT_TRUE(created.is_err());
    auto error = rstd::move(created).unwrap_err();
    ASSERT_TRUE(error.is_Message());
    EXPECT_TRUE(error.as_Message().message.as_str().contains("not LLD"_str));
}

TEST(ClangToolchain, DoesNotPublishOneOutputWhenAnotherIsMissing) {
    auto created = ClangToolchain::create(lito::config::ToolchainSpec {
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
