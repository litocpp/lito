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

TEST(CompilerArguments, CarriesTypedNativePreprocessorEffects) {
    auto parser = make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto arguments =
        parser->parse(strings("-include"_str, "forced.hpp"_str), "compiler.arguments"_str);
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
