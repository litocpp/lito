#include <rstd/test/gtest.hpp>

import rstd;
import rstd.serde;
import rstd.toml;
import rstd.test;
import lito.core;
import lito.pack;
import lito.system;
import lito.tools.cmake;
import lito.toolchain;
import lito.driver;
import lito.test.support;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;
using namespace lito_test;
using PathBuf = rstd::path::PathBuf;

class Manifest : public ProjectFixture {
protected:
    auto manifest(ref<str> name, ref<str> contents, ref<str> filename = "lito.toml"_str)
        -> lito::source::SourceTreeResult<lito::source::SourceMaterialization> {
        const ProjectFile files[] = {
            { filename, contents },
        };
        return materialize(name, files);
    }
};

TEST_F(Manifest, PackageAndWorkspaceMetadataAreOwnedByManifest) {
    auto package_project = manifest("package-license"_str, R"toml([package]
name = "fixture-package-license"
version = "0.1.0"
license = "MIT OR Apache-2.0"
authors = ["Lito Authors <authors@example.invalid>", "Lito Contributors"]
description = "Manifest metadata fixture"
readme = false
repository = "https://example.invalid/fixture"
documentation = "https://docs.example.invalid/fixture"

[lib]
name = "fixture-package-license"
module = "fixture.package_license"
archive = "fixture-package-license"
)toml"_str);
    ASSERT_TRUE(package_project.is_ok());
    auto package = lito::manifest::load_package_manifest(package_project->root.as_path());
    ASSERT_TRUE(package.is_ok());
    EXPECT_EQ(package->license.source, lito::manifest::PackageLicenseSource::Explicit);
    ASSERT_TRUE(package->license.value.is_some());
    EXPECT_EQ(package->license.value->as_str(), "MIT OR Apache-2.0"_str);
    EXPECT_EQ(package->authors.source, lito::manifest::PackageAuthorsSource::Explicit);
    ASSERT_EQ(package->authors.values.len(), usize(2));
    EXPECT_EQ(package->authors.values[usize {}].as_str(),
              "Lito Authors <authors@example.invalid>"_str);
    EXPECT_EQ(package->authors.values[usize(1)].as_str(), "Lito Contributors"_str);
    EXPECT_EQ(package->description.source, lito::manifest::PackageMetadataSource::Explicit);
    ASSERT_TRUE(package->description.value.is_some());
    EXPECT_EQ(package->description.value->as_str(), "Manifest metadata fixture"_str);
    ASSERT_TRUE(package->repository.value.is_some());
    EXPECT_EQ(package->repository.value->as_str(), "https://example.invalid/fixture"_str);
    ASSERT_TRUE(package->documentation.value.is_some());
    EXPECT_EQ(package->documentation.value->as_str(), "https://docs.example.invalid/fixture"_str);
    EXPECT_EQ(package->readme.source, lito::manifest::PackageReadmeSource::Disabled);
    EXPECT_TRUE(package->readme.path.is_none());

    auto workspace_project = manifest("workspace-license"_str, R"toml([workspace]
name = "fixture-workspace-license"
members = ["package"]

[workspace.package]
version = "0.1.0"
license = "MIT OR Apache-2.0"
authors = ["Lito Authors <authors@example.invalid>"]
description = "Workspace metadata fixture"
readme = "README.md"
repository = "https://example.invalid/workspace"
documentation = "https://docs.example.invalid/workspace"
)toml"_str);
    ASSERT_TRUE(workspace_project.is_ok());
    auto workspace = lito::manifest::load_manifest_document(workspace_project->root.as_path());
    ASSERT_TRUE(workspace.is_ok());
    ASSERT_TRUE(workspace->workspace.is_some());
    ASSERT_TRUE(workspace->workspace->package.license.is_some());
    EXPECT_EQ(workspace->workspace->package.license->as_str(), "MIT OR Apache-2.0"_str);
    ASSERT_TRUE(workspace->workspace->package.authors.is_some());
    ASSERT_EQ(workspace->workspace->package.authors->len(), usize(1));
    EXPECT_EQ((*workspace->workspace->package.authors)[usize {}].as_str(),
              "Lito Authors <authors@example.invalid>"_str);
    ASSERT_TRUE(workspace->workspace->package.description.is_some());
    EXPECT_EQ(workspace->workspace->package.description->as_str(),
              "Workspace metadata fixture"_str);
    ASSERT_TRUE(workspace->workspace->package.repository.is_some());
    EXPECT_EQ(workspace->workspace->package.repository->as_str(),
              "https://example.invalid/workspace"_str);
    ASSERT_TRUE(workspace->workspace->package.documentation.is_some());
    EXPECT_EQ(workspace->workspace->package.documentation->as_str(),
              "https://docs.example.invalid/workspace"_str);
    ASSERT_TRUE(workspace->workspace->package.readme.is_some());
    EXPECT_TRUE(workspace->workspace->package.readme->enabled);
    EXPECT_EQ(workspace->workspace->package.readme->path.as_path(),
              workspace_project->root.join(PathBuf::from("README.md"_str).as_path()).as_path());
}

TEST_F(Manifest, WorkspaceMetadataResolvesBeforeStandalonePackaging) {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"toml([workspace]
name = "fixture-workspace-metadata"
members = ["package"]

[workspace.package]
version = "1.2.3"
description = "Inherited package description"
readme = "README.md"
repository = "https://example.invalid/inherited"
documentation = "https://docs.example.invalid/inherited"
)toml"_str },
        { "README.md"_str, "# Inherited README\n"_str },
        { "package/lito.toml"_str, R"toml([package]
name = "fixture-inherited-metadata"
version.workspace = true
description.workspace = true
readme.workspace = true
repository.workspace = true
documentation.workspace = true

[lib]
name = "fixture-inherited-metadata"
module = "fixture.inherited_metadata"
archive = "fixture-inherited-metadata"
sources = ["src/lib.cppm"]
)toml"_str },
        { "package/src/lib.cppm"_str, "export module fixture.inherited_metadata;\n"_str },
    };
    auto project = materialize("workspace-metadata"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto document = lito::manifest::load_manifest_document(project->root.as_path());
    ASSERT_TRUE(document.is_ok());
    ASSERT_TRUE(document->workspace.is_some());
    auto catalog =
        lito::workspace::load_workspace_catalog(rstd::move(document->workspace).unwrap());
    ASSERT_TRUE(catalog.is_ok());
    auto package = catalog->package("fixture-inherited-metadata"_str);
    ASSERT_TRUE(package.is_some());
    ASSERT_TRUE((**package).description.value.is_some());
    EXPECT_EQ((**package).description.value->as_str(), "Inherited package description"_str);
    ASSERT_TRUE((**package).repository.value.is_some());
    EXPECT_EQ((**package).repository.value->as_str(), "https://example.invalid/inherited"_str);
    ASSERT_TRUE((**package).documentation.value.is_some());
    EXPECT_EQ((**package).documentation.value->as_str(),
              "https://docs.example.invalid/inherited"_str);
    EXPECT_EQ((**package).readme.source, lito::manifest::PackageReadmeSource::Workspace);
    ASSERT_TRUE((**package).readme.path.is_some());
    EXPECT_EQ((**package).readme.path->as_path(),
              project->root.join(PathBuf::from("README.md"_str).as_path()).as_path());
    ASSERT_TRUE((**package).readme.archive_path.is_some());
    EXPECT_EQ((**package).readme.archive_path->as_str(), "README.md"_str);

    auto file_set = lito::manifest::PackageFileSetResolver::resolve(**package);
    ASSERT_TRUE(file_set.is_ok());
    auto paths      = file_set->paths();
    auto has_readme = false;
    for (const auto& path : paths) {
        if (path.as_str() == "README.md"_str) has_readme = true;
    }
    EXPECT_TRUE(has_readme);

    auto standalone = lito::manifest::serialize_standalone_package_manifest(
        **package,
        lito::manifest::StandaloneManifestOptions {
            .owner_registry =
                lito::registry::RegistryId::parse("https://registry.example/"_str).unwrap(),
        });
    ASSERT_TRUE(standalone.is_ok());
    EXPECT_TRUE(standalone->as_str().contains("readme = \"README.md\""_str));
    EXPECT_FALSE(standalone->as_str().contains("workspace = true"_str));
}

