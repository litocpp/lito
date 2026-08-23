#include <rstd/test/gtest.hpp>

import rstd;
import lito.crypto;
import rstd.json;
import rstd.test;
import lito.core;
import lito.driver;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

constexpr auto signed_index_fixture =
    R"json({"signed":{"schema":"lito.registry.package-index.v1","registry":"https://registry.example/","package":"sample","revision":"7","sequence":"1042","releases":[{"version":"1.2.3","source":"sha256:2222222222222222222222222222222222222222222222222222222222222222","manifest":"sha256:3333333333333333333333333333333333333333333333333333333333333333","blob":{"digest":"sha256:1111111111111111111111111111111111111111111111111111111111111111","size":"42","format":"lito.package.tar-zstd.v1"},"dependencies":[{"alias":"helper","registry":"https://registry.example/","package":"helper","requirement":"^1.0.0","kind":"normal","visibility":"public","features":["fast"],"default_features":true}],"published_at":"2026-08-20T12:34:56Z","release":"sha256:c957118a7ed266dad59b4f5e225ed24b4cb47697d061a36d7bbc4ba808125691","yanked":false,"deprecated":null}],"tags":{"latest":"1.2.3"}},"signatures":[{"algorithm":"ed25519","key_id":"sha256:56475aa75463474c0285df5dbf2bcab73da651358839e9b77481b2eab107708c","signature":"eJgNnhrPzJ6U5CmHusb9HyVRW0MiOvmZkwKBT83ke53IhpJuR1l5SMnKTCZ7YgLdLTj73bJ4LBO1wvvExsBbDw"}]})json"_str;

constexpr auto signed_release_fixture =
    R"json({"signed":{"blob":{"digest":"sha256:1111111111111111111111111111111111111111111111111111111111111111","format":"lito.package.tar-zstd.v1","size":"42"},"dependencies":[{"alias":"helper","default_features":true,"features":["fast"],"kind":"normal","package":"helper","registry":"https://registry.example/","requirement":"^1.0.0","visibility":"public"}],"manifest":"sha256:3333333333333333333333333333333333333333333333333333333333333333","package":"sample","published_at":"2026-08-20T12:34:56Z","registry":"https://registry.example/","schema":"lito.registry.release.v1","source":"sha256:2222222222222222222222222222222222222222222222222222222222222222","version":"1.2.3"},"signatures":[{"algorithm":"ed25519","key_id":"sha256:21fe31dfa154a261626bf854046fd2271b7bed4b6abe45aa58877ef47f9721b9","signature":"mbKoaYTGgjfvds36ntR-fMuNa9SqmKs8JMicDB9qGpy7BBvMNhQaRh6feFH9TiNx2l1OGVgZ8R7vGl6_AMKSBg"}]})json"_str;

constexpr auto solver_sample_fixture =
    R"json({"signed":{"schema":"lito.registry.package-index.v1","registry":"https://registry.example/","package":"sample","revision":"1","sequence":"1","releases":[{"version":"2.0.0","source":"sha256:4444444444444444444444444444444444444444444444444444444444444444","manifest":"sha256:5555555555555555555555555555555555555555555555555555555555555555","blob":{"digest":"sha256:6666666666666666666666666666666666666666666666666666666666666666","size":"42","format":"lito.package.tar-zstd.v1"},"dependencies":[],"published_at":"2026-08-20T12:34:56Z","release":"sha256:f3ffc6a5d0e2837fb501142420b982c5bc7938856175fc2caf11d748a199bcda","yanked":true,"deprecated":null},{"version":"1.5.0","source":"sha256:1111111111111111111111111111111111111111111111111111111111111111","manifest":"sha256:2222222222222222222222222222222222222222222222222222222222222222","blob":{"digest":"sha256:3333333333333333333333333333333333333333333333333333333333333333","size":"42","format":"lito.package.tar-zstd.v1"},"dependencies":[{"alias":"helper","registry":"https://registry.example/","package":"helper","requirement":"^2.0.0","kind":"normal","visibility":"public","features":[],"default_features":true}],"published_at":"2026-08-20T12:34:56Z","release":"sha256:9545e0fb72d9adafbdac732c62acf16bfa290ae88d9e9ba9435ba974c0f3eed1","yanked":false,"deprecated":null},{"version":"1.2.3","source":"sha256:7777777777777777777777777777777777777777777777777777777777777777","manifest":"sha256:8888888888888888888888888888888888888888888888888888888888888888","blob":{"digest":"sha256:9999999999999999999999999999999999999999999999999999999999999999","size":"42","format":"lito.package.tar-zstd.v1"},"dependencies":[{"alias":"helper","registry":"https://registry.example/","package":"helper","requirement":"^1.0.0","kind":"normal","visibility":"public","features":[],"default_features":true}],"published_at":"2026-08-20T12:34:56Z","release":"sha256:7c2aa9ca393a25bce87b4f01af0baeedf3ccf54f2014455ba5966036105475a8","yanked":false,"deprecated":null}],"tags":{"latest":"2.0.0"}},"signatures":[{"algorithm":"ed25519","key_id":"sha256:56475aa75463474c0285df5dbf2bcab73da651358839e9b77481b2eab107708c","signature":"GgGPCjzwAnYwts2dYOM-nAFQNGOh4HnVMrLDmX_mHrTsXEgoJBSVEvxlKBEiD54nhxMfndfYLoJB9NKo1GcuCg"}]})json"_str;

constexpr auto solver_helper_fixture =
    R"json({"signed":{"schema":"lito.registry.package-index.v1","registry":"https://registry.example/","package":"helper","revision":"1","sequence":"1","releases":[{"version":"2.0.0","source":"sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","manifest":"sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb","blob":{"digest":"sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc","size":"42","format":"lito.package.tar-zstd.v1"},"dependencies":[],"published_at":"2026-08-20T12:34:56Z","release":"sha256:4859560e12db862a8a2ee91817015b08c285356d50bee35d52f5e9cbcb17cde7","yanked":false,"deprecated":null},{"version":"1.0.0","source":"sha256:dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd","manifest":"sha256:eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee","blob":{"digest":"sha256:ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff","size":"42","format":"lito.package.tar-zstd.v1"},"dependencies":[],"published_at":"2026-08-20T12:34:56Z","release":"sha256:80ab425ab95a10973ff549d92ed49beebf6a619f66a816d24c86310c5349c29f","yanked":false,"deprecated":null}],"tags":{"latest":"2.0.0"}},"signatures":[{"algorithm":"ed25519","key_id":"sha256:56475aa75463474c0285df5dbf2bcab73da651358839e9b77481b2eab107708c","signature":"TnLlWAZFTSz4tbLu6-dS3q_l25aQqJuZwq_ozrjH74NF6Membe7GsgN5wDARRKQjwBlVbiXyU6QfTCefR5d1DQ"}]})json"_str;

auto registry_package(ref<str> name) -> lito::registry::RegistryPackageId {
    return lito::registry::RegistryPackageId {
        .registry = lito::registry::RegistryId::parse("https://registry.example/"_str).unwrap(),
        .name     = lito::registry::RegistryPackageName::parse(name).unwrap(),
    };
}

auto registry_package_from(ref<str> registry, ref<str> name) -> lito::registry::RegistryPackageId {
    return lito::registry::RegistryPackageId {
        .registry = lito::registry::RegistryId::parse(registry).unwrap(),
        .name     = lito::registry::RegistryPackageName::parse(name).unwrap(),
    };
}

struct SolverFixtureProvider {
    lito::registry::Ed25519PublicKey key;
    usize                            loads {};

    static auto parse_fixture(SolverFixtureProvider&                   self,
                              const lito::registry::RegistryPackageId& package,
                              ref<str>                                 fixture) noexcept
        -> lito::registry::RegistryIndexLoadResult {
        auto parsed =
            lito::registry::parse_verified_package_index(fixture.as_bytes(), package, self.key);
        if (parsed.is_err()) {
            return Err(lito::registry::RegistryIndexError {
                .kind    = lito::registry::RegistryIndexErrorKind::Schema,
                .package = package.clone(),
                .message = rstd::format("{}", rstd::move(parsed).unwrap_err()),
            });
        }
        return Ok(rstd::move(parsed).unwrap());
    }

