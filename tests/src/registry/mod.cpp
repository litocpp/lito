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

constexpr auto package_index_fixture =
    R"json({"schema":"lito.registry.package-index.v1","registry":"https://registry.example/","package":"sample","releases":[{"version":"1.2.3","checksum":"1111111111111111111111111111111111111111111111111111111111111111","dependencies":[{"alias":"helper","registry":"https://registry.example/","package":"helper","requirement":"^1.0.0","kind":"normal","visibility":"public","features":["fast"],"default_features":true}],"yanked":false,"published_at":"2026-08-20T12:34:56Z"}]})json"_str;

constexpr auto changed_package_index_fixture =
    R"json({"schema":"lito.registry.package-index.v1","registry":"https://registry.example/","package":"sample","releases":[{"version":"1.2.3","checksum":"2222222222222222222222222222222222222222222222222222222222222222","dependencies":[],"yanked":false,"published_at":"2026-08-20T12:34:56Z"}]})json"_str;

constexpr auto solver_sample_fixture =
    R"json({"schema":"lito.registry.package-index.v1","registry":"https://registry.example/","package":"sample","releases":[{"version":"2.0.0","checksum":"6666666666666666666666666666666666666666666666666666666666666666","dependencies":[],"yanked":true,"published_at":"2026-08-20T12:34:56Z"},{"version":"1.5.0","checksum":"3333333333333333333333333333333333333333333333333333333333333333","dependencies":[{"alias":"helper","registry":"https://registry.example/","package":"helper","requirement":"^2.0.0","kind":"normal","visibility":"public","features":[],"default_features":true}],"yanked":false,"published_at":"2026-08-20T12:34:56Z"},{"version":"1.2.3","checksum":"9999999999999999999999999999999999999999999999999999999999999999","dependencies":[{"alias":"helper","registry":"https://registry.example/","package":"helper","requirement":"^1.0.0","kind":"normal","visibility":"public","features":[],"default_features":true}],"yanked":false,"published_at":"2026-08-20T12:34:56Z"}]})json"_str;

constexpr auto solver_helper_fixture =
    R"json({"schema":"lito.registry.package-index.v1","registry":"https://registry.example/","package":"helper","releases":[{"version":"2.0.0","checksum":"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc","dependencies":[],"yanked":false,"published_at":"2026-08-20T12:34:56Z"},{"version":"1.0.0","checksum":"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff","dependencies":[],"yanked":false,"published_at":"2026-08-20T12:34:56Z"}]})json"_str;

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

auto registry_version(ref<str> value) -> lito::registry::SemanticVersion {
    return rstd::move(lito::registry::SemanticVersion::parse(value)).unwrap();
}

auto package_checksum(ref<str> value) -> lito::registry::PackageChecksum {
    return rstd::move(lito::registry::PackageChecksum::parse(value)).unwrap();
}

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
                .index = registry_endpoint("https://registry.example/v1/index/{package}.json"_str,
                                           lito::registry::RegistryEndpointKind::Index),
                .blob  = registry_endpoint(
                    "https://registry.example/v1/blobs/sha256/{checksum}.tar.zst"_str,
                    lito::registry::RegistryEndpointKind::Blob),
            },
        .api = registry_fixed_endpoint("https://registry.example/"_str),
    };
}

struct SolverFixtureProvider {
    usize loads {};

    static auto load(void* context, const lito::registry::RegistryPackageId& package) noexcept
        -> lito::registry::RegistryIndexLoadResult {
        auto& self = *static_cast<SolverFixtureProvider*>(context);
        ++self.loads;
        auto fixture = package.name.as_str() == "sample"_str   ? solver_sample_fixture
                       : package.name.as_str() == "helper"_str ? solver_helper_fixture
                                                               : ""_str;
        if (fixture.is_empty()) {
            return Err(lito::registry::RegistryIndexError {
                .kind    = lito::registry::RegistryIndexErrorKind::NotFound,
                .package = package.clone(),
                .message = String::make("fixture is missing"_str),
            });
        }
        auto parsed = lito::registry::parse_package_index(fixture.as_bytes(), package);
        if (parsed.is_err()) {
            return Err(lito::registry::RegistryIndexError {
                .kind    = lito::registry::RegistryIndexErrorKind::Schema,
                .package = package.clone(),
                .message = rstd::format("{}", rstd::move(parsed).unwrap_err()),
            });
        }
        return Ok(rstd::move(parsed).unwrap());
    }

