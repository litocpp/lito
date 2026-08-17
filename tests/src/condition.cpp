#include <rstd/test/gtest.hpp>

import rstd;
import lito.core;

using namespace rstd::prelude;
using namespace rstd::literals;

TEST(Condition, ParsesAndEvaluatesTypedExpressions) {
    auto expression = lito::condition::parse(
        R"(target.os == "linux" && (!build.cross || feature.ffi))"_str);
    ASSERT_TRUE(expression.is_ok());

    auto context = lito::condition::Context {};
    context.set_string(String::make("target.os"_str), String::make("linux"_str));
    context.set_bool(String::make("build.cross"_str), true);
    context.set_bool(String::make("feature.ffi"_str), true);
    auto evaluated = lito::condition::evaluate(*expression, context);
    ASSERT_TRUE(evaluated.is_ok());
    EXPECT_TRUE(*evaluated);
}

TEST(Condition, ShortCircuitsLogicalOperators) {
    auto expression = lito::condition::parse("true || missing.key"_str);
    ASSERT_TRUE(expression.is_ok());
    auto evaluated = lito::condition::evaluate(*expression, lito::condition::Context {});
    ASSERT_TRUE(evaluated.is_ok());
    EXPECT_TRUE(*evaluated);
}

TEST(Condition, RejectsUnknownKeysAndTypeMismatches) {
    auto unknown = lito::condition::parse("missing.key"_str);
    ASSERT_TRUE(unknown.is_ok());
    EXPECT_TRUE(lito::condition::evaluate(*unknown, lito::condition::Context {}).is_err());

    auto comparison = lito::condition::parse(R"(target.os == true)"_str);
    ASSERT_TRUE(comparison.is_ok());
    auto context = lito::condition::Context {};
    context.set_string(String::make("target.os"_str), String::make("linux"_str));
    EXPECT_TRUE(lito::condition::evaluate(*comparison, context).is_err());
    EXPECT_TRUE(lito::condition::parse("target.os &&"_str).is_err());
}

TEST(Condition, DistinguishesHostTargetAndCrossState) {
    auto expression = lito::condition::parse(
        R"(target.os == "windows" && host.os == "linux" && build.cross)"_str);
    ASSERT_TRUE(expression.is_ok());
    auto context = lito::condition::Context {};
    context.set_string(String::make("target.os"_str), String::make("windows"_str));
    context.set_string(String::make("host.os"_str), String::make("linux"_str));
    context.set_bool(String::make("build.cross"_str), true);

    auto evaluated = lito::condition::evaluate(*expression, context);
    ASSERT_TRUE(evaluated.is_ok());
    EXPECT_TRUE(*evaluated);
}
