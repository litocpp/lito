#include <rstd/test/gtest.hpp>

import rstd;
import rstd.json;
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

auto package_metadata_manifest(ref<str> version) -> String {
    return rstd::format(R"cache([package]
name = "fixture-package-metadata-cache"
version = "{}"

[[bin]]
name = "fixture-package-metadata-cache"
link-stdlib = false
sources = ["src/main.cpp", "src/version.cpp", "src/feature.cpp", "src/unused.cpp"]

[features.optional]
default = false

[features.unused]
default = false
)cache",
                        version);
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

    auto scan_record = cold->product.build_directory.join(
        PathBuf::from(
            "lito-scan-cache/fixture-scan-cache/lib/fixture-scan-cache/src/lib.cppm.json"_str)
            .as_path());
    auto scan_record_text = rstd::fs::read_to_string(scan_record.as_path());
    ASSERT_TRUE(scan_record_text.is_ok());
    auto scan_record_json = rstd::json::from_str(scan_record_text->as_str());
    ASSERT_TRUE(scan_record_json.is_ok());
    (*scan_record_json)["source-origin"_str] =
        rstd::json::Value::String(String::make("path+different-package-source"_str));
    auto changed_record = rstd::json::to_string(
        *scan_record_json, rstd::json::FormatOptions { .pretty = true, .indent = usize(2) });
    ASSERT_TRUE(rstd::fs::write(scan_record.as_path(), changed_record.as_str().as_bytes()).is_ok());
    auto changed_origin =
        lito::build(build_request(fixture.as_path(), output.as_path(), Vec<String>::make()));
    ASSERT_TRUE(changed_origin.is_ok());
    EXPECT_EQ(changed_origin->frontend.persistent_scan_context, usize(1));
    EXPECT_EQ(changed_origin->frontend.analyze_builds, usize(1));

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

TEST_F(ScanCache, IncludeLookupCachesNotDirectoryCandidatesAsMissing) {
    constexpr ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "fixture-scan-not-directory"
version = "0.1.0"

[[bin]]
name = "fixture-scan-not-directory"
link-stdlib = false
sources = ["src/main.cpp"]

[usage]
private-include-directories = ["first", "second"]
)toml"_str },
        { "first/nested"_str, "aggregate header\n"_str },
        { "second/nested/choice.hpp"_str, "#define LITO_CHOICE 1\n"_str },
        { "src/main.cpp"_str,
          "#include <nested/choice.hpp>\n"
          "auto main() -> int { return LITO_CHOICE == 1 ? 0 : 1; }\n"_str },
    };
    auto project = materialize("scan-not-directory"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto output  = build_root("scan-not-directory"_str);
    auto request = build_request(
        project->root.as_path(), output.as_path(), strings("fixture-scan-not-directory"_str));

    auto cold = lito::build(request);
    ASSERT_TRUE(cold.is_ok());
    auto warm = lito::build(request);
    ASSERT_TRUE(warm.is_ok());
    EXPECT_EQ(warm->frontend.persistent_scan_hits, usize(1));

    auto ancestor = project->root.join(PathBuf::from("first/nested"_str).as_path());
    ASSERT_TRUE(rstd::fs::remove_file(ancestor.as_path()).is_ok());
    ASSERT_TRUE(rstd::fs::create_dir_all(ancestor.as_path()).is_ok());
    auto preferred = ancestor.join(PathBuf::from("choice.hpp"_str).as_path());
    ASSERT_TRUE(
        rstd::fs::write(preferred.as_path(), "#define LITO_CHOICE 1\n"_str.as_bytes()).is_ok());

    auto changed = lito::build(request);
    ASSERT_TRUE(changed.is_ok());
    EXPECT_EQ(changed->frontend.persistent_scan_include_lookup, usize(1));
    EXPECT_EQ(changed->frontend.analyze_builds, usize(1));
}

TEST_F(ScanCache, EmbeddedResourcesInvalidateLookupAndContentCaches) {
    constexpr ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "fixture-embed-cache"
version = "0.1.0"
standard = "c++20"

[[bin]]
link-stdlib = false
name = "fixture-embed-cache"
sources = ["src/main.cpp"]
)toml"_str },
        { "src/asset.txt"_str, "abcd"_str },
        { "src/empty.txt"_str, ""_str },
        { "staged/asset.txt"_str, "wxyz"_str },
        { "staged/optional.txt"_str, "optional"_str },
        { "src/main.cpp"_str, R"cpp(#if __has_embed("empty.txt") != __STDC_EMBED_EMPTY__
#error empty resource probe returned the wrong state
#endif

constexpr unsigned char embedded[] = {
#embed "asset.txt" limit(2) clang::offset(1) prefix(7,) suffix(,9)
};

#if __has_embed("optional.txt") == __STDC_EMBED_FOUND__
constexpr unsigned char optional[] = {
#embed "optional.txt" limit(1)
};
#endif

static_assert(sizeof(embedded) == 4);
static_assert(embedded[0] == 7 && embedded[3] == 9);

auto main() -> int {
    return 0;
}
)cpp"_str },
    };
    auto project = materialize("embed-cache"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto output  = build_root("embed-cache"_str);
    auto request = build_request(
        project->root.as_path(), output.as_path(), strings("fixture-embed-cache"_str));

    auto cold = lito::build(request);
    if (cold.is_err()) {
        auto message = error_chain_text(cold.unwrap_err());
        rstd::test::fail_current(message.as_str(), __FILE__, __LINE__, true);
        return;
    }
    EXPECT_EQ(cold->compiled, usize(1));

    auto warm = lito::build(request);
    ASSERT_TRUE(warm.is_ok());
    EXPECT_EQ(warm->compiled, usize {});
    EXPECT_EQ(warm->frontend.persistent_scan_hits, usize(1));

    auto staged_optional = project->root.join(PathBuf::from("staged/optional.txt"_str).as_path());
    auto optional        = project->root.join(PathBuf::from("src/optional.txt"_str).as_path());
    ASSERT_TRUE(rstd::fs::copy(staged_optional.as_path(), optional.as_path()).is_ok());
    auto found = lito::build(request);
    ASSERT_TRUE(found.is_ok());
    EXPECT_EQ(found->frontend.persistent_scan_embed_lookup, usize(1));
    EXPECT_EQ(found->compiled, usize(1));

    auto staged_asset = project->root.join(PathBuf::from("staged/asset.txt"_str).as_path());
    auto asset        = project->root.join(PathBuf::from("src/asset.txt"_str).as_path());
    ASSERT_TRUE(rstd::fs::copy(staged_asset.as_path(), asset.as_path()).is_ok());
    auto changed = lito::build(request);
    ASSERT_TRUE(changed.is_ok());
    EXPECT_EQ(changed->frontend.persistent_scan_file_dependency, usize(1));
    EXPECT_EQ(changed->compiled, usize(1));
}

