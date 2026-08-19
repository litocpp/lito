module;
#include <rstd/macro.hpp>

export module lito.system:environment;

import rstd;
import :error;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace rstd::literals;

export namespace lito::system
{

struct ProcessEnvironmentSpec {
    Vec<PathBuf> append_path;

    auto clone() const -> ProcessEnvironmentSpec {
        return ProcessEnvironmentSpec {
            .append_path = as<Clone>(append_path).clone(),
        };
    }
};

auto is_searchable_executable_name(ref<rstd::path::Path> path) -> bool {
    auto components = path.components();
    auto first      = components.next();
    return first.is_some() && first->is_normal() && components.next().is_none();
}

} // namespace lito::system

namespace lito::system
{

template<typename T>
auto environment_failure(String message) -> SystemResult<T> {
    return Err(SystemError::Environment(rstd::move(message)));
}

template<typename T>
auto environment_io_failure(ref<str>               operation,
                            ref<rstd::path::Path>  path,
                            rstd::io::error::Error source) -> SystemResult<T> {
    return Err(SystemError::Io(String::make(operation), PathBuf::from(path), rstd::move(source)));
}

auto same_path(ref<rstd::path::Path> left, ref<rstd::path::Path> right) -> bool {
    return left.as_os_str().as_encoded_bytes() == right.as_os_str().as_encoded_bytes();
}

auto single_component(ref<rstd::path::Path> path) -> bool {
    auto components = path.components();
    auto first      = components.next();
    return first.is_some() && first->is_normal() && components.next().is_none();
}

auto append_search_directories(String& output, const Vec<PathBuf>& directories) -> void {
    for (usize index {}; index < directories.len(); ++index) {
        if (index != usize {}) output.push_str(", "_str);
        output.push_str(directories[index].as_path().as_os_str().to_string_lossy().as_str());
    }
}

auto executable_candidate(ref<rstd::path::Path> path) -> SystemResult<bool> {
    auto exists = rstd::fs::exists(path);
    if (exists.is_err()) {
        return environment_io_failure<bool>(
            "inspect executable candidate"_str, path, rstd::move(exists).unwrap_err());
    }
    if (! *exists) return Ok(false);
    auto metadata = rstd::fs::metadata(path);
    if (metadata.is_err()) {
        return environment_io_failure<bool>(
            "inspect executable candidate"_str, path, rstd::move(metadata).unwrap_err());
    }
    if (! metadata->is_file()) return Ok(false);
#if RSTD_OS_UNIX
    return Ok((metadata->permissions().mode() & u32(0111)) != u32 {});
#else
    return Ok(true);
#endif
}

} // namespace lito::system

export namespace lito::system
{

class ResolvedProcessEnvironment {
public:
    static auto resolve(const ProcessEnvironmentSpec& specification)
        -> SystemResult<ResolvedProcessEnvironment> {
        auto cwd = rstd::fs::canonicalize(PathBuf::from("."_str).as_path());
        if (cwd.is_err()) {
            return environment_io_failure<ResolvedProcessEnvironment>(
                "resolve Lito invocation directory"_str,
                PathBuf::from("."_str).as_path(),
                rstd::move(cwd).unwrap_err());
        }
        auto inherited  = rstd::env::var_os("PATH"_str);
        auto extensions = rstd::env::var_os("PATHEXT"_str);
        if (inherited.is_some()) {
            return resolve(specification,
                           Some(inherited->as_os_str()),
                           cwd->as_path(),
                           extensions.is_some() ? Some(extensions->as_os_str()) : None());
        }
        return resolve(specification,
                       None(),
                       cwd->as_path(),
                       extensions.is_some() ? Some(extensions->as_os_str()) : None());
    }

    static auto resolve(const ProcessEnvironmentSpec& specification,
                        Option<ref<rstd::ffi::OsStr>> inherited_path,
                        ref<rstd::path::Path>         invocation_directory,
                        Option<ref<rstd::ffi::OsStr>> executable_extensions = None())
        -> SystemResult<ResolvedProcessEnvironment> {
        auto cwd = rstd::fs::canonicalize(invocation_directory);
        if (cwd.is_err()) {
            return environment_io_failure<ResolvedProcessEnvironment>(
                "resolve Lito invocation directory"_str,
                invocation_directory,
                rstd::move(cwd).unwrap_err());
        }

        auto directories = Vec<PathBuf>::make();
        if (inherited_path.is_some()) {
            auto paths = rstd::env::split_paths(*inherited_path);
            for (auto entry = paths.next(); entry.is_some(); entry = paths.next()) {
                auto path = rstd::move(entry).unwrap();
                if (path.is_empty()) {
                    directories.push(cwd->clone());
                } else if (path.as_path().is_relative()) {
                    directories.push(cwd->join(path.as_path()));
                } else {
                    directories.push(rstd::move(path));
                }
            }
        }
        for (const auto& path : specification.append_path) directories.push(path.clone());

        auto child_path = rstd::env::join_paths(directories.as_slice());
        if (child_path.is_err()) {
            return Err(SystemError::PathJoin(rstd::move(child_path).unwrap_err()));
        }
        auto extensions = Vec<rstd::ffi::OsString>::make();
#if RSTD_OS_WINDOWS
        auto defaults = rstd::ffi::OsString::from(".COM;.EXE;.BAT;.CMD"_str);
        auto values =
            executable_extensions.is_some() ? *executable_extensions : defaults.as_os_str();
        auto parsed = rstd::env::split_paths(values);
        for (auto extension = parsed.next(); extension.is_some(); extension = parsed.next()) {
            if (! extension->is_empty()) {
                extensions.push(rstd::ffi::OsString::from(extension->as_path().as_os_str()));
            }
        }
#else
        (void)executable_extensions;
#endif
        return Ok(ResolvedProcessEnvironment(
            rstd::move(directories), rstd::move(child_path).unwrap(), rstd::move(extensions)));
    }