    static auto load(void* context, const lito::registry::RegistryPackageId& package) noexcept
        -> lito::registry::RegistryIndexLoadResult {
        auto& self = *static_cast<SolverFixtureProvider*>(context);
        ++self.loads;
        if (package.name.as_str() == "sample"_str) {
            return parse_fixture(self, package, solver_sample_fixture);
        }
        if (package.name.as_str() == "helper"_str) {
            return parse_fixture(self, package, solver_helper_fixture);
        }
        return Err(lito::registry::RegistryIndexError {
            .kind    = lito::registry::RegistryIndexErrorKind::NotFound,
            .package = package.clone(),
            .message = String::make("fixture is missing"_str),
        });
    }
};

auto registry_fixed_endpoint(ref<str> value) -> lito::registry::RegistryFixedEndpoint {
    return lito::registry::RegistryFixedEndpoint::parse(value).unwrap();
}

auto registry_endpoint(ref<str> value, lito::registry::RegistryEndpointKind kind)
    -> lito::registry::RegistryEndpointTemplate {
    return lito::registry::RegistryEndpointTemplate::parse(value, kind).unwrap();
}

auto registry_test_config() -> lito::config::NamedRegistryConfig {
    return lito::config::NamedRegistryConfig {
        .name     = String::make("fixture"_str),
        .identity = lito::registry::RegistryId::parse("https://registry.example/"_str).unwrap(),
        .endpoints =
            lito::registry::RegistryDataEndpoints {
                .config = registry_fixed_endpoint("https://registry.example/v1/config.json"_str),
                .index  = registry_endpoint("https://registry.example/v1/index/{package}.json"_str,
                                            lito::registry::RegistryEndpointKind::Index),
                .blob = registry_endpoint("https://registry.example/v1/blobs/{sha256}.tar.zst"_str,
                                          lito::registry::RegistryEndpointKind::Blob),
                .release =
                    registry_endpoint("https://registry.example/v1/releases/{sha256}.json"_str,
                                      lito::registry::RegistryEndpointKind::Release),
                .event = registry_endpoint("https://registry.example/v1/events/{sequence}.json"_str,
                                           lito::registry::RegistryEndpointKind::Event),
                .checkpoint =
                    registry_fixed_endpoint("https://registry.example/v1/checkpoint.json"_str),
            },
        .api                = registry_fixed_endpoint("https://registry.example/"_str),
        .trusted_public_key = lito::registry::Ed25519PublicKey::parse(
                                  "A6EHv_POEL4dcN0Y50vAmWfk1jCbpQ1fHdyGZBJVMbg"_str)
                                  .unwrap(),
    };
}

struct IndexHttpFixture {
    String body;
    usize  calls {};
    bool   not_modified {};
    bool   saw_condition {};

    static auto get(void* context, const lito::registry::RegistryHttpRequest& request) noexcept
        -> lito::registry::RegistryHttpResult {
        auto& self = *static_cast<IndexHttpFixture*>(context);
        ++self.calls;
        self.saw_condition = request.if_none_match.is_some();
        return Ok(lito::registry::RegistryHttpResponse {
            .status = self.not_modified ? u16(304) : u16(200),
            .body   = self.not_modified ? String::make() : self.body.clone(),
            .etag   = Some(String::make("\"fixture-etag\""_str)),
        });
    }

    auto transport() -> lito::registry::RegistryHttpTransport {
        return lito::registry::RegistryHttpTransport {
            .context = this,
            .get     = get,
        };
    }
};

struct RegistryGraphFixture {
    Option<lito::registry::ResolvedRegistryGraphSource> selected;
    usize                                               calls {};
    usize                                               requirements {};
    bool                                                accepted {};

    static auto resolve(void*                                           context,
                        slice<lito::registry::RegistryGraphRequirement> requirements) noexcept
        -> lito::registry::RegistryGraphResult<Vec<lito::registry::ResolvedRegistryGraphSource>> {
        auto& self = *static_cast<RegistryGraphFixture*>(context);
        ++self.calls;
        self.requirements = requirements.len();
        auto version      = lito::registry::SemanticVersion::parse("1.5.0"_str).unwrap();
        self.accepted     = requirements.len() == usize(2);
        for (const auto& requirement : requirements) {
            self.accepted = self.accepted && requirement.package.as_str() == "sample"_str &&
                            requirement.requirement.matches(version);
        }
        if (self.selected.is_none()) {
            return Err(lito::registry::RegistryGraphError {
                .message = String::make("fixture source was already consumed"_str),
            });
        }
        auto result = Vec<lito::registry::ResolvedRegistryGraphSource>::make();
        result.push(rstd::move(self.selected).unwrap());
        return Ok(rstd::move(result));
    }

    auto provider() noexcept -> lito::registry::RegistryGraphProvider {
        return lito::registry::RegistryGraphProvider {
            .context = this,
            .resolve = resolve,
        };
    }
};

struct BlobTransportFixture {
    String bytes;
    usize  calls {};

    static auto download(void*                                              context,
                         const lito::registry::RegistryBlobDownloadRequest& request) noexcept
        -> lito::registry::RegistryArtifactResult<empty> {
        auto& self = *static_cast<BlobTransportFixture*>(context);
        ++self.calls;
        auto written =
            rstd::fs::write(request.destination.as_path(), self.bytes.as_str().as_bytes());
        if (written.is_err()) {
            return Err(lito::registry::RegistryArtifactError {
                .kind    = lito::registry::RegistryArtifactErrorKind::Io,
                .package = request.package.clone(),
                .message = String::make("fixture write failed"_str),
            });
        }
        return Ok(empty {});
    }

    auto transport() -> lito::registry::RegistryBlobTransport {
        return lito::registry::RegistryBlobTransport {
            .context  = this,
            .download = download,
        };
    }
};

struct CopyBlobTransportFixture {
    PathBuf source;
    usize   calls {};

    static auto download(void*                                              context,
                         const lito::registry::RegistryBlobDownloadRequest& request) noexcept
        -> lito::registry::RegistryArtifactResult<empty> {
        auto& self = *static_cast<CopyBlobTransportFixture*>(context);
        ++self.calls;
        auto copied = rstd::fs::copy(self.source.as_path(), request.destination.as_path());
        if (copied.is_err()) {
            return Err(lito::registry::RegistryArtifactError {
                .kind    = lito::registry::RegistryArtifactErrorKind::Io,
                .package = request.package.clone(),
                .message = String::make("fixture copy failed"_str),
            });
        }
        return Ok(empty {});
    }

    auto transport() -> lito::registry::RegistryBlobTransport {
        return lito::registry::RegistryBlobTransport {
            .context  = this,
            .download = download,
        };
    }
};

struct PublishTransportFixture {
    Vec<lito::registry::RegistryPublishHttpResponse> responses;
    Vec<String>                                      methods;
    Vec<String>                                      urls;
    bool                                             api_authorized { true };
    bool                                             upload_uses_archive {};

    static auto execute(void*                                             context,
                        const lito::registry::RegistryPackageId&          package,
                        const lito::registry::RegistryPublishHttpRequest& request) noexcept
        -> lito::registry::RegistryPublishResult<lito::registry::RegistryPublishHttpResponse> {
        auto& self = *static_cast<PublishTransportFixture*>(context);
        self.methods.push(request.method.clone());
        self.urls.push(request.url.clone());
        auto authorized = false;
        for (const auto& header : request.headers) {
            if (header.name.as_str() == "Authorization"_str &&
                header.value.as_str() == "Bearer fixture-token"_str) {
                authorized = true;
            }
        }
        if (request.upload.is_some()) {
            self.upload_uses_archive = true;
        } else if (! authorized) {
            self.api_authorized = false;
        }
        auto index = self.methods.len() - usize(1);
        if (index >= self.responses.len()) {
            return Err(lito::registry::RegistryPublishError {
                .kind    = lito::registry::RegistryPublishErrorKind::Network,
                .package = package.clone(),
                .message = String::make("fixture response is missing"_str),
            });
        }
        return Ok(lito::registry::RegistryPublishHttpResponse {
            .status = self.responses[index].status,
            .body   = self.responses[index].body.clone(),
        });
    }

    auto transport() -> lito::registry::RegistryPublishHttpTransport {
        return lito::registry::RegistryPublishHttpTransport {
            .context = this,
            .execute = execute,
        };
    }
};