TEST_F(ScanCache, PackageMetadataInvalidatesOnlyMaterializedInputs) {
    auto              manifest = package_metadata_manifest("0.1.0"_str);
    const ProjectFile files[]  = {
        { "lito.toml"_str, manifest.as_str() },
        { "src/main.cpp"_str,
          "auto package_version() -> const char*;\n"
          "auto package_feature() -> int;\n"
          "auto unrelated_value() -> int;\n"
          "auto main() -> int {\n"
          "    (void)package_version();\n"
          "    (void)unrelated_value();\n"
          "    return package_feature();\n"
          "}\n"_str },
        { "src/version.cpp"_str,
          "auto package_version() -> const char* { return LITO_PKG_VERSION; }\n"_str },
        { "src/feature.cpp"_str,
          "auto package_feature() -> int {\n"
          "#ifdef LITO_FEAT_OPTIONAL\n"
          "    return LITO_FEAT_OPTIONAL;\n"
          "#else\n"
          "    return 0;\n"
          "#endif\n"
          "}\n"_str },
        { "src/unused.cpp"_str, "auto unrelated_value() -> int { return 7; }\n"_str },
    };
    auto project = materialize("package-metadata-cache"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto output  = build_root("package-metadata-cache"_str);
    auto request = build_request(
        project->root.as_path(), output.as_path(), strings("fixture-package-metadata-cache"_str));

    auto cold = lito::build(request);
    ASSERT_TRUE(cold.is_ok());
    EXPECT_EQ(cold->compiled, usize(4));
    auto cold_executable = executable(*cold);
    ASSERT_TRUE(cold_executable.is_some());
    auto cold_status = rstd::process::Command::make((*cold_executable).as_os_str()).status();
    ASSERT_TRUE(cold_status.is_ok());
    EXPECT_TRUE(cold_status->success());

    auto warm = lito::build(request);
    ASSERT_TRUE(warm.is_ok());
    EXPECT_EQ(warm->compiled, usize {});
    EXPECT_EQ(warm->frontend.persistent_scan_hits, usize(4));

    auto changed_manifest = package_metadata_manifest("0.2.0"_str);
    auto manifest_path    = project->root.join(PathBuf::from("lito.toml"_str).as_path());
    ASSERT_TRUE(
        rstd::fs::write(manifest_path.as_path(), changed_manifest.as_str().as_bytes()).is_ok());
    auto changed_version = lito::build(request);
    ASSERT_TRUE(changed_version.is_ok());
    EXPECT_EQ(changed_version->compiled, usize(1));
    EXPECT_EQ(changed_version->frontend.persistent_scan_external_macro, usize(1));
    EXPECT_EQ(changed_version->frontend.persistent_scan_hits, usize(3));

    request.selection.features.enabled.push(String::make("optional"_str));
    auto changed_feature = lito::build(request);
    ASSERT_TRUE(changed_feature.is_ok());
    EXPECT_EQ(changed_feature->compiled, usize(1));
    EXPECT_EQ(changed_feature->frontend.persistent_scan_external_macro, usize(1));
    EXPECT_EQ(changed_feature->frontend.persistent_scan_hits, usize(3));
    auto enabled_executable = executable(*changed_feature);
    ASSERT_TRUE(enabled_executable.is_some());
    auto enabled_status = rstd::process::Command::make((*enabled_executable).as_os_str()).status();
    ASSERT_TRUE(enabled_status.is_ok());
    ASSERT_TRUE(enabled_status->code().is_some());
    EXPECT_EQ(*enabled_status->code(), i32(1));

    request.selection.features.enabled.push(String::make("unused"_str));
    auto changed_unused = lito::build(request);
    ASSERT_TRUE(changed_unused.is_ok());
    EXPECT_EQ(changed_unused->compiled, usize {});
    EXPECT_EQ(changed_unused->frontend.persistent_scan_hits, usize(4));
}
