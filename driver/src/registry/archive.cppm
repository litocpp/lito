module;
#include <rstd/macro.hpp>

export module lito.driver:registry.archive;

import rstd;
import lito.core;
import lito.archive;
import :registry.blob;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::registry
{

struct RegistryArchiveLimits {
    u64   maximum_unpacked_size { u64(512) * u64(1024) * u64(1024) };
    u64   maximum_file_size { u64(64) * u64(1024) * u64(1024) };
    usize maximum_entries { usize(100000) };
};

struct InspectedRegistryArchive {
    RegistryPackageArchive          archive;
    lito::source::SourceTree        tree;
    VerifiedRegistrySourceCandidate candidate;
};

class PackageArchiveInspector {
public:
    static auto inspect_candidate(const VerifiedRegistryBlob& blob,
                                  const RegistryPackageId&    package,
                                  const SemanticVersion&      version,
                                  RegistryArchiveLimits       limits = {})
        -> RegistryArtifactResult<InspectedRegistryArchive>;

    static auto inspect_at_root(const VerifiedRegistryBlob& blob,
                                const RegistryPackageId&    package,
                                const SemanticVersion&      version,
                                ref<rstd::path::Path>       manifest_root,
                                RegistryArchiveLimits       limits = {})
        -> RegistryArtifactResult<InspectedRegistryArchive>;
};

class PackageArchiveBuilder {
public:
    static auto build(const lito::manifest::PackageFileSet&            files,
                      const lito::manifest::StandalonePackageManifest& manifest,
                      const RegistryPackageId&                         package,
                      const SemanticVersion&                           version,
                      PathBuf                                          destination,
                      RegistryArchiveLimits                            limits = {})
        -> RegistryArtifactResult<InspectedRegistryArchive>;

    static auto build(const lito::source::SourceTree& tree,
                      const RegistryPackageId&        package,
                      const SemanticVersion&          version,
                      PathBuf                         destination,
                      RegistryArchiveLimits           limits = {})
        -> RegistryArtifactResult<InspectedRegistryArchive>;
};

} // namespace lito::registry

