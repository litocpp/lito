module;
#include <rstd/macro.hpp>

module lito.core;

import rstd;
import rstd.json;
import rstd.serde;
import :lock;
import :lock.wire;
import :package.graph;
import :dependency.source;
import :manifest;
import :source.git;
import :source.requirement;
import :source.resolution;
import lito.system;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace lito::system;
using namespace rstd::literals;
using Json      = rstd::json::Value;
using StringSet = rstd::collections::BTreeMap<String, empty>;
using namespace lito::lock;

template<typename T>
auto lock_failure(String message) -> LockResult<T> {
    return Err(LockError::Schema(rstd::move(message)));
}

template<typename T>
auto lock_failure(ref<str> message) -> LockResult<T> {
    return Err(LockError::Schema(String::make(message)));
}

template<typename T>
auto lock_data_failure(rstd::serde::DataPath path, ref<str> message) -> LockResult<T> {
    return Err(LockError::Data(rstd::serde::Error::invalid_value(rstd::move(path), message)));
}

template<typename T, typename Source>
    requires Impled<rstd::mtp::rm_cvf<Source>, rstd::error::Error>
auto lock_data_failure(rstd::serde::DataPath path, ref<str> message, Source source)
    -> LockResult<T> {
    return Err(LockError::Data(rstd::serde::Error::invalid_value_with_source(
        rstd::move(path), message, rstd::move(source))));
}

template<typename T>
auto lock_io_failure(ref<str> operation, ref<rstd::path::Path> path, rstd::io::error::Error source)
    -> LockResult<T> {
    return Err(LockError::Io(String::make(operation), PathBuf::from(path), rstd::move(source)));
}

auto path_string(ref<rstd::path::Path> path) -> LockResult<String> {
    auto text = path.to_str();
    if (text.is_none()) {
        return lock_failure<String>(rstd::format("lock source path '{}' is not valid UTF-8", path));
    }
    return Ok(String::make(*text));
}

auto locked_package_source(const lito::source::ResolvedPackageSource& source)
    -> LockResult<LockedSource> {
    if (source.kind == lito::source::PackageSourceKind::Path) {
        return Ok(LockedSource::Path(source.path.clone()));
    }
    if (source.kind == lito::source::PackageSourceKind::Builtin) {
        return Ok(LockedSource::Builtin(source.builtin.clone(), source.digest.clone()));
    }
    if (source.kind == lito::source::PackageSourceKind::Git) {
        return Ok(
            LockedSource::Git(source.git.clone(), source.reference.clone(), source.commit.clone()));
    }
    if (source.registry_package.is_none() || source.registry_version.is_none() ||
        source.release_digest.is_none() || source.source_digest.is_none() ||
        source.manifest_digest.is_none() || source.blob_digest.is_none() ||
        source.blob_size.is_none() || source.archive_format.is_none()) {
        return lock_failure<LockedSource>(
            "resolved Registry source is missing exact lock identity"_str);
    }
    return Ok(LockedSource::Registry(source.registry_package->clone(),
                                     source.registry_version->clone(),
                                     source.release_digest->clone(),
                                     source.source_digest->clone(),
                                     source.manifest_digest->clone(),
                                     source.blob_digest->clone(),
                                     source.blob_size->clone(),
                                     source.archive_format->clone()));
}

auto locked_external_source(const lito::dependency::ResolvedExternalSource& source)
    -> LockedSource {
    if (source.is_Path()) return LockedSource::Path(source.as_Path().path.clone());
    if (source.is_Package()) return LockedSource::Package(source.as_Package().path.clone());
    if (source.is_Git()) {
        return LockedSource::Git(source.as_Git().url.clone(),
                                 source.as_Git().reference.clone(),
                                 source.as_Git().commit.clone());
    }
    return LockedSource::Archive(source.as_Archive().url.clone(),
                                 source.as_Archive().sha256.clone());
}

auto external_order_key(const lito::dependency::ResolvedExternalSourceRecord& external) -> String {
    auto architectures = Vec<String>::with_capacity(external.architectures.len());
    for (const auto& architecture : external.architectures) {
        architectures.push(architecture.name.clone());
    }
    rstd::slice_::sort_unstable(architectures.as_mut_slice().as_mut_ref());
    auto key = external.name.clone();
    for (const auto& architecture : architectures) {
        key.push_ascii(u8('\n'));
        key.push_str(architecture.as_str());
    }
    return key;
}

