#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.core;
import lito.cpp;
import lito.driver;
import lito.test.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito_test;
using PathBuf = rstd::path::PathBuf;

class SourceDiscovery : public ProjectFixture {};

auto discovery_sources(ref<str> source) -> Vec<PathBuf> {
    auto result = Vec<PathBuf>::make();
    result.push(PathBuf::from(source));
    return result;
}

TEST_F(SourceDiscovery, BuildSummaryProjectsResolvedExternalSourceProvenance) {
    const ProjectFile files[] = {
        { "lito.toml"_str,
          R"toml([package]
name = "fixture-external-source-provenance"
version = "0.1.0"
standard = "c17"

[external-sources.upstream]
path = "upstream"

[source-groups.runtime]
external-source = "upstream"
sources = ["value.c"]

[lib]
name = "fixture-external-source-provenance"
archive = "fixture-external-source-provenance"
source-groups = ["runtime"]
)toml"_str },
        { "upstream/value.c"_str, "int fixture_value(void) { return 7; }\n"_str },
    };
    auto project = materialize("external-source-provenance"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto built = lito::build(build_request(project->root.as_path(),
                                           build_root("external-source-provenance"_str).as_path(),
                                           Vec<String>::make(),
                                           build_profile("release"_str)));
    ASSERT_TRUE(built.is_ok());
    ASSERT_EQ(built->external_source_provenance.len(), usize(1));
    const auto& source = built->external_source_provenance[usize {}];
    EXPECT_EQ(source.package.as_str(), "fixture-external-source-provenance"_str);
    EXPECT_EQ(source.name.as_str(), "upstream"_str);
    auto source_name = source.materialized_root.as_path().file_name();
    ASSERT_TRUE(source_name.is_some());
    auto source_name_text = source_name->to_str();
    ASSERT_TRUE(source_name_text.is_some());
    EXPECT_EQ(*source_name_text, "upstream"_str);
    EXPECT_TRUE(
        source.stable_source_identity.as_str().starts_with("lito-package-external-v1\n"_str));
}

TEST_F(SourceDiscovery, InvalidExplicitSourcesAreRejectedByDiscoveryOwner) {
    struct ExplicitSourceCase {
        ref<str> name;
        ref<str> source;
        ref<str> extra_path;
        ref<str> extra_contents;
    };
    constexpr ExplicitSourceCase cases[] = {
        { "duplicate"_str,
          "[\"source.cppm\", \"source.cppm\"]"_str,
          "source.cppm"_str,
          "export module fixture.duplicate;\n"_str },
        { "missing"_str, "[\"missing.cppm\"]"_str, ""_str, ""_str },
        { "outside_root"_str, "[\"../outside.cppm\"]"_str, ""_str, ""_str },
        { "unsupported"_str, "[\"source.hpp\"]"_str, "source.hpp"_str, "#pragma once\n"_str },
    };
    for (const auto& item : cases) {
        SCOPED_TRACE(item.name);
        auto manifest = rstd::format(R"toml([package]
name = "fixture-{}"
version = "0.1.0"

[lib]
name = "fixture-{}"
module = "fixture.{}"
archive = "fixture.{}"
sources = {}
)toml",
                                     item.name,
                                     item.name,
                                     item.name,
                                     item.name,
                                     item.source);
        auto tree     = lito::source::SourceTree::make();
        ASSERT_TRUE(tree.add_text("lito.toml"_str, manifest.as_str()).is_ok());
        if (! item.extra_path.is_empty()) {
            ASSERT_TRUE(tree.add_text(item.extra_path, item.extra_contents).is_ok());
        }
        auto project = materialize(item.name, tree);
        ASSERT_TRUE(project.is_ok());
        auto loaded = lito::manifest::load_package_manifest(project->root.as_path());
        ASSERT_TRUE(loaded.is_ok());
        ASSERT_EQ(loaded->targets.len(), usize(1));
        auto target = lito::cpp::ResolvedTarget {
            .source = rstd::move(lito::manifest::package_target_source(loaded->targets[usize {}])),
            .source_root = rstd::move(loaded->source_root),
        };
        auto discovered = lito::discover_explicit_sources(target);
        EXPECT_TRUE(discovered.is_err());
    }
}

