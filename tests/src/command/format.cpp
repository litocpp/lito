#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.driver;
import lito.core;
import lito.tools;
import lito.test.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito_test;
using rstd::sync::atomic::Atomic;
using PathBuf = rstd::path::PathBuf;

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
    Atomic<usize> entered {};
    Atomic<usize> active {};
    Atomic<usize> maximum {};
    auto          result = lito::format_execution::run(
        usize(12), usize(64), [&](usize index) -> lito::CommandResult<bool> {
            auto current  = active.fetch_add(usize(1)) + usize(1);
            auto previous = maximum.load();
            while (previous < current && ! maximum.compare_exchange_weak(previous, current)) {
            }
            if (index < usize(4)) {
                entered.fetch_add(usize(1));
                auto started = rstd::time::Instant::now();
                while (entered.load() < usize(4)) {
                    if (started.elapsed() > rstd::time::Duration::from_secs(rstd::u64(30))) {
                        active.fetch_sub(usize(1));
                        return Err(
                            lito::CommandError::Message("concurrency barrier timed out"_Str));
                    }
                    rstd::thread::yield_now();
                }
            }
            active.fetch_sub(usize(1));
            return Ok(index % usize(2) == usize {});
        });
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(maximum.load(), usize(4));
    EXPECT_EQ(active.load(), usize {});
    ASSERT_EQ(result->len(), usize(12));
    for (usize index {}; index < result->len(); ++index) {
        EXPECT_EQ((*result)[index], index % usize(2) == usize {});
    }
}

TEST(FormatExecution, DrainsSubmittedTasksAndChoosesEarliestError) {
    Atomic<usize> entered {};
    Atomic<usize> finished {};
    Atomic<bool>  later_failed { false };
    auto          result = lito::format_execution::run(
        usize(20), usize(2), [&](usize index) -> lito::CommandResult<bool> {
            entered.fetch_add(usize(1));
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
            finished.fetch_add(usize(1));
            return Err(lito::CommandError::Message(rstd::format("failure {}", index)));
        });
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(entered.load(), usize(2));
    EXPECT_EQ(finished.load(), usize(2));
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
    Atomic<bool> replenished { false };
    auto         result = lito::format_execution::run(
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

TEST_F(FormatCommand, BuiltinChecksAndWritesWithoutExternalTools) {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "builtin-format"
version = "0.1.0"
[[bin]]
name = "builtin-format"
sources = ["main.cpp"]
link-stdlib = false
[dependencies.unavailable]
git = "https://example.invalid/unavailable.git"
)toml"_str },
        { "main.cpp"_str, "auto main()->int{return 0;}\n"_str },
    };
    auto project = materialize("builtin-format"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto source   = project->root.join(PathBuf::from("main.cpp"_str).as_path());
    auto original = rstd::fs::read(source.as_path());
    ASSERT_TRUE(original.is_ok());
    auto request = lito::FormatRequest {
        .root    = project->root.clone(),
        .mode    = lito::FormatMode::Check,
        .backend = lito::FormatBackend::Builtin,
    };
    request.tools.clang_format = PathBuf::from("lito-format-missing-formatter"_str);
    request.tools.git          = PathBuf::from("lito-format-missing-git"_str);
    auto resolutions           = usize {};
    request.tool_reporter      = Some(lito::tools::HostToolResolutionSink {
        .context = rstd::addressof(resolutions),
        .notify =
            [](void* context, const lito::tools::HostToolResolution&) noexcept {
                ++*static_cast<usize*>(context);
            },
    });
    auto checked               = lito::format(request);
    ASSERT_TRUE(checked.is_ok());
    EXPECT_EQ(checked->files, usize(1));
    EXPECT_FALSE(checked->success());
    EXPECT_EQ(rstd::fs::read(source.as_path()).unwrap().as_slice(), original->as_slice());
    request.mode = lito::FormatMode::Write;
    auto written = lito::format(request);
    ASSERT_TRUE(written.is_ok());
    EXPECT_TRUE(written->success());
    EXPECT_NE(rstd::fs::read(source.as_path()).unwrap().as_slice(), original->as_slice());
    request.mode = lito::FormatMode::Check;
    auto clean   = lito::format(request);
    ASSERT_TRUE(clean.is_ok());
    EXPECT_TRUE(clean->success());
    EXPECT_EQ(resolutions, usize {});
}

TEST_F(FormatCommand, BuiltinFailureDoesNotOverwriteSource) {
    constexpr auto    invalid = "const char* value = \"unterminated;\n"_str;
    const ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "builtin-format-error"
version = "0.1.0"
[[bin]]
name = "builtin-format-error"
sources = ["main.cpp"]
link-stdlib = false
)toml"_str },
        { "main.cpp"_str, invalid },
    };
    auto project = materialize("builtin-format-error"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto result = lito::format(lito::FormatRequest {
        .root    = project->root.clone(),
        .backend = lito::FormatBackend::Builtin,
    });
    EXPECT_TRUE(result.is_err());
    auto source   = project->root.join(PathBuf::from("main.cpp"_str).as_path());
    auto contents = rstd::fs::read_to_string(source.as_path());
    ASSERT_TRUE(contents.is_ok());
    EXPECT_EQ(contents->as_str(), invalid);
}