TEST_F(Manifest, RegistryDependencyUsesDependencyName) {
    auto project = manifest("registry-dependency"_str, R"toml([package]
name = "fixture-registry-dependency"
version = "0.1.0"

[lib]
name = "fixture-registry-dependency"
module = "fixture.registry_dependency"
archive = "fixture-registry-dependency"

[dependencies.upstream-package]
version = "^1.4"
registry = "internal"
visibility = "private"
)toml"_str);
    ASSERT_TRUE(project.is_ok());
    auto loaded = lito::manifest::load_package_manifest(project->root.as_path());
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_EQ(loaded->dependencies.len(), usize(1));
    const auto& dependency = loaded->dependencies[usize {}];
    EXPECT_EQ(dependency.name.as_str(), "upstream-package"_str);
    ASSERT_TRUE(dependency.source.resolution.is_Registry());
    ASSERT_TRUE(dependency.source.publication.is_some());
    const auto& source = dependency.source.resolution.as_Registry();
    ASSERT_TRUE(source.registry.is_some());
    EXPECT_EQ(source.registry->as_str(), "internal"_str);
    EXPECT_EQ(source.package.as_str(), "upstream-package"_str);
    auto matching = lito::registry::SemanticVersion::parse("1.9.0"_str);
    auto rejected = lito::registry::SemanticVersion::parse("2.0.0"_str);
    ASSERT_TRUE(matching.is_ok());
    ASSERT_TRUE(rejected.is_ok());
    EXPECT_TRUE(source.requirement.matches(*matching));
    EXPECT_FALSE(source.requirement.matches(*rejected));
}

TEST_F(Manifest, LocalDependencyCarriesAnIndependentRegistryRequirement) {
    auto project = manifest("local-registry-dependency"_str, R"toml([package]
name = "fixture-local-registry-dependency"
version = "0.1.0"

[lib]
name = "fixture-local-registry-dependency"
module = "fixture.local_registry_dependency"
archive = "fixture-local-registry-dependency"

[dependencies.upstream-package]
path = "vendor/upstream"
version = "^1.4"
registry = "internal"
visibility = "private"

[dependencies.git-package]
git = "https://example.invalid/git-package.git"
tag = "v2.0.0"
version = "2.0.0"
visibility = "private"
)toml"_str);
    ASSERT_TRUE(project.is_ok());
    auto loaded = lito::manifest::load_package_manifest(project->root.as_path());
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_EQ(loaded->dependencies.len(), usize(2));
    const auto& dependency = loaded->dependencies[usize(1)];
    ASSERT_TRUE(dependency.source.resolution.is_Path());
    EXPECT_EQ(dependency.source.resolution.as_Path().path.as_path().to_str().unwrap(),
              "vendor/upstream"_str);
    ASSERT_TRUE(dependency.source.publication.is_some());
    EXPECT_EQ(dependency.source.publication->registry->as_str(), "internal"_str);
    EXPECT_EQ(dependency.source.publication->package.as_str(), "upstream-package"_str);
    EXPECT_EQ(dependency.source.publication->requirement.text(), "^1.4"_str);
    const auto& git = loaded->dependencies[usize {}];
    ASSERT_TRUE(git.source.resolution.is_Git());
    EXPECT_EQ(git.source.resolution.as_Git().reference.kind, lito::source::GitReferenceKind::Tag);
    ASSERT_TRUE(git.source.publication.is_some());
    EXPECT_EQ(git.source.publication->requirement.text(), "2.0.0"_str);
}

TEST_F(Manifest, PackageDependencySourceCombinationsAreValidated) {
    constexpr ref<str> invalid_dependencies[] = {
        "path = \"dependency\"\ngit = \"https://example.invalid/dependency.git\""_str,
        "builtin = \"dependency\"\nversion = \"0.1.0\""_str,
        "path = \"dependency\"\nregistry = \"official\""_str,
    };
    auto index = usize {};
    for (auto declaration : invalid_dependencies) {
        auto contents = rstd::format(R"toml([package]
name = "fixture-invalid-package-dependency-{}"
version = "0.1.0"

[[bin]]
name = "fixture-invalid-package-dependency-{}"
link-stdlib = false

[dependencies.dependency]
{}
visibility = "private"
)toml",
                                     index,
                                     index,
                                     declaration);
        auto fixture  = rstd::format("invalid-package-dependency-{}", index);
        auto project  = manifest(fixture.as_str(), contents.as_str());
        ASSERT_TRUE(project.is_ok());
        auto loaded = lito::manifest::load_package_manifest(project->root.as_path());
        ASSERT_TRUE(loaded.is_err());
        ++index;
    }
}

TEST_F(Manifest, StandalonePackageExpandsPublishMetadataAndRegistryAliases) {
    auto project = manifest("standalone-publish"_str, R"toml([package]
name = "fixture-standalone-publish"
version = "1.2.3"
license = "MIT"
authors = ["Lito Authors"]

[package.publish]
include = ["src/**", "LICENSE"]
exclude = ["src/generated/**"]

[lib]
name = "fixture-standalone-publish"
module = "fixture.standalone_publish"
archive = "fixture-standalone-publish"

[dependencies.same-registry]
version = "^2.0"
registry = "official"
visibility = "public"

[dependencies.local-versioned]
path = "vendor/local-versioned"
version = "1.2.0"
visibility = "private"

[dev-dependencies.other-registry]
version = "~3.1"
registry = "community"

[dev-dependencies.local-only]
path = "vendor/local-only"

[dev-dependencies.git-versioned]
git = "https://example.invalid/git-versioned.git"
rev = "v4"
version = "4.0.0"

[dev-dependencies.builtin-only]
builtin = "qt"
)toml"_str);
    ASSERT_TRUE(project.is_ok());
    auto loaded = lito::manifest::load_package_manifest(project->root.as_path());
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_TRUE(loaded->publish.include.is_some());
    ASSERT_EQ(loaded->publish.include->len(), usize(2));
    ASSERT_EQ(loaded->publish.exclude.len(), usize(1));

    auto official  = lito::registry::RegistryId::parse("https://registry.example/"_str).unwrap();
    auto community = lito::registry::RegistryId::parse("https://community.example/"_str).unwrap();
    auto aliases   = Vec<lito::manifest::StandaloneRegistryAlias>::make();
    aliases.push(lito::manifest::StandaloneRegistryAlias {
        .name     = String::make("official"_str),
        .identity = rstd::move(official),
    });
    aliases.push(lito::manifest::StandaloneRegistryAlias {
        .name     = String::make("community"_str),
        .identity = rstd::move(community),
    });
    auto serialized = lito::manifest::serialize_standalone_package_manifest(
        *loaded,
        lito::manifest::StandaloneManifestOptions {
            .owner_registry =
                lito::registry::RegistryId::parse("https://registry.example/"_str).unwrap(),
            .registry_aliases = rstd::move(aliases),
        });
    ASSERT_TRUE(serialized.is_ok());
    EXPECT_TRUE(serialized->as_str().contains("[dependencies.same-registry]"_str));
    EXPECT_TRUE(serialized->as_str().contains("[dependencies.local-versioned]"_str));
    EXPECT_TRUE(serialized->as_str().contains("version = \"1.2.0\""_str));
    EXPECT_FALSE(serialized->as_str().contains("vendor/local-versioned"_str));
    EXPECT_FALSE(serialized->as_str().contains("local-only"_str));
    EXPECT_TRUE(serialized->as_str().contains("[dev-dependencies.git-versioned]"_str));
    EXPECT_FALSE(serialized->as_str().contains("example.invalid/git-versioned"_str));
    EXPECT_FALSE(serialized->as_str().contains("builtin-only"_str));
    EXPECT_FALSE(serialized->as_str().contains("registry = \"https://registry.example/\""_str));
    EXPECT_TRUE(serialized->as_str().contains("registry = \"https://community.example/\""_str));

    auto parsed = rstd::toml::from_str(serialized->as_str());
    ASSERT_TRUE(parsed.is_ok());
}

TEST_F(Manifest, StandalonePackageRejectsVersionlessNormalLocalDependency) {
    auto project = manifest("standalone-versionless-normal"_str, R"toml([package]
name = "fixture-standalone-versionless-normal"
version = "0.1.0"

[lib]
name = "fixture-standalone-versionless-normal"
module = "fixture.standalone_versionless_normal"
archive = "fixture-standalone-versionless-normal"

[dependencies.local-only]
path = "vendor/local-only"
visibility = "private"
)toml"_str);
    ASSERT_TRUE(project.is_ok());
    auto loaded = lito::manifest::load_package_manifest(project->root.as_path());
    ASSERT_TRUE(loaded.is_ok());
    auto serialized = lito::manifest::serialize_standalone_package_manifest(
        *loaded,
        lito::manifest::StandaloneManifestOptions {
            .owner_registry =
                lito::registry::RegistryId::parse("https://registry.example/"_str).unwrap(),
        });
    ASSERT_TRUE(serialized.is_err());
    auto error = rstd::move(serialized).unwrap_err();
    EXPECT_TRUE(error_chain_text(error).as_str().contains("must declare a Registry version"_str));
}