    auto provider() noexcept -> lito::registry::RegistryIndexProvider {
        return lito::registry::RegistryIndexProvider {
            .context = this,
            .load    = load,
        };
    }
};

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
    bool                                             api_authorized { true };
    bool                                             upload_uses_archive {};

    static auto execute(void*                                             context,
                        const lito::registry::RegistryPackageId&          package,
                        const lito::registry::RegistryPublishHttpRequest& request) noexcept
        -> lito::registry::RegistryPublishResult<lito::registry::RegistryPublishHttpResponse> {
        auto& self = *static_cast<PublishTransportFixture*>(context);
        self.methods.push(request.method.clone());
        auto authorized = false;
        for (const auto& header : request.headers) {
            if (header.name.as_str() == "Authorization"_str &&
                header.value.as_str() == "Bearer fixture-token"_str) {
                authorized = true;
            }
        }
        if (request.upload.is_some())
            self.upload_uses_archive = true;
        else if (! authorized)
            self.api_authorized = false;
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
    constexpr auto checksum =
        "1111111111111111111111111111111111111111111111111111111111111111"_str;
    auto prefix =
        prepare
            ? R"json({"schema":"lito.registry.prepare-publish-session.v1","outcome":"created",)json"_str
            : "{"_str;
    auto suffix = String::make();
    if (commit) {
        suffix.push_str(
            R"json(,"commit":{"sequence":"4","package_revision":"2","checksum":"1111111111111111111111111111111111111111111111111111111111111111"})json"_str);
    }
    if (upload) {
        suffix.push_str(
            R"json(,"upload":{"method":"PUT","url":"https://uploads.example/staging/session-1","headers":{"content-type":"application/zstd"},"expires_at":"2026-08-23T12:00:00Z"})json"_str);
    }
    return rstd::format(
        R"json({}"id":"session-1","state":"{}","registry":"https://registry.example/","package":"{}","version":"1.2.3","archive":{{"checksum":"{}","size":"42","format":"lito.package.tar-zstd.v1"}}{}}})json",
        prefix,
        state,
        package,
        checksum,
        suffix);
}

auto publish_request(ref<rstd::path::Path> archive, const lito::config::RegistryBearerToken& token)
    -> lito::registry::RegistryPublishRequest {
    return lito::registry::RegistryPublishRequest {
        .api     = registry_fixed_endpoint("https://api.registry.example/"_str),
        .token   = rstd::addressof(token),
        .package = registry_package("sample"_str),
        .version = registry_version("1.2.3"_str),
        .artifact =
            lito::registry::RegistryPackageArchive {
                .checksum = package_checksum(
                    "1111111111111111111111111111111111111111111111111111111111111111"_str),
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

    auto name = lito::registry::RegistryPackageName::parse("lito_core-2"_str);
    ASSERT_TRUE(name.is_ok());
    EXPECT_EQ(name->collision_key().as_str(), "lito-core-2"_str);
    EXPECT_TRUE(lito::registry::RegistryPackageName::parse("Lito"_str).is_err());
    EXPECT_TRUE(lito::registry::RegistryPackageName::parse("-lito"_str).is_err());
    EXPECT_TRUE(lito::registry::RegistryPackageName::parse("lito/codec"_str).is_err());
}

TEST(RegistryPackageSpec, SeparatesVersionRequirementsFromCliOnlyTags) {
    auto implicit = lito::registry::RegistryPackageSpec::parse("sample"_str);
    ASSERT_TRUE(implicit.is_ok());
    ASSERT_TRUE(implicit->selector.is_Requirement());

    auto requirement = lito::registry::RegistryPackageSpec::parse("sample@0.4"_str);
    ASSERT_TRUE(requirement.is_ok());
    EXPECT_TRUE(
        requirement->selector.as_Requirement().requirement.matches(registry_version("0.4.7"_str)));

    auto tag = lito::registry::RegistryPackageSpec::parse("sample@latest"_str);
    ASSERT_TRUE(tag.is_ok());
    ASSERT_TRUE(tag->selector.is_NamedTag());
    EXPECT_EQ(tag->selector.as_NamedTag().tag.as_str(), "latest"_str);
}

TEST(RegistryChecksum, AcceptsOnlyRawLowercaseSha256) {
    constexpr auto value = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"_str;
    auto           checksum = lito::registry::PackageChecksum::parse(value);
    ASSERT_TRUE(checksum.is_ok());
    EXPECT_EQ(checksum->text().as_str(), value);
    EXPECT_TRUE(lito::registry::PackageChecksum::parse(
                    "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"_str)
                    .is_err());
    EXPECT_TRUE(lito::registry::PackageChecksum::parse(
                    "0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef"_str)
                    .is_err());
}

TEST(RegistryPublish, UploadsCommitsAndWaitsForVisibleContext) {
    auto fixture = PublishTransportFixture {};
    fixture.responses.push(lito::registry::RegistryPublishHttpResponse {
        .status = u16(201),
        .body   = publish_session_json("prepared"_str, true, true, false),
    });
    fixture.responses.push(lito::registry::RegistryPublishHttpResponse { .status = u16(200) });
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
    auto result  = lito::registry::RegistryPublishClient(fixture.transport()).publish(request);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result->state, lito::registry::RegistryPublishState::Visible);
    ASSERT_TRUE(result->checksum.is_some());
    ASSERT_EQ(fixture.methods.len(), usize(4));
    EXPECT_EQ(fixture.methods[usize {}].as_str(), "POST"_str);
    EXPECT_EQ(fixture.methods[usize(1)].as_str(), "PUT"_str);
    EXPECT_TRUE(fixture.api_authorized);
    EXPECT_TRUE(fixture.upload_uses_archive);
}

TEST(RegistryPublish, RejectsResponseContextMismatchBeforeUpload) {
    auto fixture = PublishTransportFixture {};
    fixture.responses.push(lito::registry::RegistryPublishHttpResponse {
        .status = u16(201),
        .body   = publish_session_json("prepared"_str, true, true, false, "other"_str),
    });
    auto token   = lito::config::RegistryBearerToken(String::make("fixture-token"_str));
    auto request = publish_request(PathBuf::from("fixture.tar.zst"_str).as_path(), token);
    auto result  = lito::registry::RegistryPublishClient(fixture.transport()).publish(request);
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.unwrap_err().kind, lito::registry::RegistryPublishErrorKind::Protocol);
    EXPECT_FALSE(fixture.upload_uses_archive);
}

TEST(RegistryVersion, ParsesOrdersAndMatchesRequirements) {
    auto stable = lito::registry::SemanticVersion::parse("1.2.3"_str);
    ASSERT_TRUE(stable.is_ok());
    EXPECT_TRUE(lito::registry::SemanticVersion::parse("v1.2.3"_str).is_err());
    EXPECT_TRUE(lito::registry::SemanticVersion::parse("1.2"_str).is_err());
    EXPECT_TRUE(registry_version("1.0.0-alpha"_str) < registry_version("1.0.0"_str));

    auto requirement = lito::registry::VersionRequirement::parse(">=1.2, <2"_str);
    ASSERT_TRUE(requirement.is_ok());
    EXPECT_TRUE(requirement->matches(registry_version("1.9.0"_str)));
    EXPECT_FALSE(requirement->matches(registry_version("2.0.0"_str)));
}

TEST(RegistryMetadata, ParsesTrustedRawIndexAndChecksRequestContext) {
    auto expected = registry_package("sample"_str);
    auto index    = lito::registry::parse_package_index(package_index_fixture.as_bytes(), expected);
    ASSERT_TRUE(index.is_ok());
    ASSERT_EQ(index->releases().len(), usize(1));
    const auto& release = index->releases()[usize {}];
    EXPECT_EQ(release.version.text().as_str(), "1.2.3"_str);
    EXPECT_EQ(release.checksum.text().as_str(),
              "1111111111111111111111111111111111111111111111111111111111111111"_str);
    ASSERT_EQ(release.dependencies.len(), usize(1));
    EXPECT_EQ(release.dependencies[usize {}].alias.as_str(), "helper"_str);

    EXPECT_TRUE(lito::registry::parse_package_index(package_index_fixture.as_bytes(),
                                                    registry_package("other"_str))
                    .is_err());
    EXPECT_TRUE(lito::registry::parse_package_index(
                    R"json({"signed":{},"signatures":[]})json"_str.as_bytes(), expected)
                    .is_err());
}

TEST(RegistrySolver, BacktracksAndUnifiesEachPackageToOneVersion) {
    auto fixture = SolverFixtureProvider {};
    auto roots   = Vec<lito::registry::RegistrySolverRequirement>::make();
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
        lito::registry::RegistrySolverInput { .roots = rstd::move(roots) }, fixture.provider());
    ASSERT_TRUE(solved.is_ok());
    ASSERT_EQ(solved->packages.len(), usize(2));
    EXPECT_EQ(solved->packages[usize {}].release.version.text().as_str(), "1.0.0"_str);
    EXPECT_EQ(solved->packages[usize(1)].release.version.text().as_str(), "1.2.3"_str);
}

