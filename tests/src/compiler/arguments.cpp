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
using PathBuf = rstd::path::PathBuf;

TEST(CompilerArguments, PreservesTokenRangesAndDoesNotGuessUnknownArity) {
    auto parser = make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto parsed = parser->parse(
        strings("-D"_str, "VALUE=1"_str, "-unknown-driver-option"_str, "unknown-value"_str),
        "compiler.arguments"_str);
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

TEST(CompilerArguments, RejectsInvalidAndDuplicateSchemaDefinitions) {
    auto invalid_schema = cpp::CompilerArgumentSchema::make();
    invalid_schema.add(cpp::CompilerArgumentDefinition {
        .name      = String::make("invalid"_str),
        .spellings = Vec<cpp::CompilerArgumentSpelling>::make(),
    });
    auto invalid = rstd::move(invalid_schema).build();
    ASSERT_TRUE(invalid.is_err());
    EXPECT_TRUE(invalid.unwrap_err().is_InvalidDefinition());

    auto duplicate_schema = cpp::CompilerArgumentSchema::make();
    auto first_spellings  = Vec<cpp::CompilerArgumentSpelling>::make();
    first_spellings.push(cpp::CompilerArgumentSpelling {
        .value = String::make("-duplicate"_str),
    });
    duplicate_schema.add(cpp::CompilerArgumentDefinition {
        .name      = String::make("first"_str),
        .spellings = rstd::move(first_spellings),
    });
    auto second_spellings = Vec<cpp::CompilerArgumentSpelling>::make();
    second_spellings.push(cpp::CompilerArgumentSpelling {
        .value = String::make("-duplicate"_str),
    });
    duplicate_schema.add(cpp::CompilerArgumentDefinition {
        .name      = String::make("second"_str),
        .spellings = rstd::move(second_spellings),
    });
    auto duplicate = rstd::move(duplicate_schema).build();
    ASSERT_TRUE(duplicate.is_err());
    EXPECT_TRUE(duplicate.unwrap_err().is_DuplicateSpelling());
}

TEST(CompilerArguments, ReportsMissingAndEmptyValuesAtTheParserBoundary) {
    auto parser = make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto missing = parser->parse(strings("-I"_str), "compiler.arguments"_str);
    auto empty   = parser->parse(strings("--target="_str), "compiler.arguments"_str);
    ASSERT_TRUE(missing.is_err());
    ASSERT_TRUE(empty.is_err());
    auto missing_error = rstd::move(missing).unwrap_err();
    auto empty_error   = rstd::move(empty).unwrap_err();
    ASSERT_TRUE(missing_error.is_Argument());
    ASSERT_TRUE(empty_error.is_Argument());
    EXPECT_TRUE(missing_error.as_Argument().source.is_MissingValue());
    EXPECT_TRUE(empty_error.as_Argument().source.is_EmptyValue());
}

TEST(CompilerArguments, DecodesFiniteVisibilityValuesAtTheBindingBoundary) {
    auto parser = make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto parsed = parser->parse(strings("-fvisibility=default"_str,
                                        "-fvisibility=hidden"_str,
                                        "-fvisibility=internal"_str,
                                        "-fvisibility=protected"_str,
                                        "-ftype-visibility=default"_str,
                                        "-ftype-visibility=hidden"_str,
                                        "-ftype-visibility=internal"_str,
                                        "-ftype-visibility=protected"_str,
                                        "-fvisibility-inlines-hidden"_str,
                                        "-fno-visibility-inlines-hidden"_str),
                                "compiler.visibility"_str);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_EQ(parsed->occurrences.len(), usize(10));
    EXPECT_EQ(parsed->occurrences[usize {}].argument.as_SymbolVisibility().value,
              cpp::CppSymbolVisibility::Default);
    EXPECT_EQ(parsed->occurrences[usize(1)].argument.as_SymbolVisibility().value,
              cpp::CppSymbolVisibility::Hidden);
    EXPECT_EQ(parsed->occurrences[usize(2)].argument.as_SymbolVisibility().value,
              cpp::CppSymbolVisibility::Internal);
    EXPECT_EQ(parsed->occurrences[usize(3)].argument.as_SymbolVisibility().value,
              cpp::CppSymbolVisibility::Protected);
    EXPECT_EQ(parsed->occurrences[usize(4)].argument.as_TypeVisibility().value,
              cpp::CppSymbolVisibility::Default);
    EXPECT_EQ(parsed->occurrences[usize(5)].argument.as_TypeVisibility().value,
              cpp::CppSymbolVisibility::Hidden);
    EXPECT_EQ(parsed->occurrences[usize(6)].argument.as_TypeVisibility().value,
              cpp::CppSymbolVisibility::Internal);
    EXPECT_EQ(parsed->occurrences[usize(7)].argument.as_TypeVisibility().value,
              cpp::CppSymbolVisibility::Protected);
    EXPECT_TRUE(parsed->occurrences[usize(8)].argument.as_InlineVisibilityHidden().enabled);
    EXPECT_FALSE(parsed->occurrences[usize(9)].argument.as_InlineVisibilityHidden().enabled);

    auto invalid = parser->parse(strings("-fvisibility=public"_str), "compiler.visibility"_str);
    ASSERT_TRUE(invalid.is_err());
    auto error = rstd::move(invalid).unwrap_err();
    ASSERT_TRUE(error.is_Argument());
    ASSERT_TRUE(error.as_Argument().source.is_InvalidValue());
    EXPECT_TRUE(rstd::format("{}", error).as_str().contains("compiler.visibility"_str));
    auto invalid_source = rstd::format("{}", error.as_Argument().source);
    EXPECT_TRUE(invalid_source.as_str().contains("public"_str));
    EXPECT_TRUE(invalid_source.as_str().contains("protected"_str));

    auto empty = parser->parse(strings("-ftype-visibility="_str), "compiler.visibility"_str);
    ASSERT_TRUE(empty.is_err());
    auto empty_error = rstd::move(empty).unwrap_err();
    ASSERT_TRUE(empty_error.is_Argument());
    EXPECT_TRUE(empty_error.as_Argument().source.is_EmptyValue());
    EXPECT_TRUE(rstd::format("{}", empty_error).as_str().contains("compiler.visibility"_str));
    EXPECT_TRUE(
        rstd::format("{}", empty_error.as_Argument().source).as_str().contains("protected"_str));
}

TEST(CompilerArguments, CarriesTypedNativePreprocessorEffects) {
    auto parser = make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto arguments =
        parser->parse(strings("-include"_str, "forced.hpp"_str), "compiler.arguments"_str);
    ASSERT_TRUE(arguments.is_ok());
    auto options = cpp::make_cpp_options("c++20"_str,
                                         lito::config::StandardLibrary::Libcxx,
                                         false,
                                         false,
                                         lito::manifest::Optimization::None,
                                         lito::manifest::DebugInfo::None,
                                         cpp::CppOptionLayer {
                                             .arguments = rstd::move(arguments).unwrap(),
                                         });
    ASSERT_TRUE(options.is_ok());
    ASSERT_EQ(options->vendor.len(), usize(1));
    EXPECT_EQ(options->vendor[usize {}].effect, cpp::CppVendorOptionEffect::Preprocessor);
    EXPECT_TRUE(options->vendor[usize {}].native_preprocessor_unsupported);
    EXPECT_EQ(options->vendor[usize {}].raw_tokens.len(), usize(2));
}

TEST(CompilerArguments, ClassifiesPthreadAsThreadRequirement) {
    auto parser = make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto arguments =
        parser->parse(strings("-pthread"_str, "-pthread"_str), "compiler.arguments"_str);
    ASSERT_TRUE(arguments.is_ok());
    auto options = cpp::make_cpp_options("c++20"_str,
                                         lito::config::StandardLibrary::Libcxx,
                                         false,
                                         false,
                                         lito::manifest::Optimization::None,
                                         lito::manifest::DebugInfo::None,
                                         cpp::CppOptionLayer {
                                             .arguments = rstd::move(arguments).unwrap(),
                                         });
    ASSERT_TRUE(options.is_ok());
    EXPECT_TRUE(compiler::uses_posix_threads(options->common));
    EXPECT_TRUE(options->language.modes.is_empty());

    auto normalized = normalize_clang_link_arguments(lito::link::ArgumentSequence {
        .tokens   = strings("-pthread"_str, "-ldl"_str, "-lm"_str),
        .source   = String::make("compiler arguments test"_str),
        .identity = String::make("link-v1"_str),
    });
    ASSERT_TRUE(normalized.is_ok());
    EXPECT_TRUE(normalized->requirements.posix_threads);
    ASSERT_EQ(normalized->requirements.system_libraries.len(), usize(1));
    EXPECT_EQ(normalized->requirements.system_libraries[usize {}].name.as_str(), "dl"_str);
    ASSERT_EQ(normalized->arguments.tokens.len(), usize(1));
    EXPECT_EQ(normalized->arguments.tokens[usize {}].as_str(), "-lm"_str);
}

TEST(CompilerArguments, NormalizesRuntimeSearchRequirements) {
    auto normalized = normalize_clang_link_arguments(lito::link::ArgumentSequence {
        .tokens   = strings("-Wl,-rpath,$ORIGIN"_str,
                            "-Wl,--rpath,/tmp/build-lib"_str,
                            "-Wl,-rpath,$ORIGIN"_str,
                            "-lm"_str),
        .source   = String::make("runtime search test"_str),
        .identity = String::make("runtime-search-v1"_str),
    });
    ASSERT_TRUE(normalized.is_ok());
    ASSERT_EQ(normalized->requirements.runtime_search_paths.len(), usize(2));
    EXPECT_EQ(normalized->requirements.runtime_search_paths[usize {}].path.as_str(), "$ORIGIN"_str);
    EXPECT_EQ(normalized->requirements.runtime_search_paths[usize(1)].path.as_str(),
              "/tmp/build-lib"_str);
    ASSERT_EQ(normalized->arguments.tokens.len(), usize(1));
    EXPECT_EQ(normalized->arguments.tokens[usize {}].as_str(), "-lm"_str);

    auto legacy = normalize_clang_link_arguments(lito::link::ArgumentSequence {
        .tokens   = strings("-Wl,--disable-new-dtags"_str),
        .source   = String::make("legacy runtime search test"_str),
        .identity = String::make("runtime-search-v1"_str),
    });
    ASSERT_TRUE(legacy.is_err());
    EXPECT_TRUE(legacy.unwrap_err().is_LegacyRpath());

    auto invalid =
        lito::artifact::make_origin_relative_runtime_path(PathBuf::from("../lib:other"_str));
    ASSERT_TRUE(invalid.is_err());
}

TEST(CompilerArguments, PreservesTypedLinkProfileArguments) {
    auto normalized = normalize_clang_link_arguments(lito::link::ArgumentSequence {
        .tokens =
            strings("-Wl,--as-needed"_str, "-flto=thin"_str, "-Wl,--strip-debug"_str, "-lm"_str),
        .source   = String::make("LDFLAGS"_str),
        .identity = String::make("profile-link-v1"_str),
    });
    ASSERT_TRUE(normalized.is_ok());
    ASSERT_EQ(normalized->profile_arguments.len(), usize(2));
    EXPECT_EQ(normalized->profile_arguments[usize {}].argument.as_Lto().value,
              lito::manifest::Lto::Thin);
    EXPECT_EQ(normalized->profile_arguments[usize(1)].argument.as_Strip().value,
              lito::artifact::StripMode::DebugInfo);
    ASSERT_EQ(normalized->arguments.tokens.len(), usize(4));
    EXPECT_EQ(normalized->arguments.tokens[usize(1)].as_str(), "-flto=thin"_str);
    EXPECT_EQ(normalized->arguments.tokens[usize(2)].as_str(), "-Wl,--strip-debug"_str);
}

TEST(CompilerArguments, KeepsCVendorOptionsInTheCLanguageDomain) {
    auto parser = make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto arguments =
        parser->parse_c(strings("-fno-builtin"_str, "-pthread"_str), "C compiler arguments"_str);
    ASSERT_TRUE(arguments.is_ok());
    ASSERT_EQ(arguments->occurrences.len(), usize(2));
    ASSERT_TRUE(arguments->occurrences[usize {}].argument.is_Vendor());
    EXPECT_EQ(arguments->occurrences[usize {}].argument.as_Vendor().option.effect,
              c::CVendorOptionEffect::Unknown);
    EXPECT_TRUE(arguments->occurrences[usize {}].argument.as_Vendor().option.preserve_raw_tokens);
    ASSERT_TRUE(arguments->occurrences[usize(1)].argument.is_Common());
    EXPECT_TRUE(arguments->occurrences[usize(1)].argument.as_Common().argument.is_Threading());

    auto options = c::apply_c_option_layer(
        c::make_c_options(compiler::CommonCompileOptions {}, lito::manifest::CStandard::C17),
        rstd::move(arguments).unwrap());
    ASSERT_TRUE(options.is_ok());
    ASSERT_EQ(options->vendor.len(), usize(1));
    EXPECT_EQ(options->vendor[usize {}].value.as_str(), "-fno-builtin"_str);
    EXPECT_TRUE(compiler::uses_posix_threads(options->common));
}

TEST(CompilerArguments, DecodesCommonCodegenSettingsForCAndCpp) {
    auto parser = make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto values = strings("-O2"_str, "-gline-tables-only"_str, "-flto=thin"_str);
    auto cpp    = parser->parse(values, "CXXFLAGS"_str);
    auto c      = parser->parse_c(values, "CFLAGS"_str);
    ASSERT_TRUE(cpp.is_ok());
    ASSERT_TRUE(c.is_ok());
    ASSERT_EQ(cpp->occurrences.len(), usize(3));
    ASSERT_EQ(c->occurrences.len(), usize(3));
    EXPECT_EQ(
        cpp->occurrences[usize {}].argument.as_CodegenSetting().setting.as_Optimization().value,
        lito::manifest::Optimization::Level2);
    EXPECT_EQ(cpp->occurrences[usize(1)].argument.as_CodegenSetting().setting.as_DebugInfo().value,
              lito::manifest::DebugInfo::LineTablesOnly);
    EXPECT_EQ(cpp->occurrences[usize(2)].argument.as_CodegenSetting().setting.as_Lto().value,
              lito::manifest::Lto::Thin);
    EXPECT_EQ(c->occurrences[usize {}].argument.as_CodegenSetting().setting.as_Optimization().value,
              lito::manifest::Optimization::Level2);
    EXPECT_EQ(c->occurrences[usize(1)].argument.as_CodegenSetting().setting.as_DebugInfo().value,
              lito::manifest::DebugInfo::LineTablesOnly);
    EXPECT_EQ(c->occurrences[usize(2)].argument.as_CodegenSetting().setting.as_Lto().value,
              lito::manifest::Lto::Thin);
}

TEST(CompilerArguments, DecodesMicrosoftRuntimeForCAndCppWithLastValueWins) {
    auto parser = make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto values        = strings("-fms-runtime-lib=static"_str, "-fms-runtime-lib=dll_dbg"_str);
    auto cpp_arguments = parser->parse(values, "CXXFLAGS"_str);
    auto c_arguments   = parser->parse_c(values, "CFLAGS"_str);
    ASSERT_TRUE(cpp_arguments.is_ok());
    ASSERT_TRUE(c_arguments.is_ok());
    auto cpp_options = cpp::make_cpp_options("c++20"_str,
                                             lito::config::StandardLibrary::Msvc,
                                             false,
                                             false,
                                             lito::manifest::Optimization::None,
                                             lito::manifest::DebugInfo::None,
                                             cpp::CppOptionLayer {
                                                 .arguments = rstd::move(cpp_arguments).unwrap(),
                                             });
    auto c_options   = c::apply_c_option_layer(
        c::make_c_options(compiler::CommonCompileOptions {}, lito::manifest::CStandard::C17),
        rstd::move(c_arguments).unwrap());
    ASSERT_TRUE(cpp_options.is_ok());
    ASSERT_TRUE(c_options.is_ok());
    ASSERT_TRUE(cpp_options->common.microsoft_runtime_library.is_some());
    ASSERT_TRUE(c_options->common.microsoft_runtime_library.is_some());
    EXPECT_EQ(*cpp_options->common.microsoft_runtime_library,
              compiler::MicrosoftRuntimeLibrary::DynamicDebug);
    EXPECT_EQ(*c_options->common.microsoft_runtime_library,
              compiler::MicrosoftRuntimeLibrary::DynamicDebug);

    auto invalid = parser->parse(strings("-fms-runtime-lib=dynamic"_str), "CXXFLAGS"_str);
    ASSERT_TRUE(invalid.is_err());
    auto error = invalid.unwrap_err();
    ASSERT_TRUE(error.is_Argument());
    ASSERT_TRUE(error.as_Argument().source.is_InvalidValue());
    const auto& source = error.as_Argument().source.as_InvalidValue();
    EXPECT_EQ(source.value.as_str(), "dynamic"_str);
    EXPECT_TRUE(source.expected.as_str().contains("dll_dbg"_str));
}