TEST_F(FormatCommand, BuiltinDiscoversNearestStyleAndExplicitParentInheritance) {
    struct Source {
        ref<str> path;
        ref<str> original;
        ref<str> expected;
    };
    const Source sources[] = {
        { "src/root.cpp"_str,
          "int root(){return 1;}\n"_str,
          "int root () {\n  return 1;\n}\n"_str },
        { "src/nested/child.cpp"_str,
          "int child(){return 2;}\n"_str,
          "int child () {\n      return 2;\n}\n"_str },
        { "src/isolated/value.cpp"_str,
          "int value(){return 3;}\n"_str,
          "int value() {\n   return 3;\n}\n"_str },
    };
    const ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "builtin-style-discovery"
version = "0.1.0"
[[bin]]
name = "builtin-style-discovery"
sources = ["src/root.cpp", "src/nested/child.cpp", "src/isolated/value.cpp"]
link-stdlib = false
)toml"_str },
        { ".clang-format"_str, "IndentWidth: 2\nSpaceBeforeParens: Always\n"_str },
        { "_clang-format"_str, "IndentWidth: 7\n"_str },
        { "src/nested/_clang-format"_str,
          "BasedOnStyle: InheritParentConfig\nIndentWidth: 6\n"_str },
        { "src/isolated/.clang-format"_str, "IndentWidth: 3\n"_str },
        { sources[0].path, sources[0].original },
        { sources[1].path, sources[1].original },
        { sources[2].path, sources[2].original },
    };
    auto project = materialize("builtin-style-discovery"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto request = lito::FormatRequest {
        .root    = project->root.clone(),
        .mode    = lito::FormatMode::Check,
        .backend = lito::FormatBackend::Builtin,
    };
    auto checked = lito::format(request);
    ASSERT_TRUE(checked.is_ok());
    EXPECT_EQ(checked->files, usize(3));
    EXPECT_EQ(checked->unformatted_files.len(), usize(3));
    for (const auto& source : sources) {
        auto path = project->root.join(PathBuf::from(source.path).as_path());
        EXPECT_EQ(rstd::fs::read_to_string(path.as_path()).unwrap().as_str(), source.original);
    }
    request.mode = lito::FormatMode::Write;
    auto written = lito::format(request);
    ASSERT_TRUE(written.is_ok());
    for (const auto& source : sources) {
        auto path = project->root.join(PathBuf::from(source.path).as_path());
        EXPECT_EQ(rstd::fs::read_to_string(path.as_path()).unwrap().as_str(), source.expected);
    }
    request.mode = lito::FormatMode::Check;
    auto clean   = lito::format(request);
    ASSERT_TRUE(clean.is_ok());
    EXPECT_TRUE(clean->success());
}

TEST_F(FormatCommand, BuiltinRejectsInvalidStylesBeforeWritingAnySource) {
    constexpr auto    original = "int f(){return 1;}\n"_str;
    const ProjectFile files[]  = {
        { "lito.toml"_str, R"toml([package]
name = "builtin-style-errors"
version = "0.1.0"
[[bin]]
name = "builtin-style-errors"
sources = ["src/first.cpp", "src/nested/second.cpp"]
link-stdlib = false
)toml"_str },
        { ".clang-format"_str, "IndentWidth: 2\n"_str },
        { "src/nested/.clang-format"_str, "IndentWidth: 2\n"_str },
        { "src/first.cpp"_str, original },
        { "src/nested/second.cpp"_str, original },
    };
    struct Case {
        ref<str> configuration;
        ref<str> key;
    };
    const Case cases[] = {
        { "AlignConsecutiveAssignments: true\n"_str, "AlignConsecutiveAssignments"_str },
        { "UseTab: Always\n"_str, "UseTab"_str },
        { "TabWidth: 0\n"_str, "TabWidth"_str },
        { "ColumnLimit: 80\n"_str, "ColumnLimit"_str },
        { "IndentWidth: \"\"\n"_str, "IndentWidth"_str },
        { "KeepEmptyLinesAtTheStartOfBlocks: false\n"
          "KeepEmptyLines:\n  AtStartOfBlock: true\n"_str,
          "AtStartOfBlock"_str },
    };
    auto project = materialize("builtin-style-errors"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto configuration =
        project->root.join(PathBuf::from("src/nested/.clang-format"_str).as_path());
    const ref<str> paths[] = { "src/first.cpp"_str, "src/nested/second.cpp"_str };
    for (const auto& test : cases) {
        ASSERT_TRUE(
            rstd::fs::write(configuration.as_path(), test.configuration.as_bytes()).is_ok());
        auto result = lito::format(lito::FormatRequest {
            .root    = project->root.clone(),
            .backend = lito::FormatBackend::Builtin,
        });
        ASSERT_TRUE(result.is_err()) << test.configuration;
        auto message = error_chain_text(result.unwrap_err());
        EXPECT_TRUE(message.as_str().contains(test.key)) << message;
        EXPECT_TRUE(message.as_str().contains(".clang-format"_str)) << message;
        for (auto source : paths) {
            auto path = project->root.join(PathBuf::from(source).as_path());
            EXPECT_EQ(rstd::fs::read_to_string(path.as_path()).unwrap().as_str(), original);
        }
    }
}