TEST(RegistrySolver, RejectsTheSameNameFromDifferentRegistries) {
    auto fixture = SolverFixtureProvider {};
    auto roots   = Vec<lito::registry::RegistrySolverRequirement>::make();
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
        lito::registry::RegistrySolverInput { .roots = rstd::move(roots) }, fixture.provider());
    ASSERT_TRUE(solved.is_err());
    EXPECT_TRUE(solved.unwrap_err().is_SourceConflict());
    EXPECT_EQ(fixture.loads, usize {});
}

TEST(RegistrySolver, PreservesAnExactlyLockedYankedVersion) {
    auto fixture = SolverFixtureProvider {};
    auto roots   = Vec<lito::registry::RegistrySolverRequirement>::make();
    roots.push(lito::registry::RegistrySolverRequirement {
        .package     = registry_package("sample"_str),
        .requirement = lito::registry::VersionRequirement::parse(">=2.0.0"_str).unwrap(),
        .source      = String::make("workspace dependency 'sample'"_str),
    });
    auto locked = Vec<lito::registry::RegistryLockedPreference>::make();
    locked.push(lito::registry::RegistryLockedPreference {
        .package  = registry_package("sample"_str),
        .version  = registry_version("2.0.0"_str),
        .checksum = package_checksum(
            "6666666666666666666666666666666666666666666666666666666666666666"_str),
    });
    auto solved = lito::registry::RegistryVersionSolver::solve(
        lito::registry::RegistrySolverInput {
            .roots  = rstd::move(roots),
            .locked = rstd::move(locked),
        },
        fixture.provider());
    ASSERT_TRUE(solved.is_ok());
    EXPECT_EQ(solved->packages[usize {}].release.version.text().as_str(), "2.0.0"_str);
}

