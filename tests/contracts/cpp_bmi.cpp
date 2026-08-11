#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito;

namespace
{

template<typename... Values>
auto strings(Values... values) -> lito::Vec<String> {
    auto result = lito::Vec<String>::with_capacity(usize(sizeof...(Values)));
    (result.push(String::make(values)), ...);
    return result;
}

template<typename... Values>
auto paths(Values... values) -> lito::Vec<PathBuf> {
    auto result = lito::Vec<PathBuf>::with_capacity(usize(sizeof...(Values)));
    (result.push(PathBuf::from(values)), ...);
    return result;
}

auto argument_layer(lito::Vec<String> options) -> CppArgumentLayer {
    auto parser = make_clang_cpp_argument_parser();
    if (parser.is_err()) return CppArgumentLayer {};
    auto parsed = parser->parse(options, "cpp-contract"_str);
    return parsed.is_ok() ? rstd::move(parsed).unwrap() : CppArgumentLayer {};
}

auto cpp_options(ref<str>          standard,
                 CppOptimization   optimization,
                 CppDebugInfo      debug_info,
                 lito::Vec<String> options = {}) -> CppCompileOptions {
    auto result = make_cpp_options(standard,
                                   StandardLibrary::Libcxx,
                                   false,
                                   false,
                                   optimization,
                                   debug_info,
                                   CppOptionLayer {
                                       .arguments = argument_layer(rstd::move(options)),
                                   });
    if (result.is_err()) return CppCompileOptions {};
    return rstd::move(result).unwrap();
}

auto format(ref<str> build = "clang-build-a"_str) -> BmiFormatIdentity {
    return BmiFormatIdentity {
        .family               = String::make("clang"_str),
        .compiler_build       = String::make(build),
        .target               = String::make("x86_64-unknown-linux-gnu"_str),
        .resource_environment = String::make("resource-a"_str),
    };
}

auto artifact_key(BmiRepresentation        representation,
                  BmiSourceEmbeddingPolicy embedding,
                  ref<str>                 dependency,
                  ref<str> source_content = "source-content-a"_str) -> BmiArtifactKey {
    auto dependencies = lito::Vec<BmiRecipeDependency>::make();
    if (! dependency.is_empty()) {
        dependencies.push(BmiRecipeDependency {
            .logical_name = String::make("dependency"_str),
            .artifact_key = String::make(dependency),
        });
    }
    return make_bmi_artifact_key(BmiRecipe {
        .request =
            BmiRequest {
                .representation   = representation,
                .source_embedding = embedding,
            },
        .logical_name                 = String::make("sample"_str),
        .provider_identity            = String::make("package:source.cppm"_str),
        .source_identity              = String::make("/source/source.cppm"_str),
        .source_content_identity      = String::make(source_content),
        .cpp_context_identity         = String::make("cpp-context"_str),
        .public_requirements_identity = String::make("public-requirements"_str),
        .format_identity              = String::make("clang-format"_str),
        .direct_dependencies          = rstd::move(dependencies),
    });
}

auto has_argument(const lito::Vec<String>& arguments, ref<str> expected) -> bool {
    for (const auto& argument : arguments) {
        if (argument.as_str() == expected) return true;
    }
    return false;
}

auto has_prefix(const lito::Vec<String>& arguments, ref<str> prefix) -> bool {
    for (const auto& argument : arguments) {
        if (argument.as_str().starts_with(prefix)) return true;
    }
    return false;
}

} // namespace

