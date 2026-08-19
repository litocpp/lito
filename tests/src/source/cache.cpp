#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.core;
import lito.system;
import lito.toolchain.cmake;
import lito.toolchain;
import lito.driver;
import lito.test.support;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;
using namespace lito_test;
using PathBuf = rstd::path::PathBuf;

class Source : public ProjectFixture {};

struct SourceToolCapture {
    Vec<lito::system::HostToolCapability> capabilities;
    Vec<String>                           providers;
    usize                                 candidate_missing {};
    usize                                 not_required {};
};

void capture_source_tool(void*                                   raw_context,
                         const lito::system::HostToolResolution& resolution) noexcept {
    auto& capture = *static_cast<SourceToolCapture*>(raw_context);
    if (resolution.kind == lito::system::HostToolResolution::Kind::CandidateMissing) {
        ++capture.candidate_missing;
        return;
    }
    if (resolution.kind == lito::system::HostToolResolution::Kind::NotRequired) {
        ++capture.not_required;
        return;
    }
    capture.capabilities.push(lito::system::HostToolCapability(resolution.requirement.capability));
    capture.providers.push(resolution.provider.clone());
}

TEST_F(Source, SourceCacheUsesDataHomeAndTagsOnlyCacheCategories) {
    auto directory = cache_root("source-cache-layout"_str);
    auto data_home = directory.join(PathBuf::from("data"_str).as_path());
    auto data_text = data_home.as_path().to_str();
    ASSERT_TRUE(data_text.is_some());
    EnvironmentVariableGuard xdg_data_home("XDG_DATA_HOME"_str, *data_text);

    auto root = lito::system::LitoDataRoot::resolve();
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
    EXPECT_EQ(rstd::fs::read_to_string(lock.as_path()).unwrap().as_str(), "preserve\n"_str);
    ASSERT_TRUE(rstd::fs::remove_file(lock.as_path()).is_ok());
    ASSERT_TRUE(rstd::fs::create_dir(lock.as_path()).is_ok());
    EXPECT_TRUE(root->acquire_source_cache().is_err());
}

TEST_F(Source, RelativeDataHomeFallsBackToPlatformDataDirectory) {
    auto directory = cache_root("source-cache-platform-fallback"_str);
    ASSERT_TRUE(rstd::fs::create_dir_all(directory.as_path()).is_ok());
    auto directory_text = directory.as_path().to_str();
    ASSERT_TRUE(directory_text.is_some());
    EnvironmentVariableGuard xdg_data_home("XDG_DATA_HOME"_str, "relative-data"_str);
#if defined(_WIN32)
    EnvironmentVariableGuard home("HOME"_str);
    EnvironmentVariableGuard local_app_data("LOCALAPPDATA"_str, *directory_text);
#else
    EnvironmentVariableGuard home("HOME"_str, *directory_text);
#endif

    auto root = lito::system::LitoDataRoot::resolve();
    ASSERT_TRUE(root.is_ok());
#if defined(_WIN32)
    auto expected = directory.join(PathBuf::from("lito"_str).as_path());
#else
    auto expected = directory.join(PathBuf::from(".local/share/lito"_str).as_path());
#endif
    EXPECT_EQ(root->root(), expected.as_path());
}