auto locked_source_wire(const LockedSource& source) -> LockResult<lito::lock::wire::Source> {
    if (source.is_Path()) {
        return Ok(lito::lock::wire::Source {
            .kind = String::make("path"_str),
            .path = Some(rstd_try(path_string(source.as_Path().path.as_path()))),
        });
    }
    if (source.is_Package()) {
        return Ok(lito::lock::wire::Source {
            .kind = String::make("package"_str),
            .path = Some(rstd_try(path_string(source.as_Package().path.as_path()))),
        });
    }
    if (source.is_Builtin()) {
        return Ok(lito::lock::wire::Source {
            .kind   = String::make("builtin"_str),
            .id     = Some(source.as_Builtin().id.clone()),
            .digest = Some(source.as_Builtin().digest.clone()),
        });
    }
    if (source.is_Git()) {
        return Ok(lito::lock::wire::Source {
            .kind      = String::make("git"_str),
            .url       = Some(source.as_Git().url.clone()),
            .commit    = Some(source.as_Git().commit.clone()),
            .reference = Some(lito::lock::wire::GitReference {
                .kind = String::make(
                    lito::source::git_reference_kind_name(source.as_Git().reference.kind)),
                .value = source.as_Git().reference.value.clone(),
            }),
        });
    }
    if (source.is_Archive()) {
        return Ok(lito::lock::wire::Source {
            .kind   = String::make("archive"_str),
            .url    = Some(String::make(source.as_Archive().url.as_str())),
            .sha256 = Some(source.as_Archive().sha256.to_hex()),
        });
    }
    return Ok(lito::lock::wire::Source {
        .kind      = String::make("registry"_str),
        .registry  = Some(String::make(source.as_Registry().package.registry.as_str())),
        .package   = Some(String::make(source.as_Registry().package.name.as_str())),
        .version   = Some(String::make(source.as_Registry().version.text().as_str())),
        .release   = Some(String::make(source.as_Registry().release.text().as_str())),
        .source    = Some(String::make(source.as_Registry().source.text().as_str())),
        .manifest  = Some(String::make(source.as_Registry().manifest.text().as_str())),
        .blob      = Some(String::make(source.as_Registry().blob.text().as_str())),
        .blob_size = Some(String::make(source.as_Registry().blob_size.text().as_str())),
        .format    = Some(String::make(source.as_Registry().format.as_str())),
    });
}

auto graph_wire(const lito::package::ResolvedPackageGraph& graph, u64 format_version)
    -> LockResult<lito::lock::wire::Document> {
    auto package_ids = rstd::collections::BTreeMap<String, String>::make();
    if (format_version == LOCK_FORMAT_VERSION) {
        for (const auto& package : graph.packages) {
            auto key = package.instance.clone();
            if (key.is_empty()) {
                auto coordinate = lito::package::resolved_package_instance_id(
                    package.source, package.manifest.name.as_str());
                if (coordinate.is_err()) {
                    return lock_failure<lito::lock::wire::Document>(
                        rstd::format("cannot construct lock package instance '{}': {}",
                                     package.manifest.name.as_str(),
                                     coordinate.unwrap_err()));
                }
                key = lito::package::PackageInstanceKey::from(*coordinate);
            }
            package_ids.insert(package.manifest.name.clone(), String::make(key.as_str()));
        }
    }
    auto package_indices = Vec<usize>::with_capacity(graph.packages.len());
    for (usize index {}; index < graph.packages.len(); ++index) package_indices.push(usize(index));
    rstd::slice_::sort_unstable_by(
        package_indices.as_mut_slice().as_mut_ref(), [&graph](usize left, usize right) {
            return graph.packages[left].manifest.name < graph.packages[right].manifest.name;
        });

    auto packages = Vec<lito::lock::wire::Package>::with_capacity(graph.packages.len());
    for (const auto index : package_indices) {
        const auto& package = graph.packages[index];
        auto        dependencies =
            Vec<String>::with_capacity(package.dependencies.len() + package.dev_dependencies.len());
        for (const auto& dependency : package.dependencies) {
            auto name = resolved_dependency_name(dependency);
            if (format_version == u64(2)) {
                dependencies.push(String::make(name));
            } else {
                auto id = package_ids.get(name);
                if (id.is_none()) {
                    return lock_failure<lito::lock::wire::Document>(
                        rstd::format("resolved dependency '{}' has no package instance", name));
                }
                dependencies.push((**id).clone());
            }
        }
        for (const auto& dependency : package.dev_dependencies) {
            if (format_version == u64(2)) {
                dependencies.push(dependency.name.clone());
            } else {
                auto id = package_ids.get(dependency.name.as_str());
                if (id.is_none()) {
                    return lock_failure<lito::lock::wire::Document>(rstd::format(
                        "resolved dependency '{}' has no package instance", dependency.name));
                }
                dependencies.push((**id).clone());
            }
        }
        rstd::slice_::sort_unstable(dependencies.as_mut_slice().as_mut_ref());

        auto runtime_dependencies = Vec<String>::with_capacity(package.runtime_dependencies.len());
        for (const auto& dependency : package.runtime_dependencies) {
            if (format_version == u64(2)) {
                runtime_dependencies.push(dependency.name.clone());
            } else {
                auto id = package_ids.get(dependency.name.as_str());
                if (id.is_none()) {
                    return lock_failure<lito::lock::wire::Document>(
                        rstd::format("resolved runtime dependency '{}' has no package instance",
                                     dependency.name));
                }
                runtime_dependencies.push((**id).clone());
            }
        }
        rstd::slice_::sort_unstable(runtime_dependencies.as_mut_slice().as_mut_ref());

        auto external_indices = Vec<usize>::with_capacity(package.externals.len());
        for (usize external {}; external < package.externals.len(); ++external) {
            external_indices.push(usize(external));
        }
        rstd::slice_::sort_unstable_by(external_indices.as_mut_slice().as_mut_ref(),
                                       [&package](usize left, usize right) {
                                           return external_order_key(package.externals[left]) <
                                                  external_order_key(package.externals[right]);
                                       });
        auto externals = Vec<lito::lock::wire::External>::with_capacity(package.externals.len());
        for (const auto external_index : external_indices) {
            const auto& external      = package.externals[external_index];
            auto        architectures = Vec<String>::with_capacity(external.architectures.len());
            for (const auto& architecture : external.architectures) {
                architectures.push(architecture.name.clone());
            }
            rstd::slice_::sort_unstable(architectures.as_mut_slice().as_mut_ref());
            auto optional_architectures = Option<Vec<String>> {};
            if (! architectures.is_empty()) {
                optional_architectures = Some(rstd::move(architectures));
            }
            externals.push(lito::lock::wire::External {
                .name          = external.name.clone(),
                .architectures = rstd::move(optional_architectures),
                .source = rstd_try(locked_source_wire(locked_external_source(external.source))),
            });
        }

        auto id = Option<String> {};
        if (format_version == LOCK_FORMAT_VERSION) {
            auto value = package_ids.get(package.manifest.name.as_str());
            id         = Some((**value).clone());
        }
        auto version = Option<String> {};
        if (package.manifest.version.value.is_some()) {
            version = Some(package.manifest.version.value->clone());
        }
        packages.push(lito::lock::wire::Package {
            .id      = rstd::move(id),
            .name    = package.manifest.name.clone(),
            .version = rstd::move(version),
            .source = rstd_try(locked_source_wire(rstd_try(locked_package_source(package.source)))),
            .manifest             = rstd_try(path_string(package.source_manifest.as_path())),
            .dependencies         = rstd::move(dependencies),
            .runtime_dependencies = rstd::move(runtime_dependencies),
            .externals            = rstd::move(externals),
        });
    }
    return Ok(lito::lock::wire::Document {
        .version  = format_version,
        .packages = rstd::move(packages),
    });
}

