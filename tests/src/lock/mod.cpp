#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito;
import lito.lock;
import lito.package;
import lito.package.graph_contract;
import lito.workspace.contract;
import lito.workspace.resolver;
import lito.platform;
import lito.dependency;
import lito.dependency.cmake;
import lito.source;
import lito.manifest;
import lito.toolchain;
import lito.build.discovery;
import lito.build.layout;
import lito.system.environment;
import lito.system.process;
import lito.system.storage;
import lito.test.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito_test;
using PathBuf = rstd::path::PathBuf;

TEST(Lock, OnlyCurrentLockVersionIsAcceptedByLockStore) {
    EXPECT_TRUE(locked_graph_is_current("lock/default-update"_str));
    for (const auto path : INVALID_LOCKS) {
        auto current = locked_graph_is_current(path);
        if (current) rstd::io::eprintln("unexpected current lock: {}", path);
        EXPECT_FALSE(current);
    }
    auto old_version =
        lito::load_lock_session(fixture_path("lock/old-version"_str).as_path(), false);
    ASSERT_TRUE(old_version.is_err());
    auto version_error = rstd::move(old_version).unwrap_err();
    ASSERT_TRUE(version_error.is_Schema());
    EXPECT_TRUE(version_error.as_Schema().message.as_str().contains("integer 8"_str));
}

TEST(Lock, OldLockRequiresUpdateMode) {
    auto directory = fixture_path("lock/old-version"_str);
    auto migration = lito::load_lock_session(directory.as_path(), false);
    ASSERT_TRUE(migration.is_err());
    auto update = lito::load_lock_session(
        directory.as_path(), lito::LockConfig {}, false, lito::GitResolutionMode::Refresh);
    ASSERT_TRUE(update.is_ok());
    auto locked = lito::load_lock_session(directory.as_path(), true);
    ASSERT_TRUE(locked.is_err());
    auto error = rstd::move(locked).unwrap_err();
    ASSERT_TRUE(error.is_Schema());
    EXPECT_TRUE(error.as_Schema().message.as_str().contains("integer 8"_str));
}
