module;
#include <rstd/enum.hpp>

export module lito.core:source.tree;

import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

enum class SourceEntryKind
{
    Directory,
    File,
};

enum class SourceFileMode
{
    Regular,
    Executable,
};

class SourceTreeError {
    RSTD_ENUM(SourceTreeError,
              (InvalidPath, (String path; String reason;)),
              (Conflict, (String path; String reason;)),
              (DestinationExists, (rstd::path::PathBuf path;)),
              (Io, (String operation; rstd::path::PathBuf path; rstd::io::error::Error source;)))
};

template<typename T>
using SourceTreeResult = Result<T, SourceTreeError>;

class SourcePath {
    String value_;

    explicit SourcePath(String value): value_(rstd::move(value)) {}

public:
    static auto parse(ref<str> value) -> SourceTreeResult<SourcePath>;

    auto as_path() const noexcept -> ref<rstd::path::Path> {
        return ref<rstd::path::Path>(value_.as_str());
    }
    auto as_str() const noexcept -> ref<str> { return value_.as_str(); }
    auto less(const SourcePath& other) const noexcept -> bool { return value_ < other.as_str(); }
    auto clone() const -> SourcePath { return SourcePath(value_.clone()); }
};

class SourceTreeEntry {
    SourcePath      path_;
    SourceEntryKind kind_ { SourceEntryKind::File };
    Vec<u8>         contents_;
    SourceFileMode  mode_ { SourceFileMode::Regular };

    SourceTreeEntry(SourcePath path, SourceEntryKind kind, Vec<u8> contents, SourceFileMode mode)
        : path_(rstd::move(path)), kind_(kind), contents_(rstd::move(contents)), mode_(mode) {}
    friend class SourceTree;

public:
    auto path() const noexcept -> const SourcePath& { return path_; }
    auto kind() const noexcept -> SourceEntryKind { return kind_; }
    auto contents() const noexcept -> slice<u8> { return contents_.as_slice(); }
    auto mode() const noexcept -> SourceFileMode { return mode_; }
    auto clone() const -> SourceTreeEntry {
        return SourceTreeEntry(path_.clone(), kind_, contents_.clone(), mode_);
    }
};

class SourceTree {
    Vec<SourceTreeEntry> entries_;

    SourceTree() noexcept = default;
    explicit SourceTree(Vec<SourceTreeEntry> entries): entries_(rstd::move(entries)) {}

    auto add_entry(SourceTreeEntry entry) -> SourceTreeResult<empty>;
    auto find(ref<str> path) noexcept -> Option<usize>;
    auto find(ref<str> path) const noexcept -> Option<usize>;

public:
    static auto make() -> SourceTree { return {}; }

    auto add_directory(ref<str> path) -> SourceTreeResult<empty>;
    auto add_bytes(ref<str> path, slice<u8> contents, SourceFileMode mode = SourceFileMode::Regular)
        -> SourceTreeResult<empty>;
    auto add_text(ref<str> path, ref<str> contents, SourceFileMode mode = SourceFileMode::Regular)
        -> SourceTreeResult<empty>;
    auto replace_bytes(ref<str>       path,
                       slice<u8>      contents,
                       SourceFileMode mode = SourceFileMode::Regular) -> SourceTreeResult<empty>;
    auto replace_text(ref<str>       path,
                      ref<str>       contents,
                      SourceFileMode mode = SourceFileMode::Regular) -> SourceTreeResult<empty>;
    auto remove(ref<str> path) -> SourceTreeResult<empty>;
    auto extend(const SourceTree& other) -> SourceTreeResult<empty>;
    auto clone() const -> SourceTree;
    auto entries() const noexcept -> slice<SourceTreeEntry> { return entries_.as_slice(); }
};

struct SourceMaterialization {
    rstd::path::PathBuf root;
    usize               entries {};
};

auto materialize_source_tree(const SourceTree& tree, ref<rstd::path::Path> destination)
    -> SourceTreeResult<SourceMaterialization>;

} // namespace lito