auto graph_json(const lito::package::ResolvedPackageGraph& graph,
                u64 format_version = LOCK_FORMAT_VERSION) -> LockResult<Json> {
    auto document = rstd_try(graph_wire(graph, format_version));
    auto encoded  = rstd::json::to_value(document);
    if (encoded.is_err()) {
        return Err(LockError::Data(rstd::move(encoded).unwrap_err_unchecked()));
    }
    return Ok(rstd::move(encoded).unwrap_unchecked());
}
auto valid_source_manifest(ref<str> value) -> bool {
    if (value.is_empty()) return false;
    return PathBuf::from(value).as_path().is_safe_relative();
}

auto valid_fetch_url(ref<str> value) -> bool {
    if (value.is_empty()) return false;
    for (const auto character : value) {
        const auto ascii = character.to_primitive();
        if (ascii < 0x20 || ascii == 0x7f) return false;
    }
    return true;
}

auto valid_lock_source_path(ref<str> value) -> bool {
    if (value.is_empty()) return false;
    return ! PathBuf::from(value).as_path().is_absolute();
}

auto parse_reference(lito::lock::wire::GitReference value,
                     rstd::serde::DataPath          path,
                     ref<str> commit) -> LockResult<lito::source::GitReference> {
    const auto default_reference = value.kind == "default"_str;
    const auto named_reference   = value.kind == "branch"_str || value.kind == "tag"_str ||
                                   value.kind == "rev"_str || value.kind == "commit"_str;
    if (! default_reference && ! named_reference) {
        return lock_data_failure<lito::source::GitReference>(path.with_field("kind"_str),
                                                             "Git reference kind is invalid"_str);
    }
    if (default_reference != value.value.is_empty()) {
        return lock_data_failure<lito::source::GitReference>(
            path.with_field("value"_str),
            "Git reference value must be empty only for the default branch"_str);
    }
    if (value.kind == "commit"_str && value.value.as_str() != commit) {
        return lock_data_failure<lito::source::GitReference>(
            path.with_field("value"_str), "Git commit reference does not match commit"_str);
    }
    auto parsed = lito::source::GitReferenceKind::DefaultBranch;
    if (value.kind == "branch"_str) parsed = lito::source::GitReferenceKind::Branch;
    if (value.kind == "tag"_str) parsed = lito::source::GitReferenceKind::Tag;
    if (value.kind == "rev"_str) parsed = lito::source::GitReferenceKind::Rev;
    if (value.kind == "commit"_str) parsed = lito::source::GitReferenceKind::Commit;
    return Ok(lito::source::GitReference { .kind = parsed, .value = rstd::move(value.value) });
}

auto source_payload_count(const lito::lock::wire::Source& value) -> usize {
    return usize(value.path.is_some()) + usize(value.id.is_some()) + usize(value.digest.is_some()) +
           usize(value.url.is_some()) + usize(value.commit.is_some()) +
           usize(value.reference.is_some()) + usize(value.sha256.is_some()) +
           usize(value.registry.is_some()) + usize(value.package.is_some()) +
           usize(value.version.is_some()) + usize(value.release.is_some()) +
           usize(value.source.is_some()) + usize(value.manifest.is_some()) +
           usize(value.blob.is_some()) + usize(value.blob_size.is_some()) +
           usize(value.format.is_some());
}

template<typename T>
auto take_source_field(Option<T>& value, rstd::serde::DataPath path, ref<str> field)
    -> LockResult<T> {
    if (value.is_none()) {
        return lock_data_failure<T>(path.with_field(field), "source field is required"_str);
    }
    return Ok(rstd::move(value).unwrap_unchecked());
}

auto require_source_shape(const lito::lock::wire::Source& value,
                          rstd::serde::DataPath           path,
                          usize                           fields) -> LockResult<empty> {
    if (source_payload_count(value) != fields) {
        return lock_data_failure<empty>(rstd::move(path),
                                        "source fields do not match its kind"_str);
    }
    return Ok(empty {});
}