TEST_F(SourceDiscovery, SourceGroupsRetainOriginsAcrossMultipleRoots) {
    const ProjectFile files[] = {
        { "local.c"_str, "int local_value(void) { return 1; }\n"_str },
        { "vendor/vendor.c"_str, "int vendor_value(void) { return 2; }\n"_str },
    };
    auto project = materialize("source-group-origins"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto groups = Vec<lito::cpp::ResolvedSourceGroup>::make();
    groups.push(lito::cpp::ResolvedSourceGroup {
        .name     = String::make("local"_str),
        .root     = project->root.clone(),
        .identity = String::make("package:fixture"_str),
        .sources  = discovery_sources("local.c"_str),
    });
    groups.push(lito::cpp::ResolvedSourceGroup {
        .name     = String::make("vendor"_str),
        .root     = project->root.join(PathBuf::from("vendor"_str).as_path()),
        .identity = String::make("archive:vendor"_str),
        .sources  = discovery_sources("vendor.c"_str),
        .external = true,
    });
    auto target = lito::cpp::ResolvedTarget {
        .id =
            lito::package::PackageTargetId {
                .package = String::make("fixture-source-groups"_str),
                .kind    = lito::package::PackageTargetKind::Library,
                .name    = String::make("fixture-source-groups"_str),
            },
        .language      = lito::manifest::PackageLanguage::C,
        .source_groups = rstd::move(groups),
        .root          = project->root.clone(),
        .source_root   = project->root.clone(),
    };

    auto discovered = lito::discover_explicit_sources(target);
    ASSERT_TRUE(discovered.is_ok());
    ASSERT_EQ(discovered->sources.len(), usize(2));
    EXPECT_EQ(discovered->sources[usize {}].origin_identity.as_str(), "package:fixture"_str);
    EXPECT_FALSE(discovered->sources[usize {}].external);
    EXPECT_EQ(discovered->sources[usize(1)].origin_identity.as_str(), "archive:vendor"_str);
    EXPECT_TRUE(discovered->sources[usize(1)].external);
}

TEST_F(SourceDiscovery, DuplicatePhysicalSourceNamesBothSourceGroups) {
    const ProjectFile files[] = {
        { "shared.c"_str, "int shared_value(void) { return 1; }\n"_str },
    };
    auto project = materialize("source-group-duplicate"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto               groups        = Vec<lito::cpp::ResolvedSourceGroup>::make();
    constexpr ref<str> group_names[] = { "first"_str, "second"_str };
    for (auto name : group_names) {
        groups.push(lito::cpp::ResolvedSourceGroup {
            .name     = String::make(name),
            .root     = project->root.clone(),
            .identity = String::make("package:fixture"_str),
            .sources  = discovery_sources("shared.c"_str),
        });
    }
    auto target = lito::cpp::ResolvedTarget {
        .id =
            lito::package::PackageTargetId {
                .package = String::make("fixture-source-groups"_str),
                .kind    = lito::package::PackageTargetKind::Library,
                .name    = String::make("fixture-source-groups"_str),
            },
        .language      = lito::manifest::PackageLanguage::C,
        .source_groups = rstd::move(groups),
        .root          = project->root.clone(),
        .source_root   = project->root.clone(),
    };

    auto discovered = lito::discover_explicit_sources(target);
    ASSERT_TRUE(discovered.is_err());
    auto message = rstd::format("{}", rstd::move(discovered).unwrap_err());
    EXPECT_TRUE(message.as_str().contains("first"_str));
    EXPECT_TRUE(message.as_str().contains("second"_str));
}