auto publish_session_json(ref<str> state,
                          bool     prepare,
                          bool     upload,
                          bool     commit,
                          ref<str> package = "sample"_str) -> String {
    constexpr auto digest =
        "sha256:1111111111111111111111111111111111111111111111111111111111111111"_str;
    auto prefix =
        prepare
            ? R"json({"schema":"lito.registry.prepare-publish-session.v1","outcome":"created",)json"_str
            : "{"_str;
    auto suffix = String::make();
    if (commit) {
        suffix.push_str(
            R"json(,"commit":{"sequence":"4","package_revision":"2","release":"sha256:2222222222222222222222222222222222222222222222222222222222222222"})json"_str);
    }
    if (upload) {
        suffix.push_str(
            R"json(,"upload":{"method":"PUT","url":"https://uploads.example/staging/session-1","headers":{"content-type":"application/zstd"},"expires_at":"2026-08-23T12:00:00Z"})json"_str);
    }
    return rstd::format(
        R"json({}"id":"session-1","state":"{}","registry":"https://registry.example/","package":"{}","version":"1.2.3","blob":{{"digest":"{}","size":"42","format":"lito.package.tar-zstd.v1"}}{}}})json",
        prefix,
        state,
        package,
        digest,
        suffix);
}

auto publish_request(ref<rstd::path::Path> archive, const lito::config::RegistryBearerToken& token)
    -> lito::registry::RegistryPublishRequest {
    return lito::registry::RegistryPublishRequest {
        .api     = registry_fixed_endpoint("https://api.registry.example/"_str),
        .token   = rstd::addressof(token),
        .package = registry_package("sample"_str),
        .version = lito::registry::SemanticVersion::parse("1.2.3"_str).unwrap(),
        .blob =
            lito::registry::RegistryBlobProjection {
                .digest =
                    lito::registry::BlobDigest::parse(
                        "sha256:1111111111111111111111111111111111111111111111111111111111111111"_str)
                        .unwrap(),
                .size   = lito::registry::RegistryBlobSize(u64(42)),
                .format = lito::registry::RegistryArchiveFormat::parse(
                              lito::registry::RegistryArchiveFormat::TAR_ZSTD_V1)
                              .unwrap(),
            },
        .archive              = PathBuf::from(archive),
        .maximum_status_polls = usize(2),
        .poll_interval        = rstd::time::Duration {},
    };
}

TEST(RegistryIdentity, AcceptsOnlyCanonicalCoordinates) {
    auto registry = lito::registry::RegistryId::parse("https://registry.litocpp.org/"_str);
    ASSERT_TRUE(registry.is_ok());
    EXPECT_EQ(registry->as_str(), "https://registry.litocpp.org/"_str);
    EXPECT_TRUE(lito::registry::RegistryId::parse("http://registry.litocpp.org/"_str).is_err());
    EXPECT_TRUE(lito::registry::RegistryId::parse("https://Registry.litocpp.org/"_str).is_err());
    EXPECT_TRUE(lito::registry::RegistryId::parse("https://registry.litocpp.org/v1"_str).is_err());
    EXPECT_TRUE(
        lito::registry::RegistryId::parse("https://registry.litocpp.org/?x=1"_str).is_err());

    auto name = lito::registry::RegistryPackageName::parse("lito_core-2"_str);
    ASSERT_TRUE(name.is_ok());
    EXPECT_EQ(name->collision_key().as_str(), "lito-core-2"_str);
    EXPECT_TRUE(lito::registry::RegistryPackageName::parse("Lito"_str).is_err());
    EXPECT_TRUE(lito::registry::RegistryPackageName::parse("-lito"_str).is_err());
    EXPECT_TRUE(lito::registry::RegistryPackageName::parse("lito/codec"_str).is_err());
    EXPECT_TRUE(lito::registry::RegistryPackageName::parse("con"_str).is_err());
}

TEST(RegistryPackageSpec, SeparatesVersionRequirementsFromCliOnlyTags) {
    auto implicit = lito::registry::RegistryPackageSpec::parse("sample"_str);
    ASSERT_TRUE(implicit.is_ok());
    ASSERT_TRUE(implicit->selector.is_Requirement());
    EXPECT_TRUE(implicit->selector.as_Requirement().requirement.matches(
        lito::registry::SemanticVersion::parse("9.4.1"_str).unwrap()));

    auto requirement = lito::registry::RegistryPackageSpec::parse("sample@0.4"_str);
    ASSERT_TRUE(requirement.is_ok());
    ASSERT_TRUE(requirement->selector.is_Requirement());
    EXPECT_TRUE(requirement->selector.as_Requirement().requirement.matches(
        lito::registry::SemanticVersion::parse("0.4.7"_str).unwrap()));
    EXPECT_FALSE(requirement->selector.as_Requirement().requirement.matches(
        lito::registry::SemanticVersion::parse("0.5.0"_str).unwrap()));

    auto tag = lito::registry::RegistryPackageSpec::parse("sample@latest"_str);
    ASSERT_TRUE(tag.is_ok());
    ASSERT_TRUE(tag->selector.is_NamedTag());
    EXPECT_EQ(tag->selector.as_NamedTag().tag.as_str(), "latest"_str);
    EXPECT_TRUE(lito::registry::RegistryPackageSpec::parse("sample@v1"_str).is_err());
    EXPECT_TRUE(lito::registry::RegistryPackageSpec::parse("sample@"_str).is_err());
}

TEST(RegistryDigest, KeepsDigestDomainsTypedAndTextCanonical) {
    constexpr auto value =
        "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"_str;
    auto blob = lito::registry::BlobDigest::parse(value);
    ASSERT_TRUE(blob.is_ok());
    EXPECT_EQ(blob->text().as_str(), value);
    EXPECT_TRUE(lito::registry::SourceDigest::parse(
                    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"_str)
                    .is_err());
    EXPECT_TRUE(lito::registry::ReleaseDigest::parse(
                    "sha256:0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef"_str)
                    .is_err());
}

auto registry_version(ref<str> value) -> lito::registry::SemanticVersion {
    return rstd::move(lito::registry::SemanticVersion::parse(value)).unwrap();
}

TEST(RegistryPublish, UploadsCommitsAndWaitsForVisibleContext) {
    auto fixture = PublishTransportFixture {};
    fixture.responses.push(lito::registry::RegistryPublishHttpResponse {
        .status = u16(201),
        .body   = publish_session_json("prepared"_str, true, true, false),
    });
    fixture.responses.push(lito::registry::RegistryPublishHttpResponse {
        .status = u16(200),
    });
    fixture.responses.push(lito::registry::RegistryPublishHttpResponse {
        .status = u16(200),
        .body   = publish_session_json("projecting"_str, false, false, true),
    });
    fixture.responses.push(lito::registry::RegistryPublishHttpResponse {
        .status = u16(200),
        .body   = publish_session_json("visible"_str, false, false, true),
    });
    auto token   = lito::config::RegistryBearerToken(String::make("fixture-token"_str));
    auto request = publish_request(PathBuf::from("fixture.tar.zst"_str).as_path(), token);
    auto client  = lito::registry::RegistryPublishClient(fixture.transport());
    auto result  = client.publish(request);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result->state, lito::registry::RegistryPublishState::Visible);
    ASSERT_TRUE(result->release.is_some());
    ASSERT_EQ(fixture.methods.len(), usize(4));
    EXPECT_EQ(fixture.methods[usize {}].as_str(), "POST"_str);
    EXPECT_EQ(fixture.methods[usize(1)].as_str(), "PUT"_str);
    EXPECT_EQ(fixture.methods[usize(2)].as_str(), "POST"_str);
    EXPECT_EQ(fixture.methods[usize(3)].as_str(), "GET"_str);
    EXPECT_TRUE(fixture.api_authorized);
    EXPECT_TRUE(fixture.upload_uses_archive);
    EXPECT_EQ(fixture.urls[usize {}].as_str(),
              "https://api.registry.example/v1/publish/sessions"_str);
}

TEST(RegistryPublish, RejectsResponseContextMismatchBeforeUpload) {
    auto fixture = PublishTransportFixture {};
    fixture.responses.push(lito::registry::RegistryPublishHttpResponse {
        .status = u16(201),
        .body   = publish_session_json("prepared"_str, true, true, false, "other"_str),
    });
    auto token   = lito::config::RegistryBearerToken(String::make("fixture-token"_str));
    auto request = publish_request(PathBuf::from("fixture.tar.zst"_str).as_path(), token);
    auto client  = lito::registry::RegistryPublishClient(fixture.transport());
    auto result  = client.publish(request);
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.unwrap_err().kind, lito::registry::RegistryPublishErrorKind::Protocol);
    EXPECT_EQ(fixture.methods.len(), usize(1));
    EXPECT_FALSE(fixture.upload_uses_archive);
}