TEST(RegistrySolver, RejectsChecksumChangeForLockedVersion) {
    auto fixture = SolverFixtureProvider {};
    auto roots   = Vec<lito::registry::RegistrySolverRequirement>::make();
    roots.push(lito::registry::RegistrySolverRequirement {
        .package     = registry_package("sample"_str),
        .requirement = lito::registry::VersionRequirement::parse("=2.0.0"_str).unwrap(),
        .source      = String::make("workspace dependency 'sample'"_str),
    });
    auto locked = Vec<lito::registry::RegistryLockedPreference>::make();
    locked.push(lito::registry::RegistryLockedPreference {
        .package  = registry_package("sample"_str),
        .version  = registry_version("2.0.0"_str),
        .checksum = package_checksum(
            "5555555555555555555555555555555555555555555555555555555555555555"_str),
    });
    auto solved = lito::registry::RegistryVersionSolver::solve(
        lito::registry::RegistrySolverInput {
            .roots  = rstd::move(roots),
            .locked = rstd::move(locked),
        },
        fixture.provider());
    ASSERT_TRUE(solved.is_err());
    auto error = rstd::move(solved).unwrap_err();
    ASSERT_TRUE(error.is_Provider());
    EXPECT_EQ(error.as_Provider().error.kind, lito::registry::RegistryIndexErrorKind::Integrity);
}

