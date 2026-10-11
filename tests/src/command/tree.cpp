#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.driver;
import lito.core;
import lito.system;
import lito.test.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito_test;
using namespace lito::package;
using PathBuf = rstd::path::PathBuf;

class TreeCommand : public ProjectFixture {};

template<typename T>
auto tree_succeeded(lito::CommandResult<T>& result) -> bool {
    if (result.is_ok()) return true;
    auto message = error_chain_text(result.unwrap_err());
    rstd::test::fail_current(message.as_str(), __FILE__, __LINE__, true);
    return false;
}

auto tree_request(ref<rstd::path::Path> root) -> lito::TreeRequest {
    auto request             = lito::TreeRequest {};
    request.selection.root   = PathBuf::from(root);
    request.configuration    = lito::config::build_configuration_request(configuration());
    request.sources.network  = lito::source::NetworkPolicy::Offline;
    request.tools.pkg_config = PathBuf::from("/missing/tree-pkg-config"_str);
    request.tools.cmake      = PathBuf::from("/missing/tree-cmake"_str);
    request.tools.cargo      = PathBuf::from("/missing/tree-cargo"_str);
    return request;
}

TEST_F(TreeCommand, OfflineRemoteMetadataNeedsCachedSource) {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "tree-offline-root"
version = "0.1.0"
[[bin]]
name = "tree-offline-root"
link-stdlib = false
sources = ["main.cpp"]
[dependencies.tree-offline-remote]
git = "https://example.invalid/lito-tree-offline-metadata.git"
)toml"_str },
        { "main.cpp"_str, "#error no scan\n"_str },
    };
    auto project = materialize("tree-offline"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto result = lito::tree(tree_request(project->root.as_path()));
    ASSERT_TRUE(result.is_err());
    EXPECT_TRUE(error_chain_text(result.unwrap_err())
                    .as_str()
                    .contains("offline source resolution cannot fetch Git source"_str));
}

TEST_F(TreeCommand, ConditionsSelectPackagesWithoutExecutingScriptsOrExternalTools) {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "root"
version = "0.1.0"
[[bin]]
name = "root"
link-stdlib = false
sources = ["main.cpp"]
[features.image]
default = false
[dependencies]
image = {path = "image", condition = 'feature.image && profile.name == "debug"'}
[external-dependencies.pkg-config.missing]
module = "lito-tree-nonexistent"
usage = "compile"
[external-dependencies.cmake.missing]
package = "Missing"
targets = [{name = "Missing::Missing"}]
[external-sources.rust]
path = "rust"
[external-dependencies.cargo.missing]
source = "rust"
package = "missing"
)toml"_str },
        { "main.cpp"_str, "#error tree must not scan sources\n"_str },
        { "build.lua"_str, "error('tree must not run build.lua')\n"_str },
        { "install.lua"_str, "error('tree must not run install.lua')\n"_str },
        { "rust/Cargo.toml"_str, "this is deliberately not a Cargo manifest\n"_str },
        { "image/lito.toml"_str, R"toml([package]
name = "image"
version = "0.1.0"
[lib]
name = "image"
archive = "image"
module = "tree.image"
)toml"_str },
        { "image/build.lua"_str, "error('tree must not run dependency build.lua')\n"_str },
    };
    auto project = materialize("tree-conditions"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto request  = tree_request(project->root.as_path());
    auto disabled = lito::tree(request);
    ASSERT_TRUE(tree_succeeded(disabled));
    EXPECT_EQ(disabled->selection.active_graph.dependencies.len(), usize {});
    auto locked = lito::lock::load_locked_project(project->root.as_path());
    ASSERT_TRUE(locked.is_ok());
    EXPECT_EQ(locked->packages.len(), usize(2));
    request.locked = true;
    request.selection.features.enabled.push("image"_Str);
    auto enabled = lito::tree(request);
    ASSERT_TRUE(tree_succeeded(enabled));
    EXPECT_EQ(enabled->selection.active_graph.dependencies.len(), usize(1));
    auto text = lito::render_tree(*enabled);
    ASSERT_TRUE(text.is_ok());
    EXPECT_TRUE(text->as_str().contains("image v0.1.0"_str));
    request.profile = Some(build_profile("release"_str));
    auto release    = lito::tree(request);
    ASSERT_TRUE(release.is_ok());
    EXPECT_TRUE(release->selection.active_graph.dependencies.is_empty());
    request.profile = Some(build_profile("unknown-tree-profile"_str));
    EXPECT_TRUE(lito::tree(request).is_err());
}