TEST(RegistryVersion, ParsesAndOrdersStrictSemanticVersions) {
    auto stable = lito::registry::SemanticVersion::parse("1.2.3"_str);
    ASSERT_TRUE(stable.is_ok());
    EXPECT_EQ(stable->text().as_str(), "1.2.3"_str);
    EXPECT_TRUE(lito::registry::SemanticVersion::parse("v1.2.3"_str).is_err());
    EXPECT_TRUE(lito::registry::SemanticVersion::parse("1.2"_str).is_err());
    EXPECT_TRUE(lito::registry::SemanticVersion::parse("01.2.3"_str).is_err());
    EXPECT_TRUE(lito::registry::SemanticVersion::parse("1.2.3+build"_str).is_err());
    EXPECT_TRUE(lito::registry::SemanticVersion::parse("1.2.3-01"_str).is_err());

    auto alpha   = registry_version("1.0.0-alpha"_str);
    auto alpha1  = registry_version("1.0.0-alpha.1"_str);
    auto beta    = registry_version("1.0.0-beta"_str);
    auto release = registry_version("1.0.0"_str);
    EXPECT_TRUE(alpha < alpha1);
    EXPECT_TRUE(alpha1 < beta);
    EXPECT_TRUE(beta < release);
}

TEST(RegistryVersion, MatchesCargoStyleIntersectionsAndPrereleases) {
    auto cargo = lito::registry::VersionRequirement::parse("0.4"_str);
    ASSERT_TRUE(cargo.is_ok());
    EXPECT_TRUE(cargo->matches(registry_version("0.4.0"_str)));
    EXPECT_TRUE(cargo->matches(registry_version("0.4.99"_str)));
    EXPECT_FALSE(cargo->matches(registry_version("0.5.0"_str)));

    auto intersection = lito::registry::VersionRequirement::parse(">=1.2, <2"_str);
    ASSERT_TRUE(intersection.is_ok());
    EXPECT_TRUE(intersection->matches(registry_version("1.9.0"_str)));
    EXPECT_FALSE(intersection->matches(registry_version("2.0.0"_str)));

    auto wildcard = lito::registry::VersionRequirement::parse("1.2.*"_str);
    ASSERT_TRUE(wildcard.is_ok());
    EXPECT_TRUE(wildcard->matches(registry_version("1.2.8"_str)));
    EXPECT_FALSE(wildcard->matches(registry_version("1.3.0"_str)));
    EXPECT_TRUE(lito::registry::VersionRequirement::parse("*"_str).is_err());
    EXPECT_TRUE(lito::registry::VersionRequirement::parse("1 || 2"_str).is_err());

    auto ordinary = lito::registry::VersionRequirement::parse(">=1.0.0, <2"_str);
    ASSERT_TRUE(ordinary.is_ok());
    EXPECT_FALSE(ordinary->matches(registry_version("1.5.0-beta.1"_str)));
    auto prerelease = lito::registry::VersionRequirement::parse(">=1.5.0-beta.1, <2"_str);
    ASSERT_TRUE(prerelease.is_ok());
    EXPECT_TRUE(prerelease->matches(registry_version("1.5.0-beta.2"_str)));
    EXPECT_FALSE(prerelease->matches(registry_version("1.6.0-beta.1"_str)));
}

TEST(RegistrySourceDigest, UsesCanonicalTreeFramingIndependentOfInsertionOrder) {
    auto left = lito::source::SourceTree::make();
    ASSERT_TRUE(left.add_text("src/lib.cppm"_str, "module value;\n"_str).is_ok());
    ASSERT_TRUE(left.add_directory("empty"_str).is_ok());
    ASSERT_TRUE(
        left.add_text("build.sh"_str, "#!/bin/sh\n"_str, lito::source::SourceFileMode::Executable)
            .is_ok());

    auto right = lito::source::SourceTree::make();
    ASSERT_TRUE(
        right.add_text("build.sh"_str, "#!/bin/sh\n"_str, lito::source::SourceFileMode::Executable)
            .is_ok());
    ASSERT_TRUE(right.add_text("src/lib.cppm"_str, "module value;\n"_str).is_ok());

    auto left_digest  = lito::source::canonical_source_digest(left);
    auto right_digest = lito::source::canonical_source_digest(right);
    ASSERT_TRUE(left_digest.is_ok());
    ASSERT_TRUE(right_digest.is_ok());
    EXPECT_EQ(left_digest->text(), right_digest->text());
    EXPECT_EQ(left_digest->text().as_str(),
              "sha256:14e327c68249dc2d577779764fe8ec4ab3b23dd9d7ebcc1f222f2ae4593d37e1"_str);
}

TEST(RegistryCanonicalJson, SortsObjectKeysByUtf16CodeUnits) {
    auto value = rstd::json::from_str(
        R"json({"\ufb33":"bmp","\ud83d\ude00":"supplementary","a":"ascii"})json"_str,
        rstd::json::ParseOptions { .reject_duplicate_keys = true });
    ASSERT_TRUE(value.is_ok());
    auto canonical = lito::registry::canonical_signed_json(*value);
    ASSERT_TRUE(canonical.is_ok());
    EXPECT_EQ(canonical->as_str(), R"json({"a":"ascii","😀":"supplementary","דּ":"bmp"})json"_str);

    auto number = rstd::json::from_str("1"_str).unwrap();
    EXPECT_TRUE(lito::registry::canonical_signed_json(number).is_err());
    EXPECT_TRUE(rstd::json::from_str(R"json({"a":true,"a":false})json"_str,
                                     rstd::json::ParseOptions {
                                         .reject_duplicate_keys = true,
                                     })
                    .is_err());
}

TEST(RegistryEvent, MatchesTheRustEventChainDigestFixture) {
    auto           previous = lito::crypto::Sha256Digest::parse_hex(
                                  "0000000000000000000000000000000000000000000000000000000000000000"_str)
                                  .unwrap();
    constexpr auto payload =
        R"json({"previous":"sha256:0000000000000000000000000000000000000000000000000000000000000000","sequence":"2"})json"_str;
    auto state = lito::crypto::Sha256::make();
    state.update("lito-registry-event-v1\0"_str.as_bytes());
    state.update(previous.as_bytes());
    state.update(payload.as_bytes());
    EXPECT_EQ(rstd::move(state).finalize_digest().to_hex().as_str(),
              "3525b4c79b53bbb98f605b0c3c006b20f816e79d007539a0399010abd3f8af26"_str);
}

TEST(RegistryEd25519, VerifiesDetachedRfc8032Vector) {
    auto public_key =
        lito::registry::Ed25519PublicKey::parse("11qYAYKxCrfVS_7TyWQHOg7hcvPapiMlrwIaaPcHURo"_str);
    auto signature = lito::registry::Ed25519Signature::parse(
        "5VZDAMNgrHKQhuLMgG6CioSHfx645dl02HPgZSJJAVVfuIIVkKM7rMYeOXAc-bRr0lv18FlbviRlUUFDjnoQCw"_str);
    ASSERT_TRUE(public_key.is_ok());
    ASSERT_TRUE(signature.is_ok());
    EXPECT_TRUE(lito::registry::verify_ed25519(*public_key, *signature, ""_str.as_bytes()).is_ok());
    EXPECT_TRUE(
        lito::registry::verify_ed25519(*public_key, *signature, "x"_str.as_bytes()).is_err());

    auto key_id = lito::registry::signing_key_id(*public_key);
    ASSERT_TRUE(key_id.is_ok());
    EXPECT_EQ(key_id->text().as_str(),
              "sha256:21fe31dfa154a261626bf854046fd2271b7bed4b6abe45aa58877ef47f9721b9"_str);
}

