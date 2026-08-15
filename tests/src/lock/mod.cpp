#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.core;
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

TEST(Lock, OnlyCurrentLockVersionIsAcceptedByLockStore) {
    EXPECT_TRUE(locked_graph_is_current("lock/default-update"_str));
    for (const auto path : INVALID_LOCKS) {
        auto current = locked_graph_is_current(path);
        if (current) rstd::io::eprintln("unexpected current lock: {}", path);
        EXPECT_FALSE(current);
    }
    auto future_version =
        lito::load_lock_session(fixture_path("lock/future-version"_str).as_path(), false);
    ASSERT_TRUE(future_version.is_err());
    auto version_error = rstd::move(future_version).unwrap_err();
    ASSERT_TRUE(version_error.is_Schema());
    EXPECT_TRUE(version_error.as_Schema().message.as_str().contains("supports version 1"_str));
}

TEST(Lock, FutureLockCannotBeDowngradedByUpdate) {
    auto directory = fixture_path("lock/future-version"_str);
    auto loading   = lito::load_lock_session(directory.as_path(), false);
    ASSERT_TRUE(loading.is_err());
    auto update = lito::load_lock_session(
        directory.as_path(), lito::LockConfig {}, false, lito::GitResolutionMode::Refresh);
    ASSERT_TRUE(update.is_err());
    auto locked = lito::load_lock_session(directory.as_path(), true);
    ASSERT_TRUE(locked.is_err());
    auto loading_error = rstd::move(loading).unwrap_err();
    ASSERT_TRUE(loading_error.is_Schema());
    EXPECT_TRUE(loading_error.as_Schema().message.as_str().contains("supports version 1"_str));
    auto update_error = rstd::move(update).unwrap_err();
    ASSERT_TRUE(update_error.is_Schema());
    EXPECT_TRUE(update_error.as_Schema().message.as_str().contains("supports version 1"_str));
    auto locked_error = rstd::move(locked).unwrap_err();
    ASSERT_TRUE(locked_error.is_Schema());
    EXPECT_TRUE(locked_error.as_Schema().message.as_str().contains("supports version 1"_str));
}