TEST_F(Manifest, PackageFileSetAppliesPortablePatternsAndFixedExcludes) {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "fixture-file-set"
version = "1.0.0"

[package.publish]
include = ["src/**", "LICENSE"]
exclude = ["src/generated/**"]

[lib]
name = "fixture-file-set"
module = "fixture.file_set"
archive = "fixture-file-set"
sources = ["src/lib.cppm"]
)toml"_str },
        { "src/lib.cppm"_str, "export module fixture.file_set;\n"_str },
        { "src/generated/unused.cpp"_str, "int unused;\n"_str },
        { "LICENSE"_str, "fixture license\n"_str },
        { ".git/config"_str, "ignored\n"_str },
        { "notes.txt"_str, "not selected\n"_str },
    };
    auto project = materialize("package-file-set"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto loaded = lito::manifest::load_package_manifest(project->root.as_path());
    ASSERT_TRUE(loaded.is_ok());
    auto resolved = lito::manifest::PackageFileSetResolver::resolve(*loaded);
    ASSERT_TRUE(resolved.is_ok());
    auto paths = resolved->paths();
    ASSERT_EQ(paths.len(), usize(3));
    EXPECT_EQ(paths[usize {}].as_str(), "LICENSE"_str);
    EXPECT_EQ(paths[usize(1)].as_str(), "lito.toml"_str);
    EXPECT_EQ(paths[usize(2)].as_str(), "src/lib.cppm"_str);
}

TEST_F(Manifest, PackageFileSetRejectsPatternsCrossingNestedPackages) {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "fixture-nested-file-set"
version = "1.0.0"

[package.publish]
include = ["**"]

[lib]
name = "fixture-nested-file-set"
module = "fixture.nested_file_set"
archive = "fixture-nested-file-set"
sources = ["src/lib.cppm"]
)toml"_str },
        { "src/lib.cppm"_str, "export module fixture.nested_file_set;\n"_str },
        { "vendor/lito.toml"_str, R"toml([package]
name = "nested"
version = "1.0.0"

[lib]
name = "nested"
module = "nested"
archive = "nested"
)toml"_str },
        { "vendor/src/lib.cppm"_str, "export module nested;\n"_str },
    };
    auto project = materialize("nested-package-file-set"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto loaded = lito::manifest::load_package_manifest(project->root.as_path());
    ASSERT_TRUE(loaded.is_ok());
    auto resolved = lito::manifest::PackageFileSetResolver::resolve(*loaded);
    ASSERT_TRUE(resolved.is_err());
    EXPECT_TRUE(resolved.unwrap_err().message.as_str().contains("nested package root"_str));
}