namespace lito
{

auto invalid_source_path(ref<str> path, ref<str> reason) -> SourceTreeResult<SourcePath> {
    return Err(SourceTreeError::InvalidPath(String::make(path), String::make(reason)));
}

auto SourcePath::parse(ref<str> value) -> SourceTreeResult<SourcePath> {
    if (value.is_empty()) return invalid_source_path(value, "path must not be empty"_str);
    if (value.starts_with("/"_str) || value.ends_with("/"_str)) {
        return invalid_source_path(value, "path must be relative without trailing separators"_str);
    }
    if (value.contains("//"_str) || value.contains("\\"_str)) {
        return invalid_source_path(value, "path must use non-empty '/' components"_str);
    }
    if (value.contains("\0"_str)) {
        return invalid_source_path(value, "path must not contain NUL"_str);
    }
    auto remaining = value;
    while (true) {
        auto separated = remaining.split_once("/"_str);
        auto component = separated.is_some() ? separated->template get<0>() : remaining;
        if (component == "."_str || component == ".."_str) {
            return invalid_source_path(value, "path must not contain '.' or '..'"_str);
        }
        if (component.contains(":"_str)) {
            return invalid_source_path(value, "path must not contain platform prefixes"_str);
        }
        if (separated.is_none()) break;
        remaining = separated->template get<1>();
    }
    auto path = rstd::path::PathBuf::from(value);
    if (! path.as_path().is_safe_relative()) {
        return invalid_source_path(value, "path escapes its materialization root"_str);
    }
    return Ok(SourcePath(String::make(value)));
}

auto source_tree_conflict(ref<str> path, String reason) -> SourceTreeResult<empty> {
    return Err(SourceTreeError::Conflict(String::make(path), rstd::move(reason)));
}

auto entry_kind_name(SourceEntryKind kind) -> ref<str> {
    return kind == SourceEntryKind::Directory ? "directory"_str : "file"_str;
}

auto SourceTree::find(ref<str> path) noexcept -> Option<usize> {
    for (auto index = usize {}; index < entries_.len(); ++index) {
        if (entries_[index].path().as_str() == path) return Some(index);
    }
    return None();
}

auto SourceTree::find(ref<str> path) const noexcept -> Option<usize> {
    for (auto index = usize {}; index < entries_.len(); ++index) {
        if (entries_[index].path().as_str() == path) return Some(index);
    }
    return None();
}

auto SourceTree::add_entry(SourceTreeEntry entry) -> SourceTreeResult<empty> {
    for (const auto& existing : entries_) {
        if (existing.path().as_str() == entry.path().as_str()) {
            return source_tree_conflict(
                entry.path().as_str(),
                rstd::format("{} already exists", entry_kind_name(existing.kind())));
        }
        auto existing_is_parent = entry.path().as_path().starts_with(existing.path().as_path());
        if (existing_is_parent && existing.kind() == SourceEntryKind::File) {
            return source_tree_conflict(
                entry.path().as_str(),
                rstd::format("file '{}' cannot contain another entry", existing.path().as_str()));
        }
        auto entry_is_parent = existing.path().as_path().starts_with(entry.path().as_path());
        if (entry_is_parent && entry.kind() == SourceEntryKind::File) {
            return source_tree_conflict(
                entry.path().as_str(),
                rstd::format("file cannot contain existing entry '{}'", existing.path().as_str()));
        }
    }
    entries_.push(rstd::move(entry));
    rstd::slice_::sort_unstable_by(entries_.as_mut_slice().as_mut_ref(),
                                   [](const SourceTreeEntry& left, const SourceTreeEntry& right) {
                                       return left.path().less(right.path());
                                   });
    return Ok(empty {});
}

auto SourceTree::add_directory(ref<str> path) -> SourceTreeResult<empty> {
    auto parsed = SourcePath::parse(path);
    if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
    return add_entry(SourceTreeEntry(rstd::move(parsed).unwrap(),
                                     SourceEntryKind::Directory,
                                     Vec<u8>::make(),
                                     SourceFileMode::Regular));
}

auto SourceTree::add_bytes(ref<str> path, slice<u8> contents, SourceFileMode mode)
    -> SourceTreeResult<empty> {
    auto parsed = SourcePath::parse(path);
    if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
    return add_entry(SourceTreeEntry(
        rstd::move(parsed).unwrap(), SourceEntryKind::File, Vec<u8>::from(contents), mode));
}

auto SourceTree::add_text(ref<str> path, ref<str> contents, SourceFileMode mode)
    -> SourceTreeResult<empty> {
    return add_bytes(path, contents.as_bytes(), mode);
}

auto SourceTree::replace_bytes(ref<str> path, slice<u8> contents, SourceFileMode mode)
    -> SourceTreeResult<empty> {
    auto parsed = SourcePath::parse(path);
    if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
    auto index = find(path);
    if (index.is_none()) {
        return source_tree_conflict(path, String::make("file does not exist"_str));
    }
    if (entries_[*index].kind() != SourceEntryKind::File) {
        return source_tree_conflict(path, String::make("entry is not a file"_str));
    }
    entries_[*index].contents_ = Vec<u8>::from(contents);
    entries_[*index].mode_     = mode;
    return Ok(empty {});
}

auto SourceTree::replace_text(ref<str> path, ref<str> contents, SourceFileMode mode)
    -> SourceTreeResult<empty> {
    return replace_bytes(path, contents.as_bytes(), mode);
}

auto SourceTree::remove(ref<str> path) -> SourceTreeResult<empty> {
    auto parsed = SourcePath::parse(path);
    if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
    auto index = find(path);
    if (index.is_none()) {
        return source_tree_conflict(path, String::make("entry does not exist"_str));
    }
    if (entries_[*index].kind() == SourceEntryKind::Directory) {
        for (const auto& existing : entries_) {
            if (existing.path().as_str() != path &&
                existing.path().as_path().starts_with(entries_[*index].path().as_path())) {
                return source_tree_conflict(
                    path, rstd::format("directory contains entry '{}'", existing.path().as_str()));
            }
        }
    }
    (void)entries_.remove(*index);
    return Ok(empty {});
}

auto SourceTree::extend(const SourceTree& other) -> SourceTreeResult<empty> {
    auto combined = clone();
    for (const auto& entry : other.entries_) {
        auto added = combined.add_entry(entry.clone());
        if (added.is_err()) return Err(rstd::move(added).unwrap_err());
    }
    *this = rstd::move(combined);
    return Ok(empty {});
}

auto SourceTree::clone() const -> SourceTree {
    auto entries = Vec<SourceTreeEntry>::with_capacity(entries_.len());
    for (const auto& entry : entries_) entries.push(entry.clone());
    return SourceTree(rstd::move(entries));
}

auto source_tree_io_failure(ref<str>               operation,
                            ref<rstd::path::Path>  path,
                            rstd::io::error::Error error) -> SourceTreeError {
    return SourceTreeError::Io(
        String::make(operation), rstd::path::PathBuf::from(path), rstd::move(error));
}

auto cleanup_materialization(ref<rstd::path::Path> destination) noexcept -> void {
    auto removed = rstd::fs::remove_dir_all(destination);
    if (removed.is_err()) {
        rstd::io::eprintln("lito: cannot clean incomplete source tree '{}': {}",
                           destination,
                           removed.unwrap_err());
    }
}

auto materialize_source_tree(const SourceTree& tree, ref<rstd::path::Path> destination)
    -> SourceTreeResult<SourceMaterialization> {
    auto exists = rstd::fs::exists(destination);
    if (exists.is_err()) {
        return Err(source_tree_io_failure("inspect materialization destination"_str,
                                          destination,
                                          rstd::move(exists).unwrap_err()));
    }
    if (*exists) {
        return Err(SourceTreeError::DestinationExists(rstd::path::PathBuf::from(destination)));
    }
    auto created = rstd::fs::create_dir(destination);
    if (created.is_err()) {
        return Err(source_tree_io_failure("create materialization destination"_str,
                                          destination,
                                          rstd::move(created).unwrap_err()));
    }

    for (const auto& entry : tree.entries()) {
        auto path = rstd::path::PathBuf::from(destination).join(entry.path().as_path());
        if (entry.kind() == SourceEntryKind::Directory) {
            auto result = rstd::fs::create_dir_all(path.as_path());
            if (result.is_err()) {
                auto error = source_tree_io_failure(
                    "create directory"_str, path.as_path(), rstd::move(result).unwrap_err());
                cleanup_materialization(destination);
                return Err(rstd::move(error));
            }
            continue;
        }
        auto parent = path.as_path().parent();
        if (parent.is_some()) {
            auto result = rstd::fs::create_dir_all(*parent);
            if (result.is_err()) {
                auto error = source_tree_io_failure(
                    "create parent directory"_str, *parent, rstd::move(result).unwrap_err());
                cleanup_materialization(destination);
                return Err(rstd::move(error));
            }
        }
        auto written = rstd::fs::write(path.as_path(), entry.contents());
        if (written.is_err()) {
            auto error = source_tree_io_failure(
                "write file"_str, path.as_path(), rstd::move(written).unwrap_err());
            cleanup_materialization(destination);
            return Err(rstd::move(error));
        }
#if RSTD_OS_UNIX
        if (entry.mode() == SourceFileMode::Executable) {
            auto permission = rstd::fs::set_permissions(
                path.as_path(), rstd::fs::Permissions::from_mode(u32(0755)));
            if (permission.is_err()) {
                auto error = source_tree_io_failure("set executable permissions"_str,
                                                    path.as_path(),
                                                    rstd::move(permission).unwrap_err());
                cleanup_materialization(destination);
                return Err(rstd::move(error));
            }
        }
#endif
    }
    return Ok(SourceMaterialization {
        .root    = rstd::path::PathBuf::from(destination),
        .entries = tree.entries().len(),
    });
}

} // namespace lito

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::SourceTreeError> : ImplBase<lito::SourceTreeError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_InvalidPath()) {
            const auto& value = error.as_InvalidPath();
            return formatter.write_fmt(
                fmt::Arguments::make("invalid source path '{}': {}", value.path, value.reason));
        }
        if (error.is_Conflict()) {
            const auto& value = error.as_Conflict();
            return formatter.write_fmt(
                fmt::Arguments::make("source tree conflict at '{}': {}", value.path, value.reason));
        }
        if (error.is_DestinationExists()) {
            return formatter.write_fmt(
                fmt::Arguments::make("source tree destination '{}' already exists",
                                     error.as_DestinationExists().path.as_path()));
        }
        const auto& value = error.as_Io();
        return formatter.write_fmt(fmt::Arguments::make(
            "cannot {} source tree '{}'", value.operation, value.path.as_path()));
    }
};

template<>
struct Impl<fmt::Debug, lito::SourceTreeError> : ImplBase<lito::SourceTreeError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::SourceTreeError> : ImplBase<lito::SourceTreeError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (error.is_Io()) return Some(dyn<error::Error>::from_ref(error.as_Io().source));
        return None();
    }
};

} // namespace rstd