TEST(RegistryMetadata, ConstructsOnlyAContextVerifiedPackageIndex) {
    auto expected = lito::registry::RegistryPackageId {
        .registry = lito::registry::RegistryId::parse("https://registry.example/"_str).unwrap(),
        .name     = lito::registry::RegistryPackageName::parse("sample"_str).unwrap(),
    };
    auto public_key =
        lito::registry::Ed25519PublicKey::parse("A6EHv_POEL4dcN0Y50vAmWfk1jCbpQ1fHdyGZBJVMbg"_str)
            .unwrap();
    auto index = lito::registry::parse_verified_package_index(
        signed_index_fixture.as_bytes(), expected, public_key);
    ASSERT_TRUE(index.is_ok());
    EXPECT_EQ(index->revision(), u64(7));
    EXPECT_EQ(index->sequence(), u64(1042));
    ASSERT_EQ(index->releases().len(), usize(1));
    const auto& release = index->releases()[usize {}];
    EXPECT_EQ(release.version.text().as_str(), "1.2.3"_str);
    EXPECT_EQ(release.release.text().as_str(),
              "sha256:c957118a7ed266dad59b4f5e225ed24b4cb47697d061a36d7bbc4ba808125691"_str);
    ASSERT_EQ(release.dependencies.len(), usize(1));
    EXPECT_EQ(release.dependencies[usize {}].alias.as_str(), "helper"_str);
    ASSERT_EQ(index->tags().len(), usize(1));
    EXPECT_EQ(index->tags()[usize {}].name.as_str(), "latest"_str);

    auto wrong_context = lito::registry::RegistryPackageId {
        .registry = expected.registry.clone(),
        .name     = lito::registry::RegistryPackageName::parse("other"_str).unwrap(),
    };
    EXPECT_TRUE(lito::registry::parse_verified_package_index(
                    signed_index_fixture.as_bytes(), wrong_context, public_key)
                    .is_err());
    auto wrong_key =
        lito::registry::Ed25519PublicKey::parse("11qYAYKxCrfVS_7TyWQHOg7hcvPapiMlrwIaaPcHURo"_str)
            .unwrap();
    EXPECT_TRUE(lito::registry::parse_verified_package_index(
                    signed_index_fixture.as_bytes(), expected, wrong_key)
                    .is_err());
}

TEST(RegistryMetadata, VerifiesImmutableReleaseIdentityAndRequestContext) {
    auto package = registry_package("sample"_str);
    auto release =
        lito::registry::ReleaseDigest::parse(
            "sha256:c957118a7ed266dad59b4f5e225ed24b4cb47697d061a36d7bbc4ba808125691"_str)
            .unwrap();
    auto key =
        lito::registry::Ed25519PublicKey::parse("11qYAYKxCrfVS_7TyWQHOg7hcvPapiMlrwIaaPcHURo"_str)
            .unwrap();
    auto verified = lito::registry::parse_verified_registry_release(
        signed_release_fixture.as_bytes(), package, release, key);
    ASSERT_TRUE(verified.is_ok());
    EXPECT_EQ(verified->release().version.text().as_str(), "1.2.3"_str);
    EXPECT_EQ(verified->release().release.text(), release.text());
    EXPECT_EQ(verified->release().dependencies.len(), usize(1));

    auto wrong = lito::registry::ReleaseDigest::parse(
                     "sha256:0000000000000000000000000000000000000000000000000000000000000000"_str)
                     .unwrap();
    EXPECT_TRUE(lito::registry::parse_verified_registry_release(
                    signed_release_fixture.as_bytes(), package, wrong, key)
                    .is_err());
}

TEST(RegistryReleaseCache, FetchesOnceAndSupportsLockedOfflineReads) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto owner  = rstd::move(temporary).unwrap();
    auto config = registry_test_config();
    config.trusted_public_key =
        lito::registry::Ed25519PublicKey::parse("11qYAYKxCrfVS_7TyWQHOg7hcvPapiMlrwIaaPcHURo"_str)
            .unwrap();
    auto fixture = IndexHttpFixture { .body = String::make(signed_release_fixture) };
    auto package = registry_package("sample"_str);
    auto release =
        lito::registry::ReleaseDigest::parse(
            "sha256:c957118a7ed266dad59b4f5e225ed24b4cb47697d061a36d7bbc4ba808125691"_str)
            .unwrap();
    auto online =
        lito::registry::RegistryReleaseClient(PathBuf::from(owner.path()),
                                              config,
                                              lito::registry::RegistryNetworkPolicy::Online,
                                              fixture.transport());
    auto fetched = online.load(package, release);
    ASSERT_TRUE(fetched.is_ok());
    EXPECT_EQ(fetched->release().version.text().as_str(), "1.2.3"_str);
    EXPECT_EQ(fixture.calls, usize(1));

    auto offline =
        lito::registry::RegistryReleaseClient(PathBuf::from(owner.path()),
                                              config,
                                              lito::registry::RegistryNetworkPolicy::Offline,
                                              lito::registry::RegistryHttpTransport {});
    auto reused = offline.load(package, release);
    ASSERT_TRUE(reused.is_ok());
    EXPECT_EQ(reused->release().release.text(), release.text());
    EXPECT_EQ(fixture.calls, usize(1));
}

TEST(RegistryGraphClient, LockedResolutionUsesExactReleaseWithoutPackageIndex) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto owner  = rstd::move(temporary).unwrap();
    auto config = registry_test_config();
    config.trusted_public_key =
        lito::registry::Ed25519PublicKey::parse("11qYAYKxCrfVS_7TyWQHOg7hcvPapiMlrwIaaPcHURo"_str)
            .unwrap();
    auto registries = Vec<lito::config::NamedRegistryConfig>::make();
    registries.push(rstd::move(config));
    auto bootstrap = lito::config::LitoBootstrapConfig(rstd::move(registries),
                                                       Some(String::make("fixture"_str)));
    auto fixture   = IndexHttpFixture { .body = String::make(signed_release_fixture) };
    auto package   = registry_package("sample"_str);
    auto release =
        lito::registry::ReleaseDigest::parse(
            "sha256:c957118a7ed266dad59b4f5e225ed24b4cb47697d061a36d7bbc4ba808125691"_str)
            .unwrap();
    auto pins = Vec<lito::source::RegistrySourcePin>::make();
    pins.push(lito::source::RegistrySourcePin {
        .package = package.clone(),
        .version = registry_version("1.2.3"_str),
        .release = release.clone(),
    });
    auto client = lito::registry::RegistryGraphClient(PathBuf::from(owner.path()),
                                                      bootstrap,
                                                      lito::registry::RegistryNetworkPolicy::Online,
                                                      fixture.transport(),
                                                      lito::registry::RegistryBlobTransport {},
                                                      true,
                                                      rstd::move(pins));
    auto requirements = Vec<lito::registry::RegistryGraphRequirement>::make();
    requirements.push(lito::registry::RegistryGraphRequirement {
        .package     = package.name.clone(),
        .requirement = lito::registry::VersionRequirement::parse("^1.0.0"_str).unwrap(),
        .source      = String::make("root dependency 'sample'"_str),
    });
    auto resolved = client.resolve(requirements.as_slice());
    ASSERT_TRUE(resolved.is_err());
    EXPECT_TRUE(resolved.unwrap_err().message.as_str().contains(
        "--locked has no exact Registry release for package 'helper'"_str));
    EXPECT_EQ(fixture.calls, usize(1));
}

auto solver_provider(SolverFixtureProvider& fixture_provider)
    -> lito::registry::RegistryIndexProvider {
    return lito::registry::RegistryIndexProvider {
        .context = rstd::addressof(fixture_provider),
        .load    = SolverFixtureProvider::load,
    };
}

auto solver_fixture_provider() -> SolverFixtureProvider {
    return SolverFixtureProvider {
        .key = lito::registry::Ed25519PublicKey::parse(
                   "A6EHv_POEL4dcN0Y50vAmWfk1jCbpQ1fHdyGZBJVMbg"_str)
                   .unwrap(),
    };
}

TEST(RegistrySolver, BacktracksDeterministicallyAcrossPackageConstraints) {
    auto fixture_provider = solver_fixture_provider();
    auto roots            = Vec<lito::registry::RegistrySolverRequirement>::make();
    roots.push(lito::registry::RegistrySolverRequirement {
        .package     = registry_package("sample"_str),
        .requirement = lito::registry::VersionRequirement::parse(">=1, <2"_str).unwrap(),
        .source      = String::make("workspace dependency 'sample'"_str),
    });
    roots.push(lito::registry::RegistrySolverRequirement {
        .package     = registry_package("helper"_str),
        .requirement = lito::registry::VersionRequirement::parse("=1.0.0"_str).unwrap(),
        .source      = String::make("workspace dependency 'helper'"_str),
    });
    auto solved = lito::registry::RegistryVersionSolver::solve(
        lito::registry::RegistrySolverInput { .roots = rstd::move(roots) },
        solver_provider(fixture_provider));
    ASSERT_TRUE(solved.is_ok());
    ASSERT_EQ(solved->packages.len(), usize(2));
    EXPECT_EQ(solved->packages[usize {}].package.name.as_str(), "helper"_str);
    EXPECT_EQ(solved->packages[usize {}].release.version.text().as_str(), "1.0.0"_str);
    EXPECT_EQ(solved->packages[usize(1)].package.name.as_str(), "sample"_str);
    EXPECT_EQ(solved->packages[usize(1)].release.version.text().as_str(), "1.2.3"_str);
    EXPECT_EQ(fixture_provider.loads, usize(2));
}

