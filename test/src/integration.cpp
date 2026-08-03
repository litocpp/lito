#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import tenon;
import tenon.doc;
import tenon.test.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

using namespace tenon_test;

TEST(Integration, ScanUsesNativePreprocessorAndDefinitions) {
    auto root   = project_root();
    auto native = tenon::scan(tenon::ScanRequest {
        .selection =
            tenon::PackageSelection {
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

    auto definitions = tenon::scan(tenon::ScanRequest {
        .selection =
            tenon::PackageSelection {
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
    auto summary = tenon::build(request);
    ASSERT_TRUE(summary.is_ok());
    EXPECT_EQ(artifact_count(*summary, tenon::ArtifactKind::StaticLibrary), usize(1));
    EXPECT_EQ(artifact_count(*summary, tenon::ArtifactKind::Executable), usize(1));
    EXPECT_EQ(artifact_count(*summary, tenon::ArtifactKind::TestExecutable), usize {});
    clear_output(output.as_path());
}

TEST(Integration, TestAttachmentKeepsProductionArtifactsIsolated) {
    auto root   = project_root();
    auto output = output_root("test-attachment"_str);
    ASSERT_TRUE(clear_output(output.as_path()));

    auto production = tenon::build(
        build_request(root.as_path(), output.as_path(), strings("fixture-test-attach-lib"_str)));
    ASSERT_TRUE(production.is_ok());
    EXPECT_EQ(artifact_count(*production, tenon::ArtifactKind::StaticLibrary), usize(1));
    EXPECT_EQ(artifact_count(*production, tenon::ArtifactKind::TestAttachmentArchive), usize {});
    auto attachment_directory = output.join(
        PathBuf::from("test/fixture-test-attach/attach/fixture-test-attach-lib"_str).as_path());
    EXPECT_FALSE(rstd::fs::exists(attachment_directory.as_path()).unwrap());

    auto tested = tenon::test(tenon::TestRequest {
        .build =
            build_request(root.as_path(), output.as_path(), strings("fixture-test-attach"_str)),
    });
    ASSERT_TRUE(tested.is_ok());
    EXPECT_TRUE(tested->success());
    EXPECT_EQ(artifact_count(tested->build, tenon::ArtifactKind::StaticLibrary), usize(1));
    EXPECT_EQ(artifact_count(tested->build, tenon::ArtifactKind::TestAttachmentArchive), usize(1));
    EXPECT_EQ(artifact_count(tested->build, tenon::ArtifactKind::TestExecutable), usize(1));
    EXPECT_TRUE(
        rstd::fs::exists(
            attachment_directory.join(PathBuf::from("libfixture_test_attach.test.a"_str).as_path())
                .as_path())
            .unwrap());
    ASSERT_TRUE(clear_output(output.as_path()));
}

TEST(Integration, DocumentationUsesFrontendFactsAndPublishesVersionedOutput) {
    auto project = project_root();
    auto output  = output_root("doc"_str);
    ASSERT_TRUE(clear_output(output.as_path()));
    auto generated = tenon::generate_documentation(tenon::DocRequest {
        .selection =
            tenon::PackageSelection {
                .root     = project.clone(),
                .packages = strings("fixture-doc-basic"_str),
            },
        .output        = output.clone(),
        .configuration = configuration(),
        .locked        = true,
    });
    ASSERT_TRUE(generated.is_ok());
    ASSERT_EQ(generated->packages.len(), usize(1));
    const auto& package = generated->packages[usize {}];
    EXPECT_EQ(package.symbols, usize(5));
    EXPECT_EQ(package.documented, usize(5));
    EXPECT_EQ(package.undocumented, usize {});
    EXPECT_EQ(package.unsupported, usize {});
    EXPECT_EQ(package.diagnostics, usize(1));
    ASSERT_EQ(package.diagnostic_details.len(), usize(1));
    EXPECT_EQ(package.diagnostic_details[usize {}].code.as_str(),
              "conflicting-symbol-documentation"_str);
    EXPECT_EQ(generated->frontend.source_reads, usize(1));
    EXPECT_EQ(generated->frontend.lex_builds, usize(1));
    EXPECT_EQ(generated->frontend.documentation_builds, usize(1));
    EXPECT_EQ(generated->frontend.documentation_declarations, usize(8));
    auto json = rstd::fs::read_to_string(package.json.as_path());
    ASSERT_TRUE(json.is_ok());
    EXPECT_TRUE(tenon::doc::validate_json(json->as_str()).is_ok());
    EXPECT_TRUE(tenon::doc::validate_json("{\"format\":\"tenon-doc\",\"version\":2}"_str).is_err());
    EXPECT_TRUE(json->as_str().contains("Fixture module overview."_str));
    EXPECT_TRUE(json->as_str().contains("\"toolchain-target\""_str));
    EXPECT_TRUE(json->as_str().contains("\"language-standard\": \"c++20\""_str));
    EXPECT_TRUE(json->as_str().contains("\"reexports\""_str));
    EXPECT_TRUE(json->as_str().contains("\"end-column\""_str));
    EXPECT_TRUE(json->as_str().contains("\"path\": \"src/lib.cppm\""_str));
    auto project_text = project.as_path().to_str();
    ASSERT_TRUE(project_text.is_some());
    EXPECT_FALSE(json->as_str().contains(*project_text));
    EXPECT_TRUE(json->as_str().contains("Adds two values"_str));
    EXPECT_FALSE(json->as_str().contains("Forward declaration documentation."_str));
    EXPECT_TRUE(json->as_str().contains("conflicting-symbol-documentation"_str));
    EXPECT_TRUE(json->as_str().contains("fixture::nested::make"_str));
    EXPECT_FALSE(json->as_str().contains("fixture::nested::T"_str));
    EXPECT_TRUE(json->as_str().contains("\"group\": \"Arithmetic\""_str));
    EXPECT_FALSE(json->as_str().contains("Inactive declaration documentation."_str));
    EXPECT_FALSE(json->as_str().contains("doxygen_hidden"_str));
    EXPECT_FALSE(json->as_str().contains("private member"_str));
    auto symbol_directory = package.directory.join(PathBuf::from("symbol"_str).as_path());
    auto opened_symbols   = rstd::fs::read_dir(symbol_directory.as_path());
    ASSERT_TRUE(opened_symbols.is_ok());
    auto symbol_entries = rstd::move(opened_symbols).unwrap();
    auto symbol_pages   = String::make();
    for (auto next = symbol_entries.next(); next.is_some(); next = symbol_entries.next()) {
        auto entry = rstd::move(next).unwrap();
        ASSERT_TRUE(entry.is_ok());
        auto page = rstd::fs::read_to_string(entry->path().as_path());
        ASSERT_TRUE(page.is_ok());
        symbol_pages.push_str(page->as_str());
    }
    EXPECT_TRUE(symbol_pages.as_str().contains("Parameter <code>left</code>"_str));
    EXPECT_TRUE(symbol_pages.as_str().contains("<h3>Returns</h3>"_str));
    EXPECT_TRUE(symbol_pages.as_str().contains("<em>record</em>"_str));
    EXPECT_TRUE(symbol_pages.as_str().contains("href=\"https://example.com\""_str));
    EXPECT_TRUE(symbol_pages.as_str().contains("&lt;script&gt;"_str));
    EXPECT_FALSE(symbol_pages.as_str().contains("<script>"_str));
    EXPECT_TRUE(rstd::fs::exists(generated->index.as_path()).unwrap());
    auto regenerated = tenon::generate_documentation(tenon::DocRequest {
        .selection =
            tenon::PackageSelection {
                .root     = project.clone(),
                .packages = strings("fixture-doc-basic"_str),
            },
        .output        = output.clone(),
        .configuration = configuration(),
        .locked        = true,
    });
    ASSERT_TRUE(regenerated.is_ok());
    auto second_json = rstd::fs::read_to_string(regenerated->packages[usize {}].json.as_path());
    ASSERT_TRUE(second_json.is_ok());
    EXPECT_EQ(second_json->as_str(), json->as_str());
    EXPECT_TRUE(clear_output(output.as_path()));

    auto rejected = tenon::generate_documentation(tenon::DocRequest {
        .selection =
            tenon::PackageSelection {
                .root     = project.clone(),
                .packages = strings("fixture-test-app"_str),
            },
        .output        = output.clone(),
        .configuration = configuration(),
        .locked        = true,
    });
    EXPECT_TRUE(rejected.is_err());
}

TEST(Integration, TestRunsPassFailureSignalAndNoRun) {
    auto root   = project_root();
    auto output = output_root("test-command"_str);
    clear_output(output.as_path());

    auto pass_request = tenon::TestRequest {
        .build = build_request(root.as_path(), output.as_path(), strings("fixture-test-pass"_str)),
        .arguments = strings("expected-argument"_str),
    };
    auto passed = tenon::test(rstd::move(pass_request));
    ASSERT_TRUE(passed.is_ok());
    EXPECT_TRUE(passed->success());
    ASSERT_EQ(passed->executions.len(), usize(1));
    EXPECT_TRUE(passed->executions[usize {}].success());

    auto no_run_request = tenon::TestRequest {
        .build = build_request(
            root.as_path(),
            output.as_path(),
            strings("fixture-test-pass"_str, "fixture-test-fail"_str, "fixture-test-signal"_str),
            tenon::BuildProfile::Release),
        .no_run = true,
    };
    auto no_run = tenon::test(rstd::move(no_run_request));
    ASSERT_TRUE(no_run.is_ok());
    EXPECT_EQ(no_run->build.profile.as_str(), "release"_str);
    EXPECT_TRUE(no_run->executions.is_empty());
    EXPECT_EQ(artifact_count(no_run->build, tenon::ArtifactKind::TestExecutable), usize(3));

    auto failure = tenon::test(tenon::TestRequest {
        .build = build_request(root.as_path(), output.as_path(), strings("fixture-test-fail"_str)),
    });
    ASSERT_TRUE(failure.is_ok());
    EXPECT_FALSE(failure->success());

    auto signal = tenon::test(tenon::TestRequest {
        .build =
            build_request(root.as_path(), output.as_path(), strings("fixture-test-signal"_str)),
    });
    ASSERT_TRUE(signal.is_ok());
    EXPECT_FALSE(signal->success());

    auto production = tenon::test(tenon::TestRequest {
        .build = build_request(root.as_path(), output.as_path(), strings("fixture-test-app"_str)),
    });
    EXPECT_TRUE(production.is_err());
    clear_output(output.as_path());
}

TEST(Integration, CompileTestsReportOutcomesAndReuse) {
    auto root   = project_root();
    auto output = output_root("compile-test"_str);
    clear_output(output.as_path());

    auto passed = tenon::test(tenon::TestRequest {
        .build =
            build_request(root.as_path(), output.as_path(), strings("fixture-compile-pass"_str)),
    });
    ASSERT_TRUE(passed.is_ok());
    EXPECT_TRUE(passed->success());
    ASSERT_EQ(passed->build.compile_tests.len(), usize(3));
    for (const auto& execution : passed->build.compile_tests) {
        EXPECT_TRUE(execution.success());
    }

    auto reused = tenon::test(tenon::TestRequest {
        .build =
            build_request(root.as_path(), output.as_path(), strings("fixture-compile-pass"_str)),
    });
    ASSERT_TRUE(reused.is_ok());
    EXPECT_TRUE(reused->success());
    EXPECT_GE(reused->build.reused, usize(4));

    auto mismatch = tenon::test(tenon::TestRequest {
        .build = build_request(
            root.as_path(), output.as_path(), strings("fixture-compile-mismatch"_str)),
    });
    ASSERT_TRUE(mismatch.is_ok());
    EXPECT_FALSE(mismatch->success());
    ASSERT_EQ(mismatch->build.compile_tests.len(), usize(1));
    EXPECT_TRUE(mismatch->build.compile_tests[usize {}].mismatch.is_some());

    auto unsupported = tenon::build(
        build_request(root.as_path(), output.as_path(), strings("fixture-windows-only"_str)));
    EXPECT_TRUE(unsupported.is_err());
    clear_output(output.as_path());
}

TEST(Integration, EnvironmentIsSharedWithinBuild) {
    auto root   = project_root();
    auto output = output_root("environment"_str);
    clear_output(output.as_path());
    auto summary = tenon::build(
        build_request(root.as_path(), output.as_path(), strings("fixture-environment-cache"_str)));
    ASSERT_TRUE(summary.is_ok());
    EXPECT_EQ(summary->toolchain.preprocessor_environment_entries, usize(1));
    EXPECT_EQ(summary->toolchain.preprocessor_environment_queries, usize(1));
    EXPECT_GE(summary->toolchain.preprocessor_environment_hits, usize(1));
    auto report  = output.join(PathBuf::from("timing.txt"_str).as_path());
    auto emitted = tenon::timing_output::emit(*summary,
                                              tenon::timing_output::OutputOptions {
                                                  .file = Some(report.clone()),
                                              });
    ASSERT_TRUE(emitted.is_ok());
    auto contents = rstd::fs::read_to_string(report.as_path());
    ASSERT_TRUE(contents.is_ok());
    EXPECT_TRUE(contents->as_str().contains("frontend"_str));
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
        tenon::build(build_request(fixture.as_path(), output.as_path(), Vec<String>::make()));
    ASSERT_TRUE(cold.is_ok());
    EXPECT_EQ(cold->frontend.persistent_scan_misses, usize(1));
    EXPECT_EQ(cold->frontend.persistent_scan_refresh, usize(1));
    EXPECT_EQ(cold->frontend.analyze_builds, usize(1));

    auto warm =
        tenon::build(build_request(fixture.as_path(), output.as_path(), Vec<String>::make()));
    ASSERT_TRUE(warm.is_ok());
    EXPECT_EQ(warm->frontend.persistent_scan_hits, usize(1));
    EXPECT_EQ(warm->frontend.analyze_builds, usize {});
    EXPECT_EQ(warm->compiled, usize {});

    auto staged_optional = fixture.join(PathBuf::from("staged/optional.hpp"_str).as_path());
    auto high_optional   = fixture.join(PathBuf::from("high/optional.hpp"_str).as_path());
    ASSERT_TRUE(rstd::fs::copy(staged_optional.as_path(), high_optional.as_path()).is_ok());
    auto optional =
        tenon::build(build_request(fixture.as_path(), output.as_path(), Vec<String>::make()));
    ASSERT_TRUE(optional.is_ok());
    EXPECT_EQ(optional->frontend.persistent_scan_include_lookup, usize(1));
    EXPECT_EQ(optional->frontend.analyze_builds, usize(1));

    auto staged_priority = fixture.join(PathBuf::from("staged/choice.hpp"_str).as_path());
    auto high_priority   = fixture.join(PathBuf::from("high/choice.hpp"_str).as_path());
    ASSERT_TRUE(rstd::fs::copy(staged_priority.as_path(), high_priority.as_path()).is_ok());
    auto priority =
        tenon::build(build_request(fixture.as_path(), output.as_path(), Vec<String>::make()));
    ASSERT_TRUE(priority.is_ok());
    EXPECT_EQ(priority->frontend.persistent_scan_include_lookup, usize(1));

    auto staged_header = fixture.join(PathBuf::from("staged/choice-low.hpp"_str).as_path());
    auto low_header    = fixture.join(PathBuf::from("low/choice.hpp"_str).as_path());
    ASSERT_TRUE(rstd::fs::copy(staged_header.as_path(), low_header.as_path()).is_ok());
    auto header =
        tenon::build(build_request(fixture.as_path(), output.as_path(), Vec<String>::make()));
    ASSERT_TRUE(header.is_ok());
    EXPECT_EQ(header->frontend.persistent_scan_file_dependency, usize(1));

    auto staged_source  = fixture.join(PathBuf::from("staged/lib.cppm"_str).as_path());
    auto primary_source = fixture.join(PathBuf::from("src/lib.cppm"_str).as_path());
    ASSERT_TRUE(rstd::fs::copy(staged_source.as_path(), primary_source.as_path()).is_ok());
    auto changed_source =
        tenon::build(build_request(fixture.as_path(), output.as_path(), Vec<String>::make()));
    ASSERT_TRUE(changed_source.is_ok());
    EXPECT_EQ(changed_source->frontend.persistent_scan_source, usize(1));

    clear_output(base.as_path());

    auto dynamic_base = output_root("scan-cache-dynamic"_str);
    clear_output(dynamic_base.as_path());
    auto dynamic_fixture = dynamic_base.join(PathBuf::from("fixture"_str).as_path());
    auto dynamic_output  = dynamic_base.join(PathBuf::from("output"_str).as_path());
    auto dynamic_source  = root("cache/dynamic"_str);
    ASSERT_TRUE(copy_directory(dynamic_source.as_path(), dynamic_fixture.as_path()));
    auto dynamic_cold = tenon::build(
        build_request(dynamic_fixture.as_path(), dynamic_output.as_path(), Vec<String>::make()));
    ASSERT_TRUE(dynamic_cold.is_ok());
    auto dynamic_warm = tenon::build(
        build_request(dynamic_fixture.as_path(), dynamic_output.as_path(), Vec<String>::make()));
    ASSERT_TRUE(dynamic_warm.is_ok());
    EXPECT_EQ(dynamic_warm->frontend.persistent_scan_hits, usize {});
    EXPECT_EQ(dynamic_warm->frontend.persistent_scan_uncacheable, usize(1));
    EXPECT_EQ(dynamic_warm->frontend.analyze_builds, usize(1));
    clear_output(dynamic_base.as_path());
}

TEST(Integration, InvalidArtifactsAndDependenciesAreRejected) {
    auto multiple =
        tenon::load_manifest_document(root("test-command/invalid-artifact"_str).as_path());
    EXPECT_TRUE(multiple.is_err());

    auto invalid = root("test-command/invalid-dependency"_str);
    auto output  = output_root("invalid-dependency"_str);
    clear_output(output.as_path());
    auto request    = build_request(invalid.as_path(), output.as_path(), Vec<String>::make());
    request.locked  = false;
    auto dependency = tenon::build(request);
    EXPECT_TRUE(dependency.is_err());
    clear_output(output.as_path());
}