TEST_F(Manifest, PackageFileSetRequiresDeclaredSourcesToBeSelected) {
    auto project = manifest("missing-published-source"_str, R"toml([package]
name = "fixture-missing-published-source"
version = "1.0.0"

[package.publish]
include = ["LICENSE"]

[lib]
name = "fixture-missing-published-source"
module = "fixture.missing_published_source"
archive = "fixture-missing-published-source"
sources = ["src/lib.cppm"]
)toml"_str);
    ASSERT_TRUE(project.is_ok());
    auto license = project->root.join(PathBuf::from("LICENSE"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(license.as_path(), "license\n"_str.as_bytes()).is_ok());
    auto source_directory = project->root.join(PathBuf::from("src"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir(source_directory.as_path()).is_ok());
    auto source = source_directory.join(PathBuf::from("lib.cppm"_str).as_path());
    ASSERT_TRUE(
        rstd::fs::write(source.as_path(), "export module fixture;\n"_str.as_bytes()).is_ok());
    auto loaded = lito::manifest::load_package_manifest(project->root.as_path());
    ASSERT_TRUE(loaded.is_ok());
    auto resolved = lito::manifest::PackageFileSetResolver::resolve(*loaded);
    ASSERT_TRUE(resolved.is_err());
    EXPECT_TRUE(resolved.unwrap_err().message.as_str().contains("target source"_str));
}

TEST_F(Manifest, PackPackageBuildsAndReinspectsStandaloneArchive) {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "fixture-pack-package"
version = "1.2.3"

[lib]
name = "fixture-pack-package"
module = "fixture.pack_package"
archive = "fixture-pack-package"
sources = ["src/lib.cppm"]
)toml"_str },
        { "src/lib.cppm"_str, "export module fixture.pack_package;\n"_str },
    };
    auto project = materialize("pack-package"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto output = build_root("pack-package"_str)
                      .join(PathBuf::from("fixture-pack-package-1.2.3.tar.zst"_str).as_path());
    auto packed = lito::pack_package(lito::PackPackageRequest {
        .root   = project->root.clone(),
        .output = Some(output.clone()),
        .registry =
            lito::PackageRegistryContext {
                .owner =
                    lito::registry::RegistryId::parse("https://registry.example/"_str).unwrap(),
            },
    });
    ASSERT_TRUE(packed.is_ok());
    ASSERT_TRUE(packed->artifact.is_some());
    EXPECT_EQ(packed->files.len(), usize(2));
    EXPECT_TRUE(rstd::fs::exists(output.as_path()).unwrap());
    EXPECT_EQ(packed->artifact->candidate.manifest.name.as_str(), "fixture-pack-package"_str);
    EXPECT_EQ(packed->artifact->candidate.version.text(), "1.2.3"_str);
}

struct InvalidManifestCase {
    ref<str> name;
    ref<str> contents;
};

constexpr InvalidManifestCase invalid_manifest_cases[] = {
    { "manifest-empty-target-array"_str, R"lito(bin = []

[package]
name = "fixture-empty-target-array"
version = "0.1.0"

[lib]
name = "fixture-empty-target-array"
module = "fixture.empty_target_array"
archive = "fixture-empty-target-array"
sources = ["lib.cppm"]
)lito"_str },
    { "manifest-discovery-field"_str, R"lito([package]
name = "discovery-field"
version = "0.1.0"

[lib]
name = "discovery-field"
module = "fixture.discovery.field"
archive = "fixture.discovery.field"
discovery = "module"
)lito"_str },
    { "manifest-empty-authors"_str, R"lito([package]
name = "fixture-empty-authors"
version = "0.1.0"
authors = []
[lib]
name = "fixture-empty-authors"
module = "fixture.empty_authors"
archive = "fixture-empty-authors"
)lito"_str },
    { "manifest-empty-author"_str, R"lito([package]
name = "fixture-empty-author"
version = "0.1.0"
authors = [""]
[lib]
name = "fixture-empty-author"
module = "fixture.empty_author"
archive = "fixture-empty-author"
)lito"_str },
    { "manifest-duplicate-authors"_str, R"lito([package]
name = "fixture-duplicate-authors"
version = "0.1.0"
authors = ["Lito Authors", "Lito Authors"]
[lib]
name = "fixture-duplicate-authors"
module = "fixture.duplicate_authors"
archive = "fixture-duplicate-authors"
)lito"_str },
    { "manifest-git-commit-invalid"_str, R"lito([package]
name = "fixture-git-commit-invalid"
version = "0.1.0"

[lib]
name = "fixture-git-commit-invalid"
module = "fixture.git.commit_invalid"
archive = "fixture.git.commit_invalid"
sources = ["source.cppm"]

[dependencies.fixture-dependency]
git = "https://example.invalid/dependency.git"
commit = "0123456789abcdef"
visibility = "private"
)lito"_str },
    { "manifest-git-multiple-selectors"_str, R"lito([package]
name = "fixture-git-multiple-selectors"
version = "0.1.0"

[lib]
name = "fixture-git-multiple-selectors"
module = "fixture.git.multiple_selectors"
archive = "fixture.git.multiple_selectors"
sources = ["source.cppm"]

[dependencies.fixture-dependency]
git = "https://example.invalid/dependency.git"
branch = "main"
rev = "0123456789abcdef"
visibility = "private"
)lito"_str },
    { "manifest-git-path-and-git"_str, R"lito([package]
name = "fixture-git-path-and-git"
version = "0.1.0"

[lib]
name = "fixture-git-path-and-git"
module = "fixture.git.path_and_git"
archive = "fixture.git.path_and_git"
sources = ["source.cppm"]

[dependencies.fixture-dependency]
path = "../dependency"
git = "https://example.invalid/dependency.git"
visibility = "private"
)lito"_str },
    { "manifest-git-url-fragment"_str, R"lito([package]
name = "fixture-git-url-fragment"
version = "0.1.0"

[lib]
name = "fixture-git-url-fragment"
module = "fixture.git.url_fragment"
archive = "fixture.git.url_fragment"
sources = ["source.cppm"]

[dependencies.fixture-dependency]
git = "https://example.invalid/dependency.git#main"
visibility = "private"
)lito"_str },
    { "manifest-build-tools-duplicate-host"_str, R"lito([package]
name = "build-tool-duplicate-host"

[build-tools.generator]
version = "1.2.3"
executable = "bin/generator"

[build-tools.generator.archives.linux-x86_64]
url = "https://example.com/generator-x86_64.tgz"
sha256 = "0000000000000000000000000000000000000000000000000000000000000000"

[build-tools.generator.archives.linux-amd64]
url = "https://example.com/generator-amd64.tgz"
sha256 = "1111111111111111111111111111111111111111111111111111111111111111"
)lito"_str },
    { "manifest-build-tools-invalid-executable"_str, R"lito([package]
name = "build-tool-invalid-executable"

[build-tools.generator]
version = "1.2.3"
executable = "../generator"

[build-tools.generator.archives.linux-x86_64]
url = "https://example.com/generator.tgz"
sha256 = "0000000000000000000000000000000000000000000000000000000000000000"
)lito"_str },
    { "manifest-build-tools-invalid-resource"_str, R"lito([package]
name = "build-tool-invalid-resource"

[[bin]]
link-stdlib = false
name = "tool-consumer"
sources = ["main.cpp"]
resources = [
  { name = "frontend", root = "source", path = "frontend/default" },
]
)lito"_str },
    { "manifest-build-tools-cross-package-resource"_str, R"lito([package]
name = "build-tool-cross-package-resource"

[[bin]]
link-stdlib = false
name = "tool-consumer"
sources = ["main.cpp"]
resources = [
  { name = "frontend", package = "provider", root = "generated", path = "frontend/default" },
]
)lito"_str },
    { "manifest-build-tools-invalid-version"_str, R"lito([package]
name = "build-tool-invalid-version"

[build-tools.generator]
version = "latest"
executable = "bin/generator"

[build-tools.generator.archives.linux-x86_64]
url = "https://example.com/generator.tgz"
sha256 = "0000000000000000000000000000000000000000000000000000000000000000"
)lito"_str },
    { "manifest-build-tools-unknown-field"_str, R"lito([package]
name = "build-tool-unknown-field"

[build-tools.generator]
version = "1.2.3"
executable = "bin/generator"
command = "generator"

[build-tools.generator.archives.linux-x86_64]
url = "https://example.com/generator.tgz"
sha256 = "0000000000000000000000000000000000000000000000000000000000000000"
)lito"_str },
    { "manifest-script-invalid-host"_str, R"lito([package]
name = "script-invalid-host"
version = "0.1.0"

[script]
supports = ["unknown"]
)lito"_str },
    { "manifest-dependency-conflicting-source"_str, R"lito([package]
name = "dependency-conflicting-source"
version = "0.1.0"

[lib]
name = "dependency-conflicting-source"
module = "fixture.dependency_conflicting_source"
archive = "fixture.dependency_conflicting_source"

[dependencies.lito-qt]
builtin = "qt"
path = "../qt"
)lito"_str },
    { "manifest-legacy-target-schema"_str, R"lito([package]
name = "fixture-legacy-target-schema"
version = "0.1.0"
module = "fixture.legacy"

[library]
archive = "fixture-legacy"
sources = ["lib.cppm"]
)lito"_str },
    { "manifest-feature-invalid-name"_str, R"lito([package]
name = "fixture-feature-invalid-name"
version = "0.1.0"

[features."1invalid"]
default = false
)lito"_str },
    { "manifest-feature-macro-collision"_str, R"lito([package]
name = "fixture-feature-macro-collision"
version = "0.1.0"

[features.foo-bar]
default = false

[features.foo_bar]
default = false
)lito"_str },
    { "manifest-feature-custom-macro"_str, R"lito([package]
name = "fixture-feature-custom-macro"
version = "0.1.0"

[features.ffi]
default = false
macro = "FIXTURE_FEATURE_FFI"
)lito"_str },
    { "manifest-when-invalid-expression"_str, R"lito([package]
name = "fixture-when-invalid-expression"
version = "0.1.0"

[[when]]
condition = "target.os =="

[when.usage]
private-definitions = ["FIXTURE_CONDITION"]
)lito"_str },
    { "manifest-package-name-dot"_str, R"lito([package]
name = "fixture.package.dot"
version = "0.1.0"

[lib]
name = "fixture.package.dot"
module = "fixture.package.dot"
archive = "fixture.package.dot"
sources = ["source.cppm"]
)lito"_str },
    { "manifest-package-name-empty"_str, R"lito([package]
name = ""
version = "0.1.0"

[lib]
name = ""
module = "fixture.package.empty"
archive = "fixture.package.empty"
sources = ["source.cppm"]
)lito"_str },
    { "manifest-package-version-missing"_str, R"lito([package]
name = "fixture-package-version-missing"

[lib]
name = "fixture-package-version-missing"
module = "fixture.package_version_missing"
archive = "fixture-package-version-missing"
sources = ["lib.cppm"]
)lito"_str },
    { "manifest-profile-cycle"_str, R"lito([package]
name = "fixture-profile-cycle"
version = "0.1.0"

[[bin]]
link-stdlib = false
name = "profile-cycle"
sources = ["main.cpp"]

[profile.first]
inherits = "second"

[profile.second]
inherits = "first"
)lito"_str },
    { "manifest-profile-base-inherits"_str, R"lito([package]
name = "fixture-profile-base-inherits"
version = "0.1.0"

[[bin]]
link-stdlib = false
name = "profile-base-inherits"
sources = ["main.cpp"]

[profile.base]
inherits = "debug"
)lito"_str },
    { "manifest-profile-type"_str, R"lito([package]
name = "fixture-profile-type"
version = "0.1.0"

[[bin]]
link-stdlib = false
name = "profile-type"
sources = ["main.cpp"]

[profile]
rtti = "disabled"
)lito"_str },
    { "manifest-public-usage-without-lib"_str, R"lito([package]
name = "fixture-public-usage-without-lib"
version = "0.1.0"

[[bin]]
link-stdlib = false
name = "fixture-public-usage-without-lib"
sources = ["main.cpp"]

[usage]
public-definitions = ["FIXTURE_PUBLIC=1"]
)lito"_str },
    { "install-manifest-runtime-path-and-git"_str, R"lito([package]
name = "fixture-runtime-path-and-git"
version = "0.1.0"

[runtime-dependencies.helper]
path = "../../install-only"
git = "https://example.invalid/runtime-helper.git"
)lito"_str },
    { "install-manifest-runtime-unknown-field"_str, R"lito([package]
name = "fixture-runtime-unknown-field"
version = "0.1.0"

[runtime-dependencies.helper]
path = "../../install-only"
unknown = true
)lito"_str },
    { "install-manifest-runtime-visibility"_str, R"lito([package]
name = "fixture-runtime-visibility"
version = "0.1.0"

[runtime-dependencies.helper]
path = "../../install-only"
visibility = "private"
)lito"_str },
    { "install-manifest-runtime-workspace-and-path"_str, R"lito([package]
name = "fixture-runtime-workspace-and-path"
version = "0.1.0"

[runtime-dependencies.helper]
workspace = true
path = "../../install-only"
)lito"_str },
    { "manifest-source-root-descendant"_str, R"lito([package]
name = "fixture-source-root-descendant"
version = "0.1.0"
source-root = "src"

[[bin]]
link-stdlib = false
name = "fixture-source-root-descendant"
sources = ["main.cpp"]
)lito"_str },
    { "manifest-legacy-minimum-standard"_str, R"lito([package]
name = "fixture-legacy-minimum-standard"
version = "0.1.0"
minimum-standard = "c17"

[lib]
name = "fixture-legacy-minimum-standard"
archive = "fixture-legacy-minimum-standard"
sources = ["lib.c"]
)lito"_str },
    { "manifest-legacy-language"_str, R"lito([package]
name = "fixture-legacy-language"
version = "0.1.0"
language = "c"

[lib]
name = "fixture-legacy-language"
archive = "fixture-legacy-language"
sources = ["lib.c"]
)lito"_str },
    { "manifest-unsupported-cpp-standard"_str, R"lito([package]
name = "fixture-unsupported-cpp-standard"
version = "0.1.0"
standard = "c++17"

[lib]
name = "fixture-unsupported-cpp-standard"
module = "fixture.unsupported_cpp_standard"
archive = "fixture-unsupported-cpp-standard"
sources = ["lib.cppm"]
)lito"_str },
    { "manifest-standard-without-compile-target"_str, R"lito([package]
name = "fixture-standard-without-compile-target"
version = "0.1.0"
standard = "c17"

[runtime-dependencies.helper]
path = "../install-only"
)lito"_str },
    { "manifest-c-module"_str, R"lito([package]
name = "fixture-c-module"
version = "0.1.0"
standard = "c99"

[lib]
name = "fixture-c-module"
module = "fixture.c_module"
archive = "fixture-c-module"
sources = ["lib.c"]
)lito"_str },
    { "manifest-c-implicit-source-discovery"_str, R"lito([package]
name = "fixture-c-implicit-source-discovery"
version = "0.1.0"
standard = "c99"

[lib]
name = "fixture-c-implicit-source-discovery"
archive = "fixture-c-implicit-source-discovery"
)lito"_str },
    { "manifest-source-group-condition"_str, R"lito([package]
name = "fixture-source-group-condition"
version = "0.1.0"

[source-groups.runtime]
sources = ["runtime.cpp"]
condition = 'target.os == "linux"'

[lib]
name = "fixture-source-group-condition"
module = "fixture.source_group_condition"
archive = "fixture-source-group-condition"
source-groups = ["runtime"]
)lito"_str },
    { "manifest-target-link-stdlib-type"_str, R"lito([package]
name = "fixture-target-link-stdlib-type"
version = "0.1.0"

[[bin]]
name = "fixture-target-link-stdlib-type"
sources = ["main.cpp"]
link-stdlib = "disabled"
)lito"_str },
    { "manifest-test-attach-unknown-key"_str, R"lito([package]
name = "invalid-test-attach"

[[test]]
link-stdlib = false
name = "invalid-test-attach"
sources = ["main.cpp"]

[[test.attach]]
package = "fixture-library"
sources = ["attached.cppm"]
unknown = true
)lito"_str },
    { "manifest-toml-explicit-dependency"_str, R"lito([package]
name = "fixture-dependency"
version = "0.1.0"

[lib]
name = "fixture-dependency"
module = "fixture.dependency"
archive = "fixture.dependency"
sources = ["source.cppm"]

[dependencies."fixture.base"]
path = "../base"
visibility = "public"
)lito"_str },
    { "manifest-toml-explicit-version-workspace-false"_str, R"lito([package]
name = "fixture-version_workspace_false"
version.workspace = false

[lib]
name = "fixture-version_workspace_false"
module = "fixture.version_workspace_false"
archive = "fixture.version_workspace_false"
sources = ["source.cppm"]
)lito"_str },
    { "workspace-cmake-definition-targets"_str, R"lito([workspace]
name = "fixture-workspace-cmake-definition-targets"
members = ["member"]

[workspace.external-dependencies.cmake.fixture]
package = "LitoFixture"
path = "member"
targets = [{ name = "LitoFixture::fixture", visibility = "private" }]
)lito"_str },
    { "workspace-dependency-definition-visibility"_str, R"lito([workspace]
name = "fixture-workspace-dependency-definition-visibility"
members = ["member"]

[workspace.dependencies.member]
path = "member"
visibility = "private"
)lito"_str },
    { "workspace-dependency-reference-mixed"_str, R"lito([package]
name = "fixture-workspace-dependency-reference-mixed"
version = "0.1.0"

[lib]
name = "fixture-workspace-dependency-reference-mixed"
module = "fixture.workspace_dependency_reference_mixed"
archive = "fixture-workspace-dependency-reference-mixed"

[dependencies.mixed]
workspace = true
path = "../mixed"
visibility = "private"
)lito"_str },
    { "workspace-mixed"_str, R"lito([workspace]
name = "fixture-mixed-workspace"
members = ["member"]

[package]
name = "fixture-workspace-mixed"
version = "0.1.0"
)lito"_str },
    { "registry-package-alias"_str, R"lito([package]
name = "fixture-registry-package-alias"
version = "0.1.0"

[lib]
name = "fixture-registry-package-alias"
module = "fixture.registry_package_alias"
archive = "fixture-registry-package-alias"

[dependencies.local-name]
version = "1.0.0"
package = "upstream-package"
visibility = "private"
)lito"_str },
};

TEST_F(Manifest, InvalidManifestDocumentsAreRejectedByManifestOwner) {
    for (const auto& item : invalid_manifest_cases) {
        SCOPED_TRACE(item.name);
        auto project = manifest(item.name, item.contents);
        ASSERT_TRUE(project.is_ok());
        auto loaded = lito::manifest::load_manifest_document(project->root.as_path());
        EXPECT_TRUE(loaded.is_err());
    }
}

TEST_F(Manifest, ManifestSchemaErrorRetainsFileAndNodeOwnership) {
    auto project = manifest("discovery-field"_str, invalid_manifest_cases[1].contents);
    ASSERT_TRUE(project.is_ok());
    auto loaded = lito::manifest::load_manifest_document(project->root.as_path());
    ASSERT_TRUE(loaded.is_err());
    auto error = rstd::move(loaded).unwrap_err();
    ASSERT_TRUE(error.is_File());
    const auto& file = error.as_File().source;
    EXPECT_EQ(file.path.as_path(),
              project->root.join(PathBuf::from("lito.toml"_str).as_path()).as_path());
    ASSERT_TRUE(file.cause.is_Schema());
    const auto& schema = file.cause.as_Schema().source;
    ASSERT_TRUE(schema.is_Parse());
    const auto& parse = schema.as_Parse().source;
    ASSERT_TRUE(parse.is_UnknownField());
    EXPECT_EQ(rstd::format("{}", parse.as_UnknownField().node), "manifest.lib"_str);
    EXPECT_EQ(parse.as_UnknownField().field.as_str(), "discovery"_str);

    auto manifest_source = as<rstd::error::Error>(error).source();
    ASSERT_TRUE(manifest_source.is_some());
    EXPECT_TRUE(rstd::error::is<lito::manifest::ManifestFileError>(*manifest_source));
    auto file_source = (*manifest_source)->source();
    ASSERT_TRUE(file_source.is_some());
    EXPECT_TRUE(rstd::error::is<lito::manifest::ManifestFileCause>(*file_source));
    auto cause_source = (*file_source)->source();
    ASSERT_TRUE(cause_source.is_some());
    EXPECT_TRUE(rstd::error::is<lito::manifest::ManifestSchemaError>(*cause_source));
}

TEST_F(Manifest, TypedManifestDataKeepsItsStructuralPath) {
    auto project = manifest("typed-data-path"_str, R"lito([package]
name = "typed-data-path"
version = "0.1.0"

[[bin]]
name = "typed-data-path"
sources = ["main.cpp"]
link-stdlib = false

[build-tools.generator]
version = "1.2.3"
executable = "bin/generator"
command = "generator"

[build-tools.generator.archives.linux-x86_64]
url = "https://example.com/generator.tgz"
sha256 = "0000000000000000000000000000000000000000000000000000000000000000"
)lito"_str);
    ASSERT_TRUE(project.is_ok());
    auto loaded = lito::manifest::load_manifest_document(project->root.as_path());
    ASSERT_TRUE(loaded.is_err());
    auto error = rstd::move(loaded).unwrap_err_unchecked();
    ASSERT_TRUE(error.is_File());
    const auto& file = error.as_File().source;
    ASSERT_TRUE(file.cause.is_Schema());
    const auto& schema = file.cause.as_Schema().source;
    ASSERT_TRUE(schema.is_Data());
    const auto& data = schema.as_Data().source;
    EXPECT_EQ(data.kind(), rstd::serde::ErrorKind::UnknownField);
    auto path = data.path().segments();
    ASSERT_EQ(path.len(), usize(3));
    EXPECT_EQ(path[usize {}].name().unwrap(), "build-tools"_str);
    EXPECT_EQ(path[usize(1)].name().unwrap(), "generator"_str);
    EXPECT_EQ(path[usize(2)].name().unwrap(), "command"_str);

    auto source = as<rstd::error::Error>(schema).source();
    ASSERT_TRUE(source.is_some());
    EXPECT_TRUE(rstd::error::is<rstd::serde::Error>(*source));
}

TEST_F(Manifest, PackageManifestOwnsTypedTargetCollection) {
    auto multi_target = manifest("multi-target"_str, R"toml([package]
name = "fixture-multi-target"
version = "0.1.0"

[lib]
name = "shared"
module = "fixture.multi"
archive = "fixture_multi"
sources = ["src/lib.cppm"]

[[bin]]
link-stdlib = false
name = "shared"
sources = ["src/main.cpp"]

[[bin]]
name = "tool"
sources = ["src/main.cpp"]
link-stdlib = false

[[test]]
link-stdlib = false
name = "shared"
sources = ["src/main.cpp"]

[[test]]
name = "unit"
sources = ["src/main.cpp"]
link-stdlib = false

[[bench]]
link-stdlib = false
name = "shared"
sources = ["src/main.cpp"]

[[bench]]
name = "speed"
sources = ["src/main.cpp"]
link-stdlib = false
)toml"_str);
    ASSERT_TRUE(multi_target.is_ok());
    auto loaded = lito::manifest::load_package_manifest(multi_target->root.as_path());
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_EQ(loaded->targets.len(), usize(7));

    auto libraries  = usize {};
    auto binaries   = usize {};
    auto tests      = usize {};
    auto benchmarks = usize {};
    auto no_stdlib  = usize {};
    for (const auto& target : loaded->targets) {
        switch (lito::manifest::package_target_kind(target)) {
        case lito::package::PackageTargetKind::Library: ++libraries; break;
        case lito::package::PackageTargetKind::Plugin: break;
        case lito::package::PackageTargetKind::Binary: ++binaries; break;
        case lito::package::PackageTargetKind::Test: ++tests; break;
        case lito::package::PackageTargetKind::Benchmark: ++benchmarks; break;
        case lito::package::PackageTargetKind::ProcMacro:
        case lito::package::PackageTargetKind::TestAttachment:
        case lito::package::PackageTargetKind::CompileTest: break;
        }
        if (! lito::manifest::package_target_links_stdlib(target)) ++no_stdlib;
    }
    EXPECT_EQ(libraries, usize(1));
    EXPECT_EQ(binaries, usize(2));
    EXPECT_EQ(tests, usize(2));
    EXPECT_EQ(benchmarks, usize(2));
    EXPECT_EQ(no_stdlib, usize(6));

    for (const auto& target : loaded->targets) {
        EXPECT_EQ(lito::manifest::package_target_source(target).discovery,
                  lito::manifest::SourceDiscoveryMode::Explicit);
    }

    auto module_project = manifest("directory-markers"_str, R"toml([package]
name = "fixture-convention-markers"
version = "0.1.0"

[lib]
name = "fixture-convention-markers"
module = "fixture.convention.markers"
archive = "fixture.convention.markers"
)toml"_str);
    ASSERT_TRUE(module_project.is_ok());
    auto module = lito::manifest::load_package_manifest(module_project->root.as_path());
    ASSERT_TRUE(module.is_ok());
    ASSERT_EQ(module->targets.len(), usize(1));
    EXPECT_EQ(lito::manifest::package_target_source(module->targets[usize {}]).discovery,
              lito::manifest::SourceDiscoveryMode::Module);
}

TEST_F(Manifest, LibraryOutputDistinguishesStaticArchivesAndSharedArtifacts) {
    auto shared_project = manifest("shared-library"_str, R"toml([package]
name = "fixture-shared-library"
version = "0.1.0"

[lib]
name = "fixture-shared-library"
kind = "shared"
artifact = "fixture_shared"
module = "fixture.shared_library"
linker-options = ["-Wl,--version-script=fixture.map"]
)toml"_str);
    ASSERT_TRUE(shared_project.is_ok());
    auto shared = lito::manifest::load_package_manifest(shared_project->root.as_path());
    ASSERT_TRUE(shared.is_ok());
    ASSERT_EQ(shared->targets.len(), usize(1));
    ASSERT_TRUE(shared->targets[usize {}].is_Library());
    EXPECT_TRUE(shared->targets[usize {}].as_Library().output.is_Shared());
    EXPECT_EQ(shared->targets[usize {}].as_Library().output.as_Shared().artifact.as_str(),
              "fixture_shared"_str);
    ASSERT_EQ(shared->targets[usize {}].as_Library().linker_options.len(), usize(1));
    EXPECT_EQ(shared->targets[usize {}].as_Library().linker_options[usize {}].as_str(),
              "-Wl,--version-script=fixture.map"_str);

    constexpr ref<str> invalid[] = {
        R"toml([package]
name = "fixture-shared-archive"
[lib]
name = "fixture-shared-archive"
kind = "shared"
archive = "fixture"
)toml"_str,
        R"toml([package]
name = "fixture-static-artifact"
[lib]
name = "fixture-static-artifact"
kind = "static"
artifact = "fixture"
)toml"_str,
        R"toml([package]
name = "fixture-static-linker-options"
[lib]
name = "fixture-static-linker-options"
archive = "fixture"
linker-options = ["-Wl,--as-needed"]
)toml"_str,
        R"toml([package]