TEST_F(TreeCommand, PurposesEnableOnlyTheirDevelopmentAndRuntimeEdges) {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "root"
version = "0.1.0"
autoexamples = false
[[bin]]
name = "root"
link-stdlib = false
sources = ["main.cpp"]
[[test]]
name = "root-test"
link-stdlib = false
sources = ["main.cpp"]
[[bench]]
name = "root-bench"
link-stdlib = false
sources = ["main.cpp"]
[[example]]
name = "root-example"
link-stdlib = false
sources = ["main.cpp"]
[dev-dependencies]
development = {path = "development"}
[runtime-dependencies]
runtime = {path = "runtime"}
)toml"_str },
        { "main.cpp"_str, "#error tree must not scan\n"_str },
        { "development/lito.toml"_str, R"toml([package]
name = "development"
version = "0.1.0"
[lib]
name = "development"
archive = "development"
module = "tree.development"
)toml"_str },
        { "runtime/lito.toml"_str, "[package]\nname = 'runtime'\nversion = '0.1.0'\n"_str },
        { "runtime/install.lua"_str, "error('tree must not run runtime recipe')\n"_str },
    };
    auto project = materialize("tree-purposes"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto request    = tree_request(project->root.as_path());
    auto production = lito::tree(request);
    ASSERT_TRUE(tree_succeeded(production));
    EXPECT_TRUE(production->selection.active_graph.dependencies.is_empty());
    constexpr PackageSelectionPurpose purposes[] = {
        PackageSelectionPurpose::Test,
        PackageSelectionPurpose::Benchmark,
        PackageSelectionPurpose::Example,
    };
    for (auto purpose : purposes) {
        request.purpose = purpose;
        auto report     = lito::tree(request);
        ASSERT_TRUE(tree_succeeded(report));
        ASSERT_EQ(report->selection.active_graph.dependencies.len(), usize(1));
        EXPECT_EQ(report->selection.active_graph.dependencies[usize {}].kind,
                  DependencyKind::Development);
        auto text = lito::render_tree(*report);
        ASSERT_TRUE(text.is_ok());
        EXPECT_TRUE(text->as_str().contains("[dev-dependencies]"_str));
        EXPECT_FALSE(text->as_str().contains("[runtime-dependencies]"_str));
    }
    request.purpose = PackageSelectionPurpose::Install;
    auto installed  = lito::tree(request);
    ASSERT_TRUE(tree_succeeded(installed));
    ASSERT_EQ(installed->selection.active_graph.dependencies.len(), usize(1));
    EXPECT_EQ(installed->selection.active_graph.dependencies[usize {}].kind,
              DependencyKind::Runtime);
    auto text = lito::render_tree(*installed);
    ASSERT_TRUE(text.is_ok());
    EXPECT_TRUE(text->as_str().contains("[runtime-dependencies]"_str));
}

TEST_F(TreeCommand, ScriptRootsAndWorkspaceDefaultsUseMetadataSelection) {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"toml([workspace]
name = "tree-workspace"
members = ["a", "b"]
default-members = ["a"]
)toml"_str },
        { "a/lito.toml"_str, R"toml([package]
name = "a"
version = "0.1.0"
[script]
supports = ["build"]
[dependencies]
b = {path = "../b"}
)toml"_str },
        { "a/lib.lua"_str, "error('tree must not execute script packages')\n"_str },
        { "b/lito.toml"_str,
          "[package]\nname = 'b'\nversion = '0.1.0'\n[script]\nsupports = ['build']\n"_str },
        { "b/lib.lua"_str, "return {}\n"_str },
    };
    auto project = materialize("tree-workspace"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto request = tree_request(project->root.as_path());
    auto report  = lito::tree(request);
    ASSERT_TRUE(tree_succeeded(report));
    ASSERT_EQ(report->selection.active_graph.roots.len(), usize(1));
    EXPECT_EQ(report->selection.selected_root_names[usize {}].as_str(), "a"_str);
    auto text = lito::render_tree(*report);
    ASSERT_TRUE(text.is_ok());
    EXPECT_TRUE(text->as_str().contains("b [script]"_str) ||
                text->as_str().contains(" [script]"_str));
    request.selection.packages = strings("b"_str);
    auto b                     = lito::tree(request);
    ASSERT_TRUE(b.is_ok());
    EXPECT_TRUE(b->selection.active_graph.dependencies.is_empty());
    request.selection.packages = strings("b"_str, "a"_str);
    EXPECT_TRUE(lito::tree(request).is_ok());
    request.selection.packages = strings("b"_str, "b"_str);
    EXPECT_TRUE(lito::tree(request).is_err());
    request.selection.packages = strings("missing"_str);
    EXPECT_TRUE(lito::tree(request).is_err());
}

TEST_F(TreeCommand, CompleteLockPreservesExternalSourcesAndRejectsStaleCandidates) {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "root"
version = "0.1.0"
[[bin]]
name = "root"
sources = ["main.cpp"]
link-stdlib = false
[external-sources.payload]
archive = "https://example.invalid/lito-tree-payload.tar.gz"
sha256 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
[dependencies]
candidate = {path = "candidate", condition = "false"}
)toml"_str },
        { "main.cpp"_str, "#error no scan\n"_str },
        { "payload/data.txt"_str, "payload\n"_str },
        { "candidate/lito.toml"_str,
          "[package]\nname = 'candidate'\nversion = '0.1.0'\n[lib]\nname = 'candidate'\narchive = 'candidate'\nmodule = 'tree.candidate'\n"_str },
    };
    auto project = materialize("tree-lock"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto request   = tree_request(project->root.as_path());
    request.locked = true;
    EXPECT_TRUE(lito::tree(request).is_err());
    request.locked = false;
    auto first     = lito::tree(request);
    ASSERT_TRUE(tree_succeeded(first));
    auto path   = project->root.join(PathBuf::from("lito.lock"_str).as_path());
    auto before = rstd::fs::read_to_string(path.as_path()).unwrap();
    EXPECT_TRUE(before.as_str().contains("payload"_str));
    EXPECT_TRUE(before.as_str().contains("candidate"_str));
    request.locked = true;
    auto frozen    = lito::tree(request);
    ASSERT_TRUE(frozen.is_ok());
    EXPECT_EQ(rstd::fs::read_to_string(path.as_path()).unwrap().as_str(), before.as_str());
    auto candidate = project->root.join(PathBuf::from("candidate/lito.toml"_str).as_path());
    ASSERT_TRUE(
        rstd::fs::write_atomic(
            candidate.as_path(),
            "[package]\nname = 'candidate'\nversion = '0.1.1'\n[lib]\nname = 'candidate'\narchive = 'candidate'\nmodule = 'tree.candidate'\n"_str
                .as_bytes())
            .is_ok());
    EXPECT_TRUE(lito::tree(request).is_err());
}
