#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.core;
import lito.test.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

using namespace lito_test;

class InlineProjectFixture : public ProjectFixture {};

TEST(SourceTree, ValidatesPortablePathsAndTopology) {
    constexpr ref<str> invalid_paths[] = {
        ""_str,          "/absolute"_str, "trailing/"_str,        "double//component"_str,
        "."_str,         "../escape"_str, "nested/../escape"_str, "windows\\path"_str,
        "C:/prefix"_str,
    };
    for (auto path : invalid_paths) {
        auto tree = lito::source::SourceTree::make();
        EXPECT_TRUE(tree.add_text(path, "value"_str).is_err());
    }

    auto tree = lito::source::SourceTree::make();
    ASSERT_TRUE(tree.add_text("src"_str, "file"_str).is_ok());
    EXPECT_TRUE(tree.add_text("src/lib.cppm"_str, "module"_str).is_err());
    EXPECT_TRUE(tree.add_directory("src"_str).is_err());

    auto nested = lito::source::SourceTree::make();
    ASSERT_TRUE(nested.add_text("src/lib.cppm"_str, "module"_str).is_ok());
    EXPECT_TRUE(nested.add_text("src"_str, "file"_str).is_err());
    ASSERT_TRUE(nested.add_directory("src"_str).is_ok());
    EXPECT_TRUE(nested.remove("src"_str).is_err());
}

TEST(SourceTree, CombinesAndMutatesOnlyThroughExplicitOperations) {
    auto base = lito::source::SourceTree::make();
    ASSERT_TRUE(base.add_text("lito.toml"_str, "first"_str).is_ok());
    ASSERT_TRUE(base.add_text("src/lib.cppm"_str, "library"_str).is_ok());

    auto addition = lito::source::SourceTree::make();
    ASSERT_TRUE(addition.add_text("src/lib.cppm"_str, "duplicate"_str).is_ok());
    ASSERT_TRUE(addition.add_text("src/value.cpp"_str, "value"_str).is_ok());
    auto before = base.clone();
    EXPECT_TRUE(base.extend(addition).is_err());
    ASSERT_EQ(base.entries().len(), before.entries().len());
    EXPECT_EQ(base.entries()[usize {}].path().as_str(), before.entries()[usize {}].path().as_str());

    ASSERT_TRUE(base.replace_text("lito.toml"_str, "second"_str).is_ok());
    EXPECT_TRUE(base.replace_text("missing"_str, "value"_str).is_err());
    ASSERT_TRUE(base.remove("src/lib.cppm"_str).is_ok());
    EXPECT_TRUE(base.remove("src/lib.cppm"_str).is_err());
}

TEST(SourceTree, MaterializesForTheExistingManifestOwnerWithoutOverwriting) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto owner = rstd::move(temporary).unwrap();

    auto tree = lito::source::SourceTree::make();
    ASSERT_TRUE(tree.add_text("lito.toml"_str,
                              "[package]\n"
                              "name = \"inline-source-tree\"\n"
                              "version = \"0.1.0\"\n"
                              "\n"
                              "[lib]\n"
                              "name = \"inline-source-tree\"\n"
                              "module = \"inline.source.tree\"\n"
                              "archive = \"inline-source-tree\"\n"
                              "sources = [\"src/lib.cppm\"]\n"_str)
                    .is_ok());
    ASSERT_TRUE(
        tree.add_text("src/lib.cppm"_str, "export module inline.source.tree;\n"_str).is_ok());
    ASSERT_TRUE(tree.add_directory("empty"_str).is_ok());
    ASSERT_TRUE(tree.add_text("tools/helper"_str,
                              "#!/bin/sh\nexit 0\n"_str,
                              lito::source::SourceFileMode::Executable)
                    .is_ok());

    auto root         = PathBuf::from(owner.path()).join(PathBuf::from("project"_str).as_path());
    auto materialized = lito::source::materialize_source_tree(tree, root.as_path());
    ASSERT_TRUE(materialized.is_ok());
    EXPECT_EQ(materialized->entries, usize(4));
    EXPECT_EQ(
        rstd::fs::read_to_string(root.join(PathBuf::from("src/lib.cppm"_str).as_path()).as_path())
            .unwrap()
            .as_str(),
        "export module inline.source.tree;\n"_str);
    EXPECT_TRUE(rstd::fs::metadata(root.join(PathBuf::from("empty"_str).as_path()).as_path())
                    .unwrap()
                    .is_dir());
#if RSTD_OS_UNIX
    auto tool =
        rstd::fs::metadata(root.join(PathBuf::from("tools/helper"_str).as_path()).as_path());
    ASSERT_TRUE(tool.is_ok());
    EXPECT_NE(tool->permissions().mode() & u32(0111), u32 {});
#endif

    auto manifest = lito::manifest::load_package_manifest(root.as_path());
    if (manifest.is_err()) {
        auto message = error_chain_text(manifest.unwrap_err());
        rstd::test::fail_current(message.as_str(), __FILE__, __LINE__, true);
        return;
    }
    EXPECT_EQ(manifest->name.as_str(), "inline-source-tree"_str);

    auto occupied = PathBuf::from(owner.path()).join(PathBuf::from("occupied"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir(occupied.as_path()).is_ok());
    auto marker = occupied.join(PathBuf::from("marker"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(marker.as_path(), "preserved"_str.as_bytes()).is_ok());
    EXPECT_TRUE(lito::source::materialize_source_tree(tree, occupied.as_path()).is_err());
    EXPECT_EQ(rstd::fs::read_to_string(marker.as_path()).unwrap().as_str(), "preserved"_str);
}

TEST_F(InlineProjectFixture, OwnsIndependentProjectAndOutputRoots) {
    auto tree = lito::source::SourceTree::make();
    ASSERT_TRUE(tree.add_text("lito.toml"_str,
                              "[package]\n"
                              "name = \"inline-project\"\n"
                              "version = \"0.1.0\"\n"
                              "\n"
                              "[lib]\n"
                              "name = \"inline-project\"\n"
                              "module = \"inline.project\"\n"
                              "archive = \"inline-project\"\n"
                              "sources = [\"src/lib.cppm\"]\n"_str)
                    .is_ok());
    ASSERT_TRUE(tree.add_text("src/lib.cppm"_str, "export module inline.project;\n"_str).is_ok());

    auto project = materialize("project"_str, tree);
    ASSERT_TRUE(project.is_ok());
    auto request = project_build_request(
        "project"_str, project->root.as_path(), strings("inline-project"_str));
    EXPECT_EQ(request.selection.root.as_path(), project->root.as_path());
    EXPECT_TRUE(request.build_directory.as_path().starts_with(temp_root()));
    EXPECT_NE(request.build_directory.as_path(), project->root.as_path());
    EXPECT_TRUE(install_root("project"_str).as_path().starts_with(temp_root()));
    EXPECT_TRUE(cache_root("project"_str).as_path().starts_with(temp_root()));
    EXPECT_TRUE(tool_root("project"_str).as_path().starts_with(temp_root()));
}
