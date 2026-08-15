#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.core;
import lito.cpp;
import lito.system;
import lito.toolchain.cmake;
import lito.toolchain;
import lito.driver;
import lito.test.support;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;
using namespace lito_test;
using PathBuf = rstd::path::PathBuf;

TEST(SourceDiscovery, InvalidExplicitSourcesAreRejectedByDiscoveryOwner) {
    for (const auto path : INVALID_EXPLICIT_SOURCES) {
        auto loaded = lito::load_package_manifest(fixture_path(path).as_path());
        ASSERT_TRUE(loaded.is_ok());
        ASSERT_EQ(loaded->targets.len(), usize(1));
        auto target = lito::cpp::ResolvedTarget {
            .source      = rstd::move(lito::package_target_source(loaded->targets[usize {}])),
            .source_root = rstd::move(loaded->source_root),
        };
        auto discovered = lito::discover_explicit_sources(target);
        if (discovered.is_ok()) rstd::io::eprintln("unexpected valid sources: {}", path);
        EXPECT_TRUE(discovered.is_err());
    }
}
