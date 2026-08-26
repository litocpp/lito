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

TEST(ToolchainStandardLibrary, ResolvesAutomaticSelectionFromEffectiveTarget) {
    const auto resolve = [](ref<str> triple) {
        auto target = lito::system::parse_target_info(triple).unwrap();
        return resolve_standard_library_selection(lito::config::StandardLibrarySelection::Auto,
                                                  target);
    };
    auto linux = resolve("x86_64-pc-linux-gnu"_str);
    ASSERT_TRUE(linux.is_ok());
    EXPECT_EQ(*linux, lito::config::StandardLibrary::Libstdcxx);
    auto android = resolve("aarch64-linux-android"_str);
    ASSERT_TRUE(android.is_ok());
    EXPECT_EQ(*android, lito::config::StandardLibrary::Libcxx);
    auto macos = resolve("aarch64-apple-darwin"_str);
    ASSERT_TRUE(macos.is_ok());
    EXPECT_EQ(*macos, lito::config::StandardLibrary::Libcxx);
    auto windows = resolve("x86_64-pc-windows-msvc"_str);
    ASSERT_TRUE(windows.is_ok());
    EXPECT_EQ(*windows, lito::config::StandardLibrary::Msvc);
    EXPECT_TRUE(resolve("x86_64-w64-mingw32"_str).is_err());

    auto target = lito::system::parse_target_info("x86_64-pc-linux-gnu"_str).unwrap();
    auto explicit_selection =
        resolve_standard_library_selection(lito::config::StandardLibrarySelection::Libcxx, target);
    ASSERT_TRUE(explicit_selection.is_ok());
    EXPECT_EQ(*explicit_selection, lito::config::StandardLibrary::Libcxx);
}

