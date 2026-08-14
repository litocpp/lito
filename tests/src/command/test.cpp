#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito;
import lito.manifest;
import lito.source;
import lito.test.support;
import lito.workspace.contract;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

using namespace lito_test;

TEST(TestCommand, TestAttachmentKeepsProductionArtifactsIsolated) {
    auto root   = project_root();
    auto output = output_root("test-attachment"_str);
    ASSERT_TRUE(clear_output(output.as_path()));

    auto production = lito::build(
        build_request(root.as_path(), output.as_path(), strings("fixture-test-attach-lib"_str)));
    ASSERT_TRUE(production.is_ok());
    EXPECT_EQ(artifact_count(*production, lito::ArtifactKind::StaticLibrary), usize(1));
    EXPECT_EQ(artifact_count(*production, lito::ArtifactKind::TestAttachmentArchive), usize {});
    auto attachment_directory =
        output.join(PathBuf::from("test-attachments/fixture-test-attach/fixture-test-attach/"
                                  "fixture-test-attach-lib/fixture-test-attach-lib"_str)
                        .as_path());
    EXPECT_FALSE(rstd::fs::exists(attachment_directory.as_path()).unwrap());

    auto tested = lito::test(lito::TestRequest {
        .build =
            build_request(root.as_path(), output.as_path(), strings("fixture-test-attach"_str)),
    });
    ASSERT_TRUE(tested.is_ok());
    EXPECT_TRUE(tested->success());
    EXPECT_EQ(artifact_count(tested->build, lito::ArtifactKind::StaticLibrary), usize(1));
    EXPECT_EQ(artifact_count(tested->build, lito::ArtifactKind::TestAttachmentArchive), usize(1));
    EXPECT_EQ(artifact_count(tested->build, lito::ArtifactKind::TestExecutable), usize(1));
    EXPECT_TRUE(
        rstd::fs::exists(
            attachment_directory.join(PathBuf::from("libfixture_test_attach.test.a"_str).as_path())
                .as_path())
            .unwrap());
    ASSERT_TRUE(clear_output(output.as_path()));
}

TEST(TestCommand, TestRunsPassFailureSignalAndNoRun) {
    auto root   = project_root();
    auto output = output_root("test-command"_str);
    clear_output(output.as_path());

    auto pass_request = lito::TestRequest {
        .build = build_request(root.as_path(), output.as_path(), strings("fixture-test-pass"_str)),
        .arguments = strings("expected-argument"_str),
    };
    auto passed = lito::test(rstd::move(pass_request));
    ASSERT_TRUE(passed.is_ok());
    EXPECT_TRUE(passed->success());
    ASSERT_EQ(passed->executions.len(), usize(1));
    EXPECT_TRUE(passed->executions[usize {}].success());

    auto no_run_request = lito::TestRequest {
        .build = build_request(
            root.as_path(),
            output.as_path(),
            strings("fixture-test-pass"_str, "fixture-test-fail"_str, "fixture-test-signal"_str),
            build_profile("release"_str)),
        .no_run = true,
    };
    auto no_run = lito::test(rstd::move(no_run_request));
    ASSERT_TRUE(no_run.is_ok());
    EXPECT_EQ(no_run->build.profile.as_str(), "release"_str);
    EXPECT_TRUE(no_run->executions.is_empty());
    EXPECT_EQ(artifact_count(no_run->build, lito::ArtifactKind::TestExecutable), usize(3));

    auto failure = lito::test(lito::TestRequest {
        .build = build_request(root.as_path(), output.as_path(), strings("fixture-test-fail"_str)),
    });
    ASSERT_TRUE(failure.is_ok());
    EXPECT_FALSE(failure->success());

    auto signal = lito::test(lito::TestRequest {
        .build =
            build_request(root.as_path(), output.as_path(), strings("fixture-test-signal"_str)),
    });
    ASSERT_TRUE(signal.is_ok());
    EXPECT_FALSE(signal->success());

    auto production = lito::test(lito::TestRequest {
        .build = build_request(root.as_path(), output.as_path(), strings("fixture-test-app"_str)),
    });
    EXPECT_TRUE(production.is_err());
    clear_output(output.as_path());
}

TEST(TestCommand, CompileTestsReportOutcomesAndReuse) {
    auto root   = project_root();
    auto output = output_root("compile-test"_str);
    clear_output(output.as_path());

    auto passed = lito::test(lito::TestRequest {
        .build =
            build_request(root.as_path(), output.as_path(), strings("fixture-compile-pass"_str)),
    });
    ASSERT_TRUE(passed.is_ok());
    EXPECT_TRUE(passed->success());
    ASSERT_EQ(passed->build.compile_tests.len(), usize(3));
    for (const auto& execution : passed->build.compile_tests) {
        EXPECT_TRUE(execution.success());
    }

    auto reused = lito::test(lito::TestRequest {
        .build =
            build_request(root.as_path(), output.as_path(), strings("fixture-compile-pass"_str)),
    });
    ASSERT_TRUE(reused.is_ok());
    EXPECT_TRUE(reused->success());
    EXPECT_GE(reused->build.reused, usize(4));

    auto mismatch = lito::test(lito::TestRequest {
        .build = build_request(
            root.as_path(), output.as_path(), strings("fixture-compile-mismatch"_str)),
    });
    ASSERT_TRUE(mismatch.is_ok());
    EXPECT_FALSE(mismatch->success());
    ASSERT_EQ(mismatch->build.compile_tests.len(), usize(1));
    EXPECT_TRUE(mismatch->build.compile_tests[usize {}].mismatch.is_some());

    auto unsupported = lito::build(
        build_request(root.as_path(), output.as_path(), strings("fixture-windows-only"_str)));
    EXPECT_TRUE(unsupported.is_err());
    clear_output(output.as_path());
}

TEST(TestCommand, InvalidArtifactsAndDependenciesAreRejected) {
    auto multiple =
        lito::load_manifest_document(fixture_path("command/test/invalid-artifact"_str).as_path());
    EXPECT_TRUE(multiple.is_err());

    auto invalid = fixture_path("command/test/invalid-dependency"_str);
    auto output  = output_root("invalid-dependency"_str);
    clear_output(output.as_path());
    auto request    = build_request(invalid.as_path(), output.as_path(), Vec<String>::make());
    request.locked  = false;
    auto dependency = lito::build(request);
    EXPECT_TRUE(dependency.is_err());
    clear_output(output.as_path());
}
