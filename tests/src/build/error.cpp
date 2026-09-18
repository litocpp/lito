#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.cpp;
import lito.driver;
import lito.system;

using namespace rstd::prelude;
using namespace rstd::literals;

template<typename E>
void expect_owned_io(ref<str> expected = "cannot read product 'product.json'"_str) {
    auto error = [] {
        auto operation = "read product"_Str;
        auto path      = rstd::path::PathBuf::from("product.json"_str);
        return E::Io(operation.as_str().into(),
                     rstd::path::PathBuf::from(path.as_path()),
                     rstd::io::error::Error::from_raw_os_error(i32(5)));
    }();
    ASSERT_TRUE(error.is_Io());
    EXPECT_EQ(error.as_Io().operation.as_str(), "read product"_str);
    EXPECT_EQ(error.as_Io().path.as_path().to_string_lossy().as_str(), "product.json"_str);
    EXPECT_EQ(error.as_Io().source.raw_os_error().unwrap(), i32(5));
    EXPECT_TRUE(as<rstd::error::Error>(error).source().is_some());
    EXPECT_EQ(rstd::format("{}", error).as_str(), expected);
}

TEST(BuildErrorFactory, OwnsIoContextAndPreservesSource) {
    expect_owned_io<lito::BuildProductError>();
    expect_owned_io<lito::BuildLayoutError>();
    expect_owned_io<lito::BuildScriptError>();
    expect_owned_io<lito::cpp::SourceDiscoveryError>(
        "cannot read product source path 'product.json'"_str);
}

template<typename E>
void expect_system_io() {
    auto error =
        E::System(lito::system::SystemError::Io("read tool"_Str,
                                                rstd::path::PathBuf::from("tool"_str),
                                                rstd::io::error::Error::from_raw_os_error(i32(5))));
    ASSERT_TRUE(error.is_System());
    const auto& system = error.as_System().source;
    ASSERT_TRUE(system.is_Io());
    EXPECT_EQ(system.as_Io().operation.as_str(), "read tool"_str);
    EXPECT_EQ(system.as_Io().path.as_path().to_string_lossy().as_str(), "tool"_str);
    EXPECT_EQ(system.as_Io().source.raw_os_error().unwrap(), i32(5));
    EXPECT_TRUE(as<rstd::error::Error>(error).source().is_some());
    EXPECT_TRUE(as<rstd::error::Error>(system).source().is_some());
}

TEST(BuildErrorFactory, PreservesSystemWrapper) {
    expect_system_io<lito::HostBuildToolError>();
    expect_system_io<lito::ArtifactProcessorError>();
}

TEST(BuildErrorFactory, PreservesActionBranchesAndScriptWrapper) {
    auto path = rstd::path::PathBuf::from("output"_str);
    auto receipt =
        lito::BuildToolActionError::Receipt("read receipt"_Str,
                                            rstd::path::PathBuf::from(path.as_path()),
                                            rstd::io::error::Error::from_raw_os_error(i32(5)));
    ASSERT_TRUE(receipt.is_Receipt());
    EXPECT_EQ(receipt.as_Receipt().operation.as_str(), "read receipt"_str);
    EXPECT_EQ(receipt.as_Receipt().path.as_path().to_string_lossy().as_str(), "output"_str);
    EXPECT_EQ(receipt.as_Receipt().source.raw_os_error().unwrap(), i32(5));
    auto publication =
        lito::BuildToolActionError::Publication("publish output"_Str,
                                                rstd::path::PathBuf::from(path.as_path()),
                                                rstd::io::error::Error::from_raw_os_error(i32(5)));
    auto script = lito::BuildScriptError::BuildToolAction(rstd::move(publication));
    ASSERT_TRUE(script.is_BuildToolAction());
    const auto& action = script.as_BuildToolAction().source;
    ASSERT_TRUE(action.is_Publication());
    EXPECT_EQ(action.as_Publication().operation.as_str(), "publish output"_str);
    EXPECT_EQ(action.as_Publication().source.raw_os_error().unwrap(), i32(5));
    EXPECT_TRUE(as<rstd::error::Error>(script).source().is_some());
    EXPECT_TRUE(as<rstd::error::Error>(action).source().is_some());
}
