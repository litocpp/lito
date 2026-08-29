#include <rstd/test/gtest.hpp>

import rstd;
import lito.core;

using namespace rstd::prelude;
using namespace rstd::literals;

TEST(Condition, ParsesAndEvaluatesTypedExpressions) {
    auto expression =
        lito::condition::parse(R"(target.os == "linux" && (!build.cross || feature.ffi))"_str);
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

TEST(System, ClassifiesWindowsTargetEnvironments) {
    auto msvc  = lito::system::parse_target_info("x86_64-pc-windows-msvc"_str);
    auto gnu   = lito::system::parse_target_info("x86_64-w64-windows-gnu"_str);
    auto mingw = lito::system::parse_target_info("x86_64-w64-mingw32"_str);
    auto linux = lito::system::parse_target_info("aarch64-unknown-linux-gnu"_str);
    ASSERT_TRUE(msvc.is_ok());
    ASSERT_TRUE(gnu.is_ok());
    ASSERT_TRUE(mingw.is_ok());
    ASSERT_TRUE(linux.is_ok());
    EXPECT_TRUE(msvc->is_msvc());
    EXPECT_TRUE(gnu->is_gnu());
    EXPECT_TRUE(mingw->is_gnu());
    EXPECT_TRUE(linux->is_gnu());
    EXPECT_EQ(msvc->environment_name(), "msvc"_str);
    EXPECT_EQ(gnu->environment_name(), "gnu"_str);
}

TEST(System, PreservesTargetComponentsAndDerivesPlatforms) {
    struct TargetCase {
        ref<str>                     triple;
        lito::system::Architecture   architecture;
        ref<str>                     vendor;
        ref<str>                     operating_system;
        Option<ref<str>>             environment;
        lito::system::TargetPlatform platform;
        lito::system::TargetFamily   family;
    };
    constexpr TargetCase cases[] = {
        { "x86_64-unknown-linux-gnu"_str,
          lito::system::Architecture::X86_64,
          "unknown"_str,
          "linux"_str,
          Some("gnu"_str),
          lito::system::TargetPlatform::Linux,
          lito::system::TargetFamily::Unix },
        { "aarch64-unknown-linux-android"_str,
          lito::system::Architecture::Aarch64,
          "unknown"_str,
          "linux"_str,
          Some("android"_str),
          lito::system::TargetPlatform::Android,
          lito::system::TargetFamily::Unix },
        { "aarch64-apple-darwin"_str,
          lito::system::Architecture::Aarch64,
          "apple"_str,
          "darwin"_str,
          None(),
          lito::system::TargetPlatform::Macos,
          lito::system::TargetFamily::Unix },
        { "x86_64-pc-windows-msvc"_str,
          lito::system::Architecture::X86_64,
          "pc"_str,
          "windows"_str,
          Some("msvc"_str),
          lito::system::TargetPlatform::Windows,
          lito::system::TargetFamily::Windows },
        { "wasm32-unknown-unknown"_str,
          lito::system::Architecture::Wasm32,
          "unknown"_str,
          "unknown"_str,
          None(),
          lito::system::TargetPlatform::Unknown,
          lito::system::TargetFamily::Unknown },
    };
    for (const auto& item : cases) {
        auto target = lito::system::parse_target_info(item.triple);
        ASSERT_TRUE(target.is_ok());
        EXPECT_EQ(target->triple.as_str(), item.triple);
        EXPECT_EQ(target->architecture, item.architecture);
        EXPECT_EQ(target->vendor.as_str(), item.vendor);
        EXPECT_EQ(target->operating_system.as_str(), item.operating_system);
        EXPECT_EQ(target->environment.is_some(), item.environment.is_some());
        if (item.environment.is_some()) {
            EXPECT_EQ(target->environment->as_str(), *item.environment);
        }
        EXPECT_EQ(target->platform, item.platform);
        EXPECT_EQ(target->family, item.family);
    }
}

TEST(System, NamesPluginsForTargetPlatforms) {
    auto linux   = lito::system::parse_target_info("x86_64-unknown-linux-gnu"_str).unwrap();
    auto macos   = lito::system::parse_target_info("aarch64-apple-darwin"_str).unwrap();
    auto windows = lito::system::parse_target_info("x86_64-pc-windows-msvc"_str).unwrap();

    EXPECT_EQ(lito::system::plugin_filename("pmacro"_str, linux).as_str(), "libpmacro.so"_str);
    EXPECT_EQ(lito::system::plugin_filename("pmacro"_str, macos).as_str(), "libpmacro.dylib"_str);
    EXPECT_EQ(lito::system::plugin_filename("pmacro"_str, windows).as_str(), "pmacro.dll"_str);
}