TEST(RegistrySolver, MergesRequirementsForTheSamePackage) {
    auto fixture_provider = solver_fixture_provider();
    auto roots            = Vec<lito::registry::RegistrySolverRequirement>::make();
    roots.push(lito::registry::RegistrySolverRequirement {
        .package     = registry_package("sample"_str),
        .requirement = lito::registry::VersionRequirement::parse(">=1, <2"_str).unwrap(),
        .source      = String::make("workspace dependency 'sample'"_str),
    });
    roots.push(lito::registry::RegistrySolverRequirement {
        .package     = registry_package("sample"_str),
        .requirement = lito::registry::VersionRequirement::parse("<1.5"_str).unwrap(),
        .source      = String::make("package dependency 'sample'"_str),
    });
    auto solved = lito::registry::RegistryVersionSolver::solve(
        lito::registry::RegistrySolverInput { .roots = rstd::move(roots) },
        solver_provider(fixture_provider));
    ASSERT_TRUE(solved.is_ok());
    ASSERT_EQ(solved->packages.len(), usize(2));
    auto sample_count = usize {};
    for (const auto& package : solved->packages) {
        if (package.package.name.as_str() != "sample"_str) continue;
        ++sample_count;
        EXPECT_EQ(package.release.version.text().as_str(), "1.2.3"_str);
    }
    EXPECT_EQ(sample_count, usize(1));
    EXPECT_EQ(fixture_provider.loads, usize(2));
}

TEST(RegistrySolver, RejectsTheSameNameFromDifferentRegistries) {
    auto fixture_provider = solver_fixture_provider();
    auto roots            = Vec<lito::registry::RegistrySolverRequirement>::make();
    roots.push(lito::registry::RegistrySolverRequirement {
        .package     = registry_package_from("https://registry.example/"_str, "sample"_str),
        .requirement = lito::registry::VersionRequirement::parse("^1"_str).unwrap(),
        .source      = String::make("workspace dependency 'sample'"_str),
    });
    roots.push(lito::registry::RegistrySolverRequirement {
        .package     = registry_package_from("https://mirror.example/"_str, "sample"_str),
        .requirement = lito::registry::VersionRequirement::parse("^1"_str).unwrap(),
        .source      = String::make("package dependency 'sample'"_str),
    });
    auto solved = lito::registry::RegistryVersionSolver::solve(
        lito::registry::RegistrySolverInput { .roots = rstd::move(roots) },
        solver_provider(fixture_provider));
    ASSERT_TRUE(solved.is_err());
    auto error = rstd::move(solved).unwrap_err();
    ASSERT_TRUE(error.is_SourceConflict());
    EXPECT_EQ(error.as_SourceConflict().selected.registry.as_str(),
              "https://registry.example/"_str);
    EXPECT_EQ(error.as_SourceConflict().incoming.registry.as_str(), "https://mirror.example/"_str);
    EXPECT_EQ(fixture_provider.loads, usize {});
}

TEST(RegistrySolver, ExcludesFreshYankedVersions) {
    auto fixture_provider = solver_fixture_provider();
    auto roots            = Vec<lito::registry::RegistrySolverRequirement>::make();
    roots.push(lito::registry::RegistrySolverRequirement {
        .package     = registry_package("sample"_str),
        .requirement = lito::registry::VersionRequirement::parse(">=2.0.0"_str).unwrap(),
        .source      = String::make("workspace dependency 'sample'"_str),
    });
    auto solved = lito::registry::RegistryVersionSolver::solve(
        lito::registry::RegistrySolverInput { .roots = rstd::move(roots) },
        solver_provider(fixture_provider));
    ASSERT_TRUE(solved.is_err());
    EXPECT_TRUE(solved.unwrap_err().is_Incompatibility());
}

TEST(RegistrySolver, PreservesAnExactlyLockedYankedRelease) {
    auto fixture_provider = solver_fixture_provider();
    auto roots            = Vec<lito::registry::RegistrySolverRequirement>::make();
    roots.push(lito::registry::RegistrySolverRequirement {
        .package     = registry_package("sample"_str),
        .requirement = lito::registry::VersionRequirement::parse(">=2.0.0"_str).unwrap(),
        .source      = String::make("workspace dependency 'sample'"_str),
    });
    auto locked = Vec<lito::registry::RegistryLockedPreference>::make();
    locked.push(lito::registry::RegistryLockedPreference {
        .package = registry_package("sample"_str),
        .version = registry_version("2.0.0"_str),
        .release =
            lito::registry::ReleaseDigest::parse(
                "sha256:f3ffc6a5d0e2837fb501142420b982c5bc7938856175fc2caf11d748a199bcda"_str)
                .unwrap(),
    });
    auto solved = lito::registry::RegistryVersionSolver::solve(
        lito::registry::RegistrySolverInput {
            .roots  = rstd::move(roots),
            .locked = rstd::move(locked),
        },
        solver_provider(fixture_provider));
    ASSERT_TRUE(solved.is_ok());
    ASSERT_EQ(solved->packages.len(), usize(1));
    EXPECT_EQ(solved->packages[usize {}].release.version.text().as_str(), "2.0.0"_str);
}

TEST(RegistrySolver, ResolvesTagsFromVerifiedMetadata) {
    auto fixture_provider = solver_fixture_provider();
    auto index            = SolverFixtureProvider::load(rstd::addressof(fixture_provider),
                                                        registry_package("helper"_str));
    ASSERT_TRUE(index.is_ok());
    auto version = lito::registry::resolve_registry_tag(*index, "latest"_str);
    ASSERT_TRUE(version.is_ok());
    EXPECT_EQ(version->text().as_str(), "2.0.0"_str);
    EXPECT_TRUE(lito::registry::resolve_registry_tag(*index, "missing"_str).is_err());
}

TEST(RegistryIndexCache, RevalidatesAndSupportsStrictOfflineReads) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto owner   = rstd::move(temporary).unwrap();
    auto config  = registry_test_config();
    auto fixture = IndexHttpFixture { .body = String::make(signed_index_fixture) };
    auto online = lito::registry::RegistryIndexClient(PathBuf::from(owner.path()),
                                                      config,
                                                      lito::registry::RegistryNetworkPolicy::Online,
                                                      fixture.transport());
    auto first  = online.load(registry_package("sample"_str));
    ASSERT_TRUE(first.is_ok());
    EXPECT_EQ(first->revision(), u64(7));
    EXPECT_EQ(fixture.calls, usize(1));
    EXPECT_FALSE(fixture.saw_condition);

    fixture.not_modified = true;
    auto revalidated     = online.load(registry_package("sample"_str));
    ASSERT_TRUE(revalidated.is_ok());
    EXPECT_EQ(revalidated->revision(), u64(7));
    EXPECT_EQ(fixture.calls, usize(2));
    EXPECT_TRUE(fixture.saw_condition);

    auto offline =
        lito::registry::RegistryIndexClient(PathBuf::from(owner.path()),
                                            config,
                                            lito::registry::RegistryNetworkPolicy::Offline,
                                            lito::registry::RegistryHttpTransport {});
    auto cached = offline.load(registry_package("sample"_str));
    ASSERT_TRUE(cached.is_ok());
    EXPECT_EQ(cached->sequence(), u64(1042));
}

