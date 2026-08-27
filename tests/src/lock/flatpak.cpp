#include <rstd/test/gtest.hpp>

import rstd;
import lito.crypto;
import rstd.test;
import lito.core;
import lito.system;
import lito.tools.cargo;
import lito.tools.cmake;
import lito.toolchain;
import lito.driver;
import lito.test.support;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;
using namespace lito_test;
using PathBuf = rstd::path::PathBuf;

namespace
{

auto cargo_fixture_path(const rstd::test::TempDir& directory, ref<str> name) -> PathBuf {
    return PathBuf::from(directory.path()).join(PathBuf::from(name).as_path());
}

auto write_cargo_fixture(ref<rstd::path::Path> path, ref<str> contents) -> bool {
    return rstd::fs::write(path, contents.as_bytes()).is_ok();
}

} // namespace

TEST(Lock, FetchIdentityAndFlatpakProjectionAreStableAndDeduplicated) {
    auto format = lito::lock::parse_lock_export_format("flatpak-sources"_str);
    ASSERT_TRUE(format.is_some());
    EXPECT_EQ(*format, lito::lock::LockExportFormat::FlatpakSources);
    EXPECT_EQ(lito::lock::lock_export_format_name(*format), "flatpak-sources"_str);
    EXPECT_TRUE(lito::lock::parse_lock_export_format("unknown"_str).is_none());

    auto git_url  = "https://example.invalid/shared.git"_str;
    auto commit   = "0123456789abcdef0123456789abcdef01234567"_str;
    auto identity = lito::source::git_fetch_identity(git_url, commit);
    EXPECT_EQ(lito::source::fetch_identity_text(identity).as_str(),
              "lito-fetch-v1\ngit\nhttps://example.invalid/shared.git\n"
              "0123456789abcdef0123456789abcdef01234567"_str);
    EXPECT_EQ(lito::source::fetch_identity_stable_key(identity).len(), usize(64));

    auto project = lito::lock::LockedProject {};
    project.packages.push(lito::lock::LockedPackage {
        .name         = String::make("app"_str),
        .version      = Some(String::make("0.1.0"_str)),
        .source       = None(),
        .dependencies = Vec<String>::make(),
    });
    auto x86_architectures = Vec<Architecture>::make();
    x86_architectures.push(Architecture::X86_64);
    project.packages[usize {}].externals.push(lito::lock::LockedPackageExternalSource {
        .name          = String::make("shared-x86"_str),
        .architectures = rstd::move(x86_architectures),
        .source        = lito::lock::LockedSource::Git(String::make(git_url), String::make(commit)),
    });
    auto arm_architectures = Vec<Architecture>::make();
    arm_architectures.push(Architecture::Aarch64);
    project.packages[usize {}].externals.push(lito::lock::LockedPackageExternalSource {
        .name          = String::make("shared-arm"_str),
        .architectures = rstd::move(arm_architectures),
        .source        = lito::lock::LockedSource::Git(String::make(git_url), String::make(commit)),
    });
    project.packages[usize {}].externals.push(lito::lock::LockedPackageExternalSource {
        .name          = String::make("archive"_str),
        .architectures = Vec<Architecture>::make(),
        .source        = lito::lock::LockedSource::Archive(
            lito::parse::FetchUrl::parse("https://example.invalid/archive.tar.gz"_str).unwrap(),
            lito::crypto::Sha256Digest::parse_hex(
                "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"_str)
                .unwrap()),
    });
    auto first  = lito::lock::flatpak_sources_json(project);
    auto second = lito::lock::flatpak_sources_json(project);
    ASSERT_TRUE(first.is_ok());
    ASSERT_TRUE(second.is_ok());
    EXPECT_EQ(*first, *second);
    auto git_source = first->as_str().split_once("\"type\": \"git\""_str);
    ASSERT_TRUE(git_source.is_some());
    EXPECT_FALSE(git_source->get<1>().contains("\"type\": \"git\""_str));
    EXPECT_TRUE(first->as_str().contains("\"type\": \"file\""_str));
    EXPECT_TRUE(first->as_str().contains("\"only-arches\""_str));
    EXPECT_FALSE(first->as_str().contains("\"type\": \"inline\""_str));
    EXPECT_TRUE(first->as_str().contains("v1/git/"_str));
    EXPECT_TRUE(first->as_str().contains("v1/archives/"_str));
    EXPECT_TRUE(first->as_str().contains("\"dest-filename\": \"source.archive\""_str));
}

TEST(Lock, PackageGitSourceExportsWithoutLocalExternalEntries) {
    auto project = lito::lock::LockedProject {};
    project.packages.push(lito::lock::LockedPackage {
        .name    = String::make("wavsen"_str),
        .version = Some(String::make("0.1.0"_str)),
        .source  = Some(lito::lock::LockedSource::Git(
            String::make("https://example.invalid/wavsen.git"_str),
            String::make("0123456789abcdef0123456789abcdef01234567"_str))),
    });

    auto exported = lito::lock::flatpak_sources_json(project);
    ASSERT_TRUE(exported.is_ok());
    auto git_source = exported->as_str().split_once("\"type\": \"git\""_str);
    ASSERT_TRUE(git_source.is_some());
    EXPECT_FALSE(git_source->get<1>().contains("\"type\": \"git\""_str));
    EXPECT_TRUE(exported->as_str().contains("https://example.invalid/wavsen.git"_str));
}