auto parse_locked_source(lito::lock::wire::Source value,
                         rstd::serde::DataPath    path,
                         bool                     external_source) -> LockResult<LockedSource> {
    if (value.kind == "path"_str) {
        rstd_try(require_source_shape(value, path.clone(), usize(1)));
        auto source_path = rstd_try(take_source_field(value.path, path.clone(), "path"_str));
        if (! valid_lock_source_path(source_path.as_str())) {
            return lock_data_failure<LockedSource>(path.with_field("path"_str),
                                                   "source path must be relative"_str);
        }
        return Ok(LockedSource::Path(PathBuf::from(rstd::move(source_path))));
    }
    if (value.kind == "package"_str && external_source) {
        rstd_try(require_source_shape(value, path.clone(), usize(1)));
        auto source_path = rstd_try(take_source_field(value.path, path.clone(), "path"_str));
        if (source_path.is_empty() ||
            ! PathBuf::from(source_path.as_str()).as_path().is_safe_relative()) {
            return lock_data_failure<LockedSource>(
                path.with_field("path"_str), "package source path must be safe and relative"_str);
        }
        return Ok(LockedSource::Package(PathBuf::from(rstd::move(source_path))));
    }
    if (value.kind == "builtin"_str && ! external_source) {
        rstd_try(require_source_shape(value, path.clone(), usize(2)));
        auto id          = rstd_try(take_source_field(value.id, path.clone(), "id"_str));
        auto digest_text = rstd_try(take_source_field(value.digest, path.clone(), "digest"_str));
        if (! lito::manifest::valid_package_name(id.as_str())) {
            return lock_data_failure<LockedSource>(path.with_field("id"_str),
                                                   "builtin id is not a package name"_str);
        }
        auto digest =
            lito::parse::parse_sha256(digest_text.as_str(), lito::parse::Sha256TextMode::Canonical);
        if (digest.is_err()) {
            return lock_data_failure<LockedSource>(path.with_field("digest"_str),
                                                   "builtin digest is invalid"_str,
                                                   rstd::move(digest).unwrap_err_unchecked());
        }
        return Ok(
            LockedSource::Builtin(rstd::move(id), rstd::move(digest).unwrap_unchecked().to_hex()));
    }
    if (value.kind == "git"_str) {
        rstd_try(require_source_shape(value, path.clone(), usize(3)));
        auto url    = rstd_try(take_source_field(value.url, path.clone(), "url"_str));
        auto commit = rstd_try(take_source_field(value.commit, path.clone(), "commit"_str));
        auto reference =
            rstd_try(take_source_field(value.reference, path.clone(), "reference"_str));
        if (! valid_fetch_url(url.as_str())) {
            return lock_data_failure<LockedSource>(path.with_field("url"_str),
                                                   "Git URL is invalid"_str);
        }
        if (! lito::source::git_commit_is_valid(commit.as_str())) {
            return lock_data_failure<LockedSource>(path.with_field("commit"_str),
                                                   "Git commit is not a full object id"_str);
        }
        auto parsed_reference = rstd_try(parse_reference(
            rstd::move(reference), path.with_field("reference"_str), commit.as_str()));
        return Ok(
            LockedSource::Git(rstd::move(url), rstd::move(parsed_reference), rstd::move(commit)));
    }
    if (value.kind == "archive"_str && external_source) {
        rstd_try(require_source_shape(value, path.clone(), usize(2)));
        auto url_text = rstd_try(take_source_field(value.url, path.clone(), "url"_str));
        auto url      = lito::parse::FetchUrl::parse(url_text.as_str());
        if (url.is_err()) {
            return lock_data_failure<LockedSource>(path.with_field("url"_str),
                                                   "archive URL is invalid"_str,
                                                   rstd::move(url).unwrap_err_unchecked());
        }
        auto sha256_text = rstd_try(take_source_field(value.sha256, path.clone(), "sha256"_str));
        auto sha256 =
            lito::parse::parse_sha256(sha256_text.as_str(), lito::parse::Sha256TextMode::Canonical);
        if (sha256.is_err()) {
            return lock_data_failure<LockedSource>(path.with_field("sha256"_str),
                                                   "archive SHA256 is invalid"_str,
                                                   rstd::move(sha256).unwrap_err_unchecked());
        }
        return Ok(LockedSource::Archive(rstd::move(url).unwrap_unchecked(),
                                        rstd::move(sha256).unwrap_unchecked()));
    }
    if (value.kind == "registry"_str && ! external_source) {
        rstd_try(require_source_shape(value, path.clone(), usize(9)));
        auto registry = rstd_try(take_source_field(value.registry, path.clone(), "registry"_str));
        auto package  = rstd_try(take_source_field(value.package, path.clone(), "package"_str));
        auto version  = rstd_try(take_source_field(value.version, path.clone(), "version"_str));
        auto release  = rstd_try(take_source_field(value.release, path.clone(), "release"_str));
        auto source   = rstd_try(take_source_field(value.source, path.clone(), "source"_str));
        auto manifest = rstd_try(take_source_field(value.manifest, path.clone(), "manifest"_str));
        auto blob     = rstd_try(take_source_field(value.blob, path.clone(), "blob"_str));
        auto blob_size =
            rstd_try(take_source_field(value.blob_size, path.clone(), "blob-size"_str));
        auto format      = rstd_try(take_source_field(value.format, path.clone(), "format"_str));
        auto registry_id = lito::registry::RegistryId::parse(registry.as_str());
        if (registry_id.is_err()) {
            return lock_data_failure<LockedSource>(path.with_field("registry"_str),
                                                   "Registry id is invalid"_str);
        }
        auto package_name = lito::registry::RegistryPackageName::parse(package.as_str());
        if (package_name.is_err()) {
            return lock_data_failure<LockedSource>(path.with_field("package"_str),
                                                   "Registry package name is invalid"_str);
        }
        auto semantic_version = lito::registry::SemanticVersion::parse(version.as_str());
        if (semantic_version.is_err()) {
            return lock_data_failure<LockedSource>(path.with_field("version"_str),
                                                   "Registry version is invalid"_str);
        }
        auto release_digest = lito::registry::ReleaseDigest::parse(release.as_str());
        if (release_digest.is_err()) {
            return lock_data_failure<LockedSource>(path.with_field("release"_str),
                                                   "Registry release digest is invalid"_str);
        }
        auto source_digest = lito::registry::SourceDigest::parse(source.as_str());
        if (source_digest.is_err()) {
            return lock_data_failure<LockedSource>(path.with_field("source"_str),
                                                   "Registry source digest is invalid"_str);
        }
        auto manifest_digest = lito::registry::ManifestDigest::parse(manifest.as_str());
        if (manifest_digest.is_err()) {
            return lock_data_failure<LockedSource>(path.with_field("manifest"_str),
                                                   "Registry manifest digest is invalid"_str);
        }
        auto blob_digest = lito::registry::BlobDigest::parse(blob.as_str());
        if (blob_digest.is_err()) {
            return lock_data_failure<LockedSource>(path.with_field("blob"_str),
                                                   "Registry blob digest is invalid"_str);
        }
        auto parsed_blob_size = lito::registry::RegistryBlobSize::parse(blob_size.as_str());
        if (parsed_blob_size.is_err()) {
            return lock_data_failure<LockedSource>(path.with_field("blob-size"_str),
                                                   "Registry blob size is invalid"_str);
        }
        auto archive_format = lito::registry::RegistryArchiveFormat::parse(format.as_str());
        if (archive_format.is_err()) {
            return lock_data_failure<LockedSource>(path.with_field("format"_str),
                                                   "Registry archive format is invalid"_str);
        }
        return Ok(LockedSource::Registry(
            lito::registry::RegistryPackageId {
                .registry = rstd::move(registry_id).unwrap_unchecked(),
                .name     = rstd::move(package_name).unwrap_unchecked(),
            },
            rstd::move(semantic_version).unwrap_unchecked(),
            rstd::move(release_digest).unwrap_unchecked(),
            rstd::move(source_digest).unwrap_unchecked(),
            rstd::move(manifest_digest).unwrap_unchecked(),
            rstd::move(blob_digest).unwrap_unchecked(),
            rstd::move(parsed_blob_size).unwrap_unchecked(),
            rstd::move(archive_format).unwrap_unchecked()));
    }
    return lock_data_failure<LockedSource>(path.with_field("kind"_str),
                                           "source kind is not allowed here"_str);
}

