#include <rstd/test/gtest.hpp>
#include <atomic>

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

TEST(FormatExecution, BoundsWorkersAndFallsBack) {
    EXPECT_EQ(lito::format_execution::worker_count(usize(20), Some(usize(64))), usize(4));
    EXPECT_EQ(lito::format_execution::worker_count(usize(2), Some(usize(64))), usize(2));
    EXPECT_EQ(lito::format_execution::worker_count(usize(20), None()), usize(1));
    EXPECT_EQ(lito::format_execution::worker_count(usize(20), Some(usize(1))), usize(1));
    EXPECT_EQ(lito::format_execution::worker_count(usize {}, Some(usize(64))), usize {});
    auto empty =
        lito::format_execution::run(usize {}, usize(4), [](usize) -> lito::CommandResult<bool> {
            ADD_FAILURE();
            return Ok(true);
        });
    ASSERT_TRUE(empty.is_ok());
    EXPECT_TRUE(empty->is_empty());
}

TEST(FormatExecution, OverlapsBoundedWorkAndKeepsResultOrder) {
    std::atomic<unsigned> entered { 0 };
    std::atomic<unsigned> active { 0 };
    std::atomic<unsigned> maximum { 0 };
    auto                  result = lito::format_execution::run(
        usize(12), usize(64), [&](usize index) -> lito::CommandResult<bool> {
            auto current  = active.fetch_add(1) + 1;
            auto previous = maximum.load();
            while (previous < current && ! maximum.compare_exchange_weak(previous, current)) {
            }
            if (index < usize(4)) {
                entered.fetch_add(1);
                auto started = rstd::time::Instant::now();
                while (entered.load() < 4) {
                    if (started.elapsed() > rstd::time::Duration::from_secs(rstd::u64(30))) {
                        active.fetch_sub(1);
                        return Err(
                            lito::CommandError::Message("concurrency barrier timed out"_Str));
                    }
                    rstd::thread::yield_now();
                }
            }
            active.fetch_sub(1);
            return Ok(index % usize(2) == usize {});
        });
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(maximum.load(), 4u);
    EXPECT_EQ(active.load(), 0u);
    ASSERT_EQ(result->len(), usize(12));
    for (usize index {}; index < result->len(); ++index) {
        EXPECT_EQ((*result)[index], index % usize(2) == usize {});
    }
}

TEST(FormatExecution, DrainsSubmittedTasksAndChoosesEarliestError) {
    std::atomic<unsigned> entered { 0 };
    std::atomic<unsigned> finished { 0 };
    std::atomic<bool>     later_failed { false };
    auto                  result = lito::format_execution::run(
        usize(20), usize(2), [&](usize index) -> lito::CommandResult<bool> {
            entered.fetch_add(1);
            if (index == usize {}) {
                auto started = rstd::time::Instant::now();
                while (! later_failed.load()) {
                    if (started.elapsed() > rstd::time::Duration::from_secs(rstd::u64(30))) {
                        return Err(lito::CommandError::Message("error barrier timed out"_Str));
                    }
                    rstd::thread::yield_now();
                }
            } else {
                later_failed.store(true);
            }
            finished.fetch_add(1);
            return Err(lito::CommandError::Message(rstd::format("failure {}", index)));
        });
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(entered.load(), 2u);
    EXPECT_EQ(finished.load(), 2u);
    EXPECT_EQ(rstd::format("{}", result.unwrap_err()).as_str(), "failure 0"_str);
}

TEST(FormatExecution, SingleWorkerStopsAtFailure) {
    auto calls  = usize {};
    auto result = lito::format_execution::run(
        usize(8), usize(1), [&](usize index) -> lito::CommandResult<bool> {
            ++calls;
            if (index == usize(2)) return Err(lito::CommandError::Message("failed"_Str));
            return Ok(false);
        });
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(calls, usize(3));
}

TEST(FormatExecution, ReordersCompletionsByInputIndex) {
    std::atomic<bool> replenished { false };
    auto              result = lito::format_execution::run(
        usize(3), usize(2), [&](usize index) -> lito::CommandResult<bool> {
            if (index == usize {}) {
                auto started = rstd::time::Instant::now();
                while (! replenished.load()) {
                    if (started.elapsed() > rstd::time::Duration::from_secs(rstd::u64(30))) {
                        return Err(lito::CommandError::Message("completion barrier timed out"_Str));
                    }
                    rstd::thread::yield_now();
                }
            }
            if (index == usize(2)) replenished.store(true);
            return Ok(index != usize(1));
        });
    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(result->len(), usize(3));
    EXPECT_TRUE((*result)[usize {}]);
    EXPECT_FALSE((*result)[usize(1)]);
    EXPECT_TRUE((*result)[usize(2)]);
}

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

    auto test_manifest = tests.join(PathBuf::from("lito.toml"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(test_manifest.as_path(), R"toml([package]
name = "fixture-format-tests"
version = "0.1.0"
source-root = ".."

[[test]]
link-stdlib = false
name = "fixture-format-tests"
sources = ["src/main.cpp", "tests/main.cpp"]
)toml"_str.as_bytes())
                    .is_ok());
    ASSERT_TRUE(rstd::fs::write(source.as_path(), unformatted.as_bytes()).is_ok());
    auto duplicate_config = lito::config::load_project_config(fixture.as_path());
    ASSERT_TRUE(duplicate_config.is_ok());
    auto duplicate = lito::format(lito::FormatRequest {
        .root        = fixture.clone(),
        .environment = rstd::move(duplicate_config->environment),
        .tools       = rstd::move(duplicate_config->tools),
        .mode        = lito::FormatMode::Check,
    });
    if (duplicate.is_err()) rstd::io::eprintln("{}", error_chain_text(duplicate.unwrap_err()));
    ASSERT_TRUE(duplicate.is_ok());
    EXPECT_EQ(duplicate->packages, usize(2));
    EXPECT_EQ(duplicate->files, usize(2));
    ASSERT_EQ(duplicate->unformatted_files.len(), usize(1));
    EXPECT_EQ(duplicate->unformatted_files[usize {}].as_path(), source.as_path());

    ASSERT_TRUE(rstd::fs::remove_file(tests.join(PathBuf::from("main.cpp"_str).as_path()).as_path())
                    .is_ok());
    auto missing_config = lito::config::load_project_config(fixture.as_path());
    ASSERT_TRUE(missing_config.is_ok());
    auto missing = lito::format(lito::FormatRequest {
        .root        = fixture.clone(),
        .environment = rstd::move(missing_config->environment),
        .tools       = rstd::move(missing_config->tools),
    });
    EXPECT_TRUE(missing.is_err());
    auto not_written = rstd::fs::read_to_string(source.as_path());
    ASSERT_TRUE(not_written.is_ok());
    EXPECT_EQ(not_written->as_str(), unformatted);
}
