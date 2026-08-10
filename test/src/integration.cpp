#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito;
import lito.test.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

using namespace lito_test;

namespace
{

auto regular_file_count(ref<rstd::path::Path> directory) -> Option<usize> {
    auto opened = rstd::fs::read_dir(directory);
    if (opened.is_err()) return None();
    auto count   = usize {};
    auto entries = rstd::move(opened).unwrap();
    for (auto next = entries.next(); next.is_some(); next = entries.next()) {
        auto item = rstd::move(next).unwrap();
        if (item.is_err()) return None();
        auto entry = rstd::move(item).unwrap();
        auto type  = entry.file_type();
        if (type.is_err()) return None();
        if (type->is_file()) {
            ++count;
        } else if (type->is_dir()) {
            auto nested = regular_file_count(entry.path().as_path());
            if (nested.is_none()) return None();
            count += *nested;
        }
    }
    return Some(count);
}

} // namespace

TEST(Integration, ScanUsesNativePreprocessorAndDefinitions) {
    auto root   = project_root();
    auto native = lito::scan(lito::ScanRequest {
        .selection =
            lito::PackageSelection {
                .root     = root.clone(),
                .packages = strings("fixture-preprocessor-native"_str),
            },
        .source        = PathBuf::from("preprocessor-native/src/lib.cppm"_str),
        .configuration = configuration(),
        .locked        = true,
    });
    ASSERT_TRUE(native.is_ok());
    ASSERT_TRUE(native->result.provided.is_some());
    EXPECT_EQ(native->result.provided->logical_name.as_str(), "fixture.preprocessor.native"_str);
    EXPECT_TRUE(has_import(*native, "fixture.preprocessor.native:dependency"_str));
    EXPECT_FALSE(has_import(*native, "fixture.preprocessor.native:native_builtin_failure"_str));

    auto native_json = lito::scan_report_json(*native);
    ASSERT_TRUE(native_json.is_ok());
    EXPECT_TRUE(native_json->as_str().contains("\"format\": \"lito-scan\""_str));

    auto p1689_json = lito::scan_report_json(*native, lito::ScanOutputFormat::P1689);
    ASSERT_TRUE(p1689_json.is_ok());
    EXPECT_TRUE(p1689_json->as_str().contains("\"version\": 1"_str));
    EXPECT_TRUE(p1689_json->as_str().contains("\"revision\": 0"_str));
    EXPECT_TRUE(p1689_json->as_str().contains("\"primary-output\":"_str));
    EXPECT_TRUE(p1689_json->as_str().contains("\"provides\":"_str));
    EXPECT_TRUE(
        p1689_json->as_str().contains("\"logical-name\": \"fixture.preprocessor.native\""_str));
    EXPECT_TRUE(p1689_json->as_str().contains(
        "\"logical-name\": \"fixture.preprocessor.native:dependency\""_str));
    EXPECT_FALSE(p1689_json->as_str().contains("\"format\":"_str));
    EXPECT_FALSE(p1689_json->as_str().contains("\"headers\":"_str));

    auto definitions = lito::scan(lito::ScanRequest {
        .selection =
            lito::PackageSelection {
                .root     = root.clone(),
                .packages = strings("fixture-scan-definitions"_str),
            },
        .source        = PathBuf::from("scan-definitions/src/lib.cppm"_str),
        .configuration = configuration(),
        .locked        = true,
    });
    ASSERT_TRUE(definitions.is_ok());
    EXPECT_TRUE(has_import(*definitions, "fixture.scan.definitions:defined"_str));
    EXPECT_FALSE(has_import(*definitions, "fixture.scan.definitions:missing"_str));
    EXPECT_FALSE(
        has_import(*definitions, "fixture.scan.definitions:command_line_undef_failure"_str));
}

TEST(Integration, BuildSelectsProductionArtifacts) {
    auto root   = project_root();
    auto output = output_root("build"_str);
    clear_output(output.as_path());
    auto request = build_request(
        root.as_path(), output.as_path(), strings("fixture-test-lib"_str, "fixture-test-app"_str));
    auto summary = lito::build(request);
    ASSERT_TRUE(summary.is_ok());
    EXPECT_EQ(artifact_count(*summary, lito::ArtifactKind::StaticLibrary), usize(1));
    EXPECT_EQ(artifact_count(*summary, lito::ArtifactKind::Executable), usize(1));
    EXPECT_EQ(artifact_count(*summary, lito::ArtifactKind::TestExecutable), usize {});
    clear_output(output.as_path());
}

