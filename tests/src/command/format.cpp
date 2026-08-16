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

class FormatCommand : public ProjectFixture {};

TEST_F(FormatCommand, FormatCheckReportsWithoutChangingSources) {
    auto tree = environment_tool_project_tree();
    ASSERT_TRUE(tree.is_ok());
    auto materialized = materialize("format-check"_str, *tree);
    ASSERT_TRUE(materialized.is_ok());
    auto fixture = materialized->root.join(PathBuf::from("append-path"_str).as_path());

    auto           source      = fixture.join(PathBuf::from("src/main.cpp"_str).as_path());
    constexpr auto unformatted = "auto main()->int{return 0;}\n"_str;
    ASSERT_TRUE(rstd::fs::write(source.as_path(), unformatted.as_bytes()).is_ok());

    auto checked_project = lito::load_project_config(fixture.as_path());
    ASSERT_TRUE(checked_project.is_ok());
    auto checked = lito::format(lito::FormatRequest {
        .selection   = lito::PackageSelection { .root = fixture.clone() },
        .environment = rstd::move(checked_project->environment),
        .toolchain   = rstd::move(checked_project->toolchain),
        .sources     = rstd::move(checked_project->sources),
        .mode        = lito::FormatMode::Check,
    });
    ASSERT_TRUE(checked.is_ok());
    EXPECT_EQ(checked->packages, usize(1));
    EXPECT_EQ(checked->files, usize(1));
    ASSERT_EQ(checked->unformatted_files.len(), usize(1));
    EXPECT_EQ(checked->unformatted_files[usize {}].as_path(), source.as_path());
    EXPECT_FALSE(checked->success());
    auto unchanged = rstd::fs::read_to_string(source.as_path());
    ASSERT_TRUE(unchanged.is_ok());
    EXPECT_EQ(unchanged->as_str(), unformatted);

    auto format_project = lito::load_project_config(fixture.as_path());
    ASSERT_TRUE(format_project.is_ok());
    auto formatted = lito::format(lito::FormatRequest {
        .selection   = lito::PackageSelection { .root = fixture.clone() },
        .environment = rstd::move(format_project->environment),
        .toolchain   = rstd::move(format_project->toolchain),
        .sources     = rstd::move(format_project->sources),
    });
    ASSERT_TRUE(formatted.is_ok());
    EXPECT_TRUE(formatted->success());

    auto clean_project = lito::load_project_config(fixture.as_path());
    ASSERT_TRUE(clean_project.is_ok());
    auto clean = lito::format(lito::FormatRequest {
        .selection   = lito::PackageSelection { .root = fixture.clone() },
        .environment = rstd::move(clean_project->environment),
        .toolchain   = rstd::move(clean_project->toolchain),
        .sources     = rstd::move(clean_project->sources),
        .mode        = lito::FormatMode::Check,
    });
    ASSERT_TRUE(clean.is_ok());
    EXPECT_TRUE(clean->success());
}