TEST(RegistryIndexCache, RejectsRollbackAndCorruption) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto owner   = rstd::move(temporary).unwrap();
    auto config  = registry_test_config();
    auto fixture = IndexHttpFixture { .body = String::make(signed_index_fixture) };
    auto online = lito::registry::RegistryIndexClient(PathBuf::from(owner.path()),
                                                      config,
                                                      lito::registry::RegistryNetworkPolicy::Online,
                                                      fixture.transport());
    ASSERT_TRUE(online.load(registry_package("sample"_str)).is_ok());
    fixture.body     = String::make(solver_sample_fixture);
    auto rolled_back = online.load(registry_package("sample"_str));
    ASSERT_TRUE(rolled_back.is_err());
    EXPECT_EQ(rolled_back.unwrap_err().kind, lito::registry::RegistryIndexErrorKind::Rollback);

    auto registry_key = lito::crypto::sha256_hex("https://registry.example/"_str);
    auto record       = PathBuf::from(owner.path())
                            .join(PathBuf::from("indices"_str).as_path())
                            .join(PathBuf::from(registry_key).as_path())
                            .join(PathBuf::from("sample"_str).as_path())
                            .join(PathBuf::from("record.json"_str).as_path());
    ASSERT_TRUE(rstd::fs::write_atomic(record.as_path(), "broken"_str.as_bytes()).is_ok());
    auto offline =
        lito::registry::RegistryIndexClient(PathBuf::from(owner.path()),
                                            config,
                                            lito::registry::RegistryNetworkPolicy::Offline,
                                            lito::registry::RegistryHttpTransport {});
    auto corrupted = offline.load(registry_package("sample"_str));
    ASSERT_TRUE(corrupted.is_err());
    EXPECT_EQ(corrupted.unwrap_err().kind, lito::registry::RegistryIndexErrorKind::CorruptCache);
}

TEST(RegistryBlobCache, SharesVerifiedContentAcrossTransportLocations) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto owner = rstd::move(temporary).unwrap();
    auto bytes = String::make("fixture tar zstd bytes"_str);
    auto blob  = lito::registry::RegistryBlobProjection {
        .digest =
            lito::registry::BlobDigest(lito::crypto::sha256_digest(bytes.as_str().as_bytes())),
        .size   = lito::registry::RegistryBlobSize(as_cast<u64>(bytes.len())),
        .format = lito::registry::RegistryArchiveFormat::parse(
                      lito::registry::RegistryArchiveFormat::TAR_ZSTD_V1)
                      .unwrap(),
    };
    auto fixture = BlobTransportFixture { .bytes = bytes.clone() };
    auto primary = lito::registry::RegistryBlobCache(
        PathBuf::from(owner.path()),
        registry_endpoint("https://primary.example/blobs/{sha256}.tar.zst"_str,
                          lito::registry::RegistryEndpointKind::Blob),
        lito::registry::RegistryNetworkPolicy::Online,
        fixture.transport());
    auto first = primary.acquire(registry_package("sample"_str), blob);
    ASSERT_TRUE(first.is_ok());
    EXPECT_EQ(first->size, as_cast<u64>(bytes.len()));
    EXPECT_EQ(fixture.calls, usize(1));

    auto mirror = lito::registry::RegistryBlobCache(
        PathBuf::from(owner.path()),
        registry_endpoint("https://mirror.example/content/{sha256}"_str,
                          lito::registry::RegistryEndpointKind::Blob),
        lito::registry::RegistryNetworkPolicy::Offline,
        lito::registry::RegistryBlobTransport {});
    auto reused = mirror.acquire(registry_package("sample"_str), blob);
    ASSERT_TRUE(reused.is_ok());
    EXPECT_EQ(reused->path.as_path(), first->path.as_path());
    EXPECT_EQ(fixture.calls, usize(1));
}

TEST(RegistrySourceResolver, MaterializesAReadOnlyCatalogAndReusesVerifiedCaches) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto owner = rstd::move(temporary).unwrap();
    auto tree  = lito::source::SourceTree::make();
    ASSERT_TRUE(tree.add_text("lito.toml"_str,
                              R"toml([package]
name = "sample"
version = "1.2.3"

[lib]
name = "sample"
module = "sample"
archive = "sample"
)toml"_str)
                    .is_ok());
    ASSERT_TRUE(tree.add_text("src/lib.cppm"_str, "export module sample;\n"_str).is_ok());
    auto package = registry_package("sample"_str);
    auto version = registry_version("1.2.3"_str);
    auto archive = PathBuf::from(owner.path()).join(PathBuf::from("source.tar.zstd"_str).as_path());
    auto built =
        lito::registry::PackageArchiveBuilder::build(tree, package, version, archive.clone());
    ASSERT_TRUE(built.is_ok());
    auto release = lito::registry::RegistryReleaseProjection {
        .version = version.clone(),
        .release =
            lito::registry::ReleaseDigest::parse(
                "sha256:2222222222222222222222222222222222222222222222222222222222222222"_str)
                .unwrap(),
        .source       = built->candidate.source_digest.clone(),
        .manifest     = built->candidate.manifest_digest.clone(),
        .blob         = built->blob.clone(),
        .dependencies = {},
        .published_at =
            lito::registry::RegistryTimestamp::parse("2026-08-23T12:34:56Z"_str).unwrap(),
    };
    auto cache   = PathBuf::from(owner.path()).join(PathBuf::from("cache"_str).as_path());
    auto fixture = CopyBlobTransportFixture { .source = archive.clone() };
    auto online  = lito::registry::RegistrySourceResolver(
        cache.clone(),
        registry_endpoint("https://primary.example/blobs/{sha256}.tar.zst"_str,
                          lito::registry::RegistryEndpointKind::Blob),
        lito::registry::RegistryNetworkPolicy::Online,
        fixture.transport());
    auto first = online.materialize(package, release);
    ASSERT_TRUE(first.is_ok());
    EXPECT_EQ(first->catalog.names().len(), usize(1));
    EXPECT_EQ(first->catalog.names()[usize {}].as_str(), "sample"_str);
    EXPECT_EQ(first->catalog.root(), first->source.root_directory.as_path());
    auto source_file =
        first->source.root_directory.join(PathBuf::from("src/lib.cppm"_str).as_path());
    auto metadata = rstd::fs::metadata(source_file.as_path());
    ASSERT_TRUE(metadata.is_ok());
    EXPECT_TRUE(metadata->permissions().readonly());
    EXPECT_EQ(fixture.calls, usize(1));

    auto offline = lito::registry::RegistrySourceResolver(
        rstd::move(cache),
        registry_endpoint("https://mirror.example/content/{sha256}"_str,
                          lito::registry::RegistryEndpointKind::Blob),
        lito::registry::RegistryNetworkPolicy::Offline,
        lito::registry::RegistryBlobTransport {});
    auto reused = offline.materialize(package, release);
    ASSERT_TRUE(reused.is_ok());
    EXPECT_EQ(reused->source.root_directory.as_path(), first->source.root_directory.as_path());
    EXPECT_EQ(fixture.calls, usize(1));
}

