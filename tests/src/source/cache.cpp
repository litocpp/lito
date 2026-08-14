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

TEST(Source, SourceCacheUsesDataHomeAndTagsOnlyCacheCategories) {
    auto directory = output_root("source-cache-layout"_str);
    ASSERT_TRUE(clear_output(directory.as_path()));
    auto data_home = directory.join(PathBuf::from("data"_str).as_path());
    auto data_text = data_home.as_path().to_str();
    ASSERT_TRUE(data_text.is_some());
    EnvironmentVariableGuard xdg_data_home("XDG_DATA_HOME"_str, *data_text);

    auto root = lito::LitoDataRoot::resolve();
    ASSERT_TRUE(root.is_ok());
    auto expected = data_home.join(PathBuf::from("lito"_str).as_path());
    EXPECT_EQ(root->root(), expected.as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(expected.as_path()).is_ok());
    auto lock = expected.join(PathBuf::from(".source-cache"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(lock.as_path(), "preserve\n"_str.as_bytes()).is_ok());
    {
        auto session = root->acquire_source_cache();
        ASSERT_TRUE(session.is_ok());
        auto parent_tag = expected.join(PathBuf::from("CACHEDIR.TAG"_str).as_path());
        auto files      = expected.join(PathBuf::from("files"_str).as_path());
        ASSERT_TRUE(rstd::fs::exists(lock.as_path()).unwrap());
        EXPECT_EQ(rstd::fs::read_to_string(lock.as_path()).unwrap().as_str(), "preserve\n"_str);
        auto probe = rstd::fs::FileLock::try_acquire(rstd::fs::File::open(lock.as_path()).unwrap(),
                                                     rstd::fs::FileLockMode::Exclusive);
        ASSERT_TRUE(probe.is_ok());
        EXPECT_TRUE(probe->is_none());
        EXPECT_FALSE(rstd::fs::exists(parent_tag.as_path()).unwrap());
        EXPECT_FALSE(rstd::fs::exists(files.as_path()).unwrap());

        auto git = session->open_git_cache();
        ASSERT_TRUE(git.is_ok());
        auto tag = PathBuf::from(git->root()).join(PathBuf::from("CACHEDIR.TAG"_str).as_path());
        auto contents = rstd::fs::read_to_string(tag.as_path());
        ASSERT_TRUE(contents.is_ok());
        EXPECT_TRUE(
            contents->as_str().starts_with("Signature: 8a477f597d28d172789f06886806bc55"_str));
        EXPECT_FALSE(rstd::fs::exists(parent_tag.as_path()).unwrap());
        EXPECT_FALSE(rstd::fs::exists(files.as_path()).unwrap());

        ASSERT_TRUE(rstd::fs::remove_file(tag.as_path()).is_ok());
        ASSERT_TRUE(session->open_git_cache().is_ok());
        EXPECT_TRUE(rstd::fs::exists(tag.as_path()).unwrap());
        auto custom = "Signature: 8a477f597d28d172789f06886806bc55\ncustom\n"_str;
        ASSERT_TRUE(rstd::fs::write_atomic(tag.as_path(), custom.as_bytes()).is_ok());
        ASSERT_TRUE(session->open_git_cache().is_ok());
        EXPECT_EQ(rstd::fs::read_to_string(tag.as_path()).unwrap().as_str(), custom);
        ASSERT_TRUE(rstd::fs::write_atomic(tag.as_path(), "invalid\n"_str.as_bytes()).is_ok());
        EXPECT_TRUE(session->open_git_cache().is_err());
        ASSERT_TRUE(rstd::fs::remove_file(tag.as_path()).is_ok());
        ASSERT_TRUE(rstd::fs::create_dir(tag.as_path()).is_ok());
        EXPECT_TRUE(session->open_git_cache().is_err());

        auto file_cache = session->open_file_cache();
        ASSERT_TRUE(file_cache.is_ok());
        auto file_tag =
            PathBuf::from(file_cache->root()).join(PathBuf::from("CACHEDIR.TAG"_str).as_path());
        EXPECT_TRUE(rstd::fs::exists(file_tag.as_path()).unwrap());
    }
    ASSERT_TRUE(rstd::fs::remove_file(lock.as_path()).is_ok());
    ASSERT_TRUE(rstd::fs::create_dir(lock.as_path()).is_ok());
    EXPECT_TRUE(root->acquire_source_cache().is_err());
    EXPECT_TRUE(clear_output(directory.as_path()));
}

TEST(Source, RelativeDataHomeFallsBackToHomeDataDirectory) {
    auto directory = output_root("source-cache-home-fallback"_str);
    ASSERT_TRUE(clear_output(directory.as_path()));
    ASSERT_TRUE(rstd::fs::create_dir_all(directory.as_path()).is_ok());
    auto home_text = directory.as_path().to_str();
    ASSERT_TRUE(home_text.is_some());
    EnvironmentVariableGuard home("HOME"_str, *home_text);
    EnvironmentVariableGuard xdg_data_home("XDG_DATA_HOME"_str, "relative-data"_str);

    auto root = lito::LitoDataRoot::resolve();
    ASSERT_TRUE(root.is_ok());
    auto expected = directory.join(PathBuf::from(".local/share/lito"_str).as_path());
    EXPECT_EQ(root->root(), expected.as_path());
    EXPECT_TRUE(clear_output(directory.as_path()));
}

TEST(Source, ArchiveDownloadCacheIsGlobalAndExtractionIsProfileLocal) {
    auto directory = output_root("archive-profile-materialization"_str);
    ASSERT_TRUE(clear_output(directory.as_path()));
    auto package = directory.join(PathBuf::from("input/package"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(package.as_path()).is_ok());
    auto payload = package.join(PathBuf::from("payload.txt"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(payload.as_path(), "archive fixture\n"_str.as_bytes()).is_ok());

    auto environment = lito::ResolvedProcessEnvironment::resolve(lito::ProcessEnvironmentSpec {});
    ASSERT_TRUE(environment.is_ok());
    auto resolver = lito::ToolResolver(*environment);
    auto cmake    = resolver.resolve(PathBuf::from("cmake"_str).as_path(), "CMake executable"_str);
    ASSERT_TRUE(cmake.is_ok());
    auto archive = directory.join(PathBuf::from("fixture.tar"_str).as_path());
    auto command = rstd::process::Command::make(cmake->executable.as_path().as_os_str());
    command.arg("-E"_str)
        .arg("tar"_str)
        .arg("cf"_str)
        .arg(archive.as_path().as_os_str())
        .arg("package"_str)
        .current_dir(directory.join(PathBuf::from("input"_str).as_path()).as_path());
    auto status = command.status();
    ASSERT_TRUE(status.is_ok());
    ASSERT_TRUE(status->success());
    auto archive_bytes = rstd::fs::read(archive.as_path());
    ASSERT_TRUE(archive_bytes.is_ok());
    auto digest = rstd::crypto::sha256_hex(archive_bytes->as_slice());
    auto url    = rstd::format("file://{}", archive.as_path());

    auto data_home = directory.join(PathBuf::from("data"_str).as_path());
    auto data_text = data_home.as_path().to_str();
    ASSERT_TRUE(data_text.is_some());
    EnvironmentVariableGuard xdg_data_home("XDG_DATA_HOME"_str, *data_text);
    auto debug_output   = directory.join(PathBuf::from("build/debug"_str).as_path());
    auto release_output = directory.join(PathBuf::from("build/release"_str).as_path());
    auto debug_layout =
        lito::BuildLayout::create(directory.as_path(), debug_output.as_path(), "debug"_str);
    auto release_layout =
        lito::BuildLayout::create(directory.as_path(), release_output.as_path(), "release"_str);
    ASSERT_TRUE(debug_layout.is_ok());
    ASSERT_TRUE(release_layout.is_ok());

    auto requests = Vec<lito::ArchiveSourceFetchRequest>::make();
    requests.push(lito::ArchiveSourceFetchRequest {
        .url    = url.clone(),
        .sha256 = digest.clone(),
    });
    auto first_events = FileFetchEventCapture {};
    auto first        = lito::acquire_archive_frontier(rstd::move(requests),
                                                       usize(1),
                                                       *debug_layout,
                                                       cmake->executable.as_path(),
                                                       *environment,
                                                       {},
                                                       lito::BuildObserver {
                                                           .context = rstd::addressof(first_events),
                                                           .notify  = capture_file_fetch,
                                                       });
    ASSERT_TRUE(first.is_ok());
    ASSERT_EQ(first->len(), usize(1));
    EXPECT_EQ(first_events.count, usize(1));
    EXPECT_TRUE((*first)[usize {}].root.as_path().starts_with(debug_layout->output()));

    requests.push(lito::ArchiveSourceFetchRequest {
        .url    = url.clone(),
        .sha256 = digest.clone(),
    });
    auto second_events     = FileFetchEventCapture {};
    auto cache_environment = lito::ResolvedProcessEnvironment::resolve(
        lito::ProcessEnvironmentSpec {}, None(), directory.as_path());
    ASSERT_TRUE(cache_environment.is_ok());
    auto second = lito::acquire_archive_frontier(rstd::move(requests),
                                                 usize(1),
                                                 *release_layout,
                                                 cmake->executable.as_path(),
                                                 *cache_environment,
                                                 {},
                                                 lito::BuildObserver {
                                                     .context = rstd::addressof(second_events),
                                                     .notify  = capture_file_fetch,
                                                 });
    ASSERT_TRUE(second.is_ok());
    ASSERT_EQ(second->len(), usize(1));
    EXPECT_EQ(second_events.count, usize {});
    EXPECT_TRUE((*second)[usize {}].root.as_path().starts_with(release_layout->output()));
    EXPECT_NE((*first)[usize {}].root.as_path(), (*second)[usize {}].root.as_path());
    EXPECT_TRUE(debug_layout->scan_cache_directory().as_path().starts_with(debug_layout->output()));
    EXPECT_TRUE(
        release_layout->scan_cache_directory().as_path().starts_with(release_layout->output()));

    auto fetch_identity = lito::archive_fetch_identity(url.as_str(), digest.as_str());
    auto file_key       = lito::fetch_identity_stable_key(fetch_identity);
    auto file_bucket    = data_home.join(PathBuf::from("lito/files"_str).as_path())
                              .join(PathBuf::from(file_key).as_path());
    EXPECT_TRUE(rstd::fs::exists(file_bucket.join(PathBuf::from("source"_str).as_path()).as_path())
                    .unwrap());
    EXPECT_FALSE(
        rstd::fs::exists(file_bucket.join(PathBuf::from("extracted"_str).as_path()).as_path())
            .unwrap());
    auto opened_bucket = rstd::fs::read_dir(file_bucket.as_path());
    ASSERT_TRUE(opened_bucket.is_ok());
    auto bucket_entries = rstd::move(opened_bucket).unwrap();
    auto entry_count    = usize {};
    for (auto entry = bucket_entries.next(); entry.is_some(); entry = bucket_entries.next()) {
        ASSERT_TRUE(entry->is_ok());
        EXPECT_EQ(entry->as_ref().unwrap().file_name().as_os_str().to_string_lossy().as_str(),
                  "source"_str);
        ++entry_count;
    }
    EXPECT_EQ(entry_count, usize(1));
    auto archive_identity = lito::archive_source_identity(url.as_str(), digest.as_str());
    auto debug_receipt    = debug_layout->archive_materialization(archive_identity.as_str())
                                .join(PathBuf::from("source-v2"_str).as_path());
    auto receipt          = rstd::fs::read_to_string(debug_receipt.as_path());
    ASSERT_TRUE(receipt.is_ok());
    EXPECT_TRUE(receipt->as_str().starts_with("lito-archive-materialization-v2\n"_str));
    EXPECT_FALSE(
        rstd::fs::exists(debug_output.join(PathBuf::from("CACHEDIR.TAG"_str).as_path()).as_path())
            .unwrap());
    EXPECT_TRUE(clear_output(directory.as_path()));
}
