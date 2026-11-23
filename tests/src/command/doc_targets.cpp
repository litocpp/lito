#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.driver;
import lito.test.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito_test;

class DocumentationTargets : public ProjectFixture {};

TEST_F(DocumentationTargets, PreservesLibraryPluginAndProcMacroCompileContexts) {
    const ref<str> kinds[] = { "lib"_str, "plugin"_str, "pmacro"_str };
    for (auto kind : kinds) {
        auto              manifest = rstd::format(R"toml([package]
name = "doc_target"
version = "0.1.0"
[{}]
module = "doc_target"
sources = ["api.cppm"]
)toml",
                                                  kind);
        const ProjectFile files[]  = {
            { "builtin/lito.toml"_str,
              "[package]\nname = \"pmacro\"\nversion = \"0.1.0\"\n[plugin]\nmodule = \"pmacro\"\nsources = [\"api.cppm\"]\n"_str },
            { "builtin/api.cppm"_str, "export module pmacro;\n"_str },
            { "lito.toml"_str, manifest.as_str() },
            { "api.cppm"_str,
              "export module doc_target;\nexport int documented_value() { return 42; }\n"_str },
        };
        auto project = materialize(kind, files);
        ASSERT_TRUE(project.is_ok());
        auto request =
            build_request(project->root.as_path(), build_root(kind).as_path(), Vec<String>::make());
        request.purpose              = lito::package::PackageSelectionPurpose::Documentation;
        request.result.documentation = true;
        if (kind == "pmacro"_str)
            request.sources.builtin_packages.push(lito::source::BuiltinPackageSourceEntry {
                .id     = "pmacro"_Str,
                .source = lito::source::BuiltinPackageSource::Path(
                    project->root.join(rstd::path::PathBuf::from("builtin"_str).as_path())),
            });
        auto summary = lito::build(request);
        if (summary.is_err()) {
            rstd::test::fail_current(
                error_chain_text(summary.unwrap_err()).as_str(), __FILE__, __LINE__, true);
            return;
        }
        ASSERT_EQ(summary->documentation_units.len(), usize(1));
        const auto& unit = summary->documentation_units[usize()];
        EXPECT_EQ(unit.target.package.as_str(), "doc_target"_str);
        auto expected = kind == "lib"_str      ? lito::package::PackageTargetKind::Library
                        : kind == "plugin"_str ? lito::package::PackageTargetKind::Plugin
                                               : lito::package::PackageTargetKind::ProcMacro;
        EXPECT_EQ(unit.target.kind, expected);
        EXPECT_FALSE(unit.compiler.build_identity.is_empty());
        EXPECT_FALSE(unit.compiler.target.is_empty());
        EXPECT_FALSE(unit.language_standard.is_empty());
        ASSERT_EQ(summary->selected_packages.len(), usize(1));
        EXPECT_EQ(summary->selected_packages[usize()].name.as_str(), "doc_target"_str);
    }
}

TEST_F(DocumentationTargets, KeepsMixedRootsAndExcludesHostToolDependencies) {
    const ProjectFile files[] = {
        { "lito.toml"_str,
          "[workspace]\nname = \"doc_workspace\"\nmembers = [\"library\", \"plugin\"]\n"_str },
        { "library/lito.toml"_str, R"toml([package]
name = "doc_library"
version = "0.1.0"
[lib]
module = "doc_library"
sources = ["api.cppm"]
[dependencies.doc_tool]
path = "../tool"
usage = "tool"
)toml"_str },
        { "library/api.cppm"_str, "export module doc_library;\nexport int answer = 42;\n"_str },
        { "plugin/lito.toml"_str, R"toml([package]
name = "doc_plugin"
version = "0.1.0"
[plugin]
module = "doc_plugin"
attribute-only = true
sources = ["api.cppm"]
)toml"_str },
        { "plugin/api.cppm"_str, "export module doc_plugin;\nexport int marker = 1;\n"_str },
        { "tool/lito.toml"_str, R"toml([package]
name = "doc_tool"
version = "0.1.0"
[[bin]]
name = "doc_tool"
host-tool = true
link-stdlib = false
sources = ["main.cpp"]
)toml"_str },
        { "tool/main.cpp"_str, "int main() { return 0; }\n"_str },
    };
    auto project = materialize("mixed"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto request = build_request(
        project->root.as_path(), build_root("mixed"_str).as_path(), Vec<String>::make());
    request.purpose              = lito::package::PackageSelectionPurpose::Documentation;
    request.result.documentation = true;
    auto summary                 = lito::build(request);
    if (summary.is_err()) {
        rstd::test::fail_current(
            error_chain_text(summary.unwrap_err()).as_str(), __FILE__, __LINE__, true);
        return;
    }
    ASSERT_EQ(summary->documentation_units.len(), usize(2));
    bool library = false, plugin = false;
    for (const auto& unit : summary->documentation_units) {
        library |= unit.target.package.as_str() == "doc_library"_str;
        plugin |= unit.target.package.as_str() == "doc_plugin"_str;
        EXPECT_NE(unit.target.package.as_str(), "doc_tool"_str);
    }
    EXPECT_TRUE(library);
    EXPECT_TRUE(plugin);
}