TEST(Integration, TestAttachmentKeepsProductionArtifactsIsolated) {
    auto root   = project_root();
    auto output = output_root("test-attachment"_str);
    ASSERT_TRUE(clear_output(output.as_path()));

    auto production = lito::build(
        build_request(root.as_path(), output.as_path(), strings("fixture-test-attach-lib"_str)));
    ASSERT_TRUE(production.is_ok());
    EXPECT_EQ(artifact_count(*production, lito::ArtifactKind::StaticLibrary), usize(1));
    EXPECT_EQ(artifact_count(*production, lito::ArtifactKind::TestAttachmentArchive), usize {});
    auto attachment_directory = output.join(
        PathBuf::from("test/fixture-test-attach/attach/fixture-test-attach-lib"_str).as_path());
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

TEST(Integration, TestRunsPassFailureSignalAndNoRun) {
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

TEST(Integration, CompileTestsReportOutcomesAndReuse) {
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

TEST(Integration, EnvironmentIsSharedWithinBuild) {
    auto root   = project_root();
    auto output = output_root("environment"_str);
    clear_output(output.as_path());
    auto request =
        build_request(root.as_path(), output.as_path(), strings("fixture-environment-cache"_str));
    request.execution.scan.jobs    = Some(usize(2));
    request.execution.compile.jobs = Some(usize(2));
    auto summary                   = lito::build(request);
    ASSERT_TRUE(summary.is_ok());
    EXPECT_EQ(summary->toolchain.preprocessor_environment_entries, usize(1));
    EXPECT_EQ(summary->toolchain.preprocessor_environment_queries, usize(1));
    EXPECT_GE(summary->toolchain.preprocessor_environment_hits, usize(1));
    EXPECT_EQ(summary->scan_profile.execution().jobs, usize(2));
    EXPECT_EQ(summary->scan_profile.execution().tasks, usize(2));
    EXPECT_EQ(summary->scan_profile.execution().max_active, usize(2));
    EXPECT_FALSE(summary->scan_profile.execution().task_work.is_zero());
    EXPECT_FALSE(summary->scan_profile.execution().completion_wait.is_zero());
    EXPECT_EQ(summary->compile_execution.jobs, usize(2));
    EXPECT_EQ(summary->compile_execution.tasks, usize(2));
    EXPECT_EQ(summary->compile_execution.max_active, usize(2));
    EXPECT_FALSE(summary->compile_execution.task_work.is_zero());
    EXPECT_FALSE(summary->compile_execution.wall.is_zero());
    EXPECT_EQ(summary->frontend.source_requests, usize(4));
    EXPECT_EQ(summary->frontend.source_reads, usize(3));
    EXPECT_EQ(summary->frontend.lex_builds, usize(3));
    EXPECT_EQ(summary->frontend.persistent_fingerprint_requests, usize(4));
    EXPECT_EQ(summary->frontend.persistent_fingerprint_hits, usize(1));
    EXPECT_EQ(summary->frontend.persistent_fingerprint_builds, usize(3));
    auto report  = output.join(PathBuf::from("timing.txt"_str).as_path());
    auto emitted = lito::timing_output::emit(*summary,
                                             lito::timing_output::OutputOptions {
                                                 .file = Some(report.clone()),
                                             });
    ASSERT_TRUE(emitted.is_ok());
    auto contents = rstd::fs::read_to_string(report.as_path());
    ASSERT_TRUE(contents.is_ok());
    EXPECT_TRUE(contents->as_str().contains("frontend"_str));
    EXPECT_TRUE(contents->as_str().contains("compile execution"_str));
    EXPECT_TRUE(contents->as_str().contains("build.compile"_str));
    EXPECT_TRUE(contents->as_str().contains("aggregate timing"_str));
    clear_output(output.as_path());
}

TEST(Integration, ScanCacheReusesAndInvalidatesOwnedInputs) {
    auto base = output_root("scan-cache"_str);
    clear_output(base.as_path());
    auto fixture = base.join(PathBuf::from("fixture"_str).as_path());
    auto output  = base.join(PathBuf::from("output"_str).as_path());
    auto source  = root("cache/scan"_str);
    ASSERT_TRUE(copy_directory(source.as_path(), fixture.as_path()));

    auto cold =
        lito::build(build_request(fixture.as_path(), output.as_path(), Vec<String>::make()));
    ASSERT_TRUE(cold.is_ok());
    EXPECT_EQ(cold->frontend.persistent_scan_misses, usize(1));
    EXPECT_EQ(cold->frontend.persistent_scan_refresh, usize(1));
    EXPECT_EQ(cold->frontend.analyze_builds, usize(1));
    auto bmi_directory = output.join(PathBuf::from("bmi"_str).as_path());
    auto cold_bmis     = regular_file_count(bmi_directory.as_path());
    ASSERT_TRUE(cold_bmis.is_some());
    EXPECT_EQ(*cold_bmis, usize(1));

    auto warm =
        lito::build(build_request(fixture.as_path(), output.as_path(), Vec<String>::make()));
    ASSERT_TRUE(warm.is_ok());
    EXPECT_EQ(warm->frontend.persistent_scan_hits, usize(1));
    EXPECT_EQ(warm->frontend.analyze_builds, usize {});
    EXPECT_EQ(warm->compiled, usize {});

    auto staged_optional = fixture.join(PathBuf::from("staged/optional.hpp"_str).as_path());
    auto high_optional   = fixture.join(PathBuf::from("high/optional.hpp"_str).as_path());
    ASSERT_TRUE(rstd::fs::copy(staged_optional.as_path(), high_optional.as_path()).is_ok());
    auto optional =
        lito::build(build_request(fixture.as_path(), output.as_path(), Vec<String>::make()));
    ASSERT_TRUE(optional.is_ok());
    EXPECT_EQ(optional->frontend.persistent_scan_include_lookup, usize(1));
    EXPECT_EQ(optional->frontend.analyze_builds, usize(1));

    auto staged_priority = fixture.join(PathBuf::from("staged/choice.hpp"_str).as_path());
    auto high_priority   = fixture.join(PathBuf::from("high/choice.hpp"_str).as_path());
    ASSERT_TRUE(rstd::fs::copy(staged_priority.as_path(), high_priority.as_path()).is_ok());
    auto priority =
        lito::build(build_request(fixture.as_path(), output.as_path(), Vec<String>::make()));
    ASSERT_TRUE(priority.is_ok());
    EXPECT_EQ(priority->frontend.persistent_scan_include_lookup, usize(1));

    auto staged_header = fixture.join(PathBuf::from("staged/choice-low.hpp"_str).as_path());
    auto low_header    = fixture.join(PathBuf::from("low/choice.hpp"_str).as_path());
    ASSERT_TRUE(rstd::fs::copy(staged_header.as_path(), low_header.as_path()).is_ok());
    auto header =
        lito::build(build_request(fixture.as_path(), output.as_path(), Vec<String>::make()));
    ASSERT_TRUE(header.is_ok());
    EXPECT_EQ(header->frontend.persistent_scan_file_dependency, usize(1));

    auto staged_source  = fixture.join(PathBuf::from("staged/lib.cppm"_str).as_path());
    auto primary_source = fixture.join(PathBuf::from("src/lib.cppm"_str).as_path());
    ASSERT_TRUE(rstd::fs::copy(staged_source.as_path(), primary_source.as_path()).is_ok());
    auto changed_source =
        lito::build(build_request(fixture.as_path(), output.as_path(), Vec<String>::make()));
    ASSERT_TRUE(changed_source.is_ok());
    EXPECT_EQ(changed_source->frontend.persistent_scan_source, usize(1));
    auto changed_bmis = regular_file_count(bmi_directory.as_path());
    ASSERT_TRUE(changed_bmis.is_some());
    EXPECT_EQ(*changed_bmis, usize(1));

    clear_output(base.as_path());

    auto dynamic_base = output_root("scan-cache-dynamic"_str);
    clear_output(dynamic_base.as_path());
    auto dynamic_fixture = dynamic_base.join(PathBuf::from("fixture"_str).as_path());
    auto dynamic_output  = dynamic_base.join(PathBuf::from("output"_str).as_path());
    auto dynamic_source  = root("cache/dynamic"_str);
    ASSERT_TRUE(copy_directory(dynamic_source.as_path(), dynamic_fixture.as_path()));
    auto dynamic_cold = lito::build(
        build_request(dynamic_fixture.as_path(), dynamic_output.as_path(), Vec<String>::make()));
    ASSERT_TRUE(dynamic_cold.is_ok());
    auto dynamic_warm = lito::build(
        build_request(dynamic_fixture.as_path(), dynamic_output.as_path(), Vec<String>::make()));
    ASSERT_TRUE(dynamic_warm.is_ok());
    EXPECT_EQ(dynamic_warm->frontend.persistent_scan_hits, usize {});
    EXPECT_EQ(dynamic_warm->frontend.persistent_scan_uncacheable, usize(1));
    EXPECT_EQ(dynamic_warm->frontend.analyze_builds, usize(1));
    clear_output(dynamic_base.as_path());
}

TEST(Integration, InvalidArtifactsAndDependenciesAreRejected) {
    auto multiple =
        lito::load_manifest_document(root("test-command/invalid-artifact"_str).as_path());
    EXPECT_TRUE(multiple.is_err());

    auto invalid = root("test-command/invalid-dependency"_str);
    auto output  = output_root("invalid-dependency"_str);
    clear_output(output.as_path());
    auto request    = build_request(invalid.as_path(), output.as_path(), Vec<String>::make());
    request.locked  = false;
    auto dependency = lito::build(request);
    EXPECT_TRUE(dependency.is_err());
    clear_output(output.as_path());
}
