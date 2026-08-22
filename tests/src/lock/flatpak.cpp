#include <rstd/test/gtest.hpp>

import rstd;
import lito.crypto;
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
    auto x86_architectures = Vec<String>::make();
    x86_architectures.push(String::make("x86_64"_str));
    project.packages[usize {}].externals.push(lito::lock::LockedPackageExternalSource {
        .name          = String::make("shared-x86"_str),
        .architectures = rstd::move(x86_architectures),
        .source        = lito::lock::LockedSource::Git(String::make(git_url), String::make(commit)),
    });
    auto arm_architectures = Vec<String>::make();
    arm_architectures.push(String::make("aarch64"_str));
    project.packages[usize {}].externals.push(lito::lock::LockedPackageExternalSource {
        .name          = String::make("shared-arm"_str),
        .architectures = rstd::move(arm_architectures),
        .source        = lito::lock::LockedSource::Git(String::make(git_url), String::make(commit)),
    });
    project.packages[usize {}].externals.push(lito::lock::LockedPackageExternalSource {
        .name          = String::make("archive"_str),
        .architectures = Vec<String>::make(),
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
    EXPECT_TRUE(first->as_str().contains("\"type\": \"inline\""_str));
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
