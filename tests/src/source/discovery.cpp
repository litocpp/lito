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

class SourceDiscovery : public ProjectFixture {};

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
        auto tree     = lito::SourceTree::make();
        ASSERT_TRUE(tree.add_text("lito.toml"_str, manifest.as_str()).is_ok());
        if (! item.extra_path.is_empty()) {
            ASSERT_TRUE(tree.add_text(item.extra_path, item.extra_contents).is_ok());
        }
        auto project = materialize(item.name, tree);
        ASSERT_TRUE(project.is_ok());
        auto loaded = lito::load_package_manifest(project->root.as_path());
        ASSERT_TRUE(loaded.is_ok());
        ASSERT_EQ(loaded->targets.len(), usize(1));
        auto target = lito::cpp::ResolvedTarget {
            .source      = rstd::move(lito::package_target_source(loaded->targets[usize {}])),
            .source_root = rstd::move(loaded->source_root),
        };
        auto discovered = lito::discover_explicit_sources(target);
        EXPECT_TRUE(discovered.is_err());
    }
}
