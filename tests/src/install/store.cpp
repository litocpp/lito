#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.driver;
import lito.core;
import lito.system;
import lito.tools.cmake;
import lito.toolchain;
import lito.test.support;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;
using namespace lito_test;
using PathBuf = rstd::path::PathBuf;

TEST(Install, InstallTransactionKeepsPrimaryAndRollbackFailuresDistinct) {
    auto rollback = Vec<lito::InstallRollbackFailure>::make();
    rollback.push(lito::InstallRollbackFailure {
        .operation = String::make("restore binary"_str),
        .path      = PathBuf::from("/tmp/lito-tool"_str),
        .source    = rstd::io::error::Error::from_raw_os_error(i32(5)),
    });
    auto error = lito::InstallStoreError::Transaction(
        String::make("install publish"_str),
        Box<lito::InstallStoreError>::make(lito::InstallStoreError::Cause(
            lito::InstallStoreCause::Message(String::make("primary failure"_str)))),
        rstd::move(rollback));

    ASSERT_TRUE(error.is_Transaction());
    ASSERT_EQ(error.as_Transaction().rollback_failures.len(), usize(1));
    EXPECT_EQ(error.as_Transaction().rollback_failures[usize {}].operation.as_str(),
              "restore binary"_str);
    auto source = as<rstd::error::Error>(error).source();
    ASSERT_TRUE(source.is_some());
    EXPECT_TRUE(rstd::error::is<lito::InstallStoreError>(*source));
    EXPECT_TRUE(rstd::format("{}", error).as_str().contains("rollback cannot restore binary"_str));
}