auto locked_package_instance(const LockedSource& source, ref<str> package)
    -> LockResult<lito::package::ResolvedPackageInstanceId> {
    if (source.is_Path()) {
        return Ok(lito::package::ResolvedPackageInstanceId::Path(source.as_Path().path.clone(),
                                                                 String::make(package)));
    }
    if (source.is_Git()) {
        return Ok(lito::package::ResolvedPackageInstanceId::Git(
            source.as_Git().url.clone(), source.as_Git().commit.clone(), String::make(package)));
    }
    if (source.is_Builtin()) {
        return Ok(
            lito::package::ResolvedPackageInstanceId::Builtin(source.as_Builtin().id.clone(),
                                                              source.as_Builtin().digest.clone(),
                                                              String::make(package)));
    }
    if (source.is_Registry()) {
        if (source.as_Registry().package.name.as_str() != package) {
            return lock_failure<lito::package::ResolvedPackageInstanceId>(
                "lock Registry source package does not match lock package name"_str);
        }
        return Ok(lito::package::ResolvedPackageInstanceId::Registry(
            source.as_Registry().package.clone(),
            source.as_Registry().version.clone(),
            source.as_Registry().release.clone()));
    }
    return lock_failure<lito::package::ResolvedPackageInstanceId>(
        "lock package source cannot use an external-only source kind"_str);
}