TEST(Lock, CargoAttachmentProjectsRegistrySourcesAndSkipsPathPackages) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto owner = rstd::move(temporary).unwrap();
    auto lock  = cargo_fixture_path(owner, "Cargo.lock"_str);
    ASSERT_TRUE(write_cargo_fixture(lock.as_path(),
                                    R"(version = 4

[[package]]
name = "local"
version = "0.1.0"

[[package]]
name = "serde"
version = "1.0.228"
source = "registry+https://github.com/rust-lang/crates.io-index"
checksum = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
)"_str));

    auto document = lito::tools::cargo::parse_locked_document(lock.as_path());
    ASSERT_TRUE(document.is_ok());
    EXPECT_EQ(document->version, u64(4));
    EXPECT_EQ(document->packages.len(), usize(2));
    EXPECT_TRUE(lito::tools::cargo::locked_git_requests(*document).is_empty());

    auto projected = lito::tools::cargo::project_flatpak_sources(
        *document, Vec<lito::tools::cargo::GitCheckout>::make());
    ASSERT_TRUE(projected.is_ok());
    auto serialized = lito::flatpak::sources_json(*projected);
    ASSERT_TRUE(serialized.is_ok());
    EXPECT_TRUE(serialized->as_str().contains(
        "https://static.crates.io/crates/serde/serde-1.0.228.crate"_str));
    EXPECT_TRUE(serialized->as_str().contains("cargo/vendor/serde-1.0.228"_str));
    EXPECT_TRUE(serialized->as_str().contains(".cargo-checksum.json"_str));
    EXPECT_TRUE(serialized->as_str().contains("\"dest-filename\": \"config\""_str));
    EXPECT_FALSE(serialized->as_str().contains("cargo/vendor/local"_str));

    ASSERT_TRUE(write_cargo_fixture(lock.as_path(),
                                    R"(version = 4

[[package]]
name = "private"
version = "1.0.0"
source = "registry+https://example.invalid/index"
checksum = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
)"_str));
    EXPECT_TRUE(lito::tools::cargo::parse_locked_document(lock.as_path()).is_err());
}

TEST(Lock, CargoAttachmentProjectsGitWorkspacePackage) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto owner    = rstd::move(temporary).unwrap();
    auto checkout = cargo_fixture_path(owner, "checkout"_str);
    auto member   = checkout.join(PathBuf::from("member"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(member.as_path()).is_ok());
    ASSERT_TRUE(
        write_cargo_fixture(checkout.join(PathBuf::from("Cargo.toml"_str).as_path()).as_path(),
                            R"([workspace]
members = ["member"]

[workspace.package]
version = "1.2.3"
edition = "2021"

[workspace.dependencies]
serde = { version = "1", features = ["derive"] }
)"_str));
    ASSERT_TRUE(
        write_cargo_fixture(member.join(PathBuf::from("Cargo.toml"_str).as_path()).as_path(),
                            R"([package]
name = "fixture-git"
version.workspace = true
edition.workspace = true

[dependencies]
serde = { workspace = true, features = ["alloc"] }
)"_str));

    constexpr auto commit = "0123456789abcdef0123456789abcdef01234567"_str;
    auto           lock   = cargo_fixture_path(owner, "Cargo.lock"_str);
    ASSERT_TRUE(write_cargo_fixture(lock.as_path(),
                                    R"(version = 4

[[package]]
name = "fixture-git"
version = "1.2.3"
source = "git+https://github.com/Example/Repo.git?rev=main#0123456789abcdef0123456789abcdef01234567"
)"_str));
    auto document = lito::tools::cargo::parse_locked_document(lock.as_path());
    ASSERT_TRUE(document.is_ok());
    auto requests = lito::tools::cargo::locked_git_requests(*document);
    ASSERT_EQ(requests.len(), usize(1));
    EXPECT_EQ(requests[usize {}].url.as_str(), "https://github.com/example/repo"_str);
    EXPECT_EQ(requests[usize {}].commit.as_str(), commit);

    auto checkouts = Vec<lito::tools::cargo::GitCheckout>::make();
    checkouts.push(lito::tools::cargo::GitCheckout {
        .url    = requests[usize {}].url.clone(),
        .commit = requests[usize {}].commit.clone(),
        .root   = checkout.clone(),
    });
    auto projected = lito::tools::cargo::project_flatpak_sources(*document, checkouts);
    ASSERT_TRUE(projected.is_ok());
    auto manifest_contents = Option<ref<str>> {};
    auto config_contents   = Option<ref<str>> {};
    for (const auto& entry : projected->entries) {
        if (! entry.source.is_Inline()) continue;
        const auto& value = entry.source.as_Inline();
        if (value.filename.as_str() == "Cargo.toml"_str) {
            manifest_contents = Some(value.contents.as_str());
        } else if (value.filename.as_str() == "config"_str) {
            config_contents = Some(value.contents.as_str());
        }
    }
    ASSERT_TRUE(manifest_contents.is_some());
    EXPECT_TRUE((*manifest_contents).contains("version = \"1.2.3\""_str));
    EXPECT_TRUE((*manifest_contents).contains("edition = \"2021\""_str));
    EXPECT_TRUE((*manifest_contents).contains("\"alloc\", \"derive\""_str));
    ASSERT_TRUE(config_contents.is_some());
    EXPECT_TRUE((*config_contents).contains("rev = \"main\""_str));
    auto serialized = lito::flatpak::sources_json(*projected);
    ASSERT_TRUE(serialized.is_ok());
    EXPECT_TRUE(serialized->as_str().contains("flatpak-cargo/git/repo-0123456"_str));
    EXPECT_TRUE(serialized->as_str().contains("cargo/vendor/fixture-git"_str));
}