TEST_F(Source, ArchiveDownloadCacheIsGlobalAndExtractionIsProfileLocal) {
    auto directory = cache_root("archive profile materialization"_str);
    auto package   = directory.join(PathBuf::from("input/package"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(package.as_path()).is_ok());
    auto payload = package.join(PathBuf::from("payload.txt"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(payload.as_path(), "archive fixture\n"_str.as_bytes()).is_ok());

    auto environment =
        lito::system::ResolvedProcessEnvironment::resolve(lito::system::ProcessEnvironmentSpec {});
    ASSERT_TRUE(environment.is_ok());
    auto resolver = lito::system::ToolResolver(*environment);
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
    auto url    = String::make("file://"_str);
    for (const auto byte : archive.as_path().as_os_str().as_encoded_bytes()) {
        if (byte == u8(' ')) {
            url.push_str("%20"_str);
        } else {
            url.push_ascii(byte);
        }
    }

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

    auto requests = Vec<lito::source::ArchiveSourceFetchRequest>::make();
    requests.push(lito::source::ArchiveSourceFetchRequest {
        .url    = url.clone(),
        .sha256 = digest.clone(),
    });
    auto first_events          = FileSourceEventCapture {};
    auto debug_materialization = debug_layout->source_materialization_root();
    auto first =
        lito::source::acquire_archive_frontier(rstd::move(requests),
                                               usize(1),
                                               debug_materialization.as_path(),
                                               resolver,
                                               *environment,
                                               {},
                                               lito::source::SourceEventSink {
                                                   .context = rstd::addressof(first_events),
                                                   .notify  = capture_file_source_event,
                                               });
    if (first.is_err()) {
        auto message = rstd::format("{}", rstd::move(first).unwrap_err());
        rstd::test::fail_current(message.as_str(), __FILE__, __LINE__, true);
        return;
    }
    ASSERT_TRUE(first.is_ok());
    ASSERT_EQ(first->len(), usize(1));
    EXPECT_EQ(first_events.fetch, usize(1));
    EXPECT_EQ(first_events.extract, usize(1));
    EXPECT_TRUE((*first)[usize {}].root.as_path().starts_with(debug_layout->output()));

    requests.push(lito::source::ArchiveSourceFetchRequest {
        .url    = url.clone(),
        .sha256 = digest.clone(),
    });
    auto second_events     = FileSourceEventCapture {};
    auto cache_environment = lito::system::ResolvedProcessEnvironment::resolve(
        lito::system::ProcessEnvironmentSpec {}, None(), directory.as_path());
    ASSERT_TRUE(cache_environment.is_ok());
    auto cache_tools        = lito::system::ToolSpec {};
    cache_tools.curl        = PathBuf::from("lito-missing-curl"_str);
    auto cache_tool_capture = SourceToolCapture {};
    auto cache_resolver =
        lito::system::ToolResolver(*environment,
                                   rstd::move(cache_tools),
                                   Some(lito::system::HostToolResolutionSink {
                                       .context = rstd::addressof(cache_tool_capture),
                                       .notify  = capture_source_tool,
                                   }));
    auto release_materialization = release_layout->source_materialization_root();
    auto second =
        lito::source::acquire_archive_frontier(rstd::move(requests),
                                               usize(1),
                                               release_materialization.as_path(),
                                               cache_resolver,
                                               *cache_environment,
                                               {},
                                               lito::source::SourceEventSink {
                                                   .context = rstd::addressof(second_events),
                                                   .notify  = capture_file_source_event,
                                               });
    ASSERT_TRUE(second.is_ok());
    ASSERT_EQ(second->len(), usize(1));
    EXPECT_EQ(second_events.fetch, usize {});
    EXPECT_EQ(second_events.extract, usize(1));
    ASSERT_EQ(cache_tool_capture.capabilities.len(), usize(1));
    EXPECT_EQ(cache_tool_capture.capabilities[usize {}],
              lito::system::HostToolCapability::ArchiveExtraction);
    EXPECT_EQ(cache_tool_capture.not_required, usize(1));
    EXPECT_TRUE((*second)[usize {}].root.as_path().starts_with(release_layout->output()));
    EXPECT_NE((*first)[usize {}].root.as_path(), (*second)[usize {}].root.as_path());
    EXPECT_TRUE(debug_layout->scan_cache_directory().as_path().starts_with(debug_layout->output()));
    EXPECT_TRUE(
        release_layout->scan_cache_directory().as_path().starts_with(release_layout->output()));

    auto fetch_identity = lito::source::archive_fetch_identity(url.as_str(), digest.as_str());
    auto file_key       = lito::source::fetch_identity_stable_key(fetch_identity);
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
    auto archive_identity = lito::source::archive_source_identity(url.as_str(), digest.as_str());
    auto debug_receipt    = debug_layout->archive_materialization(archive_identity.as_str())
                                .join(PathBuf::from("source-v2"_str).as_path());
    auto receipt          = rstd::fs::read_to_string(debug_receipt.as_path());
    ASSERT_TRUE(receipt.is_ok());
    EXPECT_TRUE(receipt->as_str().starts_with("lito-archive-materialization-v2\n"_str));

    requests.push(lito::source::ArchiveSourceFetchRequest {
        .url    = url.clone(),
        .sha256 = digest.clone(),
    });
    auto reuse_capture       = SourceToolCapture {};
    auto missing_tools       = lito::system::ToolSpec {};
    missing_tools.curl       = PathBuf::from("lito-missing-curl"_str);
    missing_tools.tar        = PathBuf::from("lito-missing-tar"_str);
    missing_tools.bsdtar     = PathBuf::from("lito-missing-bsdtar"_str);
    missing_tools.cmake      = PathBuf::from("lito-missing-cmake"_str);
    auto no_path_environment = lito::system::ResolvedProcessEnvironment::resolve(
        lito::system::ProcessEnvironmentSpec {}, None(), directory.as_path());
    ASSERT_TRUE(no_path_environment.is_ok());
    auto reuse_resolver = lito::system::ToolResolver(*no_path_environment,
                                                     rstd::move(missing_tools),
                                                     Some(lito::system::HostToolResolutionSink {
                                                         .context = rstd::addressof(reuse_capture),
                                                         .notify  = capture_source_tool,
                                                     }));
    auto reused         = lito::source::acquire_archive_frontier(rstd::move(requests),
                                                                 usize(1),
                                                                 release_materialization.as_path(),
                                                                 reuse_resolver,
                                                                 *no_path_environment);
    ASSERT_TRUE(reused.is_ok());
    EXPECT_TRUE(reuse_capture.capabilities.is_empty());
    EXPECT_EQ(reuse_capture.not_required, usize(2));

    auto fallback_output = directory.join(PathBuf::from("build/fallback"_str).as_path());
    auto fallback_layout =
        lito::BuildLayout::create(directory.as_path(), fallback_output.as_path(), "fallback"_str);
    ASSERT_TRUE(fallback_layout.is_ok());
    requests.push(lito::source::ArchiveSourceFetchRequest {
        .owner  = String::make("fixture"_str),
        .name   = String::make("archive"_str),
        .url    = url.clone(),
        .sha256 = digest.clone(),
    });
    auto fallback_capture = SourceToolCapture {};
    auto fallback_tools   = lito::system::ToolSpec {};
    fallback_tools.curl   = PathBuf::from("lito-missing-curl"_str);
    fallback_tools.tar    = PathBuf::from("lito-missing-tar"_str);
    fallback_tools.bsdtar = PathBuf::from("lito-missing-bsdtar"_str);
    fallback_tools.cmake  = cmake->executable.clone();
    auto fallback_resolver =
        lito::system::ToolResolver(*no_path_environment,
                                   rstd::move(fallback_tools),
                                   Some(lito::system::HostToolResolutionSink {
                                       .context = rstd::addressof(fallback_capture),
                                       .notify  = capture_source_tool,
                                   }));
    auto fallback_materialization = fallback_layout->source_materialization_root();
    auto fallback = lito::source::acquire_archive_frontier(rstd::move(requests),
                                                           usize(1),
                                                           fallback_materialization.as_path(),
                                                           fallback_resolver,
                                                           *no_path_environment);
    ASSERT_TRUE(fallback.is_ok());
    ASSERT_EQ(fallback_capture.capabilities.len(), usize(1));
    EXPECT_EQ(fallback_capture.capabilities[usize {}],
              lito::system::HostToolCapability::ArchiveExtraction);
    EXPECT_EQ(fallback_capture.providers[usize {}].as_str(), "cmake -E tar"_str);
    EXPECT_EQ(fallback_capture.candidate_missing, usize(2));
    EXPECT_EQ(fallback_capture.not_required, usize(1));

    auto tar = resolver.resolve(PathBuf::from("tar"_str).as_path(), "tar executable"_str);
    ASSERT_TRUE(tar.is_ok());
    auto explicit_tar_output = directory.join(PathBuf::from("build/explicit-tar"_str).as_path());
    auto explicit_tar_layout = lito::BuildLayout::create(
        directory.as_path(), explicit_tar_output.as_path(), "explicit-tar"_str);
    ASSERT_TRUE(explicit_tar_layout.is_ok());
    requests.push(lito::source::ArchiveSourceFetchRequest {
        .owner  = String::make("fixture"_str),
        .name   = String::make("archive"_str),
        .url    = url.clone(),
        .sha256 = digest.clone(),
    });
    auto explicit_tar_capture = SourceToolCapture {};
    auto explicit_tar_tools   = lito::system::ToolSpec {};
    explicit_tar_tools.tar    = tar->executable.clone();
    explicit_tar_tools.mark_configured(lito::system::Tool::Tar);
    auto explicit_tar_resolver =
        lito::system::ToolResolver(*environment,
                                   rstd::move(explicit_tar_tools),
                                   Some(lito::system::HostToolResolutionSink {
                                       .context = rstd::addressof(explicit_tar_capture),
                                       .notify  = capture_source_tool,
                                   }));
    auto explicit_tar_materialization = explicit_tar_layout->source_materialization_root();
    auto explicit_tar =
        lito::source::acquire_archive_frontier(rstd::move(requests),
                                               usize(1),
                                               explicit_tar_materialization.as_path(),
                                               explicit_tar_resolver,
                                               *environment);
    ASSERT_TRUE(explicit_tar.is_ok());
    ASSERT_EQ(explicit_tar_capture.providers.len(), usize(1));
    EXPECT_EQ(explicit_tar_capture.providers[usize {}].as_str(), "tar"_str);

    auto missing_output = directory.join(PathBuf::from("build/missing"_str).as_path());
    auto missing_layout =
        lito::BuildLayout::create(directory.as_path(), missing_output.as_path(), "missing"_str);
    ASSERT_TRUE(missing_layout.is_ok());
    requests.push(lito::source::ArchiveSourceFetchRequest {
        .owner  = String::make("fixture"_str),
        .name   = String::make("archive"_str),
        .url    = rstd::move(url),
        .sha256 = rstd::move(digest),
    });
    auto unavailable_tools   = lito::system::ToolSpec {};
    unavailable_tools.curl   = PathBuf::from("lito-missing-curl"_str);
    unavailable_tools.tar    = PathBuf::from("lito-missing-tar"_str);
    unavailable_tools.bsdtar = PathBuf::from("lito-missing-bsdtar"_str);
    unavailable_tools.cmake  = PathBuf::from("lito-missing-cmake"_str);
    auto unavailable_resolver =
        lito::system::ToolResolver(*no_path_environment, rstd::move(unavailable_tools));
    auto missing_materialization = missing_layout->source_materialization_root();
    auto unavailable = lito::source::acquire_archive_frontier(rstd::move(requests),
                                                              usize(1),
                                                              missing_materialization.as_path(),
                                                              unavailable_resolver,
                                                              *no_path_environment);
    ASSERT_TRUE(unavailable.is_err());
    auto unavailable_message = rstd::format("{}", rstd::move(unavailable).unwrap_err());
    EXPECT_TRUE(unavailable_message.as_str().contains("fixture:archive"_str));
    EXPECT_TRUE(unavailable_message.as_str().contains("lito-missing-bsdtar"_str));
    EXPECT_TRUE(unavailable_message.as_str().contains("lito-missing-tar"_str));
    EXPECT_TRUE(unavailable_message.as_str().contains("lito-missing-cmake"_str));

    requests.push(lito::source::ArchiveSourceFetchRequest {
        .owner = String::make("fixture"_str),
        .name  = String::make("offline"_str),
        .url   = String::make("https://example.invalid/offline.tar"_str),
        .sha256 =
            String::make("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"_str),
    });
    auto offline_tools = lito::system::ToolSpec {};
    offline_tools.curl = PathBuf::from("lito-missing-curl"_str);
    auto offline_resolver =
        lito::system::ToolResolver(*no_path_environment, rstd::move(offline_tools));
    auto offline_output = directory.join(PathBuf::from("build/offline"_str).as_path());
    auto offline =
        lito::source::acquire_archive_frontier(rstd::move(requests),
                                               usize(1),
                                               offline_output.as_path(),
                                               offline_resolver,
                                               *no_path_environment,
                                               lito::source::PackageSourceConfig {
                                                   .network = lito::source::NetworkPolicy::Offline,
                                               });
    ASSERT_TRUE(offline.is_err());
    auto offline_message = rstd::format("{}", rstd::move(offline).unwrap_err());
    EXPECT_TRUE(offline_message.as_str().contains("offline source resolution"_str));
    EXPECT_FALSE(offline_message.as_str().contains("lito-missing-curl"_str));
    EXPECT_FALSE(
        rstd::fs::exists(debug_output.join(PathBuf::from("CACHEDIR.TAG"_str).as_path()).as_path())
            .unwrap());
}