namespace
{

using namespace lito::registry;

template<typename T>
auto archive_failure(RegistryArtifactErrorKind kind,
                     const RegistryPackageId&  package,
                     String                    message) -> RegistryArtifactResult<T> {
    return Err(RegistryArtifactError {
        .kind    = kind,
        .package = package.clone(),
        .message = rstd::move(message),
    });
}

template<typename T>
auto archive_failure(const RegistryPackageId& package, ref<str> message)
    -> RegistryArtifactResult<T> {
    return archive_failure<T>(RegistryArtifactErrorKind::Archive, package, String::make(message));
}

template<typename T>
auto archive_library_failure(const RegistryPackageId& package, lito::archive::ArchiveError error)
    -> RegistryArtifactResult<T> {
    return archive_failure<T>(error.kind == lito::archive::ArchiveErrorKind::Io
                                  ? RegistryArtifactErrorKind::Io
                                  : RegistryArtifactErrorKind::Archive,
                              package,
                              rstd::move(error.message));
}

auto archive_decoded_size_limit(RegistryArchiveLimits limits, const RegistryPackageId& package)
    -> RegistryArtifactResult<u64> {
    auto entries  = u64(limits.maximum_entries.to_primitive());
    auto overhead = u64(6144);
    if (limits.maximum_unpacked_size > u64::MAX - u64(1024)) {
        return archive_failure<u64>(package, "archive limits overflow the decoded size bound"_str);
    }
    if (entries > (u64::MAX - limits.maximum_unpacked_size - u64(1024)) / overhead) {
        return archive_failure<u64>(package, "archive limits overflow the decoded size bound"_str);
    }
    return Ok(limits.maximum_unpacked_size + entries * overhead + u64(1024));
}

struct RelativeArchivePath {
    String path;
    bool   root {};
};

auto relative_path(ref<str> raw, ref<str> root, bool directory, const RegistryPackageId& package)
    -> RegistryArtifactResult<RelativeArchivePath> {
    auto value = raw;
    if (directory && value.ends_with("/"_str)) {
        value = value.get(usize {}, value.len() - usize(1)).unwrap();
    }
    if (value == root) return Ok(RelativeArchivePath { .root = true });
    auto prefix = rstd::format("{}/", root);
    if (! value.starts_with(prefix.as_str())) {
        return archive_failure<RelativeArchivePath>(
            package, rstd::format("archive entry '{}' is outside top-level root '{}/'", raw, root));
    }
    auto relative = value.get(prefix.len(), value.len()).unwrap();
    if (relative.is_empty()) return Ok(RelativeArchivePath { .root = true });
    return Ok(RelativeArchivePath { .path = String::make(relative) });
}

auto add_directory(lito::source::SourceTree& tree, ref<str> path, const RegistryPackageId& package)
    -> RegistryArtifactResult<empty> {
    auto added = tree.add_directory(path);
    if (added.is_err()) {
        return archive_failure<empty>(RegistryArtifactErrorKind::Archive,
                                      package,
                                      rstd::format("invalid archive directory '{}': {}",
                                                   path,
                                                   rstd::move(added).unwrap_err()));
    }
    return Ok(empty {});
}

auto add_file(lito::source::SourceTree&    tree,
              ref<str>                     path,
              slice<u8>                    bytes,
              lito::source::SourceFileMode mode,
              const RegistryPackageId&     package) -> RegistryArtifactResult<empty> {
    auto added = tree.add_bytes(path, bytes, mode);
    if (added.is_err()) {
        return archive_failure<empty>(
            RegistryArtifactErrorKind::Archive,
            package,
            rstd::format("invalid archive file '{}': {}", path, rstd::move(added).unwrap_err()));
    }
    return Ok(empty {});
}

auto decode_archive(const VerifiedRegistryBlob& blob,
                    const RegistryPackageId&    package,
                    const SemanticVersion&      version,
                    RegistryArchiveLimits       limits)
    -> RegistryArtifactResult<lito::source::SourceTree> {
    auto reader_result = lito::archive::TarZstdReader::open(
        blob.path.as_path(), rstd_try(archive_decoded_size_limit(limits, package)));
    if (reader_result.is_err()) {
        return archive_library_failure<lito::source::SourceTree>(
            package, rstd::move(reader_result).unwrap_err());
    }
    auto reader       = rstd::move(reader_result).unwrap();
    auto top_root     = rstd::format("{}-{}", package.name.as_str(), version.text().as_str());
    auto tree         = lito::source::SourceTree::make();
    auto entry_count  = usize {};
    auto unpacked     = u64 {};
    auto root_entries = usize {};
    while (true) {
        auto next = reader.next_entry();
        if (next.is_err()) {
            return archive_library_failure<lito::source::SourceTree>(package,
                                                                     rstd::move(next).unwrap_err());
        }
        auto optional = rstd::move(next).unwrap();
        if (optional.is_none()) break;
        auto entry = rstd::move(optional).unwrap();
        ++entry_count;
        if (entry_count > limits.maximum_entries) {
            return archive_failure<lito::source::SourceTree>(package,
                                                             "archive has too many entries"_str);
        }
        auto decoded_path = String::from_utf8(rstd::move(entry.path));
        if (decoded_path.is_err()) {
            return archive_failure<lito::source::SourceTree>(
                package, "archive entry path is not valid UTF-8"_str);
        }
        auto raw_path     = rstd::move(decoded_path).unwrap();
        auto is_directory = entry.kind == lito::archive::TarEntryKind::Directory;
        auto relative =
            rstd_try(relative_path(raw_path.as_str(), top_root.as_str(), is_directory, package));
        auto permissions = entry.mode;
        if (is_directory) {
            if (permissions != u32(0755) || entry.size != u64 {}) {
                return archive_failure<lito::source::SourceTree>(
                    package, rstd::format("archive directory '{}' must use mode 0755", raw_path));
            }
            if (relative.root) {
                ++root_entries;
                if (root_entries != usize(1)) {
                    return archive_failure<lito::source::SourceTree>(
                        package, "archive repeats its top-level root"_str);
                }
            } else {
                rstd_try(add_directory(tree, relative.path.as_str(), package));
            }
            auto skipped = reader.skip_entry_data();
            if (skipped.is_err()) {
                return archive_library_failure<lito::source::SourceTree>(
                    package, rstd::move(skipped).unwrap_err());
            }
            continue;
        }
        if (relative.root || (permissions != u32(0644) && permissions != u32(0755))) {
            return archive_failure<lito::source::SourceTree>(
                package, rstd::format("archive file '{}' has an invalid path or mode", raw_path));
        }
        if (entry.size > limits.maximum_file_size) {
            return archive_failure<lito::source::SourceTree>(
                package, rstd::format("archive file '{}' exceeds the file size limit", raw_path));
        }
        if (entry.size > limits.maximum_unpacked_size - unpacked) {
            return archive_failure<lito::source::SourceTree>(
                package, "archive exceeds the unpacked size limit"_str);
        }
        unpacked += entry.size;
        auto bytes  = Vec<u8>::with_capacity(usize(entry.size.to_primitive()));
        auto buffer = array<u8, 65536> {};
        while (bytes.len() < usize(entry.size.to_primitive())) {
            auto wanted = usize(entry.size.to_primitive()) - bytes.len();
            if (wanted > buffer.len()) wanted = buffer.len();
            auto read = reader.read_entry_data(
                mut_ref<u8[]>::from_raw_parts(buffer.as_mut_ptr().as_raw_ptr(), wanted));
            if (read.is_err()) {
                return archive_library_failure<lito::source::SourceTree>(
                    package, rstd::move(read).unwrap_err());
            }
            if (*read == usize {}) {
                return archive_failure<lito::source::SourceTree>(
                    package, "archive file data does not match its declared size"_str);
            }
            bytes.extend_from_slice(slice<u8>::from_raw_parts(buffer.as_ptr().as_raw_ptr(), *read));
        }
        rstd_try(add_file(tree,
                          relative.path.as_str(),
                          bytes.as_slice(),
                          permissions == u32(0755) ? lito::source::SourceFileMode::Executable
                                                   : lito::source::SourceFileMode::Regular,
                          package));
    }
    auto finished = reader.finish();
    if (finished.is_err()) {
        return archive_library_failure<lito::source::SourceTree>(package,
                                                                 rstd::move(finished).unwrap_err());
    }
    if (root_entries != usize(1)) {
        return archive_failure<lito::source::SourceTree>(
            package, "archive has no unique top-level package root"_str);
    }
    return Ok(rstd::move(tree));
}

auto contains_string(slice<String> values, ref<str> value) noexcept -> bool {
    for (const auto& candidate : values) {
        if (candidate.as_str() == value) return true;
    }
    return false;
}

auto archive_directories(const lito::source::SourceTree& tree) -> Vec<String> {
    auto directories = Vec<String>::make();
    for (const auto& entry : tree.entries()) {
        if (entry.kind() != lito::source::SourceEntryKind::File) continue;
        auto remaining = entry.path().as_str();
        auto prefix    = String::make();
        while (true) {
            auto separated = remaining.split_once("/"_str);
            if (separated.is_none()) break;
            if (! prefix.is_empty()) prefix.push_ascii('/');
            prefix.push_str(separated->template get<0>());
            if (! contains_string(directories.as_slice(), prefix.as_str())) {
                directories.push(prefix.clone());
            }
            remaining = separated->template get<1>();
        }
    }
    rstd::slice_::sort_unstable_by(directories.as_mut_slice().as_mut_ref(),
                                   [](const String& left, const String& right) {
                                       return left < right.as_str();
                                   });
    return directories;
}

auto inspect_candidate_with_root(const VerifiedRegistryBlob&   blob,
                                 const RegistryPackageId&      package,
                                 const SemanticVersion&        version,
                                 Option<ref<rstd::path::Path>> manifest_root,
                                 RegistryArchiveLimits         limits)
    -> RegistryArtifactResult<InspectedRegistryArchive> {
    auto tree      = rstd_try(decode_archive(blob, package, version, limits));
    auto candidate = manifest_root.is_some()
                         ? inspect_registry_source_tree_at(tree, package, version, *manifest_root)
                         : inspect_registry_source_tree(tree, package, version);
    if (candidate.is_err()) return Err(rstd::move(candidate).unwrap_err());
    return Ok(InspectedRegistryArchive {
        .archive =
            RegistryPackageArchive {
                .checksum = blob.checksum.clone(),
                .size     = RegistryBlobSize(blob.size),
                .format = RegistryArchiveFormat::parse(RegistryArchiveFormat::TAR_ZSTD_V1).unwrap(),
            },
        .tree      = rstd::move(tree),
        .candidate = rstd::move(candidate).unwrap(),
    });
}

} // namespace

