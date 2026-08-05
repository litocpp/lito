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
    auto project          = project_root();
    auto output           = output_root("doc"_str);
    auto data             = output_root("doc-data"_str);
    auto restored_output  = output_root("doc-restored"_str);
    auto data_only_output = output_root("doc-data-only"_str);
    auto unused_site      = output_root("doc-unused-site"_str);
    auto custom_frontend  = output_root("doc-custom-frontend"_str);
    auto custom_output    = output_root("doc-custom-site"_str);
    auto corrupt_data     = output_root("doc-corrupt-data"_str);
    ASSERT_TRUE(clear_output(output.as_path()));
    ASSERT_TRUE(clear_output(data.as_path()));
    ASSERT_TRUE(clear_output(restored_output.as_path()));
    ASSERT_TRUE(clear_output(data_only_output.as_path()));
    ASSERT_TRUE(clear_output(unused_site.as_path()));
    ASSERT_TRUE(clear_output(custom_frontend.as_path()));
    ASSERT_TRUE(clear_output(custom_output.as_path()));
    ASSERT_TRUE(clear_output(corrupt_data.as_path()));
    auto generated = tenon::generate_documentation(tenon::DocRequest {
        .selection =
            tenon::PackageSelection {
                .root     = project.clone(),
                .packages = strings("fixture-doc-basic"_str),
            },
        .output        = output.clone(),
        .data_output   = data.clone(),
        .configuration = configuration(),
        .locked        = true,
    });
    ASSERT_TRUE(generated.is_ok());
    EXPECT_TRUE(generated->site_generated);
    EXPECT_EQ(generated->data.as_path(), data.as_path());
    EXPECT_TRUE(rstd::fs::exists(generated->data_manifest.as_path()).unwrap());
    auto data_manifest = rstd::fs::read_to_string(generated->data_manifest.as_path());
    ASSERT_TRUE(data_manifest.is_ok());
    EXPECT_TRUE(data_manifest->as_str().contains("\"title\": \"tenon-test-project\""_str));
    EXPECT_TRUE(tenon::doc::validate_data(data.as_path()).is_ok());
    ASSERT_TRUE(copy_directory(data.as_path(), corrupt_data.as_path()));
    auto corrupt_sources =
        corrupt_data.join(PathBuf::from("packages/fixture-doc-basic/sources"_str).as_path());
    auto opened_corrupt = rstd::fs::read_dir(corrupt_sources.as_path());
    ASSERT_TRUE(opened_corrupt.is_ok());
    auto corrupt_entries = rstd::move(opened_corrupt).unwrap();
    auto corrupt_entry   = corrupt_entries.next();
    ASSERT_TRUE(corrupt_entry.is_some());
    auto corrupt_result = rstd::move(corrupt_entry).unwrap();
    ASSERT_TRUE(corrupt_result.is_ok());
    ASSERT_TRUE(
        rstd::fs::write_atomic(corrupt_result->path().as_path(), ("{}"_str).as_bytes()).is_ok());
    auto corrupt_validation = tenon::doc::validate_data(corrupt_data.as_path());
    ASSERT_TRUE(corrupt_validation.is_err());
    EXPECT_TRUE(corrupt_validation.unwrap_err().as_str().contains("digest mismatch"_str));
    ASSERT_EQ(generated->packages.len(), usize(1));
    const auto& package = generated->packages[usize {}];
    EXPECT_EQ(package.symbols, usize(12));
    EXPECT_EQ(package.documented, usize(12));
    EXPECT_EQ(package.undocumented, usize {});
    EXPECT_EQ(package.unsupported, usize {});
    EXPECT_EQ(package.diagnostics, usize(1));
    ASSERT_EQ(package.diagnostic_details.len(), usize(1));
    EXPECT_EQ(package.diagnostic_details[usize {}].code.as_str(),
              "conflicting-symbol-documentation"_str);
    EXPECT_EQ(generated->frontend.source_reads, usize(3));
    EXPECT_EQ(generated->frontend.lex_builds, usize(3));
    EXPECT_EQ(generated->frontend.documentation_builds, usize(3));
    EXPECT_EQ(generated->frontend.documentation_declarations, usize(15));
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
    EXPECT_TRUE(json->as_str().contains("fixture::nested::Box::operator bool"_str));
    EXPECT_TRUE(json->as_str().contains("\"namespace\": \"fixture::nested\""_str));
    EXPECT_FALSE(json->as_str().contains("fixture::nested::T"_str));
    EXPECT_TRUE(json->as_str().contains("\"group\": \"Arithmetic\""_str));
    EXPECT_FALSE(json->as_str().contains("Inactive declaration documentation."_str));
    EXPECT_FALSE(json->as_str().contains("doxygen_hidden"_str));
    EXPECT_FALSE(json->as_str().contains("private member"_str));
    auto package_page = rstd::fs::read_to_string(package.index.as_path());
    ASSERT_TRUE(package_page.is_ok());
    EXPECT_TRUE(package_page->as_str().contains("Fixture module overview."_str));
    EXPECT_TRUE(package_page->as_str().contains("<code>child</code>"_str));
    EXPECT_FALSE(package_page->as_str().contains("<code>nested</code>"_str));
    EXPECT_FALSE(package_page->as_str().contains("Public API"_str));
    EXPECT_FALSE(package_page->as_str().contains("coverage-strip"_str));
    EXPECT_FALSE(package_page->as_str().contains(">Packages</div>"_str));
    EXPECT_TRUE(package_page->as_str().contains(">On this page</div>"_str));
    EXPECT_FALSE(package_page->as_str().contains("aria-label=\"Package modules\""_str));
    auto module_directory = package.directory.join(PathBuf::from("module"_str).as_path());
    auto opened_modules   = rstd::fs::read_dir(module_directory.as_path());
    ASSERT_TRUE(opened_modules.is_ok());
    auto module_entries    = rstd::move(opened_modules).unwrap();
    auto root_module_page  = String::make();
    auto child_module_page = String::make();
    for (auto next = module_entries.next(); next.is_some(); next = module_entries.next()) {
        auto entry = rstd::move(next).unwrap();
        ASSERT_TRUE(entry.is_ok());
        auto page = rstd::fs::read_to_string(entry->path().as_path());
        ASSERT_TRUE(page.is_ok());
        EXPECT_FALSE(page->as_str().contains("Reexports"_str));
        EXPECT_FALSE(page->as_str().contains(">Packages</div>"_str));
        if (page->as_str().contains("<h1><code>fixture.doc.basic</code></h1>"_str))
            root_module_page.push_str(page->as_str());
        if (page->as_str().contains("<h1><code>fixture.doc.basic:child</code></h1>"_str))
            child_module_page.push_str(page->as_str());
    }
    EXPECT_TRUE(root_module_page.as_str().contains("<code>child</code>"_str));
    EXPECT_FALSE(root_module_page.as_str().contains("<code>nested</code>"_str));
    EXPECT_FALSE(root_module_page.as_str().contains("aria-label=\"Package modules\""_str));
    EXPECT_FALSE(root_module_page.as_str().contains("Symbols"_str));
    EXPECT_FALSE(root_module_page.as_str().contains("kind-label"_str));
    EXPECT_TRUE(root_module_page.as_str().contains("id=\"namespaces\""_str));
    EXPECT_TRUE(root_module_page.as_str().contains(">Namespaces</span>"_str));
    EXPECT_TRUE(root_module_page.as_str().contains(">fixture::nested</code></a>"_str));
    EXPECT_TRUE(root_module_page.as_str().contains("id=\"structs\""_str));
    EXPECT_TRUE(root_module_page.as_str().contains(">Structs</span>"_str));
    EXPECT_TRUE(root_module_page.as_str().contains(">Widget</code></a>"_str));
    EXPECT_TRUE(root_module_page.as_str().contains(">Box</code></a>"_str));
    EXPECT_TRUE(root_module_page.as_str().contains("id=\"functions\""_str));
    EXPECT_TRUE(root_module_page.as_str().contains(">Functions</span>"_str));
    EXPECT_TRUE(root_module_page.as_str().contains(">add</code></a>"_str));
    EXPECT_TRUE(root_module_page.as_str().contains(">make</code></a>"_str));
    EXPECT_TRUE(root_module_page.as_str().contains(
        "<span class=\"namespace-label\">fixture::nested</span>"_str));
    EXPECT_FALSE(root_module_page.as_str().contains("fixture::nested::Widget::value"_str));
    EXPECT_FALSE(root_module_page.as_str().contains("fixture::nested::Box::value"_str));
    EXPECT_TRUE(root_module_page.as_str().contains("id=\"enums\""_str));
    EXPECT_TRUE(root_module_page.as_str().contains(">Enums</span>"_str));
    EXPECT_TRUE(root_module_page.as_str().contains(">Mode</code></a>"_str));
    EXPECT_TRUE(root_module_page.as_str().contains("id=\"concepts\""_str));
    EXPECT_TRUE(root_module_page.as_str().contains(">Concepts</span>"_str));
    EXPECT_TRUE(root_module_page.as_str().contains(">Value</code></a>"_str));
    EXPECT_TRUE(root_module_page.as_str().contains("id=\"aliases\""_str));
    EXPECT_TRUE(root_module_page.as_str().contains(">Aliases</span>"_str));
    EXPECT_TRUE(root_module_page.as_str().contains(">WidgetAlias</code></a>"_str));
    EXPECT_TRUE(root_module_page.as_str().contains("id=\"variables\""_str));
    EXPECT_TRUE(root_module_page.as_str().contains(">Variables</span>"_str));
    EXPECT_TRUE(root_module_page.as_str().contains(">answer</code></a>"_str));
    EXPECT_TRUE(
        root_module_page.as_str().contains("catalog-url=\"../../../search-index.json\""_str));
    EXPECT_TRUE(root_module_page.as_str().contains("current-package=\"fixture-doc-basic\""_str));
    EXPECT_TRUE(root_module_page.as_str().contains("current-module=\"fixture.doc.basic\""_str));
    EXPECT_TRUE(child_module_page.as_str().contains("<code>nested</code>"_str));
    EXPECT_TRUE(child_module_page.as_str().contains("aria-label=\"Package modules\""_str));
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
    EXPECT_FALSE(symbol_pages.as_str().contains(">Packages</div>"_str));
    auto source_directory = package.directory.join(PathBuf::from("source"_str).as_path());
    auto opened_sources   = rstd::fs::read_dir(source_directory.as_path());
    ASSERT_TRUE(opened_sources.is_ok());
    auto source_entries = rstd::move(opened_sources).unwrap();
    auto source_pages   = String::make();
    for (auto next = source_entries.next(); next.is_some(); next = source_entries.next()) {
        auto entry = rstd::move(next).unwrap();
        ASSERT_TRUE(entry.is_ok());
        auto page = rstd::fs::read_to_string(entry->path().as_path());
        ASSERT_TRUE(page.is_ok());
        source_pages.push_str(page->as_str());
    }
    EXPECT_TRUE(source_pages.as_str().contains("&lt;script&gt;"_str));
    EXPECT_FALSE(source_pages.as_str().contains("<script>"_str));
    EXPECT_TRUE(rstd::fs::exists(generated->index.as_path()).unwrap());
    auto restored = tenon::render_documentation(tenon::DocRenderRequest {
        .working_directory = project.clone(),
        .data              = data.clone(),
        .output            = restored_output.clone(),
    });
    ASSERT_TRUE(restored.is_ok());
    EXPECT_TRUE(restored->site_generated);
    auto original_index = rstd::fs::read_to_string(generated->index.as_path());
    auto restored_index = rstd::fs::read_to_string(restored->index.as_path());
    ASSERT_TRUE(original_index.is_ok());
    ASSERT_TRUE(restored_index.is_ok());
    EXPECT_TRUE(
        original_index->as_str().contains("<title>Overview · tenon-test-project</title>"_str));
    EXPECT_TRUE(original_index->as_str().contains("<h1>tenon-test-project</h1>"_str));
    EXPECT_FALSE(original_index->as_str().contains("Workspace documentation"_str));
    EXPECT_TRUE(original_index->as_str().contains("<tenon-doc-shell>"_str));
    EXPECT_TRUE(original_index->as_str().contains("<tenon-doc-search root-prefix="_str));
    EXPECT_TRUE(original_index->as_str().contains("catalog-url=\"search-index.json\""_str));
    EXPECT_FALSE(original_index->as_str().contains("static/search-index.js\"></script>"_str));
    EXPECT_TRUE(original_index->as_str().contains(
        "<link rel=\"icon\" href=\"static/favicon.svg\" type=\"image/svg+xml\">"_str));
    EXPECT_TRUE(original_index->as_str().contains("<tenon-doc-theme-picker>"_str));
    EXPECT_TRUE(original_index->as_str().contains("<span>Packages</span>"_str));
    EXPECT_FALSE(original_index->as_str().contains(">Packages</div>"_str));
    EXPECT_TRUE(original_index->as_str().contains(">On this page</div>"_str));
    EXPECT_FALSE(original_index->as_str().contains("class=\"page-outline\""_str));
    EXPECT_FALSE(original_index->as_str().contains("aria-label=\"Packages\""_str));
    EXPECT_FALSE(original_index->as_str().contains("data-root-prefix="_str));
    EXPECT_TRUE(symbol_pages.as_str().contains("<tenon-doc-module-identity"_str));
    EXPECT_EQ(original_index->as_str(), restored_index->as_str());
    EXPECT_EQ(regular_file_count(output.as_path()), regular_file_count(restored_output.as_path()));

    auto frontend_source = root("../doc/frontend/dist"_str);
    ASSERT_TRUE(copy_directory(frontend_source.as_path(), custom_frontend.as_path()));
    auto root_template   = custom_frontend.join(PathBuf::from("templates/root.html"_str).as_path());
    auto custom_template = rstd::fs::read_to_string(root_template.as_path());
    ASSERT_TRUE(custom_template.is_ok());
    auto marked_template = String::make("<!-- custom frontend -->\n"_str);
    marked_template.push_str(custom_template->as_str());
    ASSERT_TRUE(rstd::fs::write_atomic(root_template.as_path(), marked_template.as_str().as_bytes())
                    .is_ok());
    auto custom = tenon::render_documentation(tenon::DocRenderRequest {
        .working_directory = project.clone(),
        .data              = data.clone(),
        .output            = custom_output.clone(),
        .frontend          = Some(custom_frontend.clone()),
    });
    ASSERT_TRUE(custom.is_ok());
    auto custom_index = rstd::fs::read_to_string(custom->index.as_path());
    ASSERT_TRUE(custom_index.is_ok());
    EXPECT_TRUE(custom_index->as_str().starts_with("<!-- custom frontend -->"_str));
    ASSERT_TRUE(
        rstd::fs::write_atomic(root_template.as_path(), ("{{missing.value}}"_str).as_bytes())
            .is_ok());
    auto missing_value = tenon::render_documentation(tenon::DocRenderRequest {
        .working_directory = project.clone(),
        .data              = data.clone(),
        .output            = custom_output.clone(),
        .frontend          = Some(custom_frontend.clone()),
    });
    ASSERT_TRUE(missing_value.is_err());
    EXPECT_TRUE(missing_value.unwrap_err().message.as_str().contains(
        "templates/root.html:1:1: missing template value 'missing.value'"_str));
    auto preserved_index = rstd::fs::read_to_string(custom->index.as_path());
    ASSERT_TRUE(preserved_index.is_ok());
    EXPECT_EQ(preserved_index->as_str(), custom_index->as_str());
    ASSERT_TRUE(rstd::fs::write_atomic(root_template.as_path(),
                                       ("{{> templates/root.html}}"_str).as_bytes())
                    .is_ok());
    auto partial_cycle = tenon::render_documentation(tenon::DocRenderRequest {
        .working_directory = project.clone(),
        .data              = data.clone(),
        .output            = custom_output.clone(),
        .frontend          = Some(custom_frontend.clone()),
    });
    ASSERT_TRUE(partial_cycle.is_err());
    EXPECT_TRUE(partial_cycle.unwrap_err().message.as_str().contains(
        "template partial cycle through 'templates/root.html'"_str));
    auto extra = custom_frontend.join(PathBuf::from("undeclared.txt"_str).as_path());
    ASSERT_TRUE(rstd::fs::write_atomic(extra.as_path(), ("undeclared"_str).as_bytes()).is_ok());
    EXPECT_TRUE(tenon::render_documentation(tenon::DocRenderRequest {
                                                .working_directory = project.clone(),
                                                .data              = data.clone(),
                                                .output            = custom_output.clone(),
                                                .frontend          = Some(custom_frontend.clone()),
                                            })
                    .is_err());

    auto data_only = tenon::generate_documentation(tenon::DocRequest {
        .selection =
            tenon::PackageSelection {
                .root     = project.clone(),
                .packages = strings("fixture-doc-basic"_str),
            },
        .output        = unused_site.clone(),
        .data_output   = data_only_output.clone(),
        .configuration = configuration(),
        .data_only     = true,
        .locked        = true,
    });
    ASSERT_TRUE(data_only.is_ok());
    EXPECT_FALSE(data_only->site_generated);
    EXPECT_FALSE(rstd::fs::exists(unused_site.as_path()).unwrap());
    EXPECT_TRUE(tenon::doc::validate_data(data_only_output.as_path()).is_ok());
    auto regenerated = tenon::generate_documentation(tenon::DocRequest {
        .selection =
            tenon::PackageSelection {
                .root     = project.clone(),
                .packages = strings("fixture-doc-basic"_str),
            },
        .output        = output.clone(),
        .data_output   = data.clone(),
        .configuration = configuration(),
        .locked        = true,
    });
    ASSERT_TRUE(regenerated.is_ok());
    auto second_json = rstd::fs::read_to_string(regenerated->packages[usize {}].json.as_path());
    ASSERT_TRUE(second_json.is_ok());
    EXPECT_EQ(second_json->as_str(), json->as_str());
    EXPECT_TRUE(clear_output(output.as_path()));
    EXPECT_TRUE(clear_output(data.as_path()));
    EXPECT_TRUE(clear_output(restored_output.as_path()));
    EXPECT_TRUE(clear_output(data_only_output.as_path()));
    EXPECT_TRUE(clear_output(custom_frontend.as_path()));
    EXPECT_TRUE(clear_output(custom_output.as_path()));
    EXPECT_TRUE(clear_output(corrupt_data.as_path()));

    auto rejected = tenon::generate_documentation(tenon::DocRequest {
        .selection =
            tenon::PackageSelection {
                .root     = project.clone(),
                .packages = strings("fixture-test-app"_str),
            },
        .output        = output.clone(),
        .data_output   = data.clone(),
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
    auto bmi_directory = output.join(PathBuf::from("bmi"_str).as_path());
    auto cold_bmis     = regular_file_count(bmi_directory.as_path());
    ASSERT_TRUE(cold_bmis.is_some());
    EXPECT_EQ(*cold_bmis, usize(1));

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
