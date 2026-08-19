export module lito.system:storage;

import rstd;
import :error;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace rstd::literals;

namespace lito::system
{

inline constexpr auto CACHE_TAG_SIGNATURE = "Signature: 8a477f597d28d172789f06886806bc55"_str;
inline constexpr auto CACHE_TAG_CONTENT   = "Signature: 8a477f597d28d172789f06886806bc55\n"
                                            "# This file is a cache directory tag created by Lito.\n"
                                            "# For information about cache directory tags, see:\n"
                                            "# https://bford.info/cachedir/\n"_str;

template<typename T>
auto storage_io_failure(ref<str>               operation,
                        ref<rstd::path::Path>  path,
                        rstd::io::error::Error source) -> SystemResult<T> {
    return Err(SystemError::Io(String::make(operation), PathBuf::from(path), rstd::move(source)));
}

auto join(ref<rstd::path::Path> root, ref<str> component) -> PathBuf {
    return PathBuf::from(root).join(PathBuf::from(component).as_path());
}

auto ensure_cache_directory_tag(ref<rstd::path::Path> directory) -> SystemResult<empty> {
    auto tag      = join(directory, "CACHEDIR.TAG"_str);
    auto metadata = rstd::fs::symlink_metadata(tag.as_path());
    if (metadata.is_ok()) {
        if (! metadata->is_file()) {
            return Err(SystemError::Storage(
                rstd::format("cache tag '{}' must be an ordinary file", tag.as_path())));
        }
        auto contents = rstd::fs::read(tag.as_path());
        if (contents.is_err()) {
            return storage_io_failure<empty>(
                "read cache directory tag"_str, tag.as_path(), rstd::move(contents).unwrap_err());
        }
        if (contents->len() < CACHE_TAG_SIGNATURE.len()) {
            return Err(SystemError::Storage(
                rstd::format("cache tag '{}' has an invalid signature", tag.as_path())));
        }
        for (usize index {}; index < CACHE_TAG_SIGNATURE.len(); ++index) {
            if ((*contents)[index] == CACHE_TAG_SIGNATURE.as_bytes()[index]) continue;
            return Err(SystemError::Storage(
                rstd::format("cache tag '{}' has an invalid signature", tag.as_path())));
        }
        return Ok(empty {});
    }
    auto error = rstd::move(metadata).unwrap_err();
    if (error.kind() != rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound }) {
        return storage_io_failure<empty>(
            "inspect cache directory tag"_str, tag.as_path(), rstd::move(error));
    }
    auto written = rstd::fs::write_atomic(tag.as_path(), CACHE_TAG_CONTENT.as_bytes());
    if (written.is_err()) {
        return storage_io_failure<empty>(
            "write cache directory tag"_str, tag.as_path(), rstd::move(written).unwrap_err());
    }
    return Ok(empty {});
}

struct SourceCacheState {
    rstd::fs::FileLock lock;
    PathBuf            root;
};

} // namespace lito::system

export namespace lito::system
{

class GitCacheLayout {
    PathBuf root_;

public:
    explicit GitCacheLayout(PathBuf root): root_(rstd::move(root)) {}

    auto clone() const -> GitCacheLayout { return GitCacheLayout { root_.clone() }; }

    auto root() const noexcept -> ref<rstd::path::Path> { return root_.as_path(); }
    auto repository(ref<str> repository_key) const -> PathBuf {
        return join(join(root_.as_path(), "db"_str).as_path(), repository_key);
    }
    auto checkout(ref<str> repository_key, ref<str> commit) const -> PathBuf {
        auto checkouts = join(root_.as_path(), "checkouts"_str);
        return join(join(checkouts.as_path(), repository_key).as_path(), commit);
    }
    auto checkout_receipt(ref<str> repository_key, ref<str> commit) const -> PathBuf {
        auto checkouts  = join(root_.as_path(), "checkouts"_str);
        auto repository = join(checkouts.as_path(), repository_key);
        return join(repository.as_path(), rstd::format("{}.lito-receipt", commit).as_str());
    }
};

class FileCacheLayout {
    PathBuf root_;

public:
    explicit FileCacheLayout(PathBuf root): root_(rstd::move(root)) {}

    auto clone() const -> FileCacheLayout { return FileCacheLayout { root_.clone() }; }

    auto root() const noexcept -> ref<rstd::path::Path> { return root_.as_path(); }
    auto bucket(ref<str> fetch_key) const -> PathBuf { return join(root_.as_path(), fetch_key); }
    auto source(ref<str> fetch_key) const -> PathBuf {
        return join(bucket(fetch_key).as_path(), "source"_str);
    }
};

class SourceCacheSession {
    rstd::sync::Arc<SourceCacheState> state_;

    explicit SourceCacheSession(rstd::sync::Arc<SourceCacheState> state)
        : state_(rstd::move(state)) {}
    friend class LitoDataRoot;

