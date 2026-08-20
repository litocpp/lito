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

TEST_F(FormatCommand, FormatsLocalProjectWithoutResolvingDependencies) {
    auto tree = environment_tool_project_tree();
    ASSERT_TRUE(tree.is_ok());
    auto materialized = materialize("format-check"_str, *tree);
    ASSERT_TRUE(materialized.is_ok());
    ASSERT_TRUE(prepare_environment_tool_project(materialized->root.as_path()));
    auto fixture = materialized->root.join(PathBuf::from("append-path"_str).as_path());

    auto           source      = fixture.join(PathBuf::from("src/main.cpp"_str).as_path());
    constexpr auto unformatted = "auto main()->int{return 0;}\n"_str;
    ASSERT_TRUE(rstd::fs::write(source.as_path(), unformatted.as_bytes()).is_ok());
    auto manifest      = fixture.join(PathBuf::from("lito.toml"_str).as_path());
    auto manifest_text = rstd::fs::read_to_string(manifest.as_path());
    ASSERT_TRUE(manifest_text.is_ok());
    manifest_text->push_str(R"toml(
[dependencies.fixture-format-remote]
git = "https://example.invalid/fixture-format-remote.git"
visibility = "private"
)toml"_str);
    ASSERT_TRUE(rstd::fs::write(manifest.as_path(), manifest_text->as_str().as_bytes()).is_ok());

    auto tests = fixture.join(PathBuf::from("tests"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir(tests.as_path()).is_ok());
    ASSERT_TRUE(rstd::fs::write(tests.join(PathBuf::from("lito.toml"_str).as_path()).as_path(),
                                R"toml([package]
name = "fixture-format-tests"
version = "0.1.0"

[[test]]
link-stdlib = false
name = "fixture-format-tests"
sources = ["main.cpp"]
)toml"_str.as_bytes())
                    .is_ok());
    ASSERT_TRUE(rstd::fs::write(tests.join(PathBuf::from("main.cpp"_str).as_path()).as_path(),
                                "auto main() -> int { return 0; }\n"_str.as_bytes())
                    .is_ok());

    auto checked_project = lito::config::load_project_config(fixture.as_path());
    ASSERT_TRUE(checked_project.is_ok());
    checked_project->tools.git = PathBuf::from("lito-format-missing-git"_str);
    auto checked               = lito::format(lito::FormatRequest {
        .root        = fixture.clone(),
        .environment = rstd::move(checked_project->environment),
        .tools       = rstd::move(checked_project->tools),
        .mode        = lito::FormatMode::Check,
    });
    if (checked.is_err()) rstd::io::eprintln("{}", error_chain_text(checked.unwrap_err()));
    ASSERT_TRUE(checked.is_ok());
    EXPECT_EQ(checked->packages, usize(2));
    EXPECT_EQ(checked->files, usize(2));
    ASSERT_EQ(checked->unformatted_files.len(), usize(1));
    EXPECT_EQ(checked->unformatted_files[usize {}].as_path(), source.as_path());
    EXPECT_FALSE(checked->success());
    auto unchanged = rstd::fs::read_to_string(source.as_path());
    ASSERT_TRUE(unchanged.is_ok());
    EXPECT_EQ(unchanged->as_str(), unformatted);

    auto format_project = lito::config::load_project_config(fixture.as_path());
    ASSERT_TRUE(format_project.is_ok());
    format_project->tools.git = PathBuf::from("lito-format-missing-git"_str);
    auto formatted            = lito::format(lito::FormatRequest {
        .root        = fixture.clone(),
        .environment = rstd::move(format_project->environment),
        .tools       = rstd::move(format_project->tools),
    });
    ASSERT_TRUE(formatted.is_ok());
    EXPECT_TRUE(formatted->success());

    auto clean_project = lito::config::load_project_config(fixture.as_path());
    ASSERT_TRUE(clean_project.is_ok());
    clean_project->tools.git = PathBuf::from("lito-format-missing-git"_str);
    auto clean               = lito::format(lito::FormatRequest {
        .root        = fixture.clone(),
        .environment = rstd::move(clean_project->environment),
        .tools       = rstd::move(clean_project->tools),
        .mode        = lito::FormatMode::Check,
    });
    ASSERT_TRUE(clean.is_ok());
    EXPECT_TRUE(clean->success());
}
