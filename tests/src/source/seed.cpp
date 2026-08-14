#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito;
import lito.lock;
import lito.package;
import lito.package.graph_contract;
import lito.workspace.contract;
import lito.workspace.resolver;
import lito.platform;
import lito.dependency;
import lito.dependency.cmake;
import lito.source;
import lito.manifest;
import lito.toolchain;
import lito.build.discovery;
import lito.build.layout;
import lito.system.environment;
import lito.system.process;
import lito.system.storage;
import lito.test.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito_test;
using PathBuf = rstd::path::PathBuf;

TEST(SourceSeed, FetchSeedCatalogOwnsSafeReadOnlyLookup) {
    auto directory = output_root("fetch-seed-catalog"_str);
    ASSERT_TRUE(clear_output(directory.as_path()));
    auto payload = directory.join(PathBuf::from("git/source"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(payload.as_path()).is_ok());
    auto catalog = directory.join(PathBuf::from("catalog.json"_str).as_path());
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
    auto identity = lito::git_fetch_identity("https://example.invalid/source.git"_str,
                                             "0123456789abcdef0123456789abcdef01234567"_str);
    auto roots    = Vec<PathBuf>::make();
    roots.push(directory.clone());
    auto located = lito::locate_fetch_seed(roots, identity);
    ASSERT_TRUE(located.is_ok());
    ASSERT_TRUE(located->is_some());
    EXPECT_EQ((**located).as_path(), payload.as_path());

    auto unsafe = "{\"version\":1,\"sources\":[{\"identity\":\"lito-fetch-v1\\ngit\\n"
                  "https://example.invalid/source.git\\n0123456789abcdef0123456789abcdef01234567\","
                  "\"kind\":\"git\",\"path\":\"../source\"}]}"_str;
    ASSERT_TRUE(rstd::fs::write_atomic(catalog.as_path(), unsafe.as_bytes()).is_ok());
    EXPECT_TRUE(lito::load_fetch_seed_catalog(directory.as_path()).is_err());
    EXPECT_TRUE(clear_output(directory.as_path()));
}

TEST(SourceSeed, OfflineGitResolutionUsesLockedAndExactCommitSeedsWithoutFetch) {
    auto directory = output_root("offline-git-seed"_str);
    ASSERT_TRUE(clear_output(directory.as_path()));
    auto checkout = directory.join(PathBuf::from("git/source"_str).as_path());
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
    auto catalog_path = directory.join(PathBuf::from("catalog.json"_str).as_path());
    ASSERT_TRUE(
        rstd::fs::write_atomic(catalog_path.as_path(), catalog.as_str().as_bytes()).is_ok());

    auto environment = lito::ResolvedProcessEnvironment::resolve(lito::ProcessEnvironmentSpec {});
    ASSERT_TRUE(environment.is_ok());
    auto resolver = lito::ToolResolver(*environment);
    auto pins     = Vec<lito::LockedGitSource>::make();
    pins.push(lito::LockedGitSource {
        .git       = String::make("https://example.invalid/seed-only.git"_str),
        .reference = lito::GitReference {},
        .commit    = commit->clone(),
    });
    auto seeds = Vec<PathBuf>::make();
    seeds.push(directory.clone());
    auto events = FetchEventCapture {
        .expected_url = "https://example.invalid/seed-only.git"_str,
    };
    auto manager  = lito::SourceManager(directory.as_path(),
                                        lito::PackageResolutionOptions {
                                            .locked      = true,
                                            .git_sources = rstd::move(pins),
                                            .sources =
                                                lito::PackageSourceConfig {
                                                    .fetch_seeds = rstd::move(seeds),
                                                    .network     = lito::NetworkPolicy::Offline,
                                                },
                                        },
                                        resolver,
                                        *environment,
                                        lito::BuildObserver {
                                            .context = rstd::addressof(events),
                                            .notify  = capture_fetch,
                                        });
    auto acquired = manager.acquire_external(
        lito::PackageSourceRequirement::Git(
            String::make("https://example.invalid/seed-only.git"_str), lito::GitReference {}),
        directory.as_path());
    ASSERT_TRUE(acquired.is_ok());
    EXPECT_EQ(acquired->root.as_path(), checkout.as_path());
    EXPECT_EQ(
        acquired->identity.as_str(),
        lito::git_source_identity("https://example.invalid/seed-only.git"_str, commit->as_str())
            .as_str());
    EXPECT_EQ(events.count, usize {});

    auto direct_seeds = Vec<PathBuf>::make();
    direct_seeds.push(directory.clone());
    auto direct_manager = lito::SourceManager(directory.as_path(),
                                              lito::PackageResolutionOptions {
                                                  .sources =
                                                      lito::PackageSourceConfig {
                                                          .fetch_seeds = rstd::move(direct_seeds),
                                                          .network = lito::NetworkPolicy::Offline,
                                                      },
                                              },
                                              resolver,
                                              *environment,
                                              lito::BuildObserver {
                                                  .context = rstd::addressof(events),
                                                  .notify  = capture_fetch,
                                              });
    auto direct         = direct_manager.acquire_external(
        lito::PackageSourceRequirement::Git(
            String::make("https://example.invalid/seed-only.git"_str),
            lito::GitReference {
                .kind  = lito::GitReferenceKind::Commit,
                .value = commit->clone(),
            }),
        directory.as_path());
    ASSERT_TRUE(direct.is_ok());
    EXPECT_EQ(direct->root.as_path(), checkout.as_path());
    EXPECT_EQ(events.count, usize {});
    EXPECT_TRUE(clear_output(directory.as_path()));
}
