#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.driver;
import lito.core;
import lito.test.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

using namespace lito_test;

class ScanCommand : public ProjectFixture {};

auto scan_command_tree() -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"scan([workspace]
name = "scan-command"
members = ["preprocessor-native", "scan-definitions"]

[workspace.package]
version = "0.1.0"

[profile]
exceptions = false
rtti = false
)scan"_str },
        { "preprocessor-native/lito.toml"_str, R"scan([package]
name = "fixture-preprocessor-native"
version = "0.1.0"

[lib]
name = "fixture-preprocessor-native"
module = "fixture.preprocessor.native"
archive = "fixture.preprocessor.native"

[features.docs]
default = false
)scan"_str },
        { "preprocessor-native/src/config.hpp"_str, R"scan(#pragma once

#define LITO_ENABLED 1
#define LITO_HEADER_REVISION 1
#define LITO_PARTITION :dependency
)scan"_str },
        { "preprocessor-native/src/dependency.cppm"_str,
          R"scan(export module fixture.preprocessor.native:dependency;

export auto native_preprocessor_dependency() -> int {
    return 23;
}
)scan"_str },
        { "preprocessor-native/src/lib.cppm"_str, R"scan(module;

#include "config.hpp"

#define LITO_JOIN(left, right) left##right
#define LITO_IMPORT(name) export import name;
#define LITO_OPTIONAL_IMPORT(...) __VA_OPT__(LITO_IMPORT(__VA_ARGS__))
#define LITO_PRAGMA(value) _Pragma(#value)

export module fixture.preprocessor.native;

#if defined(LITO_FEAT_DOCS)
import :docs;
#endif

#if defined(LITO_ENABLED) && LITO_JOIN(LITO_, ENABLED) && ((2 + 3 * 4) == 14) && \
    __has_include("config.hpp")
LITO_OPTIONAL_IMPORT(LITO_PARTITION)
#endif

#if __has_include("missing.hpp")
import :missing;
#endif

#if !__has_builtin(__builtin_assume) || \
    !__has_cpp_attribute(_Clang::__lifetimebound__) || \
    !__has_attribute(__type_visibility__) || \
    !__has_warning("-Winvalid-specialization") || \
    !__has_feature(cxx_unicode_literals)
import :standard_library_capability_failure;
#endif

#if __has_builtin(__builtin_lito_missing) || __has_feature(cxx_exceptions) || \
    __has_extension(cxx_exceptions) || __has_feature(cxx_rtti) || \
    __has_extension(cxx_rtti) || __is_identifier(class) || \
    __is_identifier(_Atomic) || __is_identifier(__datasizeof) || \
    !__is_identifier(lito_identifier) || defined(__EXCEPTIONS) || \
    defined(__cpp_exceptions) || defined(__GXX_RTTI) || defined(__cpp_rtti)
import :native_builtin_failure;
#endif

#if 0 && (1 / 0)
import :short_circuit_failure;
#endif

#if !(1 ? 1 : (1 / 0))
import :conditional_failure;
#endif

#define LITO_STACKED 1
#pragma push_macro("LITO_STACKED")
#undef LITO_STACKED
#define LITO_STACKED 0
#pragma pop_macro("LITO_STACKED")

#if LITO_STACKED != 1
import :pragma_stack_failure;
#endif

LITO_PRAGMA(push_macro("LITO_STACKED"))
#undef LITO_STACKED
#define LITO_STACKED 0
LITO_PRAGMA(pop_macro("LITO_STACKED"))

#if LITO_STACKED != 1
import :pragma_operator_failure;
#endif

constexpr auto ignored_import = R"tag(import fixture.preprocessor.missing;)tag";

export auto native_preprocessor_value() -> int {
    return native_preprocessor_dependency();
}

export inline constexpr auto package_version = LITO_PKG_VERSION;
)scan"_str },
        { "scan-definitions/lito.toml"_str, R"scan([package]
name = "fixture-scan-definitions"
version = "0.1.0"

[lib]
name = "fixture-scan-definitions"
module = "fixture.scan.definitions"
archive = "fixture.scan.definitions"

[usage]
private-definitions = ["LITO_SCAN_DEFINITION=7"]
options = ["-DLITO_SCAN_REMOVED=1", "-ULITO_SCAN_REMOVED", "-U__clang__"]
)scan"_str },
        { "scan-definitions/src/lib.cppm"_str, R"scan(export module fixture.scan.definitions;

#if LITO_SCAN_DEFINITION == 7
import :defined;
#else
import :missing;
#endif

#if defined(LITO_SCAN_REMOVED) || defined(__clang__)
import :command_line_undef_failure;
#endif
)scan"_str },
    };
    return source_tree(files);
}

