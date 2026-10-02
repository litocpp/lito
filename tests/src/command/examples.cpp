#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.core;
import lito.cpp;
import lito.driver;
import lito.pack;
import lito.test.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito_test;

class Examples : public ProjectFixture {};

TEST_F(Examples, DiscoversEntriesAndKeepsPackageBoundaries) {
    const ProjectFile files[] = {
        { "lito.toml"_str,
          "[package]\nname = 'examples'\nversion = '0.1.0'\n[[example]]\nname = '2048'\nlink-stdlib = false\n"_str },
        { "examples/2048/main.cppm"_str,
          "export module demo.game;\nint main() { return 0; }\n"_str },
        { "examples/simple.cpp"_str, "int main() { return 0; }\n"_str },
        { "examples/common/support.cppm"_str, "export module support;\n"_str },
        { "examples/nested/lito.toml"_str, "[package]\nname = 'nested'\n"_str },
        { "examples/nested/main.cpp"_str, "#error should not be discovered\n"_str },
    };
    auto project = materialize("example-discovery"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto document = lito::manifest::load_manifest_document(project->root.as_path());
    ASSERT_TRUE(document.is_ok());
    const auto& targets = document->package->targets;
    ASSERT_EQ(targets.len(), usize(2));
    ASSERT_TRUE(targets[usize {}].is_Example());
    const auto& game = targets[usize {}].as_Example();
    EXPECT_EQ(game.name.as_str(), "2048"_str);
    EXPECT_FALSE(game.link_stdlib);
    EXPECT_EQ(game.source.entry->as_path().to_str().unwrap(), "examples/2048/main.cppm"_str);
    EXPECT_EQ(game.source.module_root->as_path().to_str().unwrap(), "examples/2048"_str);
    EXPECT_EQ(game.source.discovery, lito::manifest::SourceDiscoveryMode::Module);
    const auto& simple = targets[usize(1)].as_Example();
    EXPECT_EQ(simple.source.discovery, lito::manifest::SourceDiscoveryMode::Explicit);
    EXPECT_EQ(simple.source.declared_sources.len(), usize(1));
}

TEST_F(Examples, ExplicitEntryOverridesAmbiguityAndDisabledDiscovery) {
    const ProjectFile files[] = {
        { "lito.toml"_str,
          "[package]\nname = 'examples'\nversion = '0.1.0'\nautoexamples = false\n[[example]]\nname = 'same'\npath = 'custom/main.cppm'\n"_str },
        { "custom/main.cppm"_str, "export module custom;\nint main() { return 0; }\n"_str },
        { "examples/same.cpp"_str, "#error unused\n"_str },
        { "examples/same/main.cpp"_str, "#error unused\n"_str },
        { "examples/other.cpp"_str, "#error unused\n"_str },
    };
    auto project = materialize("example-explicit"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto loaded = lito::manifest::load_manifest_document(project->root.as_path());
    ASSERT_TRUE(loaded.is_ok());
    EXPECT_EQ(loaded->package->targets.len(), usize(1));
    EXPECT_EQ(
        loaded->package->targets[usize {}].as_Example().source.entry->as_path().to_str().unwrap(),
        "custom/main.cppm"_str);
}

TEST_F(Examples, RejectsInvalidLayoutsAndExampleFields) {
    const ref<str> cases[] = {
        "[[example]]\nname = 'bad'\npath = 'custom/main.cppm'\nsources = ['custom/main.cppm']\n"_str,
        "[[example]]\nname = 'bad'\npath = '../main.cppm'\n"_str,
        "[[example]]\nname = 'bad'\npath = 'missing.cppm'\n"_str,
        "[[example]]\nname = 'bad'\npath = 'custom/main.cppm'\nsource-root = 'custom/main.cppm'\n"_str,
        "[[example]]\nname = 'bad'\npath = 'nested/main.cppm'\n"_str,
        "[[example]]\nname = 'bad'\npath = 'custom/main.cppm'\nhost-tool = true\n"_str,
        "[[example]]\nname = 'bad'\npath = 'custom/main.cppm'\nattach = []\n"_str,
        "[[example]]\nname = 'bad'\n"_str,
        "[[example]]\nname = 'bad'\npath = 'custom/main.cppm'\n[[example]]\nname = 'bad'\npath = 'custom/main.cppm'\n"_str,
    };
    for (usize i {}; i < usize(sizeof(cases) / sizeof(cases[0])); ++i) {
        auto contents = rstd::format("[package]\nname = 'invalid'\nversion = '0.1.0'\n{}",
                                     cases[i.to_primitive()]);
        const ProjectFile files[] = {
            { "lito.toml"_str, contents.as_str() },
            { "custom/main.cppm"_str, "export module custom;\n"_str },
            { "nested/lito.toml"_str, "[package]\nname = 'nested'\n"_str },
            { "nested/main.cppm"_str, "export module nested;\n"_str },
        };
        auto name    = rstd::format("invalid-example-{}", i);
        auto project = materialize(name.as_str(), files);
        ASSERT_TRUE(project.is_ok());
        EXPECT_TRUE(lito::manifest::load_manifest_document(project->root.as_path()).is_err());
    }
}

TEST_F(Examples, RejectsAmbiguousAutomaticEntries) {
    const ProjectFile files[] = {
        { "lito.toml"_str, "[package]\nname = 'ambiguous'\nversion = '0.1.0'\n"_str },
        { "examples/same.cpp"_str, "int main() { return 0; }\n"_str },
        { "examples/same/main.cppm"_str, "export module same;\n"_str },
    };
    auto project = materialize("ambiguous-examples"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto loaded = lito::manifest::load_manifest_document(project->root.as_path());
    ASSERT_TRUE(loaded.is_err());
}

TEST_F(Examples, BuildsIsolatedClosuresAndRunsWithArgumentsAndExitStatus) {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "example-app"
version = "0.1.0"
[lib]
name = "support"
module = "support"
archive = "support"
path = "common/lib.cppm"
source-root = "common"
[[bin]]
name = "gallery"
sources = ["production.cpp"]
link-stdlib = false
[[example]]
name = "gallery"
path = "gallery/main.cppm"
link-stdlib = false
[[example]]
name = "2048"
path = "2048/main.cppm"
link-stdlib = false
[[example]]
name = "controls"
path = "controls/main.cpp"
link-stdlib = false
)toml"_str },
        { "common/lib.cppm"_str,
          "export module support;\nexport int answer() { return 42; }\n"_str },
        { "production.cpp"_str, "int main() { return 0; }\n"_str },
        { "gallery/main.cppm"_str,
          "export module demo.gallery;\nimport support;\nint main() { return answer() == 42 ? 0 : 1; }\n"_str },
        { "gallery/unused.cppm"_str, "#error unimported file\n"_str },
        { "2048/main.cppm"_str,
          "export module demo.game;\nimport :view;\nimport support;\nint main() { return game() == answer() ? 0 : 1; }\n"_str },
        { "2048/view.cppm"_str,
          "export module demo.game:view;\nimport :game;\nexport int game() { return value(); }\n"_str },
        { "2048/game.cppm"_str,
          "export module demo.game:game;\nexport int value() { return 42; }\n"_str },
        { "controls/main.cpp"_str,
          "#include <cstdio>\n#include <cstring>\nint main(int argc, char** argv) { auto* f = std::fopen(\"marker.txt\", \"r\"); if (!f) return 9; std::fclose(f); return argc == 2 && std::strcmp(argv[1], \"two words\") == 0 ? 7 : 8; }\n"_str },
        { "marker.txt"_str, "package working directory\n"_str },
    };
    auto project = materialize("example-build"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto output  = build_root("example-build"_str);
    auto request = [&]() {
        return build_request(project->root.as_path(), output.as_path(), strings("example-app"_str));
    };
    auto production = lito::build(request());
    if (production.is_err()) rstd::io::eprintln("{}", production.unwrap_err());
    ASSERT_TRUE(production.is_ok());
    EXPECT_EQ(artifact_count(*production, lito::cpp::ArtifactKind::ExampleExecutable), usize {});
    EXPECT_EQ(artifact_count(*production, lito::cpp::ArtifactKind::Executable), usize(1));
    auto gallery_request    = request();
    gallery_request.purpose = lito::package::PackageSelectionPurpose::Example;
    gallery_request.targets = strings("example:gallery"_str);
    auto gallery            = lito::build(gallery_request);
    if (gallery.is_err()) rstd::io::eprintln("{}", gallery.unwrap_err());
    ASSERT_TRUE(gallery.is_ok());
    EXPECT_EQ(artifact_count(*gallery, lito::cpp::ArtifactKind::ExampleExecutable), usize(1));
    EXPECT_EQ(artifact_count(*gallery, lito::cpp::ArtifactKind::Executable), usize {});
    EXPECT_EQ(gallery->scanned, usize(2));
    auto repeated = lito::build(gallery_request);
    ASSERT_TRUE(repeated.is_ok());
    EXPECT_EQ(repeated->compiled, usize {});
    auto game = lito::run(lito::RunRequest { .build = request(), .example = "2048"_Str });
    if (game.is_err()) rstd::io::eprintln("{}", game.unwrap_err());
    ASSERT_TRUE(game.is_ok());
    EXPECT_TRUE(game->execution.success());
    auto controls = lito::run(lito::RunRequest {
        .build = request(), .example = "controls"_Str, .arguments = strings("two words"_str) });
    ASSERT_TRUE(controls.is_ok());
    EXPECT_EQ(controls->execution.status->code(), Some(i32(7)));
    auto all_request    = request();
    all_request.purpose = lito::package::PackageSelectionPurpose::Example;
    auto all            = lito::build(all_request);
    ASSERT_TRUE(all.is_ok());
    EXPECT_EQ(artifact_count(*all, lito::cpp::ArtifactKind::ExampleExecutable), usize(3));
}

TEST_F(Examples, EmbeddedExamplesMatchLocalDiscoveryAndPublishReferences) {
    const ProjectFile files[] = {
        { "lito.toml"_str,
          "[package]\nname = 'packed-examples'\nversion = '0.1.0'\n[[example]]\nname = 'same'\npath = 'custom/main.cppm'\n"_str },
        { "custom/main.cppm"_str, "export module custom;\nint main() { return 0; }\n"_str },
        { "examples/same.cpp"_str, "#error explicit override\n"_str },
        { "examples/same/main.cppm"_str, "#error explicit override\n"_str },
        { "examples/other/main.cppm"_str, "export module other;\nint main() { return 0; }\n"_str },
    };
    auto tree = source_tree(files);
    ASSERT_TRUE(tree.is_ok());
    auto embedded = lito::manifest::load_package_manifest_from_source_tree("examples"_str, *tree);
    if (embedded.is_err()) rstd::io::eprintln("{}", error_chain_text(embedded.unwrap_err()));
    ASSERT_TRUE(embedded.is_ok());
    EXPECT_EQ(embedded->targets.len(), usize(2));
    auto project = materialize("packed-examples"_str, *tree);
    ASSERT_TRUE(project.is_ok());
    auto manifest = lito::manifest::load_package_manifest(project->root.as_path());
    ASSERT_TRUE(manifest.is_ok());
    auto selected = lito::manifest::PackageFileSetResolver::resolve(*manifest);
    ASSERT_TRUE(selected.is_ok());
    auto standalone = lito::manifest::serialize_standalone_package_manifest(
        *manifest,
        lito::manifest::StandaloneManifestOptions {
            .owner_registry =
                lito::registry::RegistryId::parse("https://registry.example/"_str).unwrap(),
        });
    ASSERT_TRUE(standalone.is_ok());
    auto published = selected->tree().clone();
    ASSERT_TRUE(published.replace_text("lito.toml"_str, standalone->as_str()).is_ok());
    auto restored =
        lito::manifest::load_package_manifest_from_source_tree("restored-examples"_str, published);
    ASSERT_TRUE(restored.is_ok());
    EXPECT_EQ(restored->targets.len(), usize(2));
}

TEST_F(Examples, ExampleNamesRequirePackageDisambiguation) {
    const ProjectFile files[] = {
        { "lito.toml"_str, "[workspace]\nname = 'examples'\nmembers = ['a', 'b']\n"_str },
        { "a/lito.toml"_str,
          "[package]\nname = 'a'\nversion = '0.1.0'\n[[example]]\nname = 'same'\nlink-stdlib = false\n"_str },
        { "a/examples/same.cpp"_str, "int main() { return 0; }\n"_str },
        { "b/lito.toml"_str,
          "[package]\nname = 'b'\nversion = '0.1.0'\n[[example]]\nname = 'same'\nlink-stdlib = false\n"_str },
        { "b/examples/same.cpp"_str, "int main() { return 0; }\n"_str },
    };
    auto project = materialize("example-names"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto output     = build_root("example-names"_str);
    auto request    = build_request(project->root.as_path(), output.as_path(), Vec<String>::make());
    request.purpose = lito::package::PackageSelectionPurpose::Example;
    request.targets = strings("example:same"_str);
    auto ambiguous  = lito::build(request);
    ASSERT_TRUE(ambiguous.is_err());
    EXPECT_TRUE(
        error_chain_text(ambiguous.unwrap_err()).as_str().contains("select a package with -p"_str));
    request.selection.packages = strings("a"_str);
    auto selected              = lito::build(request);
    ASSERT_TRUE(selected.is_ok());
    EXPECT_EQ(artifact_count(*selected, lito::cpp::ArtifactKind::ExampleExecutable), usize(1));
}

TEST_F(Examples, FormatIncludesCustomRootsWithoutDuplicates) {
    const ProjectFile files[] = {
        { "lito.toml"_str,
          "[package]\nname = 'format-examples'\nversion = '0.1.0'\n[[example]]\nname = 'first'\npath = 'custom/first.cppm'\n[[example]]\nname = 'second'\npath = 'custom/second.cppm'\n"_str },
        { "custom/first.cppm"_str, "export module first;\n"_str },
        { "custom/second.cppm"_str, "export module second;\n"_str },
        { "custom/unused.cppm"_str, "export module unused;\n"_str },
        { "custom/nested/lito.toml"_str, "[package]\nname = 'nested'\n"_str },
        { "custom/nested/main.cpp"_str, "int main() {}\n"_str },
    };
    auto project = materialize("example-format"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto manifest = lito::manifest::load_package_manifest(project->root.as_path());
    ASSERT_TRUE(manifest.is_ok());
    auto sources = lito::discover_format_sources(*manifest);
    ASSERT_TRUE(sources.is_ok());
    EXPECT_EQ(sources->sources.len(), usize(3));
}