TEST(ClangToolchain, ProjectsLanguageSpecificScanFacts) {
    auto facts = frontend::FrontendResult {};
    facts.header_inputs.push(PathBuf::from("/tmp/c-header.h"_str));
    auto c_scan = cpp::project_frontend_analysis(
        frontend::FrontendAnalysis { .result = as<Clone>(facts).clone() },
        lito::manifest::PackageLanguage::C);
    ASSERT_TRUE(c_scan.is_ok());
    ASSERT_TRUE(c_scan->language.is_C());
    EXPECT_EQ(c_scan->language.as_C().facts.common.header_inputs.len(), usize(1));

    facts.provided = Some(frontend::ProvidedModule {
        .logical_name = String::make("invalid.c.module"_str),
        .is_interface = true,
    });
    auto invalid =
        cpp::project_frontend_analysis(frontend::FrontendAnalysis { .result = rstd::move(facts) },
                                       lito::manifest::PackageLanguage::C);
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

TEST(ClangToolchain, ParsesStandardLibraryModuleManifest) {
    auto directory = rstd::env::temp_dir().join(
        PathBuf::from(rstd::format("lito-stdlib-manifest-{}", rstd::process::id()).as_str())
            .as_path());
    auto exists = rstd::fs::exists(directory.as_path());
    ASSERT_TRUE(exists.is_ok());
    if (*exists) {
        ASSERT_TRUE(rstd::fs::remove_dir_all(directory.as_path()).is_ok());
    }
    auto include = directory.join(PathBuf::from("include"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(include.as_path()).is_ok());
    auto source = directory.join(PathBuf::from("std.cppm"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(source.as_path(), "export module std;\n"_str.as_bytes()).is_ok());
    auto manifest = directory.join(PathBuf::from("libc++.modules.json"_str).as_path());
    ASSERT_TRUE(
        rstd::fs::write(manifest.as_path(),
                        "{\n"
                        "  \"version\": 1,\n"
                        "  \"revision\": 1,\n"
                        "  \"modules\": [\n"
                        "    {\"logical-name\": \"ignored\", \"source-path\": \"std.cppm\", "
                        "\"is-std-library\": false},\n"
                        "    {\"logical-name\": \"std\", \"source-path\": \"std.cppm\", "
                        "\"is-std-library\": true, \"local-arguments\": "
                        "{\"system-include-directories\": [\"include\"]}}\n"
                        "  ]\n"
                        "}\n"_str.as_bytes())
            .is_ok());
    auto candidates = Vec<PathBuf>::make();
    candidates.push(manifest.clone());
    auto catalog = read_standard_library_module_catalog(cpp::ResolvedStandardLibrary {
        .family   = lito::config::StandardLibrary::Libcxx,
        .target   = String::make("x86_64-unknown-linux-gnu"_str),
        .artifact = directory.join(PathBuf::from("libc++.so"_str).as_path()),
        .module_manifest =
            cpp::StandardLibraryModuleManifestCandidate {
                .paths = rstd::move(candidates),
            },
    });
    ASSERT_TRUE(catalog.is_ok());
    EXPECT_EQ(catalog->modules.len(), usize(1));
    auto entry = catalog->get("std"_str);
    ASSERT_TRUE(entry.is_some());
    EXPECT_EQ((*entry)->source.as_path(),
              rstd::fs::canonicalize(source.as_path()).unwrap().as_path());
    ASSERT_EQ((*entry)->system_include_directories.len(), usize(1));
    EXPECT_EQ((*entry)->system_include_directories[usize {}].as_path(),
              rstd::fs::canonicalize(include.as_path()).unwrap().as_path());
    EXPECT_FALSE((*entry)->source_identity.is_empty());
    EXPECT_TRUE(rstd::fs::remove_dir_all(directory.as_path()).is_ok());
}

TEST(ClangToolchain, ReportsTypedMissingStandardLibraryModuleManifest) {
    auto candidates = Vec<PathBuf>::make();
    candidates.push(PathBuf::from("/tmp/lito-missing-stdlib.modules.json"_str));
    auto catalog = read_standard_library_module_catalog(cpp::ResolvedStandardLibrary {
        .family   = lito::config::StandardLibrary::Libstdcxx,
        .target   = String::make("x86_64-unknown-linux-gnu"_str),
        .artifact = PathBuf::from("/tmp/libstdc++.so"_str),
        .module_manifest =
            cpp::StandardLibraryModuleManifestCandidate {
                .paths = rstd::move(candidates),
            },
    });
    ASSERT_TRUE(catalog.is_err());
    auto error = rstd::move(catalog).unwrap_err();
    ASSERT_TRUE(error.is_StandardLibraryModule());
    EXPECT_TRUE(error.as_StandardLibraryModule().source.is_Missing());
}

TEST(ClangToolchain, ReportsTypedAmbiguousStandardLibraryModuleManifest) {
    auto directory = rstd::env::temp_dir().join(
        PathBuf::from(rstd::format("lito-stdlib-ambiguous-{}", rstd::process::id()).as_str())
            .as_path());
    auto exists = rstd::fs::exists(directory.as_path());
    ASSERT_TRUE(exists.is_ok());
    if (*exists) {
        ASSERT_TRUE(rstd::fs::remove_dir_all(directory.as_path()).is_ok());
    }
    auto first_directory  = directory.join(PathBuf::from("first"_str).as_path());
    auto second_directory = directory.join(PathBuf::from("second"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(first_directory.as_path()).is_ok());
    ASSERT_TRUE(rstd::fs::create_dir_all(second_directory.as_path()).is_ok());
    auto first  = first_directory.join(PathBuf::from("libc++.modules.json"_str).as_path());
    auto second = second_directory.join(PathBuf::from("libc++.modules.json"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(first.as_path(), "{}"_str.as_bytes()).is_ok());
    ASSERT_TRUE(rstd::fs::write(second.as_path(), "{}"_str.as_bytes()).is_ok());
    auto candidates = Vec<PathBuf>::make();
    candidates.push(rstd::move(first));
    candidates.push(rstd::move(second));
    auto catalog = read_standard_library_module_catalog(cpp::ResolvedStandardLibrary {
        .family   = lito::config::StandardLibrary::Libcxx,
        .target   = String::make("x86_64-unknown-linux-gnu"_str),
        .artifact = directory.join(PathBuf::from("libc++.so"_str).as_path()),
        .module_manifest =
            cpp::StandardLibraryModuleManifestCandidate {
                .paths = rstd::move(candidates),
            },
    });
    ASSERT_TRUE(catalog.is_err());
    auto error = rstd::move(catalog).unwrap_err();
    ASSERT_TRUE(error.is_StandardLibraryModule());
    EXPECT_TRUE(error.as_StandardLibraryModule().source.is_Ambiguous());
    EXPECT_TRUE(rstd::fs::remove_dir_all(directory.as_path()).is_ok());
}

TEST(ClangToolchain, RejectsUnsupportedStandardLibraryModuleManifestVersion) {
    auto directory = rstd::env::temp_dir().join(
        PathBuf::from(rstd::format("lito-stdlib-version-{}", rstd::process::id()).as_str())
            .as_path());
    auto exists = rstd::fs::exists(directory.as_path());
    ASSERT_TRUE(exists.is_ok());
    if (*exists) {
        ASSERT_TRUE(rstd::fs::remove_dir_all(directory.as_path()).is_ok());
    }
    ASSERT_TRUE(rstd::fs::create_dir_all(directory.as_path()).is_ok());
    auto manifest = directory.join(PathBuf::from("libstdc++.modules.json"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(manifest.as_path(),
                                "{\"version\":2,\"revision\":1,\"modules\":[]}"_str.as_bytes())
                    .is_ok());
    auto candidates = Vec<PathBuf>::make();
    candidates.push(rstd::move(manifest));
    auto catalog = read_standard_library_module_catalog(cpp::ResolvedStandardLibrary {
        .family   = lito::config::StandardLibrary::Libstdcxx,
        .target   = String::make("x86_64-unknown-linux-gnu"_str),
        .artifact = directory.join(PathBuf::from("libstdc++.so"_str).as_path()),
        .module_manifest =
            cpp::StandardLibraryModuleManifestCandidate {
                .paths = rstd::move(candidates),
            },
    });
    ASSERT_TRUE(catalog.is_err());
    auto error = rstd::move(catalog).unwrap_err();
    ASSERT_TRUE(error.is_StandardLibraryModule());
    const auto& source = error.as_StandardLibraryModule().source;
    ASSERT_TRUE(source.is_Manifest());
    EXPECT_TRUE(source.as_Manifest().message.as_str().contains("unsupported version/revision"_str));
    EXPECT_TRUE(rstd::fs::remove_dir_all(directory.as_path()).is_ok());
}

TEST(ClangToolchain, ReportsManifestEntryForMissingStandardLibraryModuleSource) {
    auto directory = rstd::env::temp_dir().join(
        PathBuf::from(rstd::format("lito-stdlib-source-{}", rstd::process::id()).as_str())
            .as_path());
    auto exists = rstd::fs::exists(directory.as_path());
    ASSERT_TRUE(exists.is_ok());
    if (*exists) {
        ASSERT_TRUE(rstd::fs::remove_dir_all(directory.as_path()).is_ok());
    }
    ASSERT_TRUE(rstd::fs::create_dir_all(directory.as_path()).is_ok());
    auto manifest = directory.join(PathBuf::from("libstdc++.modules.json"_str).as_path());
    ASSERT_TRUE(
        rstd::fs::write(
            manifest.as_path(),
            R"json({"version":1,"revision":1,"modules":[{"logical-name":"std","source-path":"missing.cc","is-std-library":true}]})json"_str
                .as_bytes())
            .is_ok());
    auto candidates = Vec<PathBuf>::make();
    candidates.push(manifest.clone());
    auto catalog = read_standard_library_module_catalog(cpp::ResolvedStandardLibrary {
        .family   = lito::config::StandardLibrary::Libstdcxx,
        .target   = String::make("x86_64-unknown-linux-gnu"_str),
        .artifact = directory.join(PathBuf::from("libstdc++.so"_str).as_path()),
        .module_manifest =
            cpp::StandardLibraryModuleManifestCandidate {
                .paths = rstd::move(candidates),
            },
    });
    ASSERT_TRUE(catalog.is_err());
    auto error = rstd::move(catalog).unwrap_err();
    ASSERT_TRUE(error.is_StandardLibraryModule());
    const auto& source = error.as_StandardLibraryModule().source;
    ASSERT_TRUE(source.is_Io());
    EXPECT_TRUE(source.as_Io().manifest.is_some());
    EXPECT_EQ(source.as_Io().manifest->as_path(), manifest.as_path());
    EXPECT_TRUE(source.as_Io().entry.is_some());
    EXPECT_EQ(source.as_Io().entry->as_str(), "manifest.modules[0]"_str);
    EXPECT_TRUE(rstd::fs::remove_dir_all(directory.as_path()).is_ok());
}

TEST(ClangToolchain, MaterializesStandardLibraryModuleAsBmiOnly) {
    auto created = ClangToolchain::create(lito::config::ToolchainSpec {
        .cxx = PathBuf::from("clang++"_str),
        .ar  = PathBuf::from("llvm-ar"_str),
    });
    ASSERT_TRUE(created.is_ok());
    auto toolchain = rstd::move(created).unwrap();
    auto options   = cpp_options(
        "c++23"_str, lito::manifest::Optimization::None, lito::manifest::DebugInfo::None);
    auto context = cpp::CompileContext {
        .id       = String::make("standard-context"_str),
        .language = cpp::LanguageCompileContext::Cpp(
            cpp::BmiRequest {}, rstd::move(options), cpp::CppPublicRequirements {}),
    };
    auto artifact = cpp::BmiArtifact {
        .logical_name      = String::make("std"_str),
        .provider_identity = String::make("standard-library:std"_str),
        .key    = cpp::BmiArtifactKey { .value = String::make("standard-library-key"_str) },
        .format = format(),
        .path   = PathBuf::from("/tmp/lito-std.pcm"_str),
    };
    auto prepared = cpp::PreparedUnit {
        .unit =
            cpp::UnitSpec {
                .owner    = cpp::CompileUnitOwner::StandardLibrary(cpp::StandardLibraryModuleUnit {
                    .logical_name = String::make("std"_str),
                }),
                .source   = PathBuf::from("/tmp/std.cc"_str),
                .object   = PathBuf::from("/tmp/std.o"_str),
                .language = cpp::LanguageSourceUnit::Cpp(Some(rstd::move(artifact))),
                .context  = rstd::addressof(context),
            },
        .working_directory = PathBuf::from("/tmp"_str),
    };
    auto scan = cpp::ScanResult {
        .language = cpp::LanguageScanResult::Cpp(cpp::CppScanResult {
            .provided = Some(frontend::ProvidedModule {
                .logical_name = String::make("std"_str),
                .is_interface = true,
            }),
        }),
    };
    auto invocation = toolchain.prepare_compile(prepared,
                                                scan,
                                                Vec<cpp::ModuleArtifactDependency>::make(),
                                                cpp::CppCompileDisposition::BmiOnly);
    ASSERT_TRUE(invocation.is_ok());
    EXPECT_TRUE(invocation->final_object.is_none());
    EXPECT_TRUE(invocation->final_bmi.is_some());
    EXPECT_TRUE(has_argument(invocation->arguments, "-x"_str));
    EXPECT_TRUE(has_argument(invocation->arguments, "c++-module"_str));
    EXPECT_TRUE(has_argument(invocation->arguments, "-Wno-reserved-module-identifier"_str));
}

TEST(ClangToolchain, RemovesTransientBmiOnlyObject) {
    auto created = ClangToolchain::create(lito::config::ToolchainSpec {
        .cxx = PathBuf::from("clang++"_str),
        .ar  = PathBuf::from("llvm-ar"_str),
    });
    ASSERT_TRUE(created.is_ok());
    auto toolchain = rstd::move(created).unwrap();
    auto directory = rstd::env::temp_dir().join(
        PathBuf::from(rstd::format("lito-bmi-only-toolchain-{}", rstd::process::id()).as_str())
            .as_path());
    auto exists = rstd::fs::exists(directory.as_path());
    ASSERT_TRUE(exists.is_ok());
    if (*exists) {
        ASSERT_TRUE(rstd::fs::remove_dir_all(directory.as_path()).is_ok());
    }
    ASSERT_TRUE(rstd::fs::create_dir_all(directory.as_path()).is_ok());
    auto staged_object = directory.join(PathBuf::from("std.o.building"_str).as_path());
    auto staged_bmi    = directory.join(PathBuf::from("std.pcm.building"_str).as_path());
    auto final_bmi     = directory.join(PathBuf::from("std.pcm"_str).as_path());
    auto script        = rstd::format(
        "printf object > '{}'; printf bmi > '{}'", staged_object.as_path(), staged_bmi.as_path());
    auto result = toolchain.execute_compile_capture(CompileInvocation {
        .arguments                  = strings("/bin/sh"_str, "-c"_str, script.as_str()),
        .working_directory          = directory.clone(),
        .identity_working_directory = String::make("bmi-only-output"_str),
        .staged_object              = staged_object.clone(),
        .staged_bmi                 = Some(staged_bmi.clone()),
        .final_bmi                  = Some(final_bmi.clone()),
    });
    ASSERT_TRUE(result.is_ok());
    auto object_exists = rstd::fs::exists(staged_object.as_path());
    auto bmi_exists    = rstd::fs::exists(final_bmi.as_path());
    ASSERT_TRUE(object_exists.is_ok());
    ASSERT_TRUE(bmi_exists.is_ok());
    EXPECT_FALSE(*object_exists);
    EXPECT_TRUE(*bmi_exists);
    EXPECT_TRUE(rstd::fs::remove_dir_all(directory.as_path()).is_ok());
}

TEST(ClangToolchain, MapsStandardLibraryLinkPolicy) {
    auto linux = lito::system::parse_target_info("x86_64-unknown-linux-gnu"_str);
    auto msvc  = lito::system::parse_target_info("x86_64-pc-windows-msvc"_str);
    auto mingw = lito::system::parse_target_info("x86_64-w64-windows-gnu"_str);
    ASSERT_TRUE(linux.is_ok());
    ASSERT_TRUE(msvc.is_ok());
    ASSERT_TRUE(mingw.is_ok());
    EXPECT_EQ(toolchain::clang_options::standard_library_linker_option(
                  lito::config::StandardLibrary::Libcxx, *linux, false),
              "-nostdlib++"_str);
    EXPECT_EQ(toolchain::clang_options::standard_library_linker_option(
                  lito::config::StandardLibrary::Libstdcxx, *linux, true),
              "-stdlib=libstdc++"_str);
    EXPECT_TRUE(toolchain::clang_options::standard_library_linker_option(
                    lito::config::StandardLibrary::Msvc, *msvc, true)
                    .is_empty());
    EXPECT_EQ(toolchain::clang_options::standard_library_linker_option(
                  lito::config::StandardLibrary::Libcxx, *msvc, true),
              "-lc++"_str);
    EXPECT_EQ(toolchain::clang_options::standard_library_linker_option(
                  lito::config::StandardLibrary::Libcxx, *mingw, true),
              "-stdlib=libc++"_str);
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
    EXPECT_TRUE(error.as_Message().message.as_str().contains("unsupported"_str));
}

TEST(LinkerIdentity, StrictlyClassifiesLldAndGnuLdVersionSignatures) {
#if RSTD_OS_UNIX
    struct SignatureCase {
        ref<str>                   name;
        ref<str>                   version;
        Option<lito::LinkerFamily> family;
    };
    constexpr SignatureCase cases[] = {
        { "lld"_str, "LLD 22.1.8"_str, Some(lito::LinkerFamily::Lld) },
        { "gnu-ld"_str, "GNU ld (GNU Binutils) 2.47"_str, Some(lito::LinkerFamily::GnuLd) },
        { "gold"_str, "GNU gold (GNU Binutils 2.47) 1.16"_str, None() },
        { "mold"_str, "mold 2.40.0 (compatible with GNU ld)"_str, None() },
        { "clang"_str, "clang version 22.1.8"_str, None() },
    };
    auto directory = rstd::fs::TempDir::make("lito-linker-probe-test"_str);
    ASSERT_TRUE(directory.is_ok());
    auto environment =
        lito::system::ResolvedProcessEnvironment::resolve(lito::system::ProcessEnvironmentSpec {});
    ASSERT_TRUE(environment.is_ok());
    for (const auto& item : cases) {
        auto executable = PathBuf::from(directory->path()).join(PathBuf::from(item.name).as_path());
        auto script     = rstd::format("#!/bin/sh\nprintf '%s\\n' '{}'\n", item.version);
        ASSERT_TRUE(rstd::fs::write(executable.as_path(), script.as_str().as_bytes()).is_ok());
        ASSERT_TRUE(rstd::fs::set_permissions(executable.as_path(),
                                              rstd::fs::Permissions::from_mode(u32(0755)))
                        .is_ok());
        auto identity = lito::probe_linker(executable.as_path(), *environment);
        if (item.family.is_some()) {
            ASSERT_TRUE(identity.is_ok());
            EXPECT_EQ(identity->family, *item.family);
        } else {
            EXPECT_TRUE(identity.is_err());
        }
    }
#endif
}

TEST(ClangToolchain, LinksTypedElfSharedLibraryWithGnuLd) {
#if RSTD_OS_UNIX
    if (! rstd::fs::exists(PathBuf::from("/usr/bin/ld"_str).as_path()).unwrap()) return;
    auto build_configuration         = configuration();
    build_configuration.toolchain.ld = PathBuf::from("/usr/bin/ld"_str);
    auto created                     = ClangToolchain::create(build_configuration.toolchain);
    ASSERT_TRUE(created.is_ok());
    EXPECT_EQ(created->linker_identity().family, lito::LinkerFamily::GnuLd);
    auto directory = rstd::fs::TempDir::make("lito-elf-shared-link-test"_str);
    ASSERT_TRUE(directory.is_ok());
    auto archive =
        PathBuf::from(directory->path()).join(PathBuf::from("libfixture.a"_str).as_path());
    auto archive_invocation =
        created->prepare_archive(archive.as_path(), Vec<PathBuf>::make(), directory->path());
    ASSERT_TRUE(archive_invocation.is_ok());
    auto archived = created->execute_archive(*archive_invocation);
    ASSERT_TRUE(archived.is_ok());
    auto version_script =
        PathBuf::from(directory->path()).join(PathBuf::from("fixture.map"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(version_script.as_path(), "LITO_1 { global: *; };\n"_str.as_bytes())
                    .is_ok());
    auto output =
        PathBuf::from(directory->path()).join(PathBuf::from("libfixture.so.1"_str).as_path());
    auto linked = created->link_elf_shared_library(lito::ElfSharedLibraryLinkRequest {
        .output = output.clone(),
        .archive =
            lito::LinkArchive {
                .path = archive.clone(),
                .mode = lito::LinkArchiveMode::Whole,
            },
        .soname            = String::make("libfixture.so.1"_str),
        .version_script    = version_script.clone(),
        .working_directory = PathBuf::from(directory->path()),
    });
    ASSERT_TRUE(linked.is_ok());
    EXPECT_EQ(linked->file.as_path(), output.as_path());
    EXPECT_EQ(linked->soname.as_str(), "libfixture.so.1"_str);
    EXPECT_TRUE(linked->link_identity.as_str().contains("GNU ld"_str));
    EXPECT_TRUE(rstd::fs::exists(output.as_path()).unwrap());
#endif
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
    if (*removed) {
        ASSERT_TRUE(rstd::fs::remove_dir_all(directory.as_path()).is_ok());
    }
    ASSERT_TRUE(rstd::fs::create_dir_all(directory.as_path()).is_ok());

    auto staged_object = directory.join(PathBuf::from("object.building"_str).as_path());
    auto final_object  = directory.join(PathBuf::from("object.o"_str).as_path());
    auto staged_bmi    = directory.join(PathBuf::from("module.building"_str).as_path());
    auto final_bmi     = directory.join(PathBuf::from("module.pcm"_str).as_path());
    auto script        = rstd::format("printf bmi > '{}'", staged_bmi.as_path());
    auto invocation    = CompileInvocation {
        .arguments                  = strings("/bin/sh"_str, "-c"_str, script.as_str()),
        .working_directory          = directory.clone(),
        .identity_working_directory = String::make("partial-output"_str),
        .staged_object              = staged_object.clone(),
        .final_object               = Some(final_object.clone()),
        .staged_bmi                 = Some(staged_bmi.clone()),
        .final_bmi                  = Some(final_bmi.clone()),
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
