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

TEST(Lock, FetchIdentityAndFlatpakProjectionAreStableAndDeduplicated) {
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
        .source       = lito::lock::LockedPackageSource::Path(PathBuf::from("."_str)),
        .manifest     = PathBuf::from("lito.toml"_str),
        .dependencies = Vec<String>::make(),
    });
    auto reference         = lito::source::GitReference {};
    auto x86_architectures = Vec<String>::make();
    x86_architectures.push(String::make("x86_64"_str));
    project.externals.push(lito::lock::LockedExternal {
        .package       = String::make("app"_str),
        .alias         = String::make("shared-x86"_str),
        .provider      = String::make("cmake"_str),
        .architectures = rstd::move(x86_architectures),
        .source        = lito::lock::LockedExternalSource::Git(
            String::make(git_url), rstd::move(reference), String::make(commit)),
    });
    auto arm_architectures = Vec<String>::make();
    arm_architectures.push(String::make("aarch64"_str));
    project.externals.push(lito::lock::LockedExternal {
        .package       = String::make("app"_str),
        .alias         = String::make("shared-arm"_str),
        .provider      = String::make("cmake"_str),
        .architectures = rstd::move(arm_architectures),
        .source        = lito::lock::LockedExternalSource::Git(
            String::make(git_url), lito::source::GitReference {}, String::make(commit)),
    });
    project.externals.push(lito::lock::LockedExternal {
        .package       = String::make("app"_str),
        .alias         = String::make("archive"_str),
        .provider      = String::make("cmake"_str),
        .architectures = Vec<String>::make(),
        .source        = lito::lock::LockedExternalSource::Archive(
            String::make("https://example.invalid/archive.tar.gz"_str),
            String::make("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"_str)),
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

TEST(Lock, PackageOwnedExternalReusesItsPackageGitFetch) {
    auto project = lito::lock::LockedProject {};
    project.packages.push(lito::lock::LockedPackage {
        .name    = String::make("wavsen"_str),
        .version = Some(String::make("0.1.0"_str)),
        .source  = lito::lock::LockedPackageSource::Git(
            String::make("https://example.invalid/wavsen.git"_str),
            lito::source::GitReference {},
            String::make("0123456789abcdef0123456789abcdef01234567"_str)),
        .manifest = PathBuf::from("lito.toml"_str),
    });
    project.externals.push(lito::lock::LockedExternal {
        .package       = String::make("wavsen"_str),
        .alias         = String::make("wavsen-shader"_str),
        .provider      = String::make("cmake"_str),
        .architectures = Vec<String>::make(),
        .source        = lito::lock::LockedExternalSource::Package(PathBuf::from("shaders"_str)),
    });

    auto exported = lito::lock::flatpak_sources_json(project);
    ASSERT_TRUE(exported.is_ok());
    auto git_source = exported->as_str().split_once("\"type\": \"git\""_str);
    ASSERT_TRUE(git_source.is_some());
    EXPECT_FALSE(git_source->get<1>().contains("\"type\": \"git\""_str));
    EXPECT_TRUE(exported->as_str().contains("https://example.invalid/wavsen.git"_str));
}