name = "fixture-two-library-outputs"
[lib]
name = "fixture-two-library-outputs"
archive = "fixture"
artifact = "fixture"
)toml"_str,
        R"toml([package]
name = "fixture-wasm-library-kind"
[lib]
name = "fixture-wasm-library-kind"
kind = "wasm"
artifact = "fixture"
)toml"_str,
        R"toml([package]
name = "fixture-wasm-target"
[wasm]
name = "fixture-wasm-target"
)toml"_str,
    };
    auto index = usize {};
    for (const auto contents : invalid) {
        auto project =
            manifest(rstd::format("invalid-library-output-{}", index).as_str(), contents);
        ASSERT_TRUE(project.is_ok());
        EXPECT_TRUE(lito::manifest::load_package_manifest(project->root.as_path()).is_err());
        ++index;
    }
}

TEST_F(Manifest, PackageStandardDefaultsToCpp20AndAcceptsExplicitCpp) {
    auto implicit_project = manifest("implicit-cpp20-standard"_str, R"toml([package]
name = "fixture-implicit-cpp20-standard"
version = "0.1.0"

[lib]
name = "fixture-implicit-cpp20-standard"
module = "fixture.implicit_cpp20_standard"
archive = "fixture-implicit-cpp20-standard"
sources = ["src/lib.cppm"]
)toml"_str);
    ASSERT_TRUE(implicit_project.is_ok());
    auto implicit = lito::manifest::load_package_manifest(implicit_project->root.as_path());
    ASSERT_TRUE(implicit.is_ok());
    ASSERT_TRUE(implicit->standard.is_some());
    ASSERT_TRUE(implicit->standard->is_Cpp());
    EXPECT_EQ(implicit->standard->as_Cpp().minimum, lito::manifest::CppStandard::Cpp20);

    auto explicit_project = manifest("explicit-cpp23-standard"_str, R"toml([package]
name = "fixture-explicit-cpp23-standard"
version = "0.1.0"
standard = "c++23"

[lib]
name = "fixture-explicit-cpp23-standard"
module = "fixture.explicit_cpp23_standard"
archive = "fixture-explicit-cpp23-standard"
sources = ["src/lib.cppm"]
)toml"_str);
    ASSERT_TRUE(explicit_project.is_ok());
    auto explicit_standard =
        lito::manifest::load_package_manifest(explicit_project->root.as_path());
    ASSERT_TRUE(explicit_standard.is_ok());
    ASSERT_TRUE(explicit_standard->standard.is_some());
    ASSERT_TRUE(explicit_standard->standard->is_Cpp());
    EXPECT_EQ(explicit_standard->standard->as_Cpp().minimum, lito::manifest::CppStandard::Cpp23);
}

