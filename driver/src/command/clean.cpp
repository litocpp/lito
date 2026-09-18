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

auto is_not_found(const rstd::io::error::Error& error) noexcept -> bool {
    return error.kind() == rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound };
}

auto clean(const CleanRequest& request) -> CommandResult<CleanSummary> {
    if (request.root.is_empty()) {
        return Err(CommandError::Message("clean project root is required"_Str));
    }
    auto canonical_root = rstd::fs::canonicalize(request.root.as_path());
    if (canonical_root.is_err()) {
        return Err(CommandError::System(SystemError::Io("resolve clean project root"_Str,
                                                        PathBuf::from(request.root.as_path()),
                                                        rstd::move(canonical_root).unwrap_err())));
    }
    auto root_metadata = rstd::fs::metadata(canonical_root->as_path());
    if (root_metadata.is_err()) {
        return Err(CommandError::System(SystemError::Io("inspect clean project root"_Str,
                                                        PathBuf::from(canonical_root->as_path()),
                                                        rstd::move(root_metadata).unwrap_err())));
    }
    if (! root_metadata->is_dir()) {
        return Err(CommandError::Message(
            rstd::format("clean project root '{}' is not a directory", canonical_root->as_path())));
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
        return Err(CommandError::System(SystemError::Io(
            "inspect clean target"_Str, PathBuf::from(target.path()), rstd::move(error))));
    }
    if (! metadata->is_dir() || metadata->is_symlink()) {
        return Err(CommandError::Message(
            rstd::format("clean target '{}' is not a real directory", target.path())));
    }

    auto canonical_target = rstd::fs::canonicalize(target.path());
    if (canonical_target.is_err()) {
        return Err(
            CommandError::System(SystemError::Io("resolve clean target"_Str,
                                                 PathBuf::from(target.path()),
                                                 rstd::move(canonical_target).unwrap_err())));
    }
    if (canonical_root->as_path().strip_prefix(canonical_target->as_path()).is_some()) {
        return Err(
            CommandError::Message(rstd::format("clean target '{}' contains project root '{}'",
                                               canonical_target->as_path(),
                                               canonical_root->as_path())));
    }

    auto removed = rstd::fs::remove_dir_all(canonical_target->as_path());
    if (removed.is_err()) {
        return Err(CommandError::System(SystemError::Io("remove clean target"_Str,
                                                        PathBuf::from(canonical_target->as_path()),
                                                        rstd::move(removed).unwrap_err())));
    }
    return Ok(CleanSummary {
        .path    = rstd::move(canonical_target).unwrap(),
        .removed = true,
    });
}

} // namespace lito