TEST(RegistryIndexCache, RevalidatesAndSupportsStrictOfflineReads) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto owner   = rstd::move(temporary).unwrap();
    auto config  = registry_test_config();
    auto fixture = IndexHttpFixture { .body = String::make(package_index_fixture) };
    auto online = lito::registry::RegistryIndexClient(PathBuf::from(owner.path()),
                                                      config,
                                                      lito::registry::RegistryNetworkPolicy::Online,
                                                      fixture.transport());
    auto first  = online.load(registry_package("sample"_str));
    ASSERT_TRUE(first.is_ok());
    EXPECT_EQ(fixture.calls, usize(1));
    EXPECT_FALSE(fixture.saw_condition);

    fixture.not_modified = true;
    ASSERT_TRUE(online.load(registry_package("sample"_str)).is_ok());
    EXPECT_TRUE(fixture.saw_condition);

    auto offline =
        lito::registry::RegistryIndexClient(PathBuf::from(owner.path()),
                                            config,
                                            lito::registry::RegistryNetworkPolicy::Offline,
                                            lito::registry::RegistryHttpTransport {});
    auto cached = offline.load(registry_package("sample"_str));
    ASSERT_TRUE(cached.is_ok());
    EXPECT_EQ(cached->releases()[usize {}].checksum.text().as_str(),
              "1111111111111111111111111111111111111111111111111111111111111111"_str);
}

TEST(RegistryIndexCache, RejectsChangedChecksumAndCorruptCache) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto owner   = rstd::move(temporary).unwrap();
    auto config  = registry_test_config();
    auto fixture = IndexHttpFixture { .body = String::make(package_index_fixture) };
    auto online = lito::registry::RegistryIndexClient(PathBuf::from(owner.path()),
                                                      config,
                                                      lito::registry::RegistryNetworkPolicy::Online,
                                                      fixture.transport());
    ASSERT_TRUE(online.load(registry_package("sample"_str)).is_ok());
    fixture.body = String::make(changed_package_index_fixture);
    auto changed = online.load(registry_package("sample"_str));
    ASSERT_TRUE(changed.is_err());
    EXPECT_EQ(changed.unwrap_err().kind, lito::registry::RegistryIndexErrorKind::Integrity);

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

TEST(RegistryBlobCache, VerifiesNewBytesAndSharesCompletedContent) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto owner = rstd::move(temporary).unwrap();
    auto bytes = String::make("fixture tar zstd bytes"_str);
    auto checksum =
        lito::registry::PackageChecksum(lito::crypto::sha256_digest(bytes.as_str().as_bytes()));
    auto fixture = BlobTransportFixture { .bytes = bytes.clone() };
    auto primary = lito::registry::RegistryBlobCache(
        PathBuf::from(owner.path()),
        registry_endpoint("https://primary.example/blobs/{checksum}.tar.zst"_str,
                          lito::registry::RegistryEndpointKind::Blob),
        lito::registry::RegistryNetworkPolicy::Online,
        fixture.transport());
    auto first = primary.acquire(registry_package("sample"_str), checksum);
    ASSERT_TRUE(first.is_ok());
    EXPECT_EQ(first->size, as_cast<u64>(bytes.len()));
    EXPECT_EQ(fixture.calls, usize(1));

    auto mirror = lito::registry::RegistryBlobCache(
        PathBuf::from(owner.path()),
        registry_endpoint("https://mirror.example/content/{checksum}"_str,
                          lito::registry::RegistryEndpointKind::Blob),
        lito::registry::RegistryNetworkPolicy::Offline,
        lito::registry::RegistryBlobTransport {});
    auto reused = mirror.acquire(registry_package("sample"_str), checksum);
    ASSERT_TRUE(reused.is_ok());
    EXPECT_EQ(reused->path.as_path(), first->path.as_path());
    EXPECT_EQ(fixture.calls, usize(1));
}

