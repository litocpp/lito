#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.driver;
import lito.core;
import lito.system;
import lito.toolchain.cmake;
import lito.toolchain;
import lito.test.support;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;
using namespace lito_test;
using PathBuf = rstd::path::PathBuf;

class GitSource : public ProjectFixture {};

TEST_F(GitSource, PackageOwnedExternalKeepsGitProvenanceAndSourceRelativePath) {
    auto directory = source_root("git-package-owned-external"_str);
    auto seed      = directory.join(PathBuf::from("seed"_str).as_path());
    auto upstream  = seed.join(PathBuf::from("git/source"_str).as_path());
    auto package   = upstream.join(PathBuf::from("pkg"_str).as_path());
    auto shaders   = package.join(PathBuf::from("shaders"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(shaders.as_path()).is_ok());
    ASSERT_TRUE(rstd::fs::write_atomic(
                    upstream.join(PathBuf::from("lito.toml"_str).as_path()).as_path(),
                    "[workspace]\nname = \"owned-workspace\"\nmembers = [\"pkg\"]\n"_str.as_bytes())
                    .is_ok());
    ASSERT_TRUE(
        rstd::fs::write_atomic(
            package.join(PathBuf::from("lito.toml"_str).as_path()).as_path(),
            "[package]\n"
            "name = \"owned-fixture\"\n"
            "version = \"0.1.0\"\n"
            "\n"
            "[lib]\n"
            "name = \"owned-fixture\"\n"
            "module = \"owned.fixture\"\n"
            "archive = \"owned.fixture\"\n"
            "sources = [\"source.cppm\"]\n"
            "\n"
            "[external-sources.shaders]\n"
            "path = \"shaders\"\n"
            "\n"
            "[external-dependencies.cmake.shader]\n"
            "package = \"FixtureShader\"\n"
            "source = \"shaders\"\n"
            "targets = [{ name = \"FixtureShader::shader\", visibility = \"private\" }]\n"_str
                .as_bytes())
            .is_ok());
    ASSERT_TRUE(
        rstd::fs::write_atomic(package.join(PathBuf::from("source.cppm"_str).as_path()).as_path(),
                               "export module owned.fixture;\n"_str.as_bytes())
            .is_ok());
    ASSERT_TRUE(rstd::fs::write_atomic(
                    shaders.join(PathBuf::from("CMakeLists.txt"_str).as_path()).as_path(),
                    "cmake_minimum_required(VERSION 3.29)\n"
                    "project(fixture_shader LANGUAGES CXX)\n"_str.as_bytes())
                    .is_ok());
    ASSERT_TRUE(git_succeeds(upstream.as_path(), "init"_str));
    ASSERT_TRUE(git_succeeds(upstream.as_path(), "config"_str, "user.name"_str, "Lito Test"_str));
    ASSERT_TRUE(git_succeeds(
        upstream.as_path(), "config"_str, "user.email"_str, "lito@example.invalid"_str));
    ASSERT_TRUE(git_succeeds(upstream.as_path(), "add"_str, "."_str));
    ASSERT_TRUE(git_succeeds(upstream.as_path(),
                             "-c"_str,
                             "commit.gpgsign=false"_str,
                             "commit"_str,
                             "-m"_str,
                             "upstream"_str));
    auto commit = git_revision(upstream.as_path(), "HEAD"_str);
    auto url    = git_url(upstream.as_path());
    ASSERT_TRUE(commit.is_some());
    ASSERT_TRUE(url.is_some());

    auto project = directory.join(PathBuf::from("project"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(project.as_path()).is_ok());
    auto manifest = rstd::format("[package]\n"
                                 "name = \"owned-consumer\"\n"
                                 "version = \"0.1.0\"\n"
                                 "\n"
                                 "[lib]\n"
                                 "name = \"owned-consumer\"\n"
                                 "module = \"owned.consumer\"\n"
                                 "archive = \"owned.consumer\"\n"
                                 "sources = [\"source.cppm\"]\n"
                                 "\n"
                                 "[dependencies.owned-fixture]\n"
                                 "git = \"{}\"\n"
                                 "commit = \"{}\"\n"
                                 "visibility = \"private\"\n",
                                 *url,
                                 commit->as_str());
    ASSERT_TRUE(
        rstd::fs::write_atomic(project.join(PathBuf::from("lito.toml"_str).as_path()).as_path(),
                               manifest.as_str().as_bytes())
            .is_ok());
    ASSERT_TRUE(
        rstd::fs::write_atomic(project.join(PathBuf::from("source.cppm"_str).as_path()).as_path(),
                               "export module owned.consumer;\n"_str.as_bytes())
            .is_ok());

    auto data_home = directory.join(PathBuf::from("data"_str).as_path());
    auto data_text = data_home.as_path().to_str();
    ASSERT_TRUE(data_text.is_some());
    EnvironmentVariableGuard xdg_data_home("XDG_DATA_HOME"_str, *data_text);
    auto                     fetch_events = FetchEventCapture { .expected_url = *url };
    auto                     updated      = lito::update_dependencies(lito::UpdateRequest {
        .root     = project.clone(),
        .observer = Some(lito::BuildEventSink {
            .context = rstd::addressof(fetch_events),
            .notify  = capture_fetch,
        }),
    });
    ASSERT_TRUE(updated.is_ok());
    EXPECT_EQ(fetch_events.count, usize(1));

    auto locked = lito::lock::load_locked_project(project.as_path());
    ASSERT_TRUE(locked.is_ok());
    const lito::lock::LockedPackage* owner = nullptr;
    for (const auto& package : locked->packages) {
        if (package.name.as_str() == "owned-fixture"_str) owner = rstd::addressof(package);
    }
    ASSERT_NE(owner, nullptr);
    ASSERT_EQ(owner->externals.len(), usize(1));
    const auto& external = owner->externals[usize {}];
    EXPECT_EQ(external.name.as_str(), "shaders"_str);
    ASSERT_TRUE(external.source.is_Package());
    EXPECT_EQ(external.source.as_Package().path.as_path(),
              PathBuf::from("pkg/shaders"_str).as_path());
    auto lock_text =
        rstd::fs::read_to_string(project.join(PathBuf::from("lito.lock"_str).as_path()).as_path());
    ASSERT_TRUE(lock_text.is_ok());
    EXPECT_FALSE(lock_text->as_str().contains("git/checkouts"_str));

    auto cached_session = lito::lock::load_lock_session(project.as_path(), true);
    ASSERT_TRUE(cached_session.is_ok());
    auto cached_environment = lito::system::ResolvedProcessEnvironment::resolve(
        lito::system::ProcessEnvironmentSpec {}, None(), directory.as_path());
    ASSERT_TRUE(cached_environment.is_ok());
    auto cached_tools = lito::system::ToolSpec {};
    cached_tools.git  = PathBuf::from("lito-missing-git"_str);
    auto cached_resolver =
        lito::system::ToolResolver(*cached_environment, rstd::move(cached_tools));
    auto cached_graph = lito::package::resolve_package_graph_with_environment(
        project.as_path(),
        cached_session->take_resolution_options(),
        cached_resolver,
        *cached_environment,
        usize(1));
    ASSERT_TRUE(cached_graph.is_ok());

    auto seed_catalog = rstd::format(
        "{{\"version\":1,\"sources\":[{{\"identity\":\"lito-fetch-v1\\ngit\\n{}\\n{}\","
        "\"kind\":\"git\",\"path\":\"git/source\"}}]}}",
        *url,
        commit->as_str());
    ASSERT_TRUE(
        rstd::fs::write_atomic(seed.join(PathBuf::from("catalog.json"_str).as_path()).as_path(),
                               seed_catalog.as_str().as_bytes())
            .is_ok());
    ASSERT_TRUE(rstd::fs::remove_dir_all(data_home.as_path()).is_ok());
    auto session = lito::lock::load_lock_session(project.as_path(), true);
    ASSERT_TRUE(session.is_ok());
    auto options          = session->take_resolution_options();
    auto external_options = options.clone();
    auto seeds            = Vec<PathBuf>::make();
    seeds.push(seed.clone());
    options.sources = lito::source::PackageSourceConfig {
        .fetch_seeds = rstd::move(seeds),
        .network     = lito::source::NetworkPolicy::Offline,
    };
    external_options.sources = options.sources.clone();
    auto environment =
        lito::system::ResolvedProcessEnvironment::resolve(lito::system::ProcessEnvironmentSpec {});
    ASSERT_TRUE(environment.is_ok());
    auto resolver       = lito::system::ToolResolver(*environment);
    auto offline_events = FetchEventCapture { .expected_url = *url };
    auto graph          = lito::package::resolve_package_graph_with_environment(
        project.as_path(),
        rstd::move(options),
        resolver,
        *environment,
        usize(1),
        lito::source::SourceEventSink {
            .context = rstd::addressof(offline_events),
            .notify  = capture_source_fetch,
        });
    ASSERT_TRUE(graph.is_ok());
    auto prepared = lito::prepare_external_dependency_sources(
        *graph, rstd::move(external_options), resolver, *environment);
    ASSERT_TRUE(prepared.is_ok());
    EXPECT_EQ(offline_events.count, usize {});
    auto found_package = false;
    for (const auto& dependency : prepared->dependencies) {
        if (graph->packages[dependency.package].manifest.name.as_str() != "owned-fixture"_str)
            continue;
        found_package      = true;
        const auto& source = dependency.requirement.source;
        ASSERT_TRUE(source.is_Directory());
        EXPECT_TRUE(source.as_Directory().root.as_path().starts_with(shaders.as_path()));
        EXPECT_TRUE(shaders.as_path().starts_with(source.as_Directory().root.as_path()));
    }
    EXPECT_TRUE(found_package);
}

TEST_F(GitSource, GitPatchManifestChangesConfiguredLock) {
    auto directory = source_root("git-patch-configured-lock"_str);
    auto upstream  = directory.join(PathBuf::from("upstream"_str).as_path());
    auto patch     = directory.join(PathBuf::from("patch"_str).as_path());
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
    auto url = git_url(upstream.as_path());
    ASSERT_TRUE(url.is_some());

    auto changed_manifest = rstd::format(
        "{}\n"
        "[external-sources.changed]\n"
        "archive = \"https://example.invalid/changed.tar.gz\"\n"
        "sha256 = \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"\n"
        "\n"
        "[external-dependencies.cmake.changed]\n"
        "package = \"Changed\"\n"
        "source = \"changed\"\n"
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

    auto patches = Vec<lito::source::GitSourcePatch>::make();
    patches.push(lito::source::GitSourcePatch {
        .git  = String::make(*url),
        .path = patch.clone(),
    });
    auto configured_lock = directory.join(PathBuf::from("configured.lock"_str).as_path());
    auto updated         = lito::update_dependencies(lito::UpdateRequest {
        .root    = project.clone(),
        .lock    = lito::lock::LockConfig { .path = configured_lock.clone() },
        .sources = lito::source::PackageSourceConfig { .patches = rstd::move(patches) },
    });
    ASSERT_TRUE(updated.is_ok());
    EXPECT_EQ(*updated, lito::lock::LockStatus::Updated);

    auto locked = lito::lock::load_locked_project(
        project.as_path(), lito::lock::LockConfig { .path = configured_lock.clone() });
    ASSERT_TRUE(locked.is_ok());
    const lito::lock::LockedPackage* owner = nullptr;
    for (const auto& package : locked->packages) {
        if (package.name.as_str() == "patch-fixture"_str) owner = rstd::addressof(package);
    }
    ASSERT_NE(owner, nullptr);
    ASSERT_EQ(owner->externals.len(), usize(1));
    EXPECT_EQ(owner->externals[usize {}].name.as_str(), "changed"_str);
    ASSERT_TRUE(owner->externals[usize {}].source.is_Archive());
    EXPECT_EQ(owner->externals[usize {}].source.as_Archive().url.as_str(),
              "https://example.invalid/changed.tar.gz"_str);
    auto default_lock = project.join(PathBuf::from("lito.lock"_str).as_path());
    auto exists       = rstd::fs::exists(default_lock.as_path());
    ASSERT_TRUE(exists.is_ok());
    EXPECT_FALSE(*exists);
}

TEST_F(GitSource, BuildResolutionReusesGitSourcePins) {
    constexpr ProjectFile update_files[] = {
        { "lito.lock"_str, R"({
  "packages": [{
    "dependencies": [],
    "externals": [],
    "manifest": "lito.toml",
    "name": "fixture-git-update",
    "runtime-dependencies": [],
    "source": {
      "commit": "0000000000000000000000000000000000000001",
      "kind": "git",
      "reference": { "kind": "branch", "value": "main" },
      "url": "https://example.invalid/repository.git"
    }
  }],
  "version": 2
})"_str },
    };
    auto update_project = materialize("git-update-lock"_str, update_files);
    ASSERT_TRUE(update_project.is_ok());
    auto directory = update_project->root.clone();

    auto building = lito::lock::load_lock_session(directory.as_path(), false);
    ASSERT_TRUE(building.is_ok());
    auto building_options = building->take_resolution_options();
    EXPECT_FALSE(building_options.locked);
    EXPECT_EQ(building_options.git, lito::source::GitResolutionMode::ReuseLocked);
    ASSERT_EQ(building_options.git_sources.len(), usize(1));
    EXPECT_EQ(building_options.git_sources[usize()].commit.as_str(),
              "0000000000000000000000000000000000000001"_str);

    auto updating = lito::lock::load_lock_session(
        directory.as_path(), false, lito::source::GitResolutionMode::Refresh);
    ASSERT_TRUE(updating.is_ok());
    auto updating_options = updating->take_resolution_options();
    EXPECT_FALSE(updating_options.locked);
    EXPECT_EQ(updating_options.git, lito::source::GitResolutionMode::Refresh);
    ASSERT_EQ(updating_options.git_sources.len(), usize(1));

    auto locked = lito::lock::load_lock_session(directory.as_path(), true);
    ASSERT_TRUE(locked.is_ok());
    auto locked_options = locked->take_resolution_options();
    EXPECT_TRUE(locked_options.locked);
    ASSERT_EQ(locked_options.git_sources.len(), usize(1));
    EXPECT_EQ(locked_options.git_sources[usize()].commit.as_str(),
              "0000000000000000000000000000000000000001"_str);

    constexpr ProjectFile pinned_files[] = {
        { "lito.lock"_str, R"({
  "packages": [{
    "dependencies": [],
    "externals": [],
    "manifest": "lito.toml",
    "name": "fixture-git-commit",
    "runtime-dependencies": [],
    "source": {
      "commit": "1111111111111111111111111111111111111111",
      "kind": "git",
      "reference": {
        "kind": "commit",
        "value": "1111111111111111111111111111111111111111"
      },
      "url": "https://example.invalid/repository.git"
    }
  }],
  "version": 2
})"_str },
    };
    auto pinned_project = materialize("git-pinned-lock"_str, pinned_files);
    ASSERT_TRUE(pinned_project.is_ok());
    auto pinned = lito::lock::load_lock_session(pinned_project->root.as_path(), false);
    ASSERT_TRUE(pinned.is_ok());
    auto pinned_options = pinned->take_resolution_options();
    ASSERT_EQ(pinned_options.git_sources.len(), usize(1));
    EXPECT_EQ(pinned_options.git_sources[usize()].reference.kind,
              lito::source::GitReferenceKind::Commit);
    EXPECT_EQ(pinned_options.git_sources[usize()].reference.value.as_str(),
              "1111111111111111111111111111111111111111"_str);
}

TEST_F(GitSource, GitUpdateRefreshesFloatingReferencesButKeepsCommitPins) {
    auto repository = source_root("git-resolution"_str);
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
    auto url = git_url(repository.as_path());
    ASSERT_TRUE(url.is_some());
    auto current = git_revision(repository.as_path(), "HEAD"_str);
    ASSERT_TRUE(current.is_some());
    ASSERT_NE(current->as_str(), previous->as_str());

    auto locked_sources = Vec<lito::source::GitSourcePin>::make();
    locked_sources.push(lito::source::GitSourcePin {
        .git       = String::make(*url),
        .reference = lito::source::GitReference {},
        .commit    = previous->clone(),
    });
    auto reuse_graph =
        external_git_graph(*url, repository.as_path(), lito::source::GitReference {});
    reuse_graph.packages[usize {}].manifest.cmake_external_dependencies.push(
        lito::dependency::CMakeDependencyRequirement {
            .alias   = String::make("fixture-reuse"_str),
            .package = String::make("Fixture"_str),
            .source  = Some(String::make("fixture"_str)),
        });
    auto reused =
        lito::prepare_external_dependency_sources(reuse_graph,
                                                  lito::source::SourceResolutionOptions {
                                                      .git_sources = rstd::move(locked_sources),
                                                  },
                                                  usize(2));
    ASSERT_TRUE(reused.is_ok());
    ASSERT_EQ(reuse_graph.packages[usize {}].externals.len(), usize(1));
    ASSERT_EQ(reused->dependencies.len(), usize(2));
    EXPECT_EQ(reused->dependencies[usize {}].requirement.source.as_Directory().identity,
              reused->dependencies[usize(1)].requirement.source.as_Directory().identity);
    auto reused_commit = resolved_git_commit(reuse_graph);
    ASSERT_TRUE(reused_commit.is_some());
    EXPECT_EQ(*reused_commit, previous->as_str());

    locked_sources.push(lito::source::GitSourcePin {
        .git       = String::make(*url),
        .reference = lito::source::GitReference {},
        .commit    = previous->clone(),
    });
    auto update_graph =
        external_git_graph(*url, repository.as_path(), lito::source::GitReference {});
    auto fetch_events = FetchEventCapture { .expected_url = *url };
    auto updated      = lito::prepare_external_dependency_sources(
        update_graph,
        lito::source::SourceResolutionOptions {
            .git         = lito::source::GitResolutionMode::Refresh,
            .git_sources = rstd::move(locked_sources),
        },
        usize(1),
        lito::BuildEventSink {
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
                                           repository.as_path(),
                                           lito::source::GitReference {
                                               .kind  = lito::source::GitReferenceKind::Commit,
                                               .value = previous->clone(),
                                           });
    auto pinned       = lito::prepare_external_dependency_sources(
        pinned_graph,
        lito::source::SourceResolutionOptions {
            .git = lito::source::GitResolutionMode::Refresh,
        });
    ASSERT_TRUE(pinned.is_ok());
    auto pinned_commit = resolved_git_commit(pinned_graph);
    ASSERT_TRUE(pinned_commit.is_some());
    EXPECT_EQ(*pinned_commit, previous->as_str());
}
