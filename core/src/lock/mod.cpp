module;
#include <rstd/macro.hpp>

module lito.core;

import rstd;
import rstd.serde;
import rstd.toml;
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
using Toml      = rstd::toml::Value;
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

auto locked_package_source(const lito::source::ResolvedPackageSource& source)
    -> LockResult<Option<LockedSource>> {
    if (source.kind == lito::source::PackageSourceKind::Path ||
        source.kind == lito::source::PackageSourceKind::Builtin) {
        return Ok(None());
    }
    if (source.kind == lito::source::PackageSourceKind::Git) {
        return Ok(Some(LockedSource::Git(source.git.clone(), source.commit.clone())));
    }
    if (source.registry_package.is_none() || source.registry_version.is_none() ||
        source.release_digest.is_none()) {
        return lock_failure<Option<LockedSource>>(
            "resolved Registry source is missing exact lock identity"_str);
    }
    return Ok(Some(LockedSource::Registry(source.registry_package->clone(),
                                          source.registry_version->clone(),
                                          source.release_digest->clone())));
}

auto locked_external_source(const lito::dependency::ResolvedExternalSource& source)
    -> Option<LockedSource> {
    if (source.is_Path() || source.is_Package()) return None();
    if (source.is_Git()) {
        return Some(LockedSource::Git(source.as_Git().url.clone(), source.as_Git().commit.clone()));
    }
    return Some(
        LockedSource::Archive(source.as_Archive().url.clone(), source.as_Archive().sha256.clone()));
}

auto external_order_key(const lito::dependency::ResolvedExternalSourceRecord& external) -> String {
    auto architectures = Vec<String>::with_capacity(external.architectures.len());
    for (const auto& architecture : external.architectures) {
        architectures.push(String::make(architecture_name(architecture)));
    }
    rstd::slice_::sort_unstable(architectures.as_mut_slice().as_mut_ref());
    auto key = external.name.clone();
    for (const auto& architecture : architectures) {
        key.push_ascii(u8('\n'));
        key.push_str(architecture.as_str());
    }
    return key;
}

struct LockedSourceWire {
    String         source;
    Option<String> checksum;
};

auto locked_source_wire(const LockedSource& source) -> LockedSourceWire {
    if (source.is_Git()) {
        return LockedSourceWire {
            .source = rstd::format(
                "git+{}#{}", source.as_Git().url.as_str(), source.as_Git().commit.as_str()),
            .checksum = None(),
        };
    }
    if (source.is_Archive()) {
        return LockedSourceWire {
            .source   = rstd::format("archive+{}", source.as_Archive().url.as_str()),
            .checksum = Some(rstd::format("sha256:{}", source.as_Archive().sha256.to_hex())),
        };
    }
    return LockedSourceWire {
        .source   = rstd::format("registry+{}{}@{}",
                                 source.as_Registry().package.registry.as_str(),
                                 source.as_Registry().package.name.as_str(),
                                 source.as_Registry().version.text().as_str()),
        .checksum = Some(source.as_Registry().release.text()),
    };
}

