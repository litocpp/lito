module lito.driver;

import rstd;
import lito.core;
import lito.system;
import :build.layout;
import :command.clean;
import :command.error;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::system;

namespace lito
{

template<typename T>
auto clean_failure(String message) -> CommandResult<T> {
    return Err(CommandError::Message(rstd::move(message)));
}

template<typename T>
auto clean_failure(ref<str> message) -> CommandResult<T> {
    return clean_failure<T>(String::make(message));
}

template<typename T>
auto clean_io_failure(ref<str> operation, ref<rstd::path::Path> path, rstd::io::error::Error error)
    -> CommandResult<T> {
    return Err(CommandError::System(
        SystemError::Io(String::make(operation), PathBuf::from(path), rstd::move(error))));
}

auto is_not_found(const rstd::io::error::Error& error) noexcept -> bool {
    return error.kind() == rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound };
}

auto clean(const CleanRequest& request) -> CommandResult<CleanSummary> {
    if (request.root.is_empty()) {
        return clean_failure<CleanSummary>("clean project root is required"_str);
    }
    auto canonical_root = rstd::fs::canonicalize(request.root.as_path());
    if (canonical_root.is_err()) {
        return clean_io_failure<CleanSummary>("resolve clean project root"_str,
                                              request.root.as_path(),
                                              rstd::move(canonical_root).unwrap_err());
    }
    auto root_metadata = rstd::fs::metadata(canonical_root->as_path());
    if (root_metadata.is_err()) {
        return clean_io_failure<CleanSummary>("inspect clean project root"_str,
                                              canonical_root->as_path(),
                                              rstd::move(root_metadata).unwrap_err());
    }
    if (! root_metadata->is_dir()) {
        return clean_failure<CleanSummary>(
            rstd::format("clean project root '{}' is not a directory", canonical_root->as_path()));
    }

    auto requested = PathBuf::make();
    auto target    = BuildDirectory::resolve_root(canonical_root->as_path(), requested.as_path());
    if (request.target.is_Profile()) {
        target = BuildDirectory::resolve(canonical_root->as_path(),
                                         requested.as_path(),
                                         request.target.as_Profile().profile.as_str());
    } else if (request.target.is_Directory()) {
        target = BuildDirectory::resolve_root(canonical_root->as_path(),
                                              request.target.as_Directory().path.as_path());
    }

    auto metadata = rstd::fs::symlink_metadata(target.path());
    if (metadata.is_err()) {
        auto error = rstd::move(metadata).unwrap_err();
        if (is_not_found(error)) {
            return Ok(CleanSummary { .path = PathBuf::from(target.path()) });
        }
        return clean_io_failure<CleanSummary>(
            "inspect clean target"_str, target.path(), rstd::move(error));
    }
    if (! metadata->is_dir() || metadata->is_symlink()) {
        return clean_failure<CleanSummary>(
            rstd::format("clean target '{}' is not a real directory", target.path()));
    }

    auto canonical_target = rstd::fs::canonicalize(target.path());
    if (canonical_target.is_err()) {
        return clean_io_failure<CleanSummary>(
            "resolve clean target"_str, target.path(), rstd::move(canonical_target).unwrap_err());
    }
    if (canonical_root->as_path().strip_prefix(canonical_target->as_path()).is_some()) {
        return clean_failure<CleanSummary>(
            rstd::format("clean target '{}' contains project root '{}'",
                         canonical_target->as_path(),
                         canonical_root->as_path()));
    }

    auto removed = rstd::fs::remove_dir_all(canonical_target->as_path());
    if (removed.is_err()) {
        return clean_io_failure<CleanSummary>("remove clean target"_str,
                                              canonical_target->as_path(),
                                              rstd::move(removed).unwrap_err());
    }
    return Ok(CleanSummary {
        .path    = rstd::move(canonical_target).unwrap(),
        .removed = true,
    });
}

} // namespace lito