    auto search_directories() const noexcept -> const Vec<PathBuf>& { return directories_; }
    auto child_path() const noexcept -> ref<rstd::ffi::OsStr> { return child_path_.as_os_str(); }
    auto executable_extensions() const noexcept -> const Vec<rstd::ffi::OsString>& {
        return executable_extensions_;
    }
    auto clone() const -> ResolvedProcessEnvironment {
        auto extensions = Vec<rstd::ffi::OsString>::with_capacity(executable_extensions_.len());
        for (const auto& extension : executable_extensions_) {
            extensions.push(rstd::ffi::OsString::from(extension.as_os_str()));
        }
        return ResolvedProcessEnvironment(as<Clone>(directories_).clone(),
                                          rstd::ffi::OsString::from(child_path_.as_os_str()),
                                          rstd::move(extensions));
    }

private:
    ResolvedProcessEnvironment(Vec<PathBuf>             directories,
                               rstd::ffi::OsString      child_path,
                               Vec<rstd::ffi::OsString> executable_extensions)
        : directories_(rstd::move(directories)),
          child_path_(rstd::move(child_path)),
          executable_extensions_(rstd::move(executable_extensions)) {}

    Vec<PathBuf>             directories_;
    rstd::ffi::OsString      child_path_;
    Vec<rstd::ffi::OsString> executable_extensions_;
};

struct ResolvedTool {
    PathBuf requested;
    PathBuf executable;

    auto clone() const -> ResolvedTool {
        return ResolvedTool {
            .requested  = requested.clone(),
            .executable = executable.clone(),
        };
    }
};

class ToolResolver {
public:
    explicit ToolResolver(const ResolvedProcessEnvironment& environment)
        : environment_(rstd::addressof(environment)) {}

    auto resolve(ref<rstd::path::Path> requested, ref<str> description)
        -> SystemResult<ResolvedTool> {
        for (const auto& cached : cache_) {
            if (same_path(cached.requested.as_path(), requested)) return Ok(cached.clone());
        }

        auto selected = Option<PathBuf> {};
        if (requested.is_absolute()) {
            auto executable = executable_candidate(requested);
            if (executable.is_err()) return Err(rstd::move(executable).unwrap_err());
            if (*executable) selected = Some(PathBuf::from(requested));
        } else if (single_component(requested)) {
            for (const auto& directory : environment_->search_directories()) {
                auto candidate = directory.join(requested);
#if RSTD_OS_WINDOWS
                auto executable = requested.extension().is_some()
                                      ? executable_candidate(candidate.as_path())
                                      : Ok(false);
#else
                auto executable = executable_candidate(candidate.as_path());
#endif
                if (executable.is_err()) return Err(rstd::move(executable).unwrap_err());
                if (*executable) {
                    selected = Some(rstd::move(candidate));
                    break;
                }
#if RSTD_OS_WINDOWS
                for (const auto& extension : environment_->executable_extensions()) {
                    auto name = rstd::ffi::OsString::from(requested.as_os_str());
                    name.push(extension.as_os_str());
                    auto extended_name = PathBuf::from(rstd::move(name));
                    candidate          = directory.join(extended_name.as_path());
                    executable         = executable_candidate(candidate.as_path());
                    if (executable.is_err()) return Err(rstd::move(executable).unwrap_err());
                    if (*executable) {
                        selected = Some(rstd::move(candidate));
                        break;
                    }
                }
                if (selected.is_some()) break;
#endif
            }
        } else {
            return environment_failure<ResolvedTool>(
                rstd::format("cannot resolve {} '{}': expected an executable name or absolute path",
                             description,
                             requested));
        }

        if (selected.is_none()) {
            auto message = rstd::format(
                "cannot resolve {} '{}' from effective PATH; searched: ", description, requested);
            append_search_directories(message, environment_->search_directories());
            return environment_failure<ResolvedTool>(rstd::move(message));
        }
        auto result = ResolvedTool {
            .requested  = PathBuf::from(requested),
            .executable = rstd::move(selected).unwrap(),
        };
        cache_.push(result.clone());
        return Ok(rstd::move(result));
    }

private:
    const ResolvedProcessEnvironment* environment_ {};
    Vec<ResolvedTool>                 cache_;
};

} // namespace lito::system