TEST(RegistryBlobCache, RejectsDownloadedBytesWithTheWrongChecksum) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto owner   = rstd::move(temporary).unwrap();
    auto fixture = BlobTransportFixture { .bytes = String::make("wrong bytes"_str) };
    auto cache   = lito::registry::RegistryBlobCache(
        PathBuf::from(owner.path()),
        registry_endpoint("https://primary.example/blobs/{checksum}.tar.zst"_str,
                          lito::registry::RegistryEndpointKind::Blob),
        lito::registry::RegistryNetworkPolicy::Online,
        fixture.transport());
    auto acquired = cache.acquire(
        registry_package("sample"_str),
        package_checksum("1111111111111111111111111111111111111111111111111111111111111111"_str));
    ASSERT_TRUE(acquired.is_err());
    EXPECT_EQ(acquired.unwrap_err().kind, lito::registry::RegistryArtifactErrorKind::Digest);
}

TEST(RegistryBlobCache, VerifiesExternalSourceBundleBytes) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto owner = rstd::move(temporary).unwrap();
    auto checksum =
        package_checksum("1111111111111111111111111111111111111111111111111111111111111111"_str);
    auto bundle_root = PathBuf::from(owner.path()).join(PathBuf::from("bundle"_str).as_path());
    auto bundled = lito::source::SourceBundleLayout(bundle_root.clone()).registry_package(checksum);
    ASSERT_TRUE(rstd::fs::create_dir_all(bundled.as_path().parent().unwrap()).is_ok());
    ASSERT_TRUE(rstd::fs::write(bundled.as_path(), "wrong bytes"_str.as_bytes()).is_ok());
    auto bundles = Vec<PathBuf>::make();
    bundles.push(rstd::move(bundle_root));
    auto cache = lito::registry::RegistryBlobCache(
        PathBuf::from(owner.path()).join(PathBuf::from("cache"_str).as_path()),
        registry_endpoint("https://primary.example/blobs/{checksum}.tar.zst"_str,
                          lito::registry::RegistryEndpointKind::Blob),
        lito::registry::RegistryNetworkPolicy::Offline,
        lito::registry::RegistryBlobTransport {},
        rstd::addressof(bundles));
    auto acquired = cache.acquire(registry_package("sample"_str), checksum);
    ASSERT_TRUE(acquired.is_err());
    EXPECT_EQ(acquired.unwrap_err().kind, lito::registry::RegistryArtifactErrorKind::Digest);
}

TEST(RegistrySourceResolver, MaterializesAReadOnlyCatalogAndReusesCaches) {
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
    auto cache   = PathBuf::from(owner.path()).join(PathBuf::from("cache"_str).as_path());
    auto fixture = CopyBlobTransportFixture { .source = archive.clone() };
    auto online  = lito::registry::RegistrySourceResolver(
        cache.clone(),
        registry_endpoint("https://primary.example/blobs/{checksum}.tar.zst"_str,
                          lito::registry::RegistryEndpointKind::Blob),
        lito::registry::RegistryNetworkPolicy::Online,
        fixture.transport());
    auto first = online.materialize(package, version, built->archive.checksum);
    ASSERT_TRUE(first.is_ok());
    EXPECT_EQ(first->catalog.names()[usize {}].as_str(), "sample"_str);
    auto source_file =
        first->source.root_directory.join(PathBuf::from("src/lib.cppm"_str).as_path());
    auto metadata = rstd::fs::metadata(source_file.as_path());
    ASSERT_TRUE(metadata.is_ok());
    EXPECT_TRUE(metadata->permissions().readonly());
    EXPECT_EQ(fixture.calls, usize(1));

    auto offline = lito::registry::RegistrySourceResolver(
        rstd::move(cache),
        registry_endpoint("https://mirror.example/content/{checksum}"_str,
                          lito::registry::RegistryEndpointKind::Blob),
        lito::registry::RegistryNetworkPolicy::Offline,
        lito::registry::RegistryBlobTransport {});
    auto reused = offline.materialize(package, version, built->archive.checksum);
    ASSERT_TRUE(reused.is_ok());
    EXPECT_EQ(reused->source.root_directory.as_path(), first->source.root_directory.as_path());
    EXPECT_EQ(fixture.calls, usize(1));
}