auto parse_lock_wire(lito::lock::wire::Document document) -> LockResult<LockedProject> {
    auto root = rstd::serde::DataPath();
    if (document.version != u64(2) && document.version != LOCK_FORMAT_VERSION) {
        return lock_failure<LockedProject>(
            rstd::format("lock.version {} is not supported; this Lito supports version {}",
                         document.version,
                         LOCK_FORMAT_VERSION));
    }
    if (document.packages.is_empty()) {
        return lock_data_failure<LockedProject>(root.with_field("packages"_str),
                                                "lock packages must not be empty"_str);
    }
    const auto legacy_edges = document.version == u64(2);
    auto       names        = StringSet::make();
    auto       ids          = StringSet::make();
    auto       name_to_id   = rstd::collections::BTreeMap<String, String>::make();
    auto       result =
        LockedProject { .packages = Vec<LockedPackage>::with_capacity(document.packages.len()) };
    for (usize package_index {}; package_index < document.packages.len(); ++package_index) {
        auto package_path = root.with_field("packages"_str).with_index(package_index);
        auto package      = rstd::move(document.packages[package_index]);
        if (legacy_edges && package.id.is_some()) {
            return lock_data_failure<LockedProject>(package_path.with_field("id"_str),
                                                    "version 2 packages must not contain id"_str);
        }
        if (! lito::manifest::valid_package_name(package.name.as_str())) {
            return lock_data_failure<LockedProject>(package_path.with_field("name"_str),
                                                    "lock package name is invalid"_str);
        }
        if (names.contains_key(package.name.as_str())) {
            return lock_data_failure<LockedProject>(package_path.with_field("name"_str),
                                                    "lock package name is repeated"_str);
        }
        names.insert(package.name.clone(), empty {});
        if (package.version.is_some() && package.version->is_empty()) {
            return lock_data_failure<LockedProject>(package_path.with_field("version"_str),
                                                    "package version must not be empty"_str);
        }
        if (! valid_source_manifest(package.manifest.as_str())) {
            return lock_data_failure<LockedProject>(
                package_path.with_field("manifest"_str),
                "package manifest must be a safe relative path"_str);
        }
        auto locked_source = rstd_try(parse_locked_source(
            rstd::move(package.source), package_path.with_field("source"_str), false));
        auto coordinate = rstd_try(locked_package_instance(locked_source, package.name.as_str()));
        auto derived_id = lito::package::PackageInstanceKey::from(coordinate);
        auto package_id = String::make(derived_id.as_str());
        if (! legacy_edges) {
            if (package.id.is_none()) {
                return lock_data_failure<LockedProject>(package_path.with_field("id"_str),
                                                        "package id is required"_str);
            }
            if (package.id->as_str() != package_id.as_str()) {
                return lock_data_failure<LockedProject>(
                    package_path.with_field("id"_str),
                    "package id does not match exact source identity"_str);
            }
        }
        if (ids.contains_key(package_id.as_str())) {
            return lock_data_failure<LockedProject>(package_path.with_field("id"_str),
                                                    "package instance is repeated"_str);
        }
        ids.insert(package_id.clone(), empty {});
        name_to_id.insert(package.name.clone(), package_id.clone());
        if (locked_source.is_Registry() &&
            (package.version.is_none() ||
             package.version->as_str() != locked_source.as_Registry().version.text().as_str())) {
            return lock_data_failure<LockedProject>(
                package_path.with_field("version"_str),
                "Registry package version does not match source version"_str);
        }

        auto locked_externals =
            Vec<LockedPackageExternalSource>::with_capacity(package.externals.len());
        auto external_identities = StringSet::make();
        for (usize external_index {}; external_index < package.externals.len(); ++external_index) {
            auto external_path =
                package_path.with_field("externals"_str).with_index(external_index);
            auto external = rstd::move(package.externals[external_index]);
            if (external.name.is_empty()) {
                return lock_data_failure<LockedProject>(external_path.with_field("name"_str),
                                                        "external name must not be empty"_str);
            }
            auto architecture_key     = String::make();
            auto locked_architectures = Vec<String>::make();
            if (external.architectures.is_some()) {
                auto architectures = rstd::move(external.architectures).unwrap_unchecked();
                if (architectures.is_empty()) {
                    return lock_data_failure<LockedProject>(
                        external_path.with_field("architectures"_str),
                        "external architectures must not be empty when present"_str);
                }
                locked_architectures = Vec<String>::with_capacity(architectures.len());
                auto seen            = StringSet::make();
                auto previous        = Option<String> {};
                for (usize index {}; index < architectures.len(); ++index) {
                    auto architecture_path =
                        external_path.with_field("architectures"_str).with_index(index);
                    auto architecture = rstd::move(architectures[index]);
                    auto canonical    = canonical_architecture(architecture.as_str());
                    if (canonical.is_err() || canonical->as_str() != architecture.as_str()) {
                        return lock_data_failure<LockedProject>(
                            rstd::move(architecture_path),
                            "external architecture is not canonical"_str);
                    }
                    if (seen.contains_key(architecture.as_str())) {
                        return lock_data_failure<LockedProject>(
                            rstd::move(architecture_path), "external architecture is repeated"_str);
                    }
                    if (previous.is_some() && architecture < *previous) {
                        return lock_data_failure<LockedProject>(
                            rstd::move(architecture_path),
                            "external architectures are not sorted"_str);
                    }
                    seen.insert(architecture.clone(), empty {});
                    previous = Some(architecture.clone());
                    if (! architecture_key.is_empty()) architecture_key.push_ascii(u8(','));
                    architecture_key.push_str(architecture.as_str());
                    locked_architectures.push(rstd::move(architecture));
                }
            }
            auto identity = rstd::format("{}\n{}", external.name, architecture_key.as_str());
            if (external_identities.contains_key(identity.as_str())) {
                return lock_data_failure<LockedProject>(external_path.with_field("name"_str),
                                                        "package external is repeated"_str);
            }
            external_identities.insert(rstd::move(identity), empty {});
            locked_externals.push(LockedPackageExternalSource {
                .name          = rstd::move(external.name),
                .architectures = rstd::move(locked_architectures),
                .source        = rstd_try(parse_locked_source(
                    rstd::move(external.source), external_path.with_field("source"_str), true)),
            });
        }

        auto validate_edges = [&](Vec<String> values, ref<str> field) -> LockResult<Vec<String>> {
            auto result_values = Vec<String>::with_capacity(values.len());
            auto seen          = StringSet::make();
            for (usize index {}; index < values.len(); ++index) {
                auto edge_path = package_path.with_field(field).with_index(index);
                auto value     = rstd::move(values[index]);
                if ((legacy_edges && ! lito::manifest::valid_package_name(value.as_str())) ||
                    (! legacy_edges && value.is_empty())) {
                    return lock_data_failure<Vec<String>>(rstd::move(edge_path),
                                                          "package reference is invalid"_str);
                }
                if (seen.contains_key(value.as_str())) {
                    return lock_data_failure<Vec<String>>(rstd::move(edge_path),
                                                          "package reference is repeated"_str);
                }
                seen.insert(value.clone(), empty {});
                result_values.push(rstd::move(value));
            }
            return Ok(rstd::move(result_values));
        };
        auto dependencies =
            rstd_try(validate_edges(rstd::move(package.dependencies), "dependencies"_str));
        auto runtime_dependencies = rstd_try(
            validate_edges(rstd::move(package.runtime_dependencies), "runtime-dependencies"_str));
        result.packages.push(LockedPackage {
            .id                   = rstd::move(package_id),
            .name                 = rstd::move(package.name),
            .version              = rstd::move(package.version),
            .source               = rstd::move(locked_source),
            .manifest             = PathBuf::from(rstd::move(package.manifest)),
            .dependencies         = rstd::move(dependencies),
            .runtime_dependencies = rstd::move(runtime_dependencies),
            .externals            = rstd::move(locked_externals),
        });
    }
    for (usize package_index {}; package_index < result.packages.len(); ++package_index) {
        auto& package       = result.packages[package_index];
        auto  resolve_edges = [&](Vec<String>& values, ref<str> field) -> LockResult<empty> {
            for (usize index {}; index < values.len(); ++index) {
                auto& value = values[index];
                if (legacy_edges) {
                    auto id = name_to_id.get(value.as_str());
                    if (id.is_some()) value = (**id).clone();
                }
                if (! ids.contains_key(value.as_str())) {
                    return lock_data_failure<empty>(
                        root.with_field("packages"_str)
                            .with_index(package_index)
                            .with_field(field)
                            .with_index(index),
                        "package reference does not identify a package"_str);
                }
            }
            return Ok(empty {});
        };
        rstd_try(resolve_edges(package.dependencies, "dependencies"_str));
        rstd_try(resolve_edges(package.runtime_dependencies, "runtime-dependencies"_str));
    }
    return Ok(rstd::move(result));
}

