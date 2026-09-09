#include <rstd/test/gtest.hpp>

import rstd;
import licrypto;
import lito.tools;
import rstd.test;
import lito.core;
import lito.system;
import lito.driver;
import lito.test.support;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;
using namespace lito_test;
using PathBuf = rstd::path::PathBuf;

class SourceBundle : public ProjectFixture {};

TEST_F(SourceBundle, LayoutOwnsVersionedReadOnlyLookup) {
    auto directory = cache_root("source-bundle-layout"_str);
    auto identity  = lito::source::git_fetch_identity(
        "https://example.invalid/source.git"_str, "0123456789abcdef0123456789abcdef01234567"_str);
    auto payload = lito::source::SourceBundleLayout(directory.clone()).git(identity);
    ASSERT_TRUE(rstd::fs::create_dir_all(payload.as_path()).is_ok());

    auto roots = Vec<PathBuf>::make();
    roots.push(directory.clone());
    auto located = lito::source::locate_source_bundle(roots, identity);
    ASSERT_TRUE(located.is_ok());
    ASSERT_TRUE(located->is_some());
    EXPECT_EQ((**located).as_path(), payload.as_path());

    auto unrelated = directory.join(PathBuf::from("git/source"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(unrelated.as_path()).is_ok());
    auto missing = lito::source::git_fetch_identity("https://example.invalid/other.git"_str,
                                                    "0123456789abcdef0123456789abcdef01234567"_str);
    auto absent  = lito::source::locate_source_bundle(roots, missing);
    ASSERT_TRUE(absent.is_ok());
    EXPECT_TRUE(absent->is_none());

    auto layout   = lito::source::SourceBundleLayout(directory.clone());
    auto manifest = PathBuf::from("rust/Cargo.toml"_str);
    auto cargo    = layout.cargo(
        "external-source-identity"_str, manifest.as_path(), "x86_64-unknown-linux-gnu"_str);
    EXPECT_EQ(layout
                  .cargo_config("external-source-identity"_str,
                                manifest.as_path(),
                                "x86_64-unknown-linux-gnu"_str)
                  .as_path(),
              cargo.join(PathBuf::from("config.toml"_str).as_path()).as_path());

    auto registry = lito::registry::RegistryPackageId {
        .registry = lito::registry::RegistryId::parse("https://registry.example/"_str).unwrap(),
        .name     = lito::registry::RegistryPackageName::parse("sample"_str).unwrap(),
    };
    auto registry_key = lito::registry::registry_key(registry.registry);
    EXPECT_EQ(layout.registry_index(registry).as_path(),
              directory.join(PathBuf::from("v1/registry/index"_str).as_path())
                  .join(PathBuf::from(registry_key.as_str()).as_path())
                  .join(PathBuf::from("sample.json"_str).as_path())
                  .as_path());

    auto pin = lito::registry::RegistryReleasePin {
        .release =
            lito::registry::RegistryReleaseId {
                .package = registry.clone(),
                .version = lito::registry::SemanticVersion::parse("1.2.3"_str).unwrap(),
            },
        .checksum = lito::registry::PackageChecksum::parse(
                        "1111111111111111222222222222222233333333333333334444444444444444"_str)
                        .unwrap(),
    };
    EXPECT_EQ(
        layout.registry_package(pin).as_path(),
        directory
            .join(PathBuf::from("v1/registry/source/sample-1.2.3-1111111111111111.tar.zst"_str)
                      .as_path())
            .as_path());

    auto patched = lito::source::acquired_git_fetch_identity(
        lito::source::AcquiredSource {
            .root     = directory.clone(),
            .identity = String::make("path+../patched-source"_str),
        },
        "https://example.invalid/source.git"_str);
    ASSERT_TRUE(patched.is_ok());
    EXPECT_TRUE(patched->is_none());
}

TEST_F(SourceBundle, OfflineArchiveAcquisitionUsesVerifiedBundleWithoutDownloadTool) {
    auto directory = cache_root("offline-archive-bundle"_str);
    auto contents  = "source bundle archive bytes"_str;
    auto url       = lito::parse::FetchUrl::parse("https://example.invalid/source.tar.gz"_str);
    ASSERT_TRUE(url.is_ok());
    auto digest   = licrypto::sha256_digest(contents);
    auto identity = lito::source::archive_fetch_identity(url->clone(), digest.clone());
    auto payload  = lito::source::SourceBundleLayout(directory.clone()).archive(identity);
    ASSERT_TRUE(rstd::fs::create_dir_all(payload.as_path().parent().unwrap()).is_ok());
    ASSERT_TRUE(rstd::fs::write_atomic(payload.as_path(), contents.as_bytes()).is_ok());

    auto roots = Vec<PathBuf>::make();
    roots.push(directory.clone());
    auto located = lito::source::locate_source_bundle(roots, identity);
    ASSERT_TRUE(located.is_ok());
    ASSERT_TRUE(located->is_some());

    auto environment =
        lito::system::ResolvedProcessEnvironment::resolve(lito::system::ProcessEnvironmentSpec {});
    ASSERT_TRUE(environment.is_ok());
    auto resolver = lito::tools::ToolResolver(*environment);
    auto requests = Vec<lito::tools::acquisition::VerifiedArchiveRequest>::make();
    requests.push(lito::tools::acquisition::VerifiedArchiveRequest {
        .label                = String::make("archive bundle"_str),
        .url                  = rstd::move(url).unwrap(),
        .sha256               = rstd::move(digest),
        .provided_source      = Some(rstd::move(located).unwrap().unwrap()),
        .download_requirement = lito::tools::external_source_tool_requirement(
            lito::tools::HostToolCapability::HttpDownload, "bundle-fixture"_str, "archive"_str),
    });
    auto acquired = lito::tools::acquisition::acquire_verified_files(
        rstd::move(requests), usize(1), resolver, *environment, true);
    ASSERT_TRUE(acquired.is_ok());
    ASSERT_EQ(acquired->len(), usize(1));
    EXPECT_EQ((*acquired)[usize {}].path.as_path(), payload.as_path());
}

TEST_F(SourceBundle, OfflineGitResolutionUsesLockedAndExactCommitBundleWithoutFetch) {
    auto directory = cache_root("offline-git-bundle"_str);
    auto work      = directory.join(PathBuf::from("work"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(work.as_path()).is_ok());
    ASSERT_TRUE(git_succeeds(work.as_path(), "init"_str));
    ASSERT_TRUE(git_succeeds(work.as_path(), "config"_str, "user.name"_str, "Lito Test"_str));
    ASSERT_TRUE(
        git_succeeds(work.as_path(), "config"_str, "user.email"_str, "lito@example.invalid"_str));
    auto content = work.join(PathBuf::from("source.txt"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(content.as_path(), "bundle\n"_str.as_bytes()).is_ok());
    ASSERT_TRUE(git_succeeds(work.as_path(), "add"_str, "source.txt"_str));
    ASSERT_TRUE(git_succeeds(work.as_path(),
                             "-c"_str,
                             "commit.gpgsign=false"_str,
                             "commit"_str,
                             "-m"_str,
                             "bundle"_str));
    auto commit = git_revision(work.as_path(), "HEAD"_str);
    ASSERT_TRUE(commit.is_some());
    auto url      = "https://example.invalid/bundle-only.git"_str;
    auto identity = lito::source::git_fetch_identity(url, commit->as_str());
    auto checkout = lito::source::SourceBundleLayout(directory.clone()).git(identity);
    ASSERT_TRUE(rstd::fs::create_dir_all(checkout.as_path().parent().unwrap()).is_ok());
    ASSERT_TRUE(git_succeeds(directory.as_path(),
                             "clone"_str,
                             "--no-checkout"_str,
                             work.as_path().as_os_str(),
                             checkout.as_path().as_os_str()));
    ASSERT_TRUE(git_succeeds(checkout.as_path(), "checkout"_str, "--detach"_str, commit->as_str()));

    auto environment =
        lito::system::ResolvedProcessEnvironment::resolve(lito::system::ProcessEnvironmentSpec {});
    ASSERT_TRUE(environment.is_ok());
    auto resolver = lito::tools::ToolResolver(*environment);
    auto pins     = Vec<lito::source::GitSourcePin>::make();
    pins.push(lito::source::GitSourcePin {
        .git    = String::make(url),
        .commit = commit->clone(),
    });
    auto bundles = Vec<PathBuf>::make();
    bundles.push(directory.clone());
    auto events = FetchEventCapture { .expected_url = url };
    auto manager =
        lito::source::SourceManager(directory.as_path(),
                                    lito::source::SourceResolutionOptions {
                                        .locked      = true,
                                        .git_sources = rstd::move(pins),
                                        .sources =
                                            lito::source::PackageSourceConfig {
                                                .source_bundles = rstd::move(bundles),
                                                .network = lito::source::NetworkPolicy::Offline,
                                            },
                                    },
                                    resolver,
                                    *environment,
                                    lito::source::SourceEventSink {
                                        .context = rstd::addressof(events),
                                        .notify  = capture_source_fetch,
                                    });
    auto acquired = manager.acquire_external(lito::source::PackageSourceRequirement::Git(
                                                 String::make(url), lito::source::GitReference {}),
                                             directory.as_path());
    ASSERT_TRUE(acquired.is_ok());
    EXPECT_EQ(acquired->root.as_path(), checkout.as_path());
    EXPECT_EQ(events.count, usize {});
}