TEST(RegistryGraphClient, LockedResolutionUsesOnlyLockAndBlob) {
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

    auto registries = Vec<lito::config::NamedRegistryConfig>::make();
    registries.push(registry_test_config());
    auto bootstrap = lito::config::LitoBootstrapConfig(rstd::move(registries),
                                                       Some(String::make("fixture"_str)));
    auto pins      = Vec<lito::source::RegistrySourcePin>::make();
    pins.push(lito::source::RegistrySourcePin {
        .package  = package.clone(),
        .version  = version.clone(),
        .checksum = built->archive.checksum.clone(),
    });
    auto http   = IndexHttpFixture { .body = String::make("must not be read"_str) };
    auto blob   = CopyBlobTransportFixture { .source = archive.clone() };
    auto client = lito::registry::RegistryGraphClient(
        PathBuf::from(owner.path()).join(PathBuf::from("cache"_str).as_path()),
        bootstrap,
        lito::registry::RegistryNetworkPolicy::Online,
        http.transport(),
        blob.transport(),
        true,
        rstd::move(pins));
    auto requirements = Vec<lito::registry::RegistryGraphRequirement>::make();
    requirements.push(lito::registry::RegistryGraphRequirement {
        .package     = package.name.clone(),
        .requirement = lito::registry::VersionRequirement::parse("^1.0.0"_str).unwrap(),
        .source      = String::make("root dependency 'sample'"_str),
    });
    auto resolved = client.resolve(requirements.as_slice());
    ASSERT_TRUE(resolved.is_ok());
    ASSERT_EQ(resolved->len(), usize(1));
    EXPECT_EQ((*resolved)[usize {}].version.text().as_str(), "1.2.3"_str);
    EXPECT_EQ(http.calls, usize {});
    EXPECT_EQ(blob.calls, usize(1));
}

TEST(RegistrySourcePath, PreservesUtf8SpellingWithoutUnicodeCollisionRules) {
    EXPECT_TRUE(lito::source::SourcePath::parse("caf\u00e9.cpp"_str).is_ok());
    EXPECT_TRUE(lito::source::SourcePath::parse("cafe\u0301.cpp"_str).is_ok());
    EXPECT_TRUE(lito::source::SourcePath::parse("CON.txt"_str).is_err());
    EXPECT_TRUE(lito::source::SourcePath::parse("trailing. "_str).is_err());

    auto ascii = lito::source::SourceTree::make();
    ASSERT_TRUE(ascii.add_text("Src/File.cpp"_str, "first"_str).is_ok());
    EXPECT_TRUE(ascii.add_text("src/file.cpp"_str, "second"_str).is_ok());
    EXPECT_TRUE(ascii.add_text("Src/File.cpp"_str, "duplicate"_str).is_err());
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
    ASSERT_TRUE(tree.add_text("src/lib.cppm"_str, "export module sample;\n"_str).is_ok());
    ASSERT_TRUE(tree.add_text("tools/generate"_str,
                              "#!/bin/sh\n"_str,
                              lito::source::SourceFileMode::Executable)
                    .is_ok());
    auto package = registry_package("sample"_str);
    auto version = registry_version("1.2.3"_str);
    auto first_path =
        PathBuf::from(owner.path()).join(PathBuf::from("first.tar.zst"_str).as_path());
    auto second_path =
        PathBuf::from(owner.path()).join(PathBuf::from("second.tar.zst"_str).as_path());
    auto first =
        lito::registry::PackageArchiveBuilder::build(tree, package, version, first_path.clone());
    ASSERT_TRUE(first.is_ok());
    auto second =
        lito::registry::PackageArchiveBuilder::build(tree, package, version, second_path.clone());
    ASSERT_TRUE(second.is_ok());
    EXPECT_EQ(first->archive.checksum.text(), second->archive.checksum.text());
    EXPECT_EQ(first->candidate.file_count, usize(3));

    auto protocol = lito::registry::serialize_verified_publish_candidate(*first);
    auto json     = rstd::json::from_str(protocol.as_str()).unwrap();
    EXPECT_EQ(json["schema"_str].as_str().unwrap(),
              lito::registry::REGISTRY_INSPECTION_CANDIDATE_SCHEMA);
    EXPECT_EQ(json["archive"_str]["checksum"_str].as_str().unwrap(),
              first->archive.checksum.text().as_str());

    auto capabilities =
        rstd::json::from_str(lito::registry::registry_inspector_capabilities_json().as_str())
            .unwrap();
    EXPECT_EQ(capabilities["schema"_str].as_str().unwrap(),
              "lito.registry.inspector-capabilities.v2"_str);
}
