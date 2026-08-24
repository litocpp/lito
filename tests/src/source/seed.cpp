#include <rstd/test/gtest.hpp>

import rstd;
import lito.tools;
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

class SourceSeed : public ProjectFixture {};

TEST_F(SourceSeed, FetchSeedCatalogOwnsSafeReadOnlyLookup) {
    auto directory = cache_root("fetch-seed-catalog"_str);
    auto payload   = directory.join(PathBuf::from("git/source"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(payload.as_path()).is_ok());
    auto catalog = directory.join(PathBuf::from(lito::source::FETCH_SEED_DOCUMENT_NAME).as_path());
    auto contents =
        "{\n"
        "  \"version\": 1,\n"
        "  \"sources\": [\n"
        "    {\n"
        "      \"identity\": \"lito-fetch-v1\\ngit\\nhttps://example.invalid/source.git\\n"
        "0123456789abcdef0123456789abcdef01234567\",\n"
        "      \"kind\": \"git\",\n"
        "      \"path\": \"git/source\"\n"
        "    }\n"
        "  ]\n"
        "}\n"_str;
    ASSERT_TRUE(rstd::fs::write_atomic(catalog.as_path(), contents.as_bytes()).is_ok());
    auto identity = lito::source::git_fetch_identity(
        "https://example.invalid/source.git"_str, "0123456789abcdef0123456789abcdef01234567"_str);
    auto roots = Vec<PathBuf>::make();
    roots.push(directory.clone());
    auto located = lito::source::locate_fetch_seed(roots, identity);
    ASSERT_TRUE(located.is_ok());
    ASSERT_TRUE(located->is_some());
    EXPECT_EQ((**located).as_path(), payload.as_path());

    auto unsafe = "{\"version\":1,\"sources\":[{\"identity\":\"lito-fetch-v1\\ngit\\n"
                  "https://example.invalid/source.git\\n0123456789abcdef0123456789abcdef01234567\","
                  "\"kind\":\"git\",\"path\":\"../source\"}]}"_str;
    ASSERT_TRUE(rstd::fs::write_atomic(catalog.as_path(), unsafe.as_bytes()).is_ok());
    EXPECT_TRUE(lito::source::load_fetch_seed_catalog(directory.as_path()).is_err());
}

TEST_F(SourceSeed, LegacyCatalogNameRemainsReadable) {
    auto directory = cache_root("legacy-fetch-seed-catalog"_str);
    auto payload   = directory.join(PathBuf::from("git/source"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(payload.as_path()).is_ok());
    auto catalog = directory.join(PathBuf::from("catalog.json"_str).as_path());
    auto contents =
        "{\"version\":1,\"sources\":[{\"identity\":\"lito-fetch-v1\\ngit\\n"
        "https://example.invalid/legacy.git\\n0123456789abcdef0123456789abcdef01234567\","
        "\"kind\":\"git\",\"path\":\"git/source\"}]}"_str;
    ASSERT_TRUE(rstd::fs::write_atomic(catalog.as_path(), contents.as_bytes()).is_ok());

    auto identity = lito::source::git_fetch_identity(
        "https://example.invalid/legacy.git"_str, "0123456789abcdef0123456789abcdef01234567"_str);
    auto roots = Vec<PathBuf>::make();
    roots.push(directory.clone());
    auto located = lito::source::locate_fetch_seed(roots, identity);
    ASSERT_TRUE(located.is_ok());
    ASSERT_TRUE(located->is_some());
    EXPECT_EQ((**located).as_path(), payload.as_path());
}

TEST_F(SourceSeed, OfflineArchiveAcquisitionUsesVerifiedSeedWithoutDownloadTool) {
    auto directory = cache_root("offline-archive-seed"_str);
    auto archives  = directory.join(PathBuf::from("archives"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(archives.as_path()).is_ok());
    auto payload  = archives.join(PathBuf::from("source.archive"_str).as_path());
    auto contents = "seed archive bytes"_str;
    ASSERT_TRUE(rstd::fs::write_atomic(payload.as_path(), contents.as_bytes()).is_ok());

    auto url = lito::parse::FetchUrl::parse("https://example.invalid/source.tar.gz"_str);
    ASSERT_TRUE(url.is_ok());
    auto digest   = lito::crypto::sha256_digest(contents);
    auto identity = lito::source::archive_fetch_identity(url->clone(), digest.clone());
    auto document =
        rstd::format("{{\"version\":1,\"sources\":[{{\"identity\":\"lito-fetch-v1\\narchive\\n"
                     "https://example.invalid/source.tar.gz\\n{}\",\"kind\":\"archive\","
                     "\"path\":\"archives/source.archive\"}}]}}",
                     digest);
    auto catalog = directory.join(PathBuf::from(lito::source::FETCH_SEED_DOCUMENT_NAME).as_path());
    ASSERT_TRUE(rstd::fs::write_atomic(catalog.as_path(), document.as_str().as_bytes()).is_ok());

    auto roots = Vec<PathBuf>::make();
    roots.push(directory.clone());
    auto located = lito::source::locate_fetch_seed(roots, identity);
    ASSERT_TRUE(located.is_ok());
    ASSERT_TRUE(located->is_some());

    auto environment =
        lito::system::ResolvedProcessEnvironment::resolve(lito::system::ProcessEnvironmentSpec {});
    ASSERT_TRUE(environment.is_ok());
    auto resolver = lito::tools::ToolResolver(*environment);
    auto requests = Vec<lito::tools::acquisition::VerifiedArchiveRequest>::make();
    requests.push(lito::tools::acquisition::VerifiedArchiveRequest {
        .label                = String::make("archive seed"_str),
        .url                  = rstd::move(url).unwrap(),
        .sha256               = rstd::move(digest),
        .seed                 = Some(rstd::move(located).unwrap().unwrap()),
        .download_requirement = lito::tools::external_source_tool_requirement(
            lito::tools::HostToolCapability::HttpDownload, "seed-fixture"_str, "archive"_str),
    });
    auto acquired = lito::tools::acquisition::acquire_verified_files(
        rstd::move(requests), usize(1), resolver, *environment, true);
    ASSERT_TRUE(acquired.is_ok());
    ASSERT_EQ(acquired->len(), usize(1));
    EXPECT_EQ((*acquired)[usize {}].path.as_path(), payload.as_path());
}

TEST_F(SourceSeed, OfflineGitResolutionUsesLockedAndExactCommitSeedsWithoutFetch) {
    auto directory = cache_root("offline-git-seed"_str);
    auto checkout  = directory.join(PathBuf::from("git/source"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(checkout.as_path()).is_ok());
    ASSERT_TRUE(git_succeeds(checkout.as_path(), "init"_str));
    ASSERT_TRUE(git_succeeds(checkout.as_path(), "config"_str, "user.name"_str, "Lito Test"_str));
    ASSERT_TRUE(git_succeeds(
        checkout.as_path(), "config"_str, "user.email"_str, "lito@example.invalid"_str));
    auto content = checkout.join(PathBuf::from("source.txt"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(content.as_path(), "seed\n"_str.as_bytes()).is_ok());
    ASSERT_TRUE(git_succeeds(checkout.as_path(), "add"_str, "source.txt"_str));
    ASSERT_TRUE(git_succeeds(checkout.as_path(),
                             "-c"_str,
                             "commit.gpgsign=false"_str,
                             "commit"_str,
                             "-m"_str,
                             "seed"_str));
    auto commit = git_revision(checkout.as_path(), "HEAD"_str);
    ASSERT_TRUE(commit.is_some());
    auto catalog =
        rstd::format("{{\"version\":1,\"sources\":[{{\"identity\":\"lito-fetch-v1\\ngit\\n"
                     "https://example.invalid/seed-only.git\\n{}\",\"kind\":\"git\","
                     "\"path\":\"git/source\"}}]}}",
                     commit->as_str());
    auto catalog_path =
        directory.join(PathBuf::from(lito::source::FETCH_SEED_DOCUMENT_NAME).as_path());
    ASSERT_TRUE(
        rstd::fs::write_atomic(catalog_path.as_path(), catalog.as_str().as_bytes()).is_ok());

    auto environment =
        lito::system::ResolvedProcessEnvironment::resolve(lito::system::ProcessEnvironmentSpec {});
    ASSERT_TRUE(environment.is_ok());
    auto resolver = lito::tools::ToolResolver(*environment);
    auto pins     = Vec<lito::source::GitSourcePin>::make();
    pins.push(lito::source::GitSourcePin {
        .git    = String::make("https://example.invalid/seed-only.git"_str),
        .commit = commit->clone(),
    });
    auto seeds = Vec<PathBuf>::make();
    seeds.push(directory.clone());
    auto events = FetchEventCapture {
        .expected_url = "https://example.invalid/seed-only.git"_str,
    };
    auto manager =
        lito::source::SourceManager(directory.as_path(),
                                    lito::source::SourceResolutionOptions {
                                        .locked      = true,
                                        .git_sources = rstd::move(pins),
                                        .sources =
                                            lito::source::PackageSourceConfig {
                                                .fetch_seeds = rstd::move(seeds),
                                                .network     = lito::source::NetworkPolicy::Offline,
                                            },
                                    },
                                    resolver,
                                    *environment,
                                    lito::source::SourceEventSink {
                                        .context = rstd::addressof(events),
                                        .notify  = capture_source_fetch,
                                    });
    auto acquired =
        manager.acquire_external(lito::source::PackageSourceRequirement::Git(
                                     String::make("https://example.invalid/seed-only.git"_str),
                                     lito::source::GitReference {}),
                                 directory.as_path());
    ASSERT_TRUE(acquired.is_ok());
    EXPECT_EQ(acquired->root.as_path(), checkout.as_path());
    EXPECT_EQ(acquired->identity.as_str(),
              lito::source::git_source_identity("https://example.invalid/seed-only.git"_str,
                                                commit->as_str())
                  .as_str());
    EXPECT_EQ(events.count, usize {});

    auto direct_seeds = Vec<PathBuf>::make();
    direct_seeds.push(directory.clone());
    auto direct_manager =
        lito::source::SourceManager(directory.as_path(),
                                    lito::source::SourceResolutionOptions {
                                        .sources =
                                            lito::source::PackageSourceConfig {
                                                .fetch_seeds = rstd::move(direct_seeds),
                                                .network     = lito::source::NetworkPolicy::Offline,
                                            },
                                    },
                                    resolver,
                                    *environment,
                                    lito::source::SourceEventSink {
                                        .context = rstd::addressof(events),
                                        .notify  = capture_source_fetch,
                                    });
    auto direct = direct_manager.acquire_external(
        lito::source::PackageSourceRequirement::Git(
            String::make("https://example.invalid/seed-only.git"_str),
            lito::source::GitReference {
                .kind  = lito::source::GitReferenceKind::Commit,
                .value = commit->clone(),
            }),
        directory.as_path());
    ASSERT_TRUE(direct.is_ok());
    EXPECT_EQ(direct->root.as_path(), checkout.as_path());
    EXPECT_EQ(events.count, usize {});
}
