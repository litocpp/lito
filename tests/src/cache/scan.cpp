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

class ScanCache : public ProjectFixture {};

auto scan_cache_tree() -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "high/unused.hpp"_str, R"cache(#pragma once
)cache"_str },
        { "lito.toml"_str, R"cache([package]
name = "fixture-scan-cache"
version = "0.1.0"

[lib]
name = "fixture-scan-cache"
module = "fixture.scan.cache"
archive = "fixture.scan.cache"

[usage]
private-include-directories = ["high", "mid", "low"]
)cache"_str },
        { "low/choice.hpp"_str, R"cache(#pragma once

#define LITO_LOW_VALUE 1
)cache"_str },
        { "mid/choice.hpp"_str, R"cache(#pragma once

#include_next <choice.hpp>

#define LITO_MID_VALUE      (10 + LITO_LOW_VALUE)
#define LITO_SELECTED_VALUE LITO_MID_VALUE
)cache"_str },
        { "src/lib.cppm"_str, R"cache(module;

#include <choice.hpp>

#if __has_include(<optional.hpp>)
#include <optional.hpp>
#else
#define LITO_OPTIONAL_VALUE 0
#endif

export module fixture.scan.cache;

export auto scan_cache_value() -> int {
    return LITO_SELECTED_VALUE + LITO_OPTIONAL_VALUE;
}
)cache"_str },
        { "staged/choice-low.hpp"_str, R"cache(#pragma once

#define LITO_LOW_VALUE 2
)cache"_str },
        { "staged/choice.hpp"_str, R"cache(#pragma once

#include_next <choice.hpp>

#undef LITO_SELECTED_VALUE
#define LITO_SELECTED_VALUE (100 + LITO_MID_VALUE)
)cache"_str },
        { "staged/lib.cppm"_str, R"cache(module;

#include <choice.hpp>

#if __has_include(<optional.hpp>)
#include <optional.hpp>
#else
#define LITO_OPTIONAL_VALUE 0
#endif

export module fixture.scan.cache;

export auto scan_cache_value() -> int {
    return LITO_SELECTED_VALUE + LITO_OPTIONAL_VALUE + 1;
}
)cache"_str },
        { "staged/optional.hpp"_str, R"cache(#pragma once

#define LITO_OPTIONAL_VALUE 1000
)cache"_str },
    };
    return source_tree(files);
}

auto dynamic_cache_tree() -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"cache([package]
name = "fixture-scan-cache-dynamic"
version = "0.1.0"

[lib]
name = "fixture-scan-cache-dynamic"
module = "fixture.scan.cache.dynamic"
archive = "fixture.scan.cache.dynamic"
)cache"_str },
        { "src/lib.cppm"_str, R"cache(export module fixture.scan.cache.dynamic;

export auto scan_cache_date() -> const char* {
    return __DATE__;
}
)cache"_str },
    };
    return source_tree(files);
}