TEST_F(Manifest, PackageStandardAndSourceGroupsAreTyped) {
    auto project = manifest("language-source-groups"_str, R"toml([package]
name = "fixture-language-source-groups"
version = "0.1.0"
standard = "c17"

[external-sources.vendor]
path = "vendor"

[source-groups.runtime]
external-source = "vendor"
sources = ["src/runtime.c"]

[source-groups.linux]
sources = ["src/linux.c"]

[lib]
name = "fixture-language-source-groups"
archive = "fixture-language-source-groups"
source-groups = ["runtime"]

[[lib.when]]
condition = 'target.os == "linux"'
source-groups = ["linux"]

[usage]
public-include-directories = [
  { external-source = "vendor", path = "src" },
]
)toml"_str);
    ASSERT_TRUE(project.is_ok());
    auto loaded = lito::manifest::load_package_manifest(project->root.as_path());
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_TRUE(loaded->standard.is_some());
    ASSERT_TRUE(loaded->standard->is_C());
    EXPECT_EQ(loaded->standard->as_C().minimum, lito::manifest::CStandard::C17);
    ASSERT_EQ(loaded->external_sources.len(), usize(1));
    EXPECT_EQ(loaded->external_sources[usize {}].name.as_str(), "vendor"_str);
    ASSERT_EQ(loaded->source_groups.len(), usize(2));
    ASSERT_EQ(loaded->targets.len(), usize(1));
    const auto& source = lito::manifest::package_target_source(loaded->targets[usize {}]);
    ASSERT_EQ(source.source_groups.len(), usize(1));
    EXPECT_EQ(source.source_groups[usize {}].as_str(), "runtime"_str);
    ASSERT_EQ(source.conditions.len(), usize(1));
    EXPECT_EQ(source.conditions[usize {}].source.as_str(), "target.os == \"linux\""_str);
    ASSERT_EQ(source.conditions[usize {}].source_groups.len(), usize(1));
    EXPECT_EQ(source.conditions[usize {}].source_groups[usize {}].as_str(), "linux"_str);
    ASSERT_EQ(loaded->usage.public_include_directory_requirements.len(), usize(1));
    const auto& include = loaded->usage.public_include_directory_requirements[usize {}];
    EXPECT_EQ(include.root, lito::dependency::IncludeDirectoryRoot::ExternalSource);
    ASSERT_TRUE(include.external_source.is_some());
    EXPECT_EQ(include.external_source->as_str(), "vendor"_str);
}

TEST_F(Manifest, ParsesConditionalUsageFeaturesAndDependencyRequests) {
    auto project = manifest("conditional-features"_str, R"toml([package]
name = "fixture-conditional-features"
version = "0.1.0"

[lib]
name = "fixture-conditional-features"
module = "fixture.conditional.features"
archive = "fixture_conditional_features"
sources = ["src/lib.cppm"]

[features.ffi]
default = true

[[when]]
condition = 'target.os == "linux" && feature.ffi'

[when.usage]
private-definitions = ["FIXTURE_LINUX"]

[dependencies.fixture-dependency]
path = "dependency"
visibility = "private"
features = ["api"]
default-features = false
)toml"_str);
    ASSERT_TRUE(project.is_ok());
    auto loaded = lito::manifest::load_package_manifest(project->root.as_path());
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_EQ(loaded->conditions.len(), usize(1));
    ASSERT_EQ(loaded->features.len(), usize(1));
    EXPECT_TRUE(loaded->features[usize {}].default_enabled);
    EXPECT_EQ(loaded->features[usize {}].macro_name, "LITO_FEAT_FFI"_str);
    ASSERT_EQ(loaded->dependencies.len(), usize(1));
    ASSERT_TRUE(loaded->dependencies[usize {}].default_features.is_some());
    EXPECT_FALSE(*loaded->dependencies[usize {}].default_features);
    ASSERT_TRUE(loaded->dependencies[usize {}].features.is_some());
    ASSERT_EQ(loaded->dependencies[usize {}].features->len(), usize(1));
    EXPECT_EQ((*loaded->dependencies[usize {}].features)[usize {}], "api"_str);
}