auto graph_wire(const lito::package::ResolvedPackageGraph& graph, u64 format_version)
    -> LockResult<lito::lock::wire::Document> {
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
            dependencies.push(String::make(name));
        }
        for (const auto& dependency : package.dev_dependencies) {
            dependencies.push(dependency.name.clone());
        }
        rstd::slice_::sort_unstable(dependencies.as_mut_slice().as_mut_ref());

        auto runtime_dependencies = Vec<String>::with_capacity(package.runtime_dependencies.len());
        for (const auto& dependency : package.runtime_dependencies) {
            runtime_dependencies.push(dependency.name.clone());
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
            auto        locked_source = locked_external_source(external.source);
            if (locked_source.is_none()) continue;
            auto architectures = Vec<String>::with_capacity(external.architectures.len());
            for (const auto& architecture : external.architectures) {
                architectures.push(String::make(architecture_name(architecture)));
            }
            rstd::slice_::sort_unstable(architectures.as_mut_slice().as_mut_ref());
            auto optional_architectures = Option<Vec<String>> {};
            if (! architectures.is_empty()) {
                optional_architectures = Some(rstd::move(architectures));
            }
            auto source = locked_source_wire(*locked_source);
            externals.push(lito::lock::wire::External {
                .name          = external.name.clone(),
                .architectures = rstd::move(optional_architectures),
                .source        = rstd::move(source.source),
                .checksum      = rstd::move(source.checksum),
            });
        }

        auto version = Option<String> {};
        if (package.manifest.version.value.is_some()) {
            version = Some(package.manifest.version.value->clone());
        }
        auto locked_source = rstd_try(locked_package_source(package.source));
        auto source_text   = Option<String> {};
        auto checksum      = Option<String> {};
        if (locked_source.is_some()) {
            auto source = locked_source_wire(*locked_source);
            source_text = Some(rstd::move(source.source));
            checksum    = rstd::move(source.checksum);
        }
        packages.push(lito::lock::wire::Package {
            .name                 = package.manifest.name.clone(),
            .version              = rstd::move(version),
            .source               = rstd::move(source_text),
            .checksum             = rstd::move(checksum),
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

struct LockProjection {
    Toml   document;
    String text;
};

auto graph_projection(const lito::package::ResolvedPackageGraph& graph,
                      u64 format_version = LOCK_FORMAT_VERSION) -> LockResult<LockProjection> {
    auto wire     = rstd_try(graph_wire(graph, format_version));
    auto text     = lito::lock::wire::encode(wire);
    auto document = rstd::toml::from_str(text.as_str());
    if (document.is_err()) {
        return lock_failure<LockProjection>("generated lock document is not valid TOML"_str);
    }
    return Ok(LockProjection {
        .document = rstd::move(document).unwrap_unchecked(),
        .text     = rstd::move(text),
    });
}
auto valid_fetch_url(ref<str> value) -> bool {
    if (value.is_empty()) return false;
    for (const auto character : value) {
        const auto ascii = character.to_primitive();
        if (ascii < 0x20 || ascii == 0x7f) return false;
    }
    return true;
}

auto reject_checksum(const Option<String>& checksum, rstd::serde::DataPath path)
    -> LockResult<empty> {
    if (checksum.is_some()) {
        return lock_data_failure<empty>(path.with_field("checksum"_str),
                                        "checksum is allowed only for archive sources"_str);
    }
    return Ok(empty {});
}

auto parse_sha256_checksum(ref<str> value, rstd::serde::DataPath path)
    -> LockResult<lito::crypto::Sha256Digest> {
    auto digest = value.strip_prefix("sha256:"_str);
    if (digest.is_none()) {
        return lock_data_failure<lito::crypto::Sha256Digest>(
            rstd::move(path), "checksum must start with 'sha256:'"_str);
    }
    auto parsed = lito::parse::parse_sha256(*digest, lito::parse::Sha256TextMode::Canonical);
    if (parsed.is_err()) {
        return lock_data_failure<lito::crypto::Sha256Digest>(
            rstd::move(path),
            "SHA-256 checksum is invalid"_str,
            rstd::move(parsed).unwrap_err_unchecked());
    }
    return Ok(rstd::move(parsed).unwrap_unchecked());
}

auto parse_locked_source(String                value,
                         Option<String>        checksum,
                         rstd::serde::DataPath path,
                         bool                  external_source) -> LockResult<LockedSource> {
    auto git = value.as_str().strip_prefix("git+"_str);
    if (git.is_some()) {
        rstd_try(reject_checksum(checksum, path.clone()));
        auto separated = git->rsplit_once("#"_str);
        if (separated.is_none() || ! valid_fetch_url(separated->get<0>()) ||
            separated->get<0>().contains("#"_str)) {
            return lock_data_failure<LockedSource>(path.with_field("source"_str),
                                                   "Git source must be 'git+<url>#<commit>'"_str);
        }
        if (! lito::source::git_commit_is_valid(separated->get<1>())) {
            return lock_data_failure<LockedSource>(path.with_field("source"_str),
                                                   "Git source commit is not a full object id"_str);
        }
        return Ok(LockedSource::Git(String::make(separated->get<0>()),
                                    String::make(separated->get<1>())));
    }

    auto archive = value.as_str().strip_prefix("archive+"_str);
    if (archive.is_some() && external_source) {
        auto url = lito::parse::FetchUrl::parse(*archive);
        if (url.is_err()) {
            return lock_data_failure<LockedSource>(path.with_field("source"_str),
                                                   "archive source URL is invalid"_str,
                                                   rstd::move(url).unwrap_err_unchecked());
        }
        if (checksum.is_none()) {
            return lock_data_failure<LockedSource>(path.with_field("checksum"_str),
                                                   "archive source checksum is required"_str);
        }
        auto sha256 =
            rstd_try(parse_sha256_checksum(checksum->as_str(), path.with_field("checksum"_str)));
        return Ok(LockedSource::Archive(rstd::move(url).unwrap_unchecked(), rstd::move(sha256)));
    }

    auto registry = value.as_str().strip_prefix("registry+"_str);
    if (registry.is_some() && ! external_source) {
        if (checksum.is_none()) {
            return lock_data_failure<LockedSource>(path.with_field("checksum"_str),
                                                   "Registry source checksum is required"_str);
        }
        auto version = registry->rsplit_once("@"_str);
        if (version.is_none()) {
            return lock_data_failure<LockedSource>(path.with_field("source"_str),
                                                   "Registry source version is missing"_str);
        }
        auto package = version->get<0>().rsplit_once("/"_str);
        if (package.is_none()) {
            return lock_data_failure<LockedSource>(path.with_field("source"_str),
                                                   "Registry source package is missing"_str);
        }
        auto registry_id =
            lito::registry::RegistryId::parse(rstd::format("{}/", package->get<0>()).as_str());
        auto package_name     = lito::registry::RegistryPackageName::parse(package->get<1>());
        auto semantic_version = lito::registry::SemanticVersion::parse(version->get<1>());
        if (registry_id.is_err() || package_name.is_err() || semantic_version.is_err()) {
            return lock_data_failure<LockedSource>(path.with_field("source"_str),
                                                   "Registry source coordinate is invalid"_str);
        }
        auto release_digest = lito::registry::ReleaseDigest::parse(checksum->as_str());
        if (release_digest.is_err()) {
            return lock_data_failure<LockedSource>(
                path.with_field("checksum"_str),
                "Registry checksum is invalid"_str,
                rstd::move(release_digest).unwrap_err_unchecked());
        }
        return Ok(LockedSource::Registry(
            lito::registry::RegistryPackageId {
                .registry = rstd::move(registry_id).unwrap_unchecked(),
                .name     = rstd::move(package_name).unwrap_unchecked(),
            },
            rstd::move(semantic_version).unwrap_unchecked(),
            rstd::move(release_digest).unwrap_unchecked()));
    }

    return lock_data_failure<LockedSource>(path.with_field("source"_str),
                                           "source kind is not allowed here"_str);
}

auto validate_locked_package_source(const Option<LockedSource>& source, ref<str> package)
    -> LockResult<empty> {
    if (source.is_some() && source->is_Registry()) {
        if (source->as_Registry().package.name.as_str() != package) {
            return lock_failure<empty>(
                "lock Registry source package does not match lock package name"_str);
        }
    }
    return Ok(empty {});
}

auto parse_lock_wire(lito::lock::wire::Document document) -> LockResult<LockedProject> {
    auto root = rstd::serde::DataPath();
    if (document.version != LOCK_FORMAT_VERSION) {
        return lock_failure<LockedProject>(
            rstd::format("lock.version {} is not supported; this Lito supports version {}",
                         document.version,
                         LOCK_FORMAT_VERSION));
    }
    if (document.packages.is_empty()) {
        return lock_data_failure<LockedProject>(root.with_field("packages"_str),
                                                "lock packages must not be empty"_str);
    }
    auto names = StringSet::make();
    auto result =
        LockedProject { .packages = Vec<LockedPackage>::with_capacity(document.packages.len()) };
    for (usize package_index {}; package_index < document.packages.len(); ++package_index) {
        auto package_path = root.with_field("packages"_str).with_index(package_index);
        auto package      = rstd::move(document.packages[package_index]);
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
        auto locked_source = Option<LockedSource> {};
        if (package.source.is_some()) {
            locked_source =
                Some(rstd_try(parse_locked_source(rstd::move(package.source).unwrap_unchecked(),
                                                  rstd::move(package.checksum),
                                                  package_path.clone(),
                                                  false)));
        } else {
            if (package.checksum.is_some()) {
                return lock_data_failure<LockedProject>(package_path.with_field("checksum"_str),
                                                        "package checksum requires a source"_str);
            }
        }
        rstd_try(validate_locked_package_source(locked_source, package.name.as_str()));
        if (locked_source.is_some() && locked_source->is_Registry() &&
            (package.version.is_none() ||
             package.version->as_str() != locked_source->as_Registry().version.text().as_str())) {
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
            auto locked_architectures = Vec<Architecture>::make();
            if (external.architectures.is_some()) {
                auto architectures = rstd::move(external.architectures).unwrap_unchecked();
                if (architectures.is_empty()) {
                    return lock_data_failure<LockedProject>(
                        external_path.with_field("architectures"_str),
                        "external architectures must not be empty when present"_str);
                }
                locked_architectures = Vec<Architecture>::with_capacity(architectures.len());
                auto seen            = StringSet::make();
                auto previous        = Option<String> {};
                for (usize index {}; index < architectures.len(); ++index) {
                    auto architecture_path =
                        external_path.with_field("architectures"_str).with_index(index);
                    auto architecture = rstd::move(architectures[index]);
                    auto parsed       = require_architecture(architecture.as_str());
                    if (parsed.is_err()) {
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
                    locked_architectures.push(rstd::move(parsed).unwrap());
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
                .source        = rstd_try(parse_locked_source(rstd::move(external.source),
                                                              rstd::move(external.checksum),
                                                              rstd::move(external_path),
                                                              true)),
            });
        }

        auto validate_edges = [&](Vec<String> values, ref<str> field) -> LockResult<Vec<String>> {
            auto result_values = Vec<String>::with_capacity(values.len());
            auto seen          = StringSet::make();
            for (usize index {}; index < values.len(); ++index) {
                auto edge_path = package_path.with_field(field).with_index(index);
                auto value     = rstd::move(values[index]);
                if (! lito::manifest::valid_package_name(value.as_str())) {
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
            .name                 = rstd::move(package.name),
            .version              = rstd::move(package.version),
            .source               = rstd::move(locked_source),
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
                if (! names.contains_key(value.as_str())) {
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

auto decode_current_lock(const Toml& document) -> LockResult<LockedProject> {
    auto decoded = rstd::toml::decode_value<lito::lock::wire::Document>(document);
    if (decoded.is_err()) {
        return Err(LockError::Data(rstd::move(decoded).unwrap_err_unchecked()));
    }
    return parse_lock_wire(rstd::move(decoded).unwrap_unchecked());
}

struct LoadedLock {
    Toml   document;
    String text;
};

auto load_existing(ref<rstd::path::Path> path) -> LockResult<Option<LoadedLock>> {
    auto exists = rstd::fs::exists(path);
    if (exists.is_err()) {
        return lock_io_failure<Option<LoadedLock>>(
            "inspect"_str, path, rstd::move(exists).unwrap_err());
    }
    if (! *exists) return Ok(Option<LoadedLock> {});
    auto contents = rstd::fs::read_to_string(path);
    if (contents.is_err()) {
        return lock_io_failure<Option<LoadedLock>>(
            "read"_str, path, rstd::move(contents).unwrap_err());
    }
    auto parsed = rstd::toml::from_str(contents->as_str());
    if (parsed.is_err()) {
        return Err(LockError::Toml(PathBuf::from(path), rstd::move(parsed).unwrap_err()));
    }
    return Ok(Some(LoadedLock {
        .document = rstd::move(parsed).unwrap(),
        .text     = rstd::move(contents).unwrap(),
    }));
}

auto lock_document_version(const Toml& document) -> Option<u64> {
    auto version = document.get("version"_str);
    if (version.is_none()) return None();
    auto integer = (**version).as_integer();
    if (integer.is_none() || *integer < i64()) return None();
    return Some(rstd::as_cast<u64>(*integer));
}

auto append_git_pin(lito::source::SourceResolutionOptions& options, ref<str> url, ref<str> commit)
    -> LockResult<empty> {
    for (const auto& existing : options.git_sources) {
        if (existing.git.as_str() != url || existing.commit.as_str() != commit) continue;
        return Ok(empty {});
    }
    options.git_sources.push(lito::source::GitSourcePin {
        .git    = String::make(url),
        .commit = String::make(commit),
    });
    return Ok(empty {});
}

template<typename RegistrySource>
auto append_registry_pin(lito::source::SourceResolutionOptions& options,
                         const RegistrySource&                  source) -> LockResult<empty> {
    for (const auto& existing : options.registry_sources) {
        if (! (existing.package == source.package)) continue;
        if (! (existing.version == source.version) || ! (existing.release == source.release)) {
            return lock_failure<empty>(
                rstd::format("lock contains conflicting exact Registry releases for package '{}'",
                             source.package.name.as_str()));
        }
        return Ok(empty {});
    }
    options.registry_sources.push(lito::source::RegistrySourcePin {
        .package = source.package.clone(),
        .version = source.version.clone(),
        .release = source.release.clone(),
    });
    return Ok(empty {});
}

auto append_project_pins(lito::source::SourceResolutionOptions& options,
                         const LockedProject&                   project) -> LockResult<empty> {
    for (const auto& package : project.packages) {
        if (package.source.is_none()) continue;
        if (package.source->is_Git()) {
            rstd_try(append_git_pin(options,
                                    package.source->as_Git().url.as_str(),
                                    package.source->as_Git().commit.as_str()));
        } else if (package.source->is_Registry()) {
            rstd_try(append_registry_pin(options, package.source->as_Registry()));
        }
    }
    for (const auto& package : project.packages) {
        for (const auto& external : package.externals) {
            if (! external.source.is_Git()) continue;
            rstd_try(append_git_pin(options,
                                    external.source.as_Git().url.as_str(),
                                    external.source.as_Git().commit.as_str()));
        }
    }
    return Ok(empty {});
}

auto write_lock(ref<rstd::path::Path> destination, ref<str> text) -> LockResult<empty> {
    auto written = rstd::fs::write_atomic(destination, text.as_bytes());
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
    return decode_current_lock(loaded->document);
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
        if (invalid != InvalidLockPolicy::Replace || ! error.is_Toml()) {
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

    auto parsed_project = decode_current_lock(existing->document);
    if (parsed_project.is_err()) {
        const auto version = lock_document_version(existing->document);
        if (invalid != InvalidLockPolicy::Replace ||
            (version.is_some() && *version > LOCK_FORMAT_VERSION)) {
            return Err(rstd::move(parsed_project).unwrap_err());
        }
        auto session           = LockSession {};
        session.root_          = PathBuf::from(root);
        session.destination_   = rstd::move(destination);
        session.existing_      = Some(rstd::move(existing->document));
        session.existing_text_ = Some(rstd::move(existing->text));
        session.options_ = lito::source::SourceResolutionOptions { .locked = false, .git = git };
        return Ok(rstd::move(session));
    }

    auto options = lito::source::SourceResolutionOptions { .locked = locked, .git = git };
    auto project = Some(rstd::move(parsed_project).unwrap());
    if (git != lito::source::GitResolutionMode::Refresh) {
        rstd_try(append_project_pins(options, *project));
    }
    auto session           = LockSession {};
    session.locked_        = locked;
    session.root_          = PathBuf::from(root);
    session.destination_   = rstd::move(destination);
    session.existing_      = Some(rstd::move(existing->document));
    session.existing_text_ = Some(rstd::move(existing->text));
    session.options_       = rstd::move(options);
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
    auto desired_result = graph_projection(graph);
    if (desired_result.is_err()) return Err(rstd::move(desired_result).unwrap_err());
    auto desired = rstd::move(desired_result).unwrap();
    if (! (graph.root_directory.as_path().starts_with(session.root_.as_path()) &&
           session.root_.as_path().starts_with(graph.root_directory.as_path()))) {
        return lock_failure<LockStatus>("lock session root does not match resolved graph root"_str);
    }
    if (session.locked_) {
        if (session.existing_.is_some() && *session.existing_ == desired.document) {
            return Ok(LockStatus::Unchanged);
        }
        return lock_failure<LockStatus>(rstd::format(
            "--locked forbids updating stale lock file '{}'", session.destination_.as_path()));
    }

    if (session.existing_.is_some() && *session.existing_ == desired.document &&
        session.existing_text_.is_some() && *session.existing_text_ == desired.text) {
        return Ok(LockStatus::Unchanged);
    }
    auto written = write_lock(session.destination_.as_path(), desired.text.as_str());
    if (written.is_err()) return Err(rstd::move(written).unwrap_err());
    return Ok(LockStatus::Updated);
}