auto lito::registry::PackageArchiveBuilder::build(
    const lito::manifest::PackageFileSet&            files,
    const lito::manifest::StandalonePackageManifest& manifest,
    const RegistryPackageId&                         package,
    const SemanticVersion&                           version,
    PathBuf                                          destination,
    RegistryArchiveLimits limits) -> RegistryArtifactResult<InspectedRegistryArchive> {
    auto tree     = files.tree().clone();
    auto replaced = tree.replace_text("lito.toml"_str, manifest.as_str());
    if (replaced.is_err()) {
        return archive_failure<InspectedRegistryArchive>(
            RegistryArtifactErrorKind::Manifest,
            package,
            rstd::format("cannot install standalone manifest into package file set: {}",
                         rstd::move(replaced).unwrap_err()));
    }
    return build(tree, package, version, rstd::move(destination), limits);
}

auto lito::registry::PackageArchiveInspector::inspect_at_root(const VerifiedRegistryBlob& blob,
                                                              const RegistryPackageId&    package,
                                                              const SemanticVersion&      version,
                                                              ref<rstd::path::Path> manifest_root,
                                                              RegistryArchiveLimits limits)
    -> RegistryArtifactResult<InspectedRegistryArchive> {
    return inspect_candidate_with_root(blob, package, version, Some(manifest_root), limits);
}