TEST(RegistryPackageGraph, SolvesAllDiscoveredRequirementsBeforeMaterialization) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto owner   = rstd::move(temporary).unwrap();
    auto root    = PathBuf::from(owner.path()).join(PathBuf::from("project"_str).as_path());
    auto project = lito::source::SourceTree::make();
    ASSERT_TRUE(project
                    .add_text("lito.toml"_str,
                              R"toml([package]
name = "app"
version = "0.1.0"

[lib]
name = "app"
module = "app"
archive = "app"

[dependencies.helper]
path = "helper"

[dependencies.sample]
version = ">=1, <3"
)toml"_str)
                    .is_ok());
    ASSERT_TRUE(project.add_text("src/lib.cppm"_str, "export module app;\n"_str).is_ok());
    ASSERT_TRUE(project
                    .add_text("helper/lito.toml"_str,
                              R"toml([package]
name = "helper"
version = "0.1.0"

[lib]
name = "helper"
module = "helper"
archive = "helper"

[dependencies.sample]
version = "<2"
)toml"_str)
                    .is_ok());
    ASSERT_TRUE(project.add_text("helper/src/lib.cppm"_str, "export module helper;\n"_str).is_ok());
    ASSERT_TRUE(lito::source::materialize_source_tree(project, root.as_path()).is_ok());

    auto registry_root =
        PathBuf::from(owner.path()).join(PathBuf::from("registry-source"_str).as_path());
    auto registry_tree = lito::source::SourceTree::make();
    ASSERT_TRUE(registry_tree
                    .add_text("lito.toml"_str,
                              R"toml([package]
name = "sample"
version = "1.5.0"

[lib]
name = "sample"
module = "sample"
archive = "sample"
)toml"_str)
                    .is_ok());
    ASSERT_TRUE(registry_tree.add_text("src/lib.cppm"_str, "export module sample;\n"_str).is_ok());
    ASSERT_TRUE(
        lito::source::materialize_source_tree(registry_tree, registry_root.as_path()).is_ok());
    auto document = lito::manifest::load_manifest_document(registry_root.as_path());
    ASSERT_TRUE(document.is_ok());
    ASSERT_TRUE(document->package.is_some());
    auto catalog =
        lito::workspace::WorkspaceCatalog::single(rstd::move(document).unwrap().package.unwrap());
    ASSERT_TRUE(catalog.is_ok());

    auto release = lito::registry::RegistryReleaseProjection {
        .version = registry_version("1.5.0"_str),
        .release =
            lito::registry::ReleaseDigest::parse(
                "sha256:1111111111111111111111111111111111111111111111111111111111111111"_str)
                .unwrap(),
        .source = lito::registry::SourceDigest::parse(
                      "sha256:2222222222222222222222222222222222222222222222222222222222222222"_str)
                      .unwrap(),
        .manifest =
            lito::registry::ManifestDigest::parse(
                "sha256:3333333333333333333333333333333333333333333333333333333333333333"_str)
                .unwrap(),
        .blob =
            lito::registry::RegistryBlobProjection {
                .digest =
                    lito::registry::BlobDigest::parse(
                        "sha256:4444444444444444444444444444444444444444444444444444444444444444"_str)
                        .unwrap(),
                .size   = lito::registry::RegistryBlobSize(u64(42)),
                .format = lito::registry::RegistryArchiveFormat::parse(
                              lito::registry::RegistryArchiveFormat::TAR_ZSTD_V1)
                              .unwrap(),
            },
        .dependencies = {},
        .published_at =
            lito::registry::RegistryTimestamp::parse("2026-08-23T12:34:56Z"_str).unwrap(),
    };
    auto fixture = RegistryGraphFixture {
        .selected = Some(lito::registry::ResolvedRegistryGraphSource {
            .package = registry_package("sample"_str),
            .release = release.clone(),
            .source =
                lito::source::ResolvedPackageSource {
                    .identity = String::make("registry+https://registry.example/sample@1.5.0"_str),
                    .kind     = lito::source::PackageSourceKind::Registry,
                    .root_directory   = registry_root.clone(),
                    .registry_package = Some(registry_package("sample"_str)),
                    .registry_version = Some(registry_version("1.5.0"_str)),
                    .release_digest   = Some(release.release.clone()),
                    .source_digest    = Some(release.source.clone()),
                    .manifest_digest  = Some(release.manifest.clone()),
                    .blob_digest      = Some(release.blob.digest.clone()),
                    .blob_size        = Some(release.blob.size.clone()),
                    .archive_format   = Some(release.blob.format.clone()),
                },
            .catalog = rstd::move(catalog).unwrap(),
        }),
    };
    auto environment =
        lito::system::ResolvedProcessEnvironment::resolve(lito::system::ProcessEnvironmentSpec {});
    ASSERT_TRUE(environment.is_ok());
    auto tools = lito::tools::ToolResolver(*environment);
    auto graph = lito::package::resolve_package_graph_with_environment(
        root.as_path(),
        lito::source::SourceResolutionOptions {},
        tools,
        *environment,
        usize(1),
        {},
        None(),
        fixture.provider());
    ASSERT_TRUE(graph.is_ok());
    EXPECT_EQ(fixture.calls, usize(1));
    EXPECT_EQ(fixture.requirements, usize(2));
    EXPECT_TRUE(fixture.accepted);
    ASSERT_EQ(graph->packages.len(), usize(3));
    EXPECT_EQ(graph->packages[usize(2)].manifest.name.as_str(), "sample"_str);
    EXPECT_EQ(graph->packages[usize(2)].source.identity.as_str(),
              "registry+https://registry.example/sample@1.5.0"_str);
}

TEST(RegistrySourcePath, PreservesUtf8SpellingWithoutUnicodeCollisionRules) {
    EXPECT_TRUE(lito::source::SourcePath::parse("caf\u00e9.cpp"_str).is_ok());
    EXPECT_TRUE(lito::source::SourcePath::parse("cafe\u0301.cpp"_str).is_ok());
    EXPECT_TRUE(lito::source::SourcePath::parse("CON.txt"_str).is_err());
    EXPECT_TRUE(lito::source::SourcePath::parse("trailing. "_str).is_err());
    EXPECT_TRUE(lito::source::SourcePath::parse("control\u0085.cpp"_str).is_err());
    EXPECT_TRUE(lito::source::SourcePath::parse("noncharacter\ufdd0.cpp"_str).is_err());

    auto ascii = lito::source::SourceTree::make();
    ASSERT_TRUE(ascii.add_text("Src/File.cpp"_str, "first"_str).is_ok());
    EXPECT_TRUE(ascii.add_text("src/file.cpp"_str, "second"_str).is_ok());
    EXPECT_TRUE(ascii.add_text("Src/File.cpp"_str, "duplicate"_str).is_err());

    auto unicode = lito::source::SourceTree::make();
    ASSERT_TRUE(unicode.add_text("stra\u00dfe.cpp"_str, "first"_str).is_ok());
    EXPECT_TRUE(unicode.add_text("STRASSE.cpp"_str, "second"_str).is_ok());

    auto composed = lito::source::SourceTree::make();
    ASSERT_TRUE(composed.add_text("caf\u00e9.cpp"_str, "contents"_str).is_ok());
    auto decomposed = lito::source::SourceTree::make();
    ASSERT_TRUE(decomposed.add_text("cafe\u0301.cpp"_str, "contents"_str).is_ok());
    EXPECT_NE(lito::source::canonical_source_digest(composed).unwrap().text(),
              lito::source::canonical_source_digest(decomposed).unwrap().text());
}

TEST(RegistryArchive, BuildsAndInspectsDeterministicTarZstd) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto owner = rstd::move(temporary).unwrap();
    auto tree  = lito::source::SourceTree::make();
    ASSERT_TRUE(tree.add_text("lito.toml"_str,
                              R"toml([package]
name = "sample"
version = "1.2.3"

[lib]
name = "sample"
module = "sample"
archive = "sample"
)toml"_str)
                    .is_ok());
    ASSERT_TRUE(tree.add_text("src/lib.cppm"_str,
                              "export module sample;\n"_str,
                              lito::source::SourceFileMode::Regular)
                    .is_ok());
    ASSERT_TRUE(tree.add_text("tools/generate"_str,
                              "#!/bin/sh\n"_str,
                              lito::source::SourceFileMode::Executable)
                    .is_ok());
    auto package = registry_package("sample"_str);
    auto version = lito::registry::SemanticVersion::parse("1.2.3"_str).unwrap();
    auto first_path =
        PathBuf::from(owner.path()).join(PathBuf::from("first.tar.zst"_str).as_path());
    auto second_path =
        PathBuf::from(owner.path()).join(PathBuf::from("second.tar.zst"_str).as_path());
    auto first =
        lito::registry::PackageArchiveBuilder::build(tree, package, version, first_path.clone());
    if (first.is_err()) rstd::io::eprintln("{}", first.as_ref().unwrap_err().message);
    ASSERT_TRUE(first.is_ok());
    EXPECT_EQ(first->candidate.file_count, usize(3));
    EXPECT_EQ(first->candidate.package.name.as_str(), "sample"_str);

    auto second =
        lito::registry::PackageArchiveBuilder::build(tree, package, version, second_path.clone());
    ASSERT_TRUE(second.is_ok());
    EXPECT_EQ(first->blob.digest.text(), second->blob.digest.text());
    EXPECT_EQ(first->candidate.source_digest.text(), second->candidate.source_digest.text());
    EXPECT_EQ(first->candidate.manifest_digest.text(), second->candidate.manifest_digest.text());

    auto protocol = lito::registry::serialize_verified_publish_candidate(*first);
    auto json     = rstd::json::from_str(protocol.as_str()).unwrap();
    EXPECT_EQ(json["schema"_str].as_str().unwrap(),
              lito::registry::REGISTRY_INSPECTION_CANDIDATE_SCHEMA);
    EXPECT_EQ(json["protocol"_str].as_str().unwrap(), lito::registry::REGISTRY_INSPECTION_PROTOCOL);
    EXPECT_EQ(json["receipt"_str].as_str().unwrap(), lito::registry::REGISTRY_INSPECTOR_RECEIPT);
    EXPECT_TRUE(json["unicode"_str].is_null());

    auto capabilities =
        rstd::json::from_str(lito::registry::registry_inspector_capabilities_json().as_str())
            .unwrap();
    EXPECT_EQ(capabilities["schema"_str].as_str().unwrap(),
              "lito.registry.inspector-capabilities.v2"_str);
    EXPECT_TRUE(capabilities["unicode"_str].is_null());
}
