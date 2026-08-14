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

TEST(Update, DependencyUpdateOwnsExplicitLockRefresh) {
    auto updated = lito::update_dependencies(lito::UpdateRequest {
        .root = fixture_path("lock/default-update"_str),
    });
    ASSERT_TRUE(updated.is_ok());
    EXPECT_EQ(*updated, lito::LockStatus::Unchanged);
}

TEST(Update, DependencyUpdateWritesConfiguredLocalLock) {
    auto source    = fixture_path("config/lock-local"_str);
    auto directory = output_root("config-lock-local"_str);
    ASSERT_TRUE(clear_output(directory.as_path()));
    ASSERT_TRUE(copy_directory(source.as_path(), directory.as_path()));

    auto config = lito::load_project_config(directory.as_path());
    ASSERT_TRUE(config.is_ok());
    auto configured_lock = config->lock.path.clone();
    auto updated         = lito::update_dependencies(lito::UpdateRequest {
        .root = config->root.clone(),
        .lock = rstd::move(config->lock),
    });
    ASSERT_TRUE(updated.is_ok());
    EXPECT_EQ(*updated, lito::LockStatus::Updated);
    auto local_exists = rstd::fs::exists(configured_lock.as_path());
    ASSERT_TRUE(local_exists.is_ok());
    EXPECT_TRUE(*local_exists);
    auto root_lock   = directory.join(rstd::path::PathBuf::from("lito.lock"_str).as_path());
    auto root_exists = rstd::fs::exists(root_lock.as_path());
    ASSERT_TRUE(root_exists.is_ok());
    EXPECT_FALSE(*root_exists);

    auto defaults = lito::load_project_config(directory.as_path(), lito::ConfigLoadMode::Disabled);
    ASSERT_TRUE(defaults.is_ok());
    auto repository_updated = lito::update_dependencies(lito::UpdateRequest {
        .root = defaults->root.clone(),
        .lock = rstd::move(defaults->lock),
    });
    ASSERT_TRUE(repository_updated.is_ok());
    EXPECT_EQ(*repository_updated, lito::LockStatus::Updated);
    root_exists = rstd::fs::exists(root_lock.as_path());
    ASSERT_TRUE(root_exists.is_ok());
    EXPECT_TRUE(*root_exists);
    EXPECT_TRUE(clear_output(directory.as_path()));
}