    auto open_cache(ref<str> name) const -> SystemResult<PathBuf> {
        auto root    = join(state_->root.as_path(), name);
        auto created = rstd::fs::create_dir_all(root.as_path());
        if (created.is_err()) {
            return storage_io_failure<PathBuf>(
                "create source cache"_str, root.as_path(), rstd::move(created).unwrap_err());
        }
        auto canonical = rstd::fs::canonicalize(root.as_path());
        if (canonical.is_err()) {
            return storage_io_failure<PathBuf>(
                "resolve source cache"_str, root.as_path(), rstd::move(canonical).unwrap_err());
        }
        auto tagged = ensure_cache_directory_tag(canonical->as_path());
        if (tagged.is_err()) return Err(rstd::move(tagged).unwrap_err());
        return Ok(rstd::move(canonical).unwrap());
    }

public:
    auto clone() const -> SourceCacheSession { return SourceCacheSession { state_.clone() }; }
    auto root() const noexcept -> ref<rstd::path::Path> { return state_->root.as_path(); }

    auto open_git_cache() const -> SystemResult<GitCacheLayout> {
        auto root = open_cache("git"_str);
        if (root.is_err()) return Err(rstd::move(root).unwrap_err());
        return Ok(GitCacheLayout { rstd::move(root).unwrap() });
    }

    auto open_file_cache() const -> SystemResult<FileCacheLayout> {
        auto root = open_cache("files"_str);
        if (root.is_err()) return Err(rstd::move(root).unwrap_err());
        return Ok(FileCacheLayout { rstd::move(root).unwrap() });
    }
};

class LitoDataRoot {
    PathBuf root_;

    explicit LitoDataRoot(PathBuf root): root_(rstd::move(root)) {}

public:
    static auto resolve() -> SystemResult<LitoDataRoot> {
        auto root       = PathBuf::make();
        auto configured = rstd::env::var("XDG_DATA_HOME"_str);
        if (configured.is_some() && ! configured->is_empty()) {
            auto candidate = PathBuf::from(rstd::move(configured).unwrap());
            if (candidate.as_path().is_absolute()) root = rstd::move(candidate);
        }
        if (root.is_empty()) {
            auto home = rstd::env::var("HOME"_str);
            if (home.is_none() || home->is_empty()) {
                return Err(SystemError::Storage(
                    String::make("source cache requires XDG_DATA_HOME or HOME"_str)));
            }
            root = PathBuf::from(rstd::move(home).unwrap());
            if (! root.as_path().is_absolute()) {
                return Err(SystemError::Storage(String::make("HOME must be an absolute path"_str)));
            }
            root.push(PathBuf::from(".local/share"_str).as_path());
        }
        root.push(PathBuf::from("lito"_str).as_path());
        return Ok(LitoDataRoot { rstd::move(root) });
    }

    auto root() const noexcept -> ref<rstd::path::Path> { return root_.as_path(); }

    auto acquire_source_cache() const -> SystemResult<SourceCacheSession> {
        auto created = rstd::fs::create_dir_all(root_.as_path());
        if (created.is_err()) {
            return storage_io_failure<SourceCacheSession>(
                "create Lito data root"_str, root_.as_path(), rstd::move(created).unwrap_err());
        }
        auto lock_path = join(root_.as_path(), ".source-cache"_str);
        auto metadata  = rstd::fs::symlink_metadata(lock_path.as_path());
        if (metadata.is_ok() && ! metadata->is_file()) {
            return Err(SystemError::Storage(rstd::format(
                "source cache lock '{}' must be an ordinary file", lock_path.as_path())));
        }
        if (metadata.is_err()) {
            auto error = rstd::move(metadata).unwrap_err();
            if (error.kind() !=
                rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound }) {
                return storage_io_failure<SourceCacheSession>(
                    "inspect source cache lock"_str, lock_path.as_path(), rstd::move(error));
            }
        }
        auto opened = rstd::fs::OpenOptions::make().read(true).write(true).create(true).open(
            lock_path.as_path());
        if (opened.is_err()) {
            return storage_io_failure<SourceCacheSession>(
                "open source cache lock"_str, lock_path.as_path(), rstd::move(opened).unwrap_err());
        }
        auto opened_metadata = opened->metadata();
        if (opened_metadata.is_err()) {
            return storage_io_failure<SourceCacheSession>("inspect opened source cache lock"_str,
                                                          lock_path.as_path(),
                                                          rstd::move(opened_metadata).unwrap_err());
        }
        if (! opened_metadata->is_file()) {
            return Err(SystemError::Storage(rstd::format(
                "source cache lock '{}' must be an ordinary file", lock_path.as_path())));
        }
        auto locked = rstd::fs::FileLock::acquire(rstd::move(opened).unwrap(),
                                                  rstd::fs::FileLockMode::Exclusive);
        if (locked.is_err()) {
            return storage_io_failure<SourceCacheSession>(
                "lock source cache"_str, lock_path.as_path(), rstd::move(locked).unwrap_err());
        }
        return Ok(SourceCacheSession {
            rstd::sync::Arc<SourceCacheState>::make(rstd::move(locked).unwrap(), root_.clone()) });
    }
};

} // namespace lito::system