auto decode_current_lock(const Json& document) -> LockResult<LockedProject> {
    auto decoded = rstd::json::decode_value<lito::lock::wire::Document>(document);
    if (decoded.is_err()) {
        return Err(LockError::Data(rstd::move(decoded).unwrap_err_unchecked()));
    }
    return parse_lock_wire(rstd::move(decoded).unwrap_unchecked());
}

auto load_existing(ref<rstd::path::Path> path) -> LockResult<Option<Json>> {
    auto exists = rstd::fs::exists(path);
    if (exists.is_err()) {
        return lock_io_failure<Option<Json>>("inspect"_str, path, rstd::move(exists).unwrap_err());
    }
    if (! *exists) return Ok(Option<Json> {});
    auto contents = rstd::fs::read_to_string(path);
    if (contents.is_err()) {
        return lock_io_failure<Option<Json>>("read"_str, path, rstd::move(contents).unwrap_err());
    }
    auto parsed = rstd::json::from_str(contents->as_str());
    if (parsed.is_err()) {
        return Err(LockError::Json(PathBuf::from(path), rstd::move(parsed).unwrap_err()));
    }
    auto document = rstd::move(parsed).unwrap();
    return Ok(Some(rstd::move(document)));
}

auto lock_document_version(const Json& document) -> Option<u64> {
    auto version = document.get("version"_str);
    if (version.is_none()) return None();
    return (**version).as_u64();
}

auto append_git_pin(lito::source::SourceResolutionOptions& options,
                    ref<str>                               url,
                    const lito::source::GitReference&      reference,
                    ref<str>                               commit) -> LockResult<empty> {
    for (const auto& existing : options.git_sources) {
        if (existing.git.as_str() != url ||
            ! lito::source::git_references_equal(existing.reference, reference)) {
            continue;
        }
        if (existing.commit.as_str() != commit) {
            return lock_failure<empty>(
                rstd::format("lock resolves Git requirement '{}#{}' to both '{}' and '{}'",
                             url,
                             reference.value.as_str(),
                             existing.commit.as_str(),
                             commit));
        }
        return Ok(empty {});
    }
    options.git_sources.push(lito::source::GitSourcePin {
        .git = String::make(url),
        .reference =
            lito::source::GitReference {
                .kind  = reference.kind,
                .value = reference.value.clone(),
            },
        .commit = String::make(commit),
    });
    return Ok(empty {});
}

auto append_project_pins(lito::source::SourceResolutionOptions& options,
                         const LockedProject&                   project) -> LockResult<empty> {
    for (const auto& package : project.packages) {
        if (! package.source.is_Git()) continue;
        rstd_try(append_git_pin(options,
                                package.source.as_Git().url.as_str(),
                                package.source.as_Git().reference,
                                package.source.as_Git().commit.as_str()));
    }
    for (const auto& package : project.packages) {
        for (const auto& external : package.externals) {
            if (! external.source.is_Git()) continue;
            rstd_try(append_git_pin(options,
                                    external.source.as_Git().url.as_str(),
                                    external.source.as_Git().reference,
                                    external.source.as_Git().commit.as_str()));
        }
    }
    return Ok(empty {});
}

auto write_lock(ref<rstd::path::Path> destination, const Json& desired) -> LockResult<empty> {
    auto text = rstd::json::to_string(
        desired, rstd::json::FormatOptions { .pretty = true, .indent = usize(2) });
    text.push_ascii(u8('\n'));

    auto written = rstd::fs::write_atomic(destination, text.as_str().as_bytes());
    if (written.is_err()) {
        return lock_io_failure<empty>(
            "atomically write"_str, destination, rstd::move(written).unwrap_err());
    }
    return Ok(empty {});
}