TEST_F(ScanCommand, ScanUsesNativePreprocessorAndDefinitions) {
    auto tree = scan_command_tree();
    ASSERT_TRUE(tree.is_ok());
    auto project = materialize("scan"_str, *tree);
    ASSERT_TRUE(project.is_ok());
    auto root   = project->root.clone();
    auto native = lito::scan(lito::ScanRequest {
        .selection =
            lito::package::PackageSelection {
                .root     = root.clone(),
                .packages = strings("fixture-preprocessor-native"_str),
            },
        .source        = PathBuf::from("preprocessor-native/src/lib.cppm"_str),
        .configuration = lito::config::build_configuration_request(configuration()),
    });
    ASSERT_TRUE(native.is_ok());
    ASSERT_TRUE(native->result.language.is_Cpp());
    const auto& native_facts = native->result.language.as_Cpp().facts;
    ASSERT_TRUE(native_facts.provided.is_some());
    EXPECT_EQ(native_facts.provided->logical_name.as_str(), "fixture.preprocessor.native"_str);
    EXPECT_TRUE(has_import(*native, "fixture.preprocessor.native:dependency"_str));
    ASSERT_EQ(native_facts.required_modules.len(), usize(1));
    EXPECT_TRUE(native_facts.required_modules[usize {}].exported);
    EXPECT_FALSE(has_import(*native, "fixture.preprocessor.native:native_builtin_failure"_str));

    auto native_json = lito::scan_report_json(*native);
    ASSERT_TRUE(native_json.is_ok());
    EXPECT_TRUE(native_json->as_str().contains("\"format\": \"lito-scan\""_str));
    EXPECT_TRUE(native_json->as_str().contains("\"version\": 4"_str));
    EXPECT_TRUE(native_json->as_str().contains("\"exported\": true"_str));
    EXPECT_TRUE(native_json->as_str().contains("\"external-macros\":"_str));
    EXPECT_TRUE(native_json->as_str().contains("\"name\": \"LITO_PKG_VERSION\""_str));
    EXPECT_TRUE(native_json->as_str().contains("\"state\": \"defined\""_str));
    EXPECT_TRUE(native_json->as_str().contains("LITO_PKG_VERSION=\\\"0.1.0\\\""_str));
    EXPECT_TRUE(native_json->as_str().contains("\"name\": \"LITO_FEAT_DOCS\""_str));
    EXPECT_TRUE(native_json->as_str().contains("\"state\": \"undefined\""_str));

    auto p1689_json = lito::scan_report_json(*native, lito::ScanOutputFormat::P1689);
    ASSERT_TRUE(p1689_json.is_ok());
    EXPECT_TRUE(p1689_json->as_str().contains("\"version\": 1"_str));
    EXPECT_TRUE(p1689_json->as_str().contains("\"revision\": 0"_str));
    EXPECT_TRUE(p1689_json->as_str().contains("\"primary-output\":"_str));
    EXPECT_TRUE(p1689_json->as_str().contains("\"provides\":"_str));
    EXPECT_TRUE(
        p1689_json->as_str().contains("\"logical-name\": \"fixture.preprocessor.native\""_str));
    EXPECT_TRUE(p1689_json->as_str().contains(
        "\"logical-name\": \"fixture.preprocessor.native:dependency\""_str));
    EXPECT_FALSE(p1689_json->as_str().contains("\"exported\":"_str));
    EXPECT_FALSE(p1689_json->as_str().contains("\"format\":"_str));
    EXPECT_FALSE(p1689_json->as_str().contains("\"headers\":"_str));

    auto definitions = lito::scan(lito::ScanRequest {
        .selection =
            lito::package::PackageSelection {
                .root     = root.clone(),
                .packages = strings("fixture-scan-definitions"_str),
            },
        .source        = PathBuf::from("scan-definitions/src/lib.cppm"_str),
        .configuration = lito::config::build_configuration_request(configuration()),
    });
    ASSERT_TRUE(definitions.is_ok());
    EXPECT_TRUE(has_import(*definitions, "fixture.scan.definitions:defined"_str));
    EXPECT_FALSE(has_import(*definitions, "fixture.scan.definitions:missing"_str));
    EXPECT_FALSE(
        has_import(*definitions, "fixture.scan.definitions:command_line_undef_failure"_str));
}
