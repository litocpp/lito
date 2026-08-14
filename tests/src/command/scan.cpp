#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito;
import lito.manifest;
import lito.source;
import lito.test.support;
import lito.workspace.contract;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

using namespace lito_test;

TEST(ScanCommand, ScanUsesNativePreprocessorAndDefinitions) {
    auto root   = project_root();
    auto native = lito::scan(lito::ScanRequest {
        .selection =
            lito::PackageSelection {
                .root     = root.clone(),
                .packages = strings("fixture-preprocessor-native"_str),
            },
        .source        = PathBuf::from("preprocessor-native/src/lib.cppm"_str),
        .configuration = configuration(),
    });
    ASSERT_TRUE(native.is_ok());
    ASSERT_TRUE(native->result.provided.is_some());
    EXPECT_EQ(native->result.provided->logical_name.as_str(), "fixture.preprocessor.native"_str);
    EXPECT_TRUE(has_import(*native, "fixture.preprocessor.native:dependency"_str));
    ASSERT_EQ(native->result.imports.len(), usize(1));
    EXPECT_TRUE(native->result.imports[usize {}].exported);
    EXPECT_FALSE(has_import(*native, "fixture.preprocessor.native:native_builtin_failure"_str));

    auto native_json = lito::scan_report_json(*native);
    ASSERT_TRUE(native_json.is_ok());
    EXPECT_TRUE(native_json->as_str().contains("\"format\": \"lito-scan\""_str));
    EXPECT_TRUE(native_json->as_str().contains("\"version\": 2"_str));
    EXPECT_TRUE(native_json->as_str().contains("\"exported\": true"_str));

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
            lito::PackageSelection {
                .root     = root.clone(),
                .packages = strings("fixture-scan-definitions"_str),
            },
        .source        = PathBuf::from("scan-definitions/src/lib.cppm"_str),
        .configuration = configuration(),
    });
    ASSERT_TRUE(definitions.is_ok());
    EXPECT_TRUE(has_import(*definitions, "fixture.scan.definitions:defined"_str));
    EXPECT_FALSE(has_import(*definitions, "fixture.scan.definitions:missing"_str));
    EXPECT_FALSE(
        has_import(*definitions, "fixture.scan.definitions:command_line_undef_failure"_str));
}
