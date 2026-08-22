#include <rstd/test/gtest.hpp>

import rstd;
import rstd.serde;
import rstd.test;
import lito.core;
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

    auto workspace_project = manifest("workspace-license"_str, R"toml([workspace]
name = "fixture-workspace-license"
members = ["package"]

[workspace.package]
version = "0.1.0"
license = "MIT OR Apache-2.0"
authors = ["Lito Authors <authors@example.invalid>"]
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
    ASSERT_TRUE(dependency.source.is_Registry());
    const auto& source = dependency.source.as_Registry();
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
        case lito::package::PackageTargetKind::Binary: ++binaries; break;
        case lito::package::PackageTargetKind::Test: ++tests; break;
        case lito::package::PackageTargetKind::Benchmark: ++benchmarks; break;
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
)toml"_str);
    ASSERT_TRUE(shared_project.is_ok());
    auto shared = lito::manifest::load_package_manifest(shared_project->root.as_path());
    ASSERT_TRUE(shared.is_ok());
    ASSERT_EQ(shared->targets.len(), usize(1));
    ASSERT_TRUE(shared->targets[usize {}].is_Library());
    EXPECT_TRUE(shared->targets[usize {}].as_Library().output.is_Shared());
    EXPECT_EQ(shared->targets[usize {}].as_Library().output.as_Shared().artifact.as_str(),
              "fixture_shared"_str);

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
name = "fixture-two-library-outputs"
[lib]
name = "fixture-two-library-outputs"
archive = "fixture"
artifact = "fixture"
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
        if (archive.host.architecture.as_str() == "x86_64"_str) has_x86_64 = true;
        if (archive.host.architecture.as_str() == "aarch64"_str) has_aarch64 = true;
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
            .architecture = lito::system::canonical_architecture("aarch64"_str).unwrap(),
            .os           = String::make("linux"_str),
        });
    ASSERT_TRUE(selected.is_ok());
    EXPECT_EQ((**selected).sha256.to_hex().as_str(),
              "1111111111111111111111111111111111111111111111111111111111111111"_str);

    auto unsupported = lito::select_host_build_tool_archive(
        tool,
        lito::system::HostInfo {
            .architecture = lito::system::canonical_architecture("x86_64"_str).unwrap(),
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
    EXPECT_TRUE(loaded->dependencies[usize {}].source.is_Builtin());
    EXPECT_EQ(loaded->dependencies[usize {}].source.as_Builtin().id.as_str(), "qt"_str);
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
    const auto& source = loaded->dependencies[usize {}].source;
    ASSERT_TRUE(source.is_Git());
    EXPECT_EQ(source.as_Git().reference.kind, lito::source::GitReferenceKind::Commit);
    EXPECT_EQ(source.as_Git().reference.value.as_str(),
              "0123456789abcdef0123456789abcdef01234567"_str);
}