TEST(CppContract, MaterializesTypedDefaultWarnings) {
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

TEST(CppContract, MaterializesTypedDefaultPositionIndependentCode) {
    auto defaults = cpp_options("c++20"_str, CppOptimization::None, CppDebugInfo::None);
    EXPECT_TRUE(defaults.codegen.position_independent_code);

    auto parsed = argument_layer(strings("-fno-PIC"_str));
    ASSERT_EQ(parsed.occurrences.len(), usize(1));
    EXPECT_TRUE(parsed.occurrences[usize {}].argument.is_PositionIndependentCode());

    auto disabled = cpp_options(
        "c++20"_str, CppOptimization::None, CppDebugInfo::None, strings("-fno-PIC"_str));
    EXPECT_FALSE(disabled.codegen.position_independent_code);
}

TEST(CppContract, NormalizesCommonOptionsBeforeToolchainMapping) {
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

TEST(CppContract, RejectsRawOverridesOfTypedSettings) {
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
    EXPECT_TRUE(optimization.unwrap_err().as_str().contains("optimization"_str));
}

TEST(CompilerArgumentContract, PreservesTokenRangesAndDoesNotGuessUnknownArity) {
    auto parser = make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto parsed = parser->parse(
        strings("-D"_str, "VALUE=1"_str, "-unknown-driver-option"_str, "unknown-value"_str),
        "contract.options"_str);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_EQ(parsed->occurrences.len(), usize(3));
    EXPECT_EQ(parsed->occurrences[usize {}].range.begin, usize {});
    EXPECT_EQ(parsed->occurrences[usize {}].range.end, usize(2));
    EXPECT_EQ(parsed->occurrences[usize(1)].range.begin, usize(2));
    EXPECT_EQ(parsed->occurrences[usize(1)].range.end, usize(3));
    EXPECT_EQ(parsed->occurrences[usize(2)].range.begin, usize(3));
    EXPECT_EQ(parsed->occurrences[usize(2)].range.end, usize(4));
    EXPECT_TRUE(parsed->occurrences[usize(1)].argument.as_Vendor().option.preserve_raw_tokens);
}

TEST(CompilerArgumentContract, RejectsInvalidAndDuplicateSchemaDefinitions) {
    auto invalid_schema = CompilerArgumentSchema::make();
    invalid_schema.add(CompilerArgumentDefinition {
        .name      = String::make("invalid"_str),
        .spellings = lito::Vec<CompilerArgumentSpelling>::make(),
    });
    auto invalid = rstd::move(invalid_schema).build();
    ASSERT_TRUE(invalid.is_err());
    EXPECT_TRUE(invalid.unwrap_err().is_InvalidDefinition());

    auto duplicate_schema = CompilerArgumentSchema::make();
    auto first_spellings  = lito::Vec<CompilerArgumentSpelling>::make();
    first_spellings.push(CompilerArgumentSpelling {
        .value = String::make("-duplicate"_str),
    });
    duplicate_schema.add(CompilerArgumentDefinition {
        .name      = String::make("first"_str),
        .spellings = rstd::move(first_spellings),
    });
    auto second_spellings = lito::Vec<CompilerArgumentSpelling>::make();
    second_spellings.push(CompilerArgumentSpelling {
        .value = String::make("-duplicate"_str),
    });
    duplicate_schema.add(CompilerArgumentDefinition {
        .name      = String::make("second"_str),
        .spellings = rstd::move(second_spellings),
    });
    auto duplicate = rstd::move(duplicate_schema).build();
    ASSERT_TRUE(duplicate.is_err());
    EXPECT_TRUE(duplicate.unwrap_err().is_DuplicateSpelling());
}

TEST(CompilerArgumentContract, ReportsMissingAndEmptyValuesAtTheParserBoundary) {
    auto parser = make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto missing = parser->parse(strings("-I"_str), "contract.options"_str);
    auto empty   = parser->parse(strings("--target="_str), "contract.options"_str);
    ASSERT_TRUE(missing.is_err());
    ASSERT_TRUE(empty.is_err());
    EXPECT_TRUE(missing.unwrap_err().as_str().contains("requires a value"_str));
    EXPECT_TRUE(empty.unwrap_err().as_str().contains("empty value"_str));
}

TEST(CompilerArgumentContract, CarriesTypedNativePreprocessorEffects) {
    auto parser = make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto arguments =
        parser->parse(strings("-include"_str, "forced.hpp"_str), "contract.options"_str);
    ASSERT_TRUE(arguments.is_ok());
    auto options = make_cpp_options("c++20"_str,
                                    StandardLibrary::Libcxx,
                                    false,
                                    false,
                                    CppOptimization::None,
                                    CppDebugInfo::None,
                                    CppOptionLayer {
                                        .arguments = rstd::move(arguments).unwrap(),
                                    });
    ASSERT_TRUE(options.is_ok());
    ASSERT_EQ(options->vendor.len(), usize(1));
    EXPECT_EQ(options->vendor[usize {}].effect, CppVendorOptionEffect::Preprocessor);
    EXPECT_TRUE(options->vendor[usize {}].native_preprocessor_unsupported);
    EXPECT_EQ(options->vendor[usize {}].raw_tokens.len(), usize(2));
}

TEST(BmiContract, SeparatesBuildRecipeFromConsumerCompatibility) {
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

TEST(BmiContract, ReportsConservativeSemanticDifferencesByField) {
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

TEST(BmiContract, ArtifactIdentityIncludesRepresentationEmbeddingAndDependencies) {
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

TEST(BmiContract, RequiresPublicPreprocessorSemanticsWithoutLeakingPrivateInputs) {
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

TEST(ClangContract, EmitsExactResolvedModuleMapping) {
    auto created = ClangToolchain::create(ToolchainSpec {
        .compiler = PathBuf::from("clang++"_str),
        .archiver = PathBuf::from("llvm-ar"_str),
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
    EXPECT_TRUE(has_argument(invocation->arguments, "-Wall"_str));
    EXPECT_TRUE(has_argument(invocation->arguments, "-Wpedantic"_str));
    EXPECT_TRUE(has_argument(invocation->arguments, "-Wno-gnu-statement-expression"_str));
    EXPECT_TRUE(has_argument(invocation->arguments, "-Wno-deprecated-declarations"_str));
    EXPECT_TRUE(has_argument(invocation->arguments, "-Wunknown-attributes"_str));
    EXPECT_TRUE(has_argument(invocation->arguments,
                             "-fmodule-file=sample.module=/tmp/sample.module.pcm"_str));
    EXPECT_FALSE(has_prefix(invocation->arguments, "-fprebuilt-module-path="_str));
}

TEST(ClangContract, RejectsNonLldLinkers) {
    auto created = ClangToolchain::create(ToolchainSpec {
        .compiler = PathBuf::from("clang++"_str),
        .linker   = PathBuf::from("ld"_str),
        .archiver = PathBuf::from("llvm-ar"_str),
    });
    ASSERT_TRUE(created.is_err());
    EXPECT_TRUE(created.unwrap_err().message.as_str().contains("not LLD"_str));
}

TEST(ClangContract, DoesNotPublishOneOutputWhenAnotherIsMissing) {
    auto created = ClangToolchain::create(ToolchainSpec {
        .compiler = PathBuf::from("clang++"_str),
        .archiver = PathBuf::from("llvm-ar"_str),
    });
    ASSERT_TRUE(created.is_ok());
    auto toolchain = rstd::move(created).unwrap();
    auto directory = rstd::env::temp_dir().join(
        PathBuf::from(rstd::format("lito-bmi-contract-{}", rstd::process::id()).as_str())
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