auto lito::registry::PackageArchiveInspector::inspect_candidate(const VerifiedRegistryBlob& blob,
                                                                const RegistryPackageId&    package,
                                                                const SemanticVersion&      version,
                                                                RegistryArchiveLimits       limits)
    -> RegistryArtifactResult<InspectedRegistryArchive> {
    return inspect_candidate_with_root(blob, package, version, None(), limits);
}

auto lito::registry::PackageArchiveBuilder::build(const lito::source::SourceTree& tree,
                                                  const RegistryPackageId&        package,
                                                  const SemanticVersion&          version,
                                                  PathBuf                         destination,
                                                  RegistryArchiveLimits           limits)
    -> RegistryArtifactResult<InspectedRegistryArchive> {
    auto parent = destination.as_path().parent();
    if (parent.is_some()) {
        auto created = rstd::fs::create_dir_all(*parent);
        if (created.is_err()) {
            return archive_failure<InspectedRegistryArchive>(
                RegistryArtifactErrorKind::Io,
                package,
                rstd::format("cannot create archive output directory '{}': {}",
                             *parent,
                             rstd::move(created).unwrap_err()));
        }
    }
    auto writer_result = lito::archive::TarZstdWriter::create(destination.as_path());
    if (writer_result.is_err()) {
        return archive_library_failure<InspectedRegistryArchive>(
            package, rstd::move(writer_result).unwrap_err());
    }
    auto writer     = rstd::move(writer_result).unwrap();
    auto root       = rstd::format("{}-{}", package.name.as_str(), version.text());
    auto wrote_root = writer.write_directory(root.as_str().as_bytes(), u32(0755));
    if (wrote_root.is_err()) {
        return archive_library_failure<InspectedRegistryArchive>(
            package, rstd::move(wrote_root).unwrap_err());
    }
    for (const auto& directory : archive_directories(tree)) {
        auto path  = rstd::format("{}/{}", root, directory);
        auto wrote = writer.write_directory(path.as_str().as_bytes(), u32(0755));
        if (wrote.is_err()) {
            return archive_library_failure<InspectedRegistryArchive>(
                package, rstd::move(wrote).unwrap_err());
        }
    }
    for (const auto& entry : tree.entries()) {
        if (entry.kind() != lito::source::SourceEntryKind::File) continue;
        auto path  = rstd::format("{}/{}", root, entry.path().as_str());
        auto wrote = writer.write_file(
            path.as_str().as_bytes(),
            entry.mode() == lito::source::SourceFileMode::Executable ? u32(0755) : u32(0644),
            entry.contents());
        if (wrote.is_err()) {
            return archive_library_failure<InspectedRegistryArchive>(
                package, rstd::move(wrote).unwrap_err());
        }
    }
    auto finished = writer.finish();
    if (finished.is_err()) {
        return archive_library_failure<InspectedRegistryArchive>(package,
                                                                 rstd::move(finished).unwrap_err());
    }
    auto archive = rstd_try(registry_package_archive_from_file(destination.as_path(), package));
    auto verified =
        rstd_try(verify_registry_blob_file(destination.clone(), package, archive.checksum));
    return PackageArchiveInspector::inspect_candidate(verified, package, version, limits);
}