TEST_F(ScanCache, ScanCacheReusesAndInvalidatesOwnedInputs) {
    auto tree = scan_cache_tree();
    ASSERT_TRUE(tree.is_ok());
    auto project = materialize("scan-cache"_str, *tree);
    ASSERT_TRUE(project.is_ok());
    auto fixture = project->root.clone();
    auto output  = build_root("scan-cache"_str);

    auto cold =
        lito::build(build_request(fixture.as_path(), output.as_path(), Vec<String>::make()));
    ASSERT_TRUE(cold.is_ok());
    EXPECT_EQ(cold->frontend.persistent_scan_misses, usize(1));
    EXPECT_EQ(cold->frontend.persistent_scan_refresh, usize(1));
    EXPECT_EQ(cold->frontend.analyze_builds, usize(1));
    auto bmi_directory = output.join(PathBuf::from("bmi"_str).as_path());
    auto cold_bmis     = regular_file_count(bmi_directory.as_path());
    ASSERT_TRUE(cold_bmis.is_some());
    EXPECT_EQ(*cold_bmis, usize(1));

    auto warm =
        lito::build(build_request(fixture.as_path(), output.as_path(), Vec<String>::make()));
    ASSERT_TRUE(warm.is_ok());
    EXPECT_EQ(warm->frontend.persistent_scan_hits, usize(1));
    EXPECT_EQ(warm->frontend.analyze_builds, usize {});
    EXPECT_EQ(warm->compiled, usize {});

    auto staged_optional = fixture.join(PathBuf::from("staged/optional.hpp"_str).as_path());
    auto high_optional   = fixture.join(PathBuf::from("high/optional.hpp"_str).as_path());
    ASSERT_TRUE(rstd::fs::copy(staged_optional.as_path(), high_optional.as_path()).is_ok());
    auto optional =
        lito::build(build_request(fixture.as_path(), output.as_path(), Vec<String>::make()));
    ASSERT_TRUE(optional.is_ok());
    EXPECT_EQ(optional->frontend.persistent_scan_include_lookup, usize(1));
    EXPECT_EQ(optional->frontend.analyze_builds, usize(1));

    auto staged_priority = fixture.join(PathBuf::from("staged/choice.hpp"_str).as_path());
    auto high_priority   = fixture.join(PathBuf::from("high/choice.hpp"_str).as_path());
    ASSERT_TRUE(rstd::fs::copy(staged_priority.as_path(), high_priority.as_path()).is_ok());
    auto priority =
        lito::build(build_request(fixture.as_path(), output.as_path(), Vec<String>::make()));
    ASSERT_TRUE(priority.is_ok());
    EXPECT_EQ(priority->frontend.persistent_scan_include_lookup, usize(1));

    auto staged_header = fixture.join(PathBuf::from("staged/choice-low.hpp"_str).as_path());
    auto low_header    = fixture.join(PathBuf::from("low/choice.hpp"_str).as_path());
    ASSERT_TRUE(rstd::fs::copy(staged_header.as_path(), low_header.as_path()).is_ok());
    auto header =
        lito::build(build_request(fixture.as_path(), output.as_path(), Vec<String>::make()));
    ASSERT_TRUE(header.is_ok());
    EXPECT_EQ(header->frontend.persistent_scan_file_dependency, usize(1));

    auto staged_source  = fixture.join(PathBuf::from("staged/lib.cppm"_str).as_path());
    auto primary_source = fixture.join(PathBuf::from("src/lib.cppm"_str).as_path());
    ASSERT_TRUE(rstd::fs::copy(staged_source.as_path(), primary_source.as_path()).is_ok());
    auto changed_source =
        lito::build(build_request(fixture.as_path(), output.as_path(), Vec<String>::make()));
    ASSERT_TRUE(changed_source.is_ok());
    EXPECT_EQ(changed_source->frontend.persistent_scan_source, usize(1));
    auto changed_bmis = regular_file_count(bmi_directory.as_path());
    ASSERT_TRUE(changed_bmis.is_some());
    EXPECT_EQ(*changed_bmis, usize(1));

    auto dynamic_tree = dynamic_cache_tree();
    ASSERT_TRUE(dynamic_tree.is_ok());
    auto dynamic_project = materialize("scan-cache-dynamic"_str, *dynamic_tree);
    ASSERT_TRUE(dynamic_project.is_ok());
    auto dynamic_fixture = dynamic_project->root.clone();
    auto dynamic_output  = build_root("scan-cache-dynamic"_str);
    auto dynamic_cold    = lito::build(
        build_request(dynamic_fixture.as_path(), dynamic_output.as_path(), Vec<String>::make()));
    ASSERT_TRUE(dynamic_cold.is_ok());
    auto dynamic_warm = lito::build(
        build_request(dynamic_fixture.as_path(), dynamic_output.as_path(), Vec<String>::make()));
    ASSERT_TRUE(dynamic_warm.is_ok());
    EXPECT_EQ(dynamic_warm->frontend.persistent_scan_hits, usize {});
    EXPECT_EQ(dynamic_warm->frontend.persistent_scan_uncacheable, usize(1));
    EXPECT_EQ(dynamic_warm->frontend.analyze_builds, usize(1));
}
