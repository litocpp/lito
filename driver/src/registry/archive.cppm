module;
#include <archive.h>
#include <archive_entry.h>
#include <cstddef>
#include <cstring>
#include <rstd/macro.hpp>

export module lito.driver:registry.archive;

import rstd;
import lito.core;
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
    RegistryBlobProjection          blob;
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

    static auto inspect(const VerifiedRegistryBlob&      blob,
                        const RegistryPackageId&         package,
                        const RegistryReleaseProjection& release,
                        RegistryArchiveLimits            limits = {})
        -> RegistryArtifactResult<InspectedRegistryArchive>;
};

class PackageArchiveBuilder {
public:
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

class ArchiveReader {
    archive* value_ {};

public:
    explicit ArchiveReader(archive* value): value_(value) {}
    ArchiveReader(const ArchiveReader&)                    = delete;
    auto operator=(const ArchiveReader&) -> ArchiveReader& = delete;
    ~ArchiveReader() {
        if (value_ != nullptr) archive_read_free(value_);
    }
    auto get() const noexcept -> archive* { return value_; }
};

class ArchiveWriter {
    archive* value_ {};

public:
    explicit ArchiveWriter(archive* value): value_(value) {}
    ArchiveWriter(const ArchiveWriter&)                    = delete;
    auto operator=(const ArchiveWriter&) -> ArchiveWriter& = delete;
    ~ArchiveWriter() {
        if (value_ != nullptr) archive_write_free(value_);
    }
    auto get() const noexcept -> archive* { return value_; }
};

class ArchiveEntry {
    archive_entry* value_ {};

public:
    ArchiveEntry(): value_(archive_entry_new()) {}
    ArchiveEntry(const ArchiveEntry&)                    = delete;
    auto operator=(const ArchiveEntry&) -> ArchiveEntry& = delete;
    ~ArchiveEntry() {
        if (value_ != nullptr) archive_entry_free(value_);
    }
    auto get() const noexcept -> archive_entry* { return value_; }
};

auto archive_message(archive* reader, ref<str> operation) -> String {
    auto message = archive_error_string(reader);
    if (message == nullptr) return String::make(operation);
    auto text = rstd::ffi::CStr::from_ptr(message).to_str();
    return text.is_err() ? String::make(operation)
                         : rstd::format("{}: {}", operation, text.unwrap());
}

auto archive_path(archive_entry* entry, const RegistryPackageId& package)
    -> RegistryArtifactResult<String> {
    auto raw = archive_entry_pathname_utf8(entry);
    if (raw == nullptr) {
        return archive_failure<String>(package, "archive entry path is not valid UTF-8"_str);
    }
    auto bytes   = Vec<u8>::from(rstd::ffi::CStr::from_ptr(raw).to_bytes());
    auto decoded = String::from_utf8(rstd::move(bytes));
    if (decoded.is_err()) {
        return archive_failure<String>(package, "archive entry path is not valid UTF-8"_str);
    }
    return Ok(rstd::move(decoded).unwrap());
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
    auto reader = ArchiveReader(archive_read_new());
    if (reader.get() == nullptr) {
        return archive_failure<lito::source::SourceTree>(package,
                                                         "cannot allocate archive reader"_str);
    }
    if (archive_read_support_filter_zstd(reader.get()) != ARCHIVE_OK ||
        archive_read_support_format_tar(reader.get()) != ARCHIVE_OK) {
        return archive_failure<lito::source::SourceTree>(
            RegistryArtifactErrorKind::Archive,
            package,
            archive_message(reader.get(), "cannot enable tar.zstd decoder"_str));
    }
    auto path = blob.path.as_path().to_str();
    if (path.is_none()) {
        return archive_failure<lito::source::SourceTree>(package,
                                                         "archive path is not valid UTF-8"_str);
    }
    auto c_path = alloc::ffi::CString::make(String::make(*path));
    if (c_path.is_err()) {
        return archive_failure<lito::source::SourceTree>(package,
                                                         "archive path contains a nul byte"_str);
    }
    if (archive_read_open_filename(reader.get(), c_path->as_ptr(), std::size_t(65536)) !=
        ARCHIVE_OK) {
        return archive_failure<lito::source::SourceTree>(
            RegistryArtifactErrorKind::Archive,
            package,
            archive_message(reader.get(), "cannot open tar.zstd archive"_str));
    }
    auto top_root       = rstd::format("{}-{}", package.name.as_str(), version.text().as_str());
    auto tree           = lito::source::SourceTree::make();
    auto entry_count    = usize {};
    auto unpacked       = u64 {};
    auto root_entries   = usize {};
    auto checked_format = false;
    while (true) {
        archive_entry* entry  = nullptr;
        auto           status = archive_read_next_header(reader.get(), &entry);
        if (status == ARCHIVE_EOF) break;
        if (status != ARCHIVE_OK || entry == nullptr) {
            return archive_failure<lito::source::SourceTree>(
                RegistryArtifactErrorKind::Archive,
                package,
                archive_message(reader.get(), "cannot read archive header"_str));
        }
        if (! checked_format) {
            checked_format = true;
            if (archive_filter_code(reader.get(), 0) != ARCHIVE_FILTER_ZSTD ||
                (archive_format(reader.get()) & ARCHIVE_FORMAT_BASE_MASK) != ARCHIVE_FORMAT_TAR) {
                return archive_failure<lito::source::SourceTree>(
                    package, "archive must be a zstd-compressed tar stream"_str);
            }
        }
        ++entry_count;
        if (entry_count > limits.maximum_entries) {
            return archive_failure<lito::source::SourceTree>(package,
                                                             "archive has too many entries"_str);
        }
        if (archive_entry_hardlink(entry) != nullptr || archive_entry_symlink(entry) != nullptr ||
            archive_entry_sparse_count(entry) != 0 || archive_entry_is_encrypted(entry) == 1) {
            return archive_failure<lito::source::SourceTree>(
                package, "archive links, sparse files, and encrypted entries are forbidden"_str);
        }
        auto file_type    = archive_entry_filetype(entry);
        auto is_directory = file_type == AE_IFDIR;
        auto is_file      = file_type == AE_IFREG;
        if (! is_directory && ! is_file) {
            return archive_failure<lito::source::SourceTree>(
                package, "archive entry must be a directory or regular file"_str);
        }
        auto raw_path = rstd_try(archive_path(entry, package));
        auto relative =
            rstd_try(relative_path(raw_path.as_str(), top_root.as_str(), is_directory, package));
        auto permissions = archive_entry_perm(entry);
        if (is_directory) {
            if (permissions != 0755) {
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
            if (archive_read_data_skip(reader.get()) != ARCHIVE_OK) {
                return archive_failure<lito::source::SourceTree>(
                    RegistryArtifactErrorKind::Archive,
                    package,
                    archive_message(reader.get(), "cannot skip archive directory data"_str));
            }
            continue;
        }
        if (relative.root || (permissions != 0644 && permissions != 0755)) {
            return archive_failure<lito::source::SourceTree>(
                package, rstd::format("archive file '{}' has an invalid path or mode", raw_path));
        }
        auto declared_size = archive_entry_size(entry);
        if (declared_size < 0 || static_cast<unsigned long long>(declared_size) >
                                     limits.maximum_file_size.to_primitive()) {
            return archive_failure<lito::source::SourceTree>(
                package, rstd::format("archive file '{}' exceeds the file size limit", raw_path));
        }
        auto size = u64(static_cast<unsigned long long>(declared_size));
        if (unpacked > limits.maximum_unpacked_size - size) {
            return archive_failure<lito::source::SourceTree>(
                package, "archive exceeds the unpacked size limit"_str);
        }
        unpacked += size;
        auto bytes  = Vec<u8>::with_capacity(usize(size.to_primitive()));
        auto buffer = array<u8, 65536> {};
        while (true) {
            auto read = archive_read_data(
                reader.get(), buffer.as_mut_ptr().as_raw_ptr(), buffer.len().to_primitive());
            if (read < 0) {
                return archive_failure<lito::source::SourceTree>(
                    RegistryArtifactErrorKind::Archive,
                    package,
                    archive_message(reader.get(), "cannot read archive file data"_str));
            }
            if (read == 0) break;
            bytes.extend_from_slice(slice<u8>::from_raw_parts(
                buffer.as_ptr().as_raw_ptr(), usize(static_cast<std::size_t>(read))));
            if (bytes.len() > usize(size.to_primitive())) {
                return archive_failure<lito::source::SourceTree>(
                    package, "archive file data exceeds its declared size"_str);
            }
        }
        if (bytes.len() != usize(size.to_primitive())) {
            return archive_failure<lito::source::SourceTree>(
                package, "archive file data does not match its declared size"_str);
        }
        rstd_try(add_file(tree,
                          relative.path.as_str(),
                          bytes.as_slice(),
                          permissions == 0755 ? lito::source::SourceFileMode::Executable
                                              : lito::source::SourceFileMode::Regular,
                          package));
    }
    if (! checked_format || root_entries != usize(1)) {
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

auto write_archive_entry(archive*                     writer,
                         ref<str>                     path,
                         bool                         directory,
                         slice<u8>                    contents,
                         lito::source::SourceFileMode mode,
                         const RegistryPackageId&     package) -> RegistryArtifactResult<empty> {
    auto c_path = alloc::ffi::CString::make(String::make(path));
    if (c_path.is_err()) {
        return archive_failure<empty>(package, "archive path contains a nul byte"_str);
    }
    auto entry = ArchiveEntry {};
    if (entry.get() == nullptr) {
        return archive_failure<empty>(package, "cannot allocate archive entry"_str);
    }
    archive_entry_set_pathname_utf8(entry.get(), c_path->as_ptr());
    archive_entry_set_filetype(entry.get(), directory ? AE_IFDIR : AE_IFREG);
    archive_entry_set_perm(
        entry.get(), directory || mode == lito::source::SourceFileMode::Executable ? 0755 : 0644);
    archive_entry_set_size(entry.get(), directory ? 0 : contents.len().to_primitive());
    archive_entry_set_uid(entry.get(), 0);
    archive_entry_set_gid(entry.get(), 0);
    archive_entry_set_uname(entry.get(), "");
    archive_entry_set_gname(entry.get(), "");
    archive_entry_set_mtime(entry.get(), 0, 0);
    archive_entry_set_atime(entry.get(), 0, 0);
    archive_entry_set_ctime(entry.get(), 0, 0);
    if (archive_write_header(writer, entry.get()) != ARCHIVE_OK) {
        return archive_failure<empty>(RegistryArtifactErrorKind::Archive,
                                      package,
                                      archive_message(writer, "cannot write archive header"_str));
    }
    auto remaining = contents;
    while (! remaining.is_empty()) {
        auto written =
            archive_write_data(writer, remaining.as_raw_ptr(), remaining.len().to_primitive());
        if (written <= 0) {
            return archive_failure<empty>(RegistryArtifactErrorKind::Archive,
                                          package,
                                          archive_message(writer, "cannot write archive data"_str));
        }
        auto consumed = usize(static_cast<std::size_t>(written));
        remaining     = slice<u8>::from_raw_parts(remaining.as_raw_ptr() + consumed.to_primitive(),
                                                  remaining.len() - consumed);
    }
    if (archive_write_finish_entry(writer) != ARCHIVE_OK) {
        return archive_failure<empty>(RegistryArtifactErrorKind::Archive,
                                      package,
                                      archive_message(writer, "cannot finish archive entry"_str));
    }
    return Ok(empty {});
}

} // namespace

auto lito::registry::PackageArchiveInspector::inspect(const VerifiedRegistryBlob&      blob,
                                                      const RegistryPackageId&         package,
                                                      const RegistryReleaseProjection& release,
                                                      RegistryArchiveLimits            limits)
    -> RegistryArtifactResult<InspectedRegistryArchive> {
    if (! (blob.digest == release.blob.digest) || blob.size != release.blob.size.value()) {
        return archive_failure<InspectedRegistryArchive>(
            RegistryArtifactErrorKind::Digest,
            package,
            String::make("verified blob does not match the selected Registry release"_str));
    }
    auto  inspected = rstd_try(inspect_candidate(blob, package, release.version, limits));
    auto& candidate = inspected.candidate;
    if (! (candidate.source_digest == release.source)) {
        return archive_failure<InspectedRegistryArchive>(
            RegistryArtifactErrorKind::Source,
            package,
            String::make("archive source digest does not match Registry metadata"_str));
    }
    if (! (candidate.manifest_digest == release.manifest)) {
        return archive_failure<InspectedRegistryArchive>(
            RegistryArtifactErrorKind::Manifest,
            package,
            String::make("archive manifest digest does not match Registry metadata"_str));
    }
    if (! registry_dependencies_match(candidate.dependencies.as_slice(),
                                      release.dependencies.as_slice())) {
        return archive_failure<InspectedRegistryArchive>(
            RegistryArtifactErrorKind::Projection,
            package,
            String::make("archive dependency projection does not match Registry metadata"_str));
    }
    return Ok(rstd::move(inspected));
}

auto lito::registry::PackageArchiveInspector::inspect_candidate(const VerifiedRegistryBlob& blob,
                                                                const RegistryPackageId&    package,
                                                                const SemanticVersion&      version,
                                                                RegistryArchiveLimits       limits)
    -> RegistryArtifactResult<InspectedRegistryArchive> {
    auto tree      = rstd_try(decode_archive(blob, package, version, limits));
    auto candidate = rstd_try(inspect_registry_source_tree(tree, package, version));
    return Ok(InspectedRegistryArchive {
        .blob =
            RegistryBlobProjection {
                .digest = blob.digest.clone(),
                .size   = RegistryBlobSize(blob.size),
                .format = RegistryArchiveFormat::parse(RegistryArchiveFormat::TAR_ZSTD_V1).unwrap(),
            },
        .tree      = rstd::move(tree),
        .candidate = rstd::move(candidate),
    });
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
    auto writer = ArchiveWriter(archive_write_new());
    if (writer.get() == nullptr) {
        return archive_failure<InspectedRegistryArchive>(package,
                                                         "cannot allocate archive writer"_str);
    }
    if (archive_write_set_format_pax_restricted(writer.get()) != ARCHIVE_OK ||
        archive_write_add_filter_zstd(writer.get()) != ARCHIVE_OK ||
        archive_write_set_filter_option(writer.get(), "zstd", "compression-level", "19") !=
            ARCHIVE_OK ||
        archive_write_set_filter_option(writer.get(), "zstd", "threads", "1") != ARCHIVE_OK) {
        return archive_failure<InspectedRegistryArchive>(
            RegistryArtifactErrorKind::Archive,
            package,
            archive_message(writer.get(), "cannot configure deterministic tar.zstd writer"_str));
    }
    auto output = destination.as_path().to_str();
    if (output.is_none()) {
        return archive_failure<InspectedRegistryArchive>(package,
                                                         "archive output path is not UTF-8"_str);
    }
    auto c_output = alloc::ffi::CString::make(String::make(*output));
    if (c_output.is_err() ||
        archive_write_open_filename(writer.get(), c_output->as_ptr()) != ARCHIVE_OK) {
        return archive_failure<InspectedRegistryArchive>(
            RegistryArtifactErrorKind::Archive,
            package,
            archive_message(writer.get(), "cannot open tar.zstd archive output"_str));
    }
    auto root = rstd::format("{}-{}", package.name.as_str(), version.text());
    rstd_try(write_archive_entry(
        writer.get(), root.as_str(), true, {}, lito::source::SourceFileMode::Regular, package));
    for (const auto& directory : archive_directories(tree)) {
        auto path = rstd::format("{}/{}", root, directory);
        rstd_try(write_archive_entry(
            writer.get(), path.as_str(), true, {}, lito::source::SourceFileMode::Regular, package));
    }
    for (const auto& entry : tree.entries()) {
        if (entry.kind() != lito::source::SourceEntryKind::File) continue;
        auto path = rstd::format("{}/{}", root, entry.path().as_str());
        rstd_try(write_archive_entry(
            writer.get(), path.as_str(), false, entry.contents(), entry.mode(), package));
    }
    if (archive_write_close(writer.get()) != ARCHIVE_OK) {
        return archive_failure<InspectedRegistryArchive>(
            RegistryArtifactErrorKind::Archive,
            package,
            archive_message(writer.get(), "cannot finish tar.zstd archive"_str));
    }
    auto blob     = rstd_try(registry_blob_projection_from_file(destination.as_path(), package));
    auto verified = rstd_try(verify_registry_blob_file(destination.clone(), package, blob));
    return PackageArchiveInspector::inspect_candidate(verified, package, version, limits);
}