auto lito::lock::load_locked_project(ref<rstd::path::Path> root, const LockConfig& config)
    -> LockResult<LockedProject> {
    auto destination = resolve_lock_path(root, config);
    auto loaded      = rstd_try(load_existing(destination.as_path()));
    if (loaded.is_none()) {
        return lock_failure<LockedProject>(
            rstd::format("lock file '{}' does not exist", destination.as_path()));
    }
    return decode_current_lock(*loaded);
}

auto lito::lock::load_lock_session(ref<rstd::path::Path>           root,
                                   const LockConfig&               config,
                                   bool                            locked,
                                   lito::source::GitResolutionMode git,
                                   InvalidLockPolicy invalid) -> LockResult<LockSession> {
    if (locked && git == lito::source::GitResolutionMode::Refresh) {
        return lock_failure<LockSession>("--locked cannot refresh Git dependencies"_str);
    }
    if (locked && invalid == InvalidLockPolicy::Replace) {
        return lock_failure<LockSession>("--locked cannot replace an invalid lock file"_str);
    }
    auto destination = resolve_lock_path(root, config);
    auto loaded      = load_existing(destination.as_path());
    if (loaded.is_err()) {
        auto error = rstd::move(loaded).unwrap_err();
        if (invalid != InvalidLockPolicy::Replace || ! error.is_Json()) {
            return Err(rstd::move(error));
        }
        auto session         = LockSession {};
        session.root_        = PathBuf::from(root);
        session.destination_ = rstd::move(destination);
        session.options_ = lito::source::SourceResolutionOptions { .locked = false, .git = git };
        return Ok(rstd::move(session));
    }
    auto existing = rstd::move(loaded).unwrap();
    if (existing.is_none()) {
        if (locked) {
            return lock_failure<LockSession>(rstd::format(
                "--locked requires an existing lock file at '{}'", destination.as_path()));
        }
        auto session         = LockSession {};
        session.root_        = PathBuf::from(root);
        session.destination_ = rstd::move(destination);
        session.options_ = lito::source::SourceResolutionOptions { .locked = locked, .git = git };
        return Ok(rstd::move(session));
    }

    const auto version = lock_document_version(*existing);
    if (version == Some(u64(1))) {
        if (locked) {
            return lock_failure<LockSession>(rstd::format(
                "lock file '{}' uses version 1, but this Lito requires version {}; run 'lito "
                "update'",
                destination.as_path(),
                LOCK_FORMAT_VERSION));
        }
        auto session         = LockSession {};
        session.root_        = PathBuf::from(root);
        session.destination_ = rstd::move(destination);
        session.existing_    = rstd::move(existing);
        session.options_ = lito::source::SourceResolutionOptions { .locked = false, .git = git };
        return Ok(rstd::move(session));
    }
    auto parsed_project = decode_current_lock(*existing);
    if (parsed_project.is_err()) {
        const auto version = lock_document_version(*existing);
        if (invalid != InvalidLockPolicy::Replace ||
            (version.is_some() && *version > LOCK_FORMAT_VERSION)) {
            return Err(rstd::move(parsed_project).unwrap_err());
        }
        auto session         = LockSession {};
        session.root_        = PathBuf::from(root);
        session.destination_ = rstd::move(destination);
        session.existing_    = rstd::move(existing);
        session.options_ = lito::source::SourceResolutionOptions { .locked = false, .git = git };
        return Ok(rstd::move(session));
    }

    auto options = lito::source::SourceResolutionOptions { .locked = locked, .git = git };
    auto project = Some(rstd::move(parsed_project).unwrap());
    rstd_try(append_project_pins(options, *project));
    auto session         = LockSession {};
    session.locked_      = locked;
    session.root_        = PathBuf::from(root);
    session.destination_ = rstd::move(destination);
    session.existing_    = rstd::move(existing);
    session.project_     = rstd::move(project);
    session.options_     = rstd::move(options);
    return Ok(rstd::move(session));
}

auto lito::lock::load_lock_session(ref<rstd::path::Path>           root,
                                   bool                            locked,
                                   lito::source::GitResolutionMode git,
                                   InvalidLockPolicy invalid) -> LockResult<LockSession> {
    return load_lock_session(root, LockConfig {}, locked, git, invalid);
}

auto lito::lock::sync_lock(const lito::package::ResolvedPackageGraph& graph, LockSession session)
    -> LockResult<LockStatus> {
    auto desired_format = LOCK_FORMAT_VERSION;
    if (session.locked_ && session.existing_.is_some() &&
        lock_document_version(*session.existing_) == Some(u64(2))) {
        desired_format = u64(2);
    }
    auto desired_result = graph_json(graph, desired_format);
    if (desired_result.is_err()) return Err(rstd::move(desired_result).unwrap_err());
    auto desired = rstd::move(desired_result).unwrap();
    if (! (graph.root_directory.as_path().starts_with(session.root_.as_path()) &&
           session.root_.as_path().starts_with(graph.root_directory.as_path()))) {
        return lock_failure<LockStatus>("lock session root does not match resolved graph root"_str);
    }
    if (session.locked_) {
        if (session.existing_.is_some() && *session.existing_ == desired) {
            return Ok(LockStatus::Unchanged);
        }
        return lock_failure<LockStatus>(rstd::format(
            "--locked forbids updating stale lock file '{}'", session.destination_.as_path()));
    }

    if (session.existing_.is_some() && *session.existing_ == desired) {
        return Ok(LockStatus::Unchanged);
    }
    auto written = write_lock(session.destination_.as_path(), desired);
    if (written.is_err()) return Err(rstd::move(written).unwrap_err());
    return Ok(LockStatus::Updated);
}
