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

TEST(GitSource, GitPatchManifestChangesConfiguredLock) {
    auto directory = output_root("git-patch-configured-lock"_str);
    ASSERT_TRUE(clear_output(directory.as_path()));
    auto upstream = directory.join(PathBuf::from("upstream"_str).as_path());
    auto patch    = directory.join(PathBuf::from("patch"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(upstream.as_path()).is_ok());
    ASSERT_TRUE(rstd::fs::create_dir_all(patch.as_path()).is_ok());
    auto manifest          = "[package]\n"
                             "name = \"patch-fixture\"\n"
                             "version = \"0.1.0\"\n"
                             "\n"
                             "[lib]\n"
                             "name = \"patch-fixture\"\n"
                             "module = \"patch.fixture\"\n"
                             "archive = \"patch.fixture\"\n"
                             "sources = [\"source.cppm\"]\n"_str;
    auto upstream_manifest = upstream.join(PathBuf::from("lito.toml"_str).as_path());
    auto patch_manifest    = patch.join(PathBuf::from("lito.toml"_str).as_path());
    ASSERT_TRUE(rstd::fs::write_atomic(upstream_manifest.as_path(), manifest.as_bytes()).is_ok());
    ASSERT_TRUE(rstd::fs::write_atomic(patch_manifest.as_path(), manifest.as_bytes()).is_ok());
    auto upstream_source = upstream.join(PathBuf::from("source.cppm"_str).as_path());
    auto patch_source    = patch.join(PathBuf::from("source.cppm"_str).as_path());
    ASSERT_TRUE(rstd::fs::write_atomic(upstream_source.as_path(),
                                       "export module patch.fixture;\n"_str.as_bytes())
                    .is_ok());
    ASSERT_TRUE(rstd::fs::write_atomic(patch_source.as_path(),
                                       "export module patch.fixture;\n"_str.as_bytes())
                    .is_ok());
    ASSERT_TRUE(git_succeeds(upstream.as_path(), "init"_str));
    ASSERT_TRUE(git_succeeds(upstream.as_path(), "config"_str, "user.name"_str, "Lito Test"_str));
    ASSERT_TRUE(git_succeeds(
        upstream.as_path(), "config"_str, "user.email"_str, "lito@example.invalid"_str));
    ASSERT_TRUE(git_succeeds(upstream.as_path(), "add"_str, "lito.toml"_str, "source.cppm"_str));
    ASSERT_TRUE(git_succeeds(upstream.as_path(),
                             "-c"_str,
                             "commit.gpgsign=false"_str,
                             "commit"_str,
                             "-m"_str,
                             "upstream"_str));
    auto commit = git_revision(upstream.as_path(), "HEAD"_str);
    ASSERT_TRUE(commit.is_some());
    auto url = upstream.as_path().to_str();
    ASSERT_TRUE(url.is_some());

    auto changed_manifest = rstd::format(
        "{}\n"
        "[external-dependencies.cmake.changed]\n"
        "find-package = \"Changed\"\n"
        "archive = \"https://example.invalid/changed.tar.gz\"\n"
        "sha256 = \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"\n"
        "integration = \"build-tree\"\n"
        "targets = [{{ name = \"Changed::changed\", visibility = \"private\" }}]\n",
        manifest);
    ASSERT_TRUE(
        rstd::fs::write_atomic(patch_manifest.as_path(), changed_manifest.as_str().as_bytes())
            .is_ok());

    auto project = directory.join(PathBuf::from("project"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(project.as_path()).is_ok());
    auto project_manifest = rstd::format("[package]\n"
                                         "name = \"patch-consumer\"\n"
                                         "version = \"0.1.0\"\n"
                                         "\n"
                                         "[lib]\n"
                                         "name = \"patch-consumer\"\n"
                                         "module = \"patch.consumer\"\n"
                                         "archive = \"patch.consumer\"\n"
                                         "sources = [\"source.cppm\"]\n"
                                         "\n"
                                         "[dependencies.patch-fixture]\n"
                                         "git = \"{}\"\n"
                                         "commit = \"{}\"\n"
                                         "visibility = \"private\"\n",
                                         *url,
                                         commit->as_str());
    ASSERT_TRUE(
        rstd::fs::write_atomic(project.join(PathBuf::from("lito.toml"_str).as_path()).as_path(),
                               project_manifest.as_str().as_bytes())
            .is_ok());
    ASSERT_TRUE(
        rstd::fs::write_atomic(project.join(PathBuf::from("source.cppm"_str).as_path()).as_path(),
                               "export module patch.consumer;\n"_str.as_bytes())
            .is_ok());

    auto patches = Vec<lito::GitSourcePatch>::make();
    patches.push(lito::GitSourcePatch {
        .git  = String::make(*url),
        .path = patch.clone(),
    });
    auto configured_lock = directory.join(PathBuf::from("configured.lock"_str).as_path());
    auto updated         = lito::update_dependencies(lito::UpdateRequest {
        .root    = project.clone(),
        .lock    = lito::LockConfig { .path = configured_lock.clone() },
        .sources = lito::PackageSourceConfig { .patches = rstd::move(patches) },
    });
    ASSERT_TRUE(updated.is_ok());
    EXPECT_EQ(*updated, lito::LockStatus::Updated);

    auto locked = lito::load_locked_project(project.as_path(),
                                            lito::LockConfig { .path = configured_lock.clone() });
    ASSERT_TRUE(locked.is_ok());
    ASSERT_EQ(locked->externals.len(), usize(1));
    EXPECT_EQ(locked->externals[usize {}].package.as_str(), "patch-fixture"_str);
    EXPECT_EQ(locked->externals[usize {}].alias.as_str(), "changed"_str);
    ASSERT_TRUE(locked->externals[usize {}].source.is_Archive());
    EXPECT_EQ(locked->externals[usize {}].source.as_Archive().url.as_str(),
              "https://example.invalid/changed.tar.gz"_str);
    auto default_lock = project.join(PathBuf::from("lito.lock"_str).as_path());
    auto exists       = rstd::fs::exists(default_lock.as_path());
    ASSERT_TRUE(exists.is_ok());
    EXPECT_FALSE(*exists);
    EXPECT_TRUE(clear_output(directory.as_path()));
}

TEST(GitSource, BuildResolutionReusesLockedGitSources) {
    auto directory = fixture_path("lock/git-update"_str);

    auto building = lito::load_lock_session(directory.as_path(), false);
    ASSERT_TRUE(building.is_ok());
    auto building_options = building->take_resolution_options();
    EXPECT_FALSE(building_options.locked);
    EXPECT_EQ(building_options.git, lito::GitResolutionMode::ReuseLocked);
    ASSERT_EQ(building_options.git_sources.len(), usize(1));
    EXPECT_EQ(building_options.git_sources[usize()].commit.as_str(),
              "0000000000000000000000000000000000000001"_str);

    auto updating =
        lito::load_lock_session(directory.as_path(), false, lito::GitResolutionMode::Refresh);
    ASSERT_TRUE(updating.is_ok());
    auto updating_options = updating->take_resolution_options();
    EXPECT_FALSE(updating_options.locked);
    EXPECT_EQ(updating_options.git, lito::GitResolutionMode::Refresh);
    ASSERT_EQ(updating_options.git_sources.len(), usize(1));

    auto locked = lito::load_lock_session(directory.as_path(), true);
    ASSERT_TRUE(locked.is_ok());
    auto locked_options = locked->take_resolution_options();
    EXPECT_TRUE(locked_options.locked);
    ASSERT_EQ(locked_options.git_sources.len(), usize(1));
    EXPECT_EQ(locked_options.git_sources[usize()].commit.as_str(),
              "0000000000000000000000000000000000000001"_str);

    auto pinned = lito::load_lock_session(fixture_path("lock/git-commit"_str).as_path(), false);
    ASSERT_TRUE(pinned.is_ok());
    auto pinned_options = pinned->take_resolution_options();
    ASSERT_EQ(pinned_options.git_sources.len(), usize(1));
    EXPECT_EQ(pinned_options.git_sources[usize()].reference.kind, lito::GitReferenceKind::Commit);
    EXPECT_EQ(pinned_options.git_sources[usize()].reference.value.as_str(),
              "1111111111111111111111111111111111111111"_str);
}

TEST(GitSource, GitUpdateRefreshesFloatingReferencesButKeepsCommitPins) {
    auto repository = output_root("git-resolution"_str);
    ASSERT_TRUE(clear_output(repository.as_path()));
    ASSERT_TRUE(rstd::fs::create_dir_all(repository.as_path()).is_ok());
    auto data_home = repository.join(PathBuf::from("data"_str).as_path());
    auto data_text = data_home.as_path().to_str();
    ASSERT_TRUE(data_text.is_some());
    EnvironmentVariableGuard xdg_data_home("XDG_DATA_HOME"_str, *data_text);
    ASSERT_TRUE(git_succeeds(repository.as_path(), "init"_str));
    ASSERT_TRUE(git_succeeds(repository.as_path(), "config"_str, "user.name"_str, "Lito Test"_str));
    ASSERT_TRUE(git_succeeds(
        repository.as_path(), "config"_str, "user.email"_str, "lito@example.invalid"_str));
    auto content = repository.join(rstd::path::PathBuf::from("content.txt"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(content.as_path(), "first\n"_str.as_bytes()).is_ok());
    ASSERT_TRUE(git_succeeds(repository.as_path(), "add"_str, "content.txt"_str));
    ASSERT_TRUE(git_succeeds(repository.as_path(),
                             "-c"_str,
                             "commit.gpgsign=false"_str,
                             "commit"_str,
                             "-m"_str,
                             "first"_str));
    auto previous = git_revision(repository.as_path(), "HEAD"_str);
    ASSERT_TRUE(previous.is_some());
    ASSERT_TRUE(rstd::fs::write(content.as_path(), "second\n"_str.as_bytes()).is_ok());
    ASSERT_TRUE(git_succeeds(repository.as_path(), "add"_str, "content.txt"_str));
    ASSERT_TRUE(git_succeeds(repository.as_path(),
                             "-c"_str,
                             "commit.gpgsign=false"_str,
                             "commit"_str,
                             "-m"_str,
                             "second"_str));
    auto url = repository.as_path().to_str();
    ASSERT_TRUE(url.is_some());
    auto current = git_revision(repository.as_path(), "HEAD"_str);
    ASSERT_TRUE(current.is_some());
    ASSERT_NE(current->as_str(), previous->as_str());

    auto locked_sources = Vec<lito::LockedGitSource>::make();
    locked_sources.push(lito::LockedGitSource {
        .git       = String::make(*url),
        .reference = lito::GitReference {},
        .commit    = previous->clone(),
    });
    auto reuse_graph = external_git_graph(*url, lito::GitReference {});
    reuse_graph.packages[usize {}].manifest.cmake_external_dependencies.push(
        lito::CMakeDependencyRequirement {
            .alias   = String::make("fixture-reuse"_str),
            .package = String::make("Fixture"_str),
            .source  = lito::CMakeDependencySource::Git(String::make(*url), lito::GitReference {}),
        });
    auto reused =
        lito::prepare_external_dependency_sources(reuse_graph,
                                                  lito::PackageResolutionOptions {
                                                      .git_sources = rstd::move(locked_sources),
                                                  },
                                                  usize(2));
    ASSERT_TRUE(reused.is_ok());
    ASSERT_EQ(reuse_graph.sources.len(), usize(1));
    ASSERT_EQ(reuse_graph.packages[usize {}].cmake_external_dependencies.len(), usize(2));
    EXPECT_EQ(reuse_graph.packages[usize {}]
                  .cmake_external_dependencies[usize {}]
                  .source.as_Directory()
                  .identity,
              reuse_graph.packages[usize {}]
                  .cmake_external_dependencies[usize(1)]
                  .source.as_Directory()
                  .identity);
    auto reused_commit = resolved_git_commit(reuse_graph);
    ASSERT_TRUE(reused_commit.is_some());
    EXPECT_EQ(*reused_commit, previous->as_str());

    locked_sources.push(lito::LockedGitSource {
        .git       = String::make(*url),
        .reference = lito::GitReference {},
        .commit    = previous->clone(),
    });
    auto update_graph = external_git_graph(*url, lito::GitReference {});
    auto fetch_events = FetchEventCapture { .expected_url = *url };
    auto updated =
        lito::prepare_external_dependency_sources(update_graph,
                                                  lito::PackageResolutionOptions {
                                                      .git = lito::GitResolutionMode::Refresh,
                                                      .git_sources = rstd::move(locked_sources),
                                                  },
                                                  usize(1),
                                                  lito::BuildObserver {
                                                      .context = rstd::addressof(fetch_events),
                                                      .notify  = capture_fetch,
                                                  });
    if (updated.is_err()) {
        rstd::io::eprintln("Git update failed: {}", error_chain_text(updated.unwrap_err()));
    }
    ASSERT_TRUE(updated.is_ok());
    EXPECT_EQ(fetch_events.count, usize(1));
    EXPECT_TRUE(fetch_events.source_matches);
    EXPECT_TRUE(fetch_events.destination_matches);
    auto updated_commit = resolved_git_commit(update_graph);
    ASSERT_TRUE(updated_commit.is_some());
    EXPECT_EQ(*updated_commit, current->as_str());

    auto database = data_home.join(PathBuf::from("lito/git/db"_str).as_path());
    auto opened   = rstd::fs::read_dir(database.as_path());
    ASSERT_TRUE(opened.is_ok());
    auto entries        = rstd::move(opened).unwrap();
    auto repository_key = String::make();
    for (auto next = entries.next(); next.is_some(); next = entries.next()) {
        ASSERT_TRUE(next->is_ok());
        auto entry = rstd::move(*next).unwrap();
        auto name  = entry.file_name().as_os_str().to_string_lossy();
        ASSERT_TRUE(repository_key.is_empty());
        repository_key = String::make(name.as_str());
    }
    EXPECT_TRUE(repository_key.as_str().contains("git-resolution-"_str));
    auto cached_repository = database.join(PathBuf::from(repository_key.as_str()).as_path());
    EXPECT_TRUE(
        rstd::fs::exists(cached_repository.join(PathBuf::from("HEAD"_str).as_path()).as_path())
            .unwrap());
    auto checkout_root = data_home.join(PathBuf::from("lito/git/checkouts"_str).as_path())
                             .join(PathBuf::from(repository_key.as_str()).as_path());
    EXPECT_TRUE(
        rstd::fs::exists(checkout_root.join(PathBuf::from(previous->as_str()).as_path()).as_path())
            .unwrap());
    EXPECT_TRUE(
        rstd::fs::exists(checkout_root.join(PathBuf::from(current->as_str()).as_path()).as_path())
            .unwrap());

    auto pinned_graph = external_git_graph(*url,
                                           lito::GitReference {
                                               .kind  = lito::GitReferenceKind::Commit,
                                               .value = previous->clone(),
                                           });
    auto pinned =
        lito::prepare_external_dependency_sources(pinned_graph,
                                                  lito::PackageResolutionOptions {
                                                      .git = lito::GitResolutionMode::Refresh,
                                                  });
    ASSERT_TRUE(pinned.is_ok());
    auto pinned_commit = resolved_git_commit(pinned_graph);
    ASSERT_TRUE(pinned_commit.is_some());
    EXPECT_EQ(*pinned_commit, previous->as_str());
    EXPECT_TRUE(clear_output(repository.as_path()));
}