TEST_F(Manifest, UsageSeparatesLocalOptionsFromTypedLinkRequirements) {
    auto project = manifest("typed-usage"_str, R"toml([package]
name = "fixture-typed-usage"
version = "0.1.0"

[lib]
name = "fixture-typed-usage"
archive = "fixture.typed_usage"
module = "fixture.typed_usage"
sources = ["lib.cppm"]

[usage]
options = ["-Wall"]
linker-options = ["-Wl,--as-needed"]
threads = true
system-libraries = ["dl", "user32"]
)toml"_str);
    ASSERT_TRUE(project.is_ok());
    auto loaded = lito::manifest::load_package_manifest(project->root.as_path());
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_EQ(loaded->usage.options.len(), usize(1));
    EXPECT_EQ(loaded->usage.options[usize {}].as_str(), "-Wall"_str);
    ASSERT_EQ(loaded->usage.linker_options.len(), usize(1));
    EXPECT_TRUE(loaded->usage.threads);
    ASSERT_EQ(loaded->usage.system_libraries.len(), usize(2));
    EXPECT_EQ(loaded->usage.system_libraries[usize {}].as_str(), "dl"_str);
    EXPECT_EQ(loaded->usage.system_libraries[usize(1)].as_str(), "user32"_str);
}

TEST_F(Manifest, PackageManifestOwnsHostBuildToolsAndRuntimeResources) {
    auto project = manifest("build-tools"_str, R"toml([package]
name = "build-tool-valid"
version = "0.1.0"

[[bin]]
link-stdlib = false
name = "tool-consumer"
sources = ["main.cpp"]
resources = [
  { name = "frontend", root = "generated", path = "frontend/default" },
]

[build-tools.generator]
version = "1.2.3"
executable = "bin/generator"

[build-tools.generator.archives.linux-x86_64]
url = "https://example.com/generator-x86_64.tgz"
sha256 = "0000000000000000000000000000000000000000000000000000000000000000"

[build-tools.generator.archives.linux-aarch64]
url = "https://example.com/generator-aarch64.tgz"
sha256 = "1111111111111111111111111111111111111111111111111111111111111111"
)toml"_str);
    ASSERT_TRUE(project.is_ok());
    auto loaded = lito::manifest::load_package_manifest(project->root.as_path());
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_EQ(loaded->build_tools.len(), usize(1));
    const auto& tool = loaded->build_tools[usize {}];
    EXPECT_EQ(tool.alias.as_str(), "generator"_str);
    EXPECT_EQ(tool.version.as_str(), "1.2.3"_str);
    EXPECT_EQ(tool.executable.as_path(), PathBuf::from("bin/generator"_str).as_path());
    ASSERT_EQ(tool.archives.len(), usize(2));
    auto has_x86_64  = false;
    auto has_aarch64 = false;
    for (const auto& archive : tool.archives) {
        EXPECT_EQ(archive.host.os.as_str(), "linux"_str);
        if (archive.host.architecture == lito::system::Architecture::X86_64) has_x86_64 = true;
        if (archive.host.architecture == lito::system::Architecture::Aarch64) has_aarch64 = true;
    }
    EXPECT_TRUE(has_x86_64);
    EXPECT_TRUE(has_aarch64);

    ASSERT_EQ(loaded->targets.len(), usize(1));
    auto resources = lito::manifest::package_target_resources(loaded->targets[usize {}]);
    ASSERT_TRUE(resources.is_some());
    ASSERT_EQ((**resources).len(), usize(1));
    EXPECT_EQ((**resources)[usize {}].name.as_str(), "frontend"_str);
    EXPECT_EQ((**resources)[usize {}].path.as_path(),
              PathBuf::from("frontend/default"_str).as_path());

    auto selected = lito::select_host_build_tool_archive(
        tool,
        lito::system::HostInfo {
            .architecture = lito::system::Architecture::Aarch64,
            .os           = String::make("linux"_str),
        });
    ASSERT_TRUE(selected.is_ok());
    EXPECT_EQ((**selected).sha256.to_hex().as_str(),
              "1111111111111111111111111111111111111111111111111111111111111111"_str);

    auto unsupported =
        lito::select_host_build_tool_archive(tool,
                                             lito::system::HostInfo {
                                                 .architecture = lito::system::Architecture::X86_64,
                                                 .os           = String::make("windows"_str),
                                             });
    ASSERT_TRUE(unsupported.is_err());
    EXPECT_TRUE(unsupported.unwrap_err().is_UnsupportedHost());
}

TEST_F(Manifest, ScriptPackageHasFixedEntryAndBuiltinDependencySource) {
    constexpr ProjectFile files[] = {
        {
            "lito.toml"_str,
            R"toml([package]
name = "fixture-lua-package"
version = "0.1.0"

[script]
supports = ["build", "install"]

[dependencies.lito-qt]
builtin = "qt"
)toml"_str,
        },
        { "lib.lua"_str, "return { enabled = true }\n"_str },
    };
    auto project = materialize("lua-package"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto loaded = lito::manifest::load_package_manifest(project->root.as_path());
    ASSERT_TRUE(loaded.is_ok());
    EXPECT_TRUE(loaded->targets.is_empty());
    ASSERT_TRUE(loaded->script.is_some());
    ASSERT_EQ(loaded->script->supports.len(), usize(2));
    ASSERT_EQ(loaded->dependencies.len(), usize(1));
    EXPECT_EQ(loaded->dependencies[usize {}].name.as_str(), "lito-qt"_str);
    EXPECT_TRUE(loaded->dependencies[usize {}].source.resolution.is_Builtin());
    EXPECT_EQ(loaded->dependencies[usize {}].source.resolution.as_Builtin().id.as_str(), "qt"_str);
}

TEST_F(Manifest, ManifestLocatorPrefersLitoAndAcceptsLegacyTenon) {
    auto legacy_project = manifest("legacy-name"_str,
                                   R"toml([package]
name = "legacy-manifest"
version = "0.1.0"

[lib]
name = "legacy-manifest"
module = "fixture.manifest.legacy"
archive = "fixture.manifest.legacy"
)toml"_str,
                                   "tenon.toml"_str);
    ASSERT_TRUE(legacy_project.is_ok());
    auto legacy = lito::manifest::load_package_manifest(legacy_project->root.as_path());
    ASSERT_TRUE(legacy.is_ok());
    EXPECT_EQ(legacy->name.as_str(), "legacy-manifest"_str);

    auto preferred_project = manifest("preferred-name"_str, R"toml([package]
name = "preferred-manifest"
version = "0.1.0"

[lib]
name = "preferred-manifest"
module = "fixture.manifest.preferred"
archive = "fixture.manifest.preferred"
)toml"_str);
    ASSERT_TRUE(preferred_project.is_ok());
    auto preferred = lito::manifest::load_package_manifest(preferred_project->root.as_path());
    ASSERT_TRUE(preferred.is_ok());
    EXPECT_EQ(preferred->name.as_str(), "preferred-manifest"_str);
}

TEST_F(Manifest, PackageSourceRootIsIndependentFromWorkspaceMemberDirectory) {
    const ProjectFile files[] = {
        {
            "lito.toml"_str,
            R"toml([workspace]
name = "fixture-shared-source-root"
members = ["packages/library"]

[workspace.package]
version = "0.1.0"
)toml"_str,
        },
        {
            "packages/library/lito.toml"_str,
            R"toml([package]
name = "fixture-shared-source-root-library"
version.workspace = true
source-root = "../.."

[lib]
name = "fixture-shared-source-root-library"
module = "fixture.shared_source_root"
archive = "fixture-shared-source-root"
sources = ["src/shared.cppm"]
)toml"_str,
        },
    };
    auto project = materialize("shared-source-root"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto package = project->root.join(PathBuf::from("packages/library"_str).as_path());
    auto loaded  = lito::manifest::load_package_manifest(package.as_path());
    ASSERT_TRUE(loaded.is_ok());
    EXPECT_NE(loaded->root.as_path(), loaded->source_root.as_path());
    EXPECT_EQ(loaded->source_root.as_path(), project->root.as_path());
}

TEST_F(Manifest, ManifestGitCommitIsTypedAndValidated) {
    auto project = manifest("git-commit"_str, R"toml([package]
name = "fixture-git-commit"
version = "0.1.0"

[lib]
name = "fixture-git-commit"
module = "fixture.git.commit"
archive = "fixture.git.commit"
sources = ["source.cppm"]

[dependencies.fixture-dependency]
git = "https://example.invalid/dependency.git"
commit = "0123456789abcdef0123456789abcdef01234567"
visibility = "private"
)toml"_str);
    ASSERT_TRUE(project.is_ok());
    auto loaded = lito::manifest::load_package_manifest(project->root.as_path());
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_EQ(loaded->dependencies.len(), usize(1));
    const auto& source = loaded->dependencies[usize {}].source.resolution;
    ASSERT_TRUE(source.is_Git());
    EXPECT_EQ(source.as_Git().reference.kind, lito::source::GitReferenceKind::Commit);
    EXPECT_EQ(source.as_Git().reference.value.as_str(),
              "0123456789abcdef0123456789abcdef01234567"_str);
}

TEST_F(Manifest, RegistryDependencyEditReplacesOnlyTheSourceSelection) {
    auto project = manifest("registry-dependency-edit"_str, R"toml(# normalized by manifest edits
[package]
name = "fixture-registry-dependency-edit"
version = "0.1.0"

[lib]
name = "fixture-registry-dependency-edit"
module = "fixture.registry_dependency_edit"
archive = "fixture-registry-dependency-edit"

[dependencies.sample]
git = "https://example.invalid/sample.git"
tag = "v1"
visibility = "public"
)toml"_str);
    ASSERT_TRUE(project.is_ok());
    auto package     = lito::registry::RegistryPackageName::parse("sample"_str);
    auto requirement = lito::registry::VersionRequirement::parse("0.4"_str);
    ASSERT_TRUE(package.is_ok());
    ASSERT_TRUE(requirement.is_ok());
    auto edited = lito::manifest::add_registry_dependency(
        project->root.as_path(), *package, *requirement, Some(String::make("official"_str)));
    ASSERT_TRUE(edited.is_ok());

    auto loaded = lito::manifest::load_package_manifest(project->root.as_path());
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_EQ(loaded->dependencies.len(), usize(1));
    const auto& dependency = loaded->dependencies[usize {}];
    ASSERT_TRUE(dependency.source.resolution.is_Registry());
    EXPECT_EQ(dependency.name.as_str(), "sample"_str);
    EXPECT_EQ(dependency.source.resolution.as_Registry().registry->as_str(), "official"_str);
    EXPECT_EQ(dependency.source.resolution.as_Registry().requirement.text(), "0.4"_str);
    ASSERT_TRUE(dependency.visibility.is_some());
    EXPECT_EQ(*dependency.visibility, lito::dependency::DependencyVisibility::Public);

    auto contents = rstd::fs::read_to_string(edited->path.as_path());
    ASSERT_TRUE(contents.is_ok());
    EXPECT_FALSE(contents->as_str().contains("example.invalid"_str));
    EXPECT_FALSE(contents->as_str().contains("# normalized"_str));
}

TEST_F(Manifest, PluginTargetUsesThePackageName) {
    auto project = manifest("compiler-plugin"_str, R"toml([package]
name = "fixture-compiler-plugin"
version = "0.1.0"

[plugin]
module = "fixture.compiler_plugin"
sources = ["src/lib.cppm", "src/plugin.cpp"]
)toml"_str);
    ASSERT_TRUE(project.is_ok());
    auto loaded = lito::manifest::load_package_manifest(project->root.as_path());
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_EQ(loaded->targets.len(), usize(1));
    ASSERT_TRUE(loaded->targets[usize {}].is_Plugin());
    EXPECT_EQ(loaded->targets[usize {}].as_Plugin().name.as_str(), "fixture-compiler-plugin"_str);

    constexpr ref<str> invalid[] = {
        R"toml([package]
name = "fixture-plugin-with-action"
version = "0.1.0"
[plugin]
module = "fixture.plugin_with_action"
action = "legacy-action"
)toml"_str,
        R"toml([package]
name = "fixture-c-plugin"
version = "0.1.0"
standard = "c17"
[plugin]
sources = ["src/plugin.c"]
)toml"_str,
        R"toml([package]
name = "fixture-mixed-plugin"
version = "0.1.0"
[lib]
module = "fixture.mixed_plugin"
[plugin]
module = "fixture.mixed_plugin_support"
)toml"_str,
    };
    auto index = usize {};
    for (auto contents : invalid) {
        auto invalid_project =
            manifest(rstd::format("invalid-plugin-{}", index).as_str(), contents);
        ASSERT_TRUE(invalid_project.is_ok());
        EXPECT_TRUE(
            lito::manifest::load_package_manifest(invalid_project->root.as_path()).is_err());
        ++index;
    }
}

TEST_F(Manifest, PmacroTargetUsesOrdinaryDependencyDomain) {
    auto provider_project = manifest("proc-macro-provider"_str, R"toml([package]
name = "fixture-proc-macro-provider"
version = "0.1.0"

[pmacro]
module = "fixture.proc_macro.provider"
sources = ["src/lib.cppm"]
)toml"_str);
    ASSERT_TRUE(provider_project.is_ok());
    auto provider = lito::manifest::load_package_manifest(provider_project->root.as_path());
    ASSERT_TRUE(provider.is_ok());
    ASSERT_EQ(provider->targets.len(), usize(1));
    ASSERT_TRUE(provider->targets[usize {}].is_ProcMacro());
    EXPECT_EQ(provider->targets[usize {}].as_ProcMacro().name.as_str(),
              "fixture-proc-macro-provider"_str);
    ASSERT_TRUE(provider->targets[usize {}].as_ProcMacro().source.module.is_some());
    EXPECT_EQ(provider->targets[usize {}].as_ProcMacro().source.module->as_str(),
              "fixture.proc_macro.provider"_str);

    auto consumer_project = manifest("proc-macro-consumer"_str, R"toml([package]
name = "fixture-proc-macro-consumer"
version = "0.1.0"

[lib]
name = "fixture-proc-macro-consumer"
module = "fixture.proc_macro.consumer"
archive = "fixture-proc-macro-consumer"

[dependencies.fixture-proc-macro-provider]
version = "^0.1"
features = ["diagnostics"]
default-features = false
)toml"_str);
    ASSERT_TRUE(consumer_project.is_ok());
    auto consumer = lito::manifest::load_package_manifest(consumer_project->root.as_path());
    ASSERT_TRUE(consumer.is_ok());
    ASSERT_EQ(consumer->dependencies.len(), usize(1));
    const auto& dependency = consumer->dependencies[usize {}];
    EXPECT_EQ(dependency.name.as_str(), "fixture-proc-macro-provider"_str);
    ASSERT_TRUE(dependency.features.is_some());
    ASSERT_EQ(dependency.features->len(), usize(1));
    EXPECT_EQ((*dependency.features)[usize {}].as_str(), "diagnostics"_str);
    ASSERT_TRUE(dependency.default_features.is_some());
    EXPECT_FALSE(*dependency.default_features);
}

TEST_F(Manifest, PmacroManifestRejectsMixedAndLegacyTargets) {
    constexpr ref<str> invalid[] = {
        R"toml([package]
name = "fixture-mixed-proc-macro"
version = "0.1.0"
[lib]
module = "fixture.mixed"
[pmacro]
module = "fixture.mixed.macros"
)toml"_str,
        R"toml([package]
name = "fixture-c-pmacro"
version = "0.1.0"
standard = "c17"
[pmacro]
sources = ["src/lib.c"]
)toml"_str,
        R"toml([package]
name = "fixture-legacy-proc-macro"
version = "0.1.0"
[proc-macro]
module = "fixture.legacy"
)toml"_str,
        R"toml([package]
name = "fixture-legacy-proc-macro-dependency"
version = "0.1.0"
[lib]
module = "fixture.legacy_dependency"
[proc-macro-dependencies.other]
version = "1.0"
)toml"_str,
    };
    auto index = usize {};
    for (auto contents : invalid) {
        auto project = manifest(rstd::format("invalid-proc-macro-{}", index).as_str(), contents);
        ASSERT_TRUE(project.is_ok());
        EXPECT_TRUE(lito::manifest::load_package_manifest(project->root.as_path()).is_err());
        ++index;
    }
}

TEST_F(Manifest, StandaloneManifestPreservesPmacroAsOrdinaryDependency) {
    auto project = manifest("standalone-proc-macro"_str, R"toml([package]
name = "fixture-standalone-proc-macro"
version = "1.2.3"

[pmacro]
module = "fixture.standalone.proc_macro"

[dependencies.fixture-proc-macro-provider]
version = "^2.0"
features = ["diagnostics"]
default-features = false
)toml"_str);
    ASSERT_TRUE(project.is_ok());
    auto loaded = lito::manifest::load_package_manifest(project->root.as_path());
    ASSERT_TRUE(loaded.is_ok());
    auto serialized = lito::manifest::serialize_standalone_package_manifest(
        *loaded,
        lito::manifest::StandaloneManifestOptions {
            .owner_registry =
                lito::registry::RegistryId::parse("https://registry.example/"_str).unwrap(),
        });
    ASSERT_TRUE(serialized.is_ok());
    EXPECT_TRUE(serialized->as_str().contains("[pmacro]"_str));
    EXPECT_FALSE(serialized->as_str().contains("[proc-macro]"_str));
    EXPECT_TRUE(serialized->as_str().contains("[dependencies.fixture-proc-macro-provider]"_str));
    EXPECT_FALSE(serialized->as_str().contains("proc-macro-dependencies"_str));
    auto reparsed = rstd::toml::from_str(serialized->as_str());
    ASSERT_TRUE(reparsed.is_ok());
}
