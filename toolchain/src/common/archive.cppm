module;
#include <rstd/macro.hpp>

export module lito.toolchain.common:archive;

import rstd;
import lito.core;
import lito.system;
import :command;
import :error;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::system;

export namespace lito
{

struct ArchiverIdentity {
    PathBuf executable;
    String  version;
    String  build_identity;

    auto clone() const -> ArchiverIdentity {
        return ArchiverIdentity {
            .executable     = executable.clone(),
            .version        = version.clone(),
            .build_identity = build_identity.clone(),
        };
    }
};

struct ArchiveInvocation {
    Vec<String> arguments;
    PathBuf     working_directory;
    PathBuf     output;
    String      archiver_identity;

    auto identity() const -> String {
        auto result = String::make("lito-archive-invocation-v1\n"_str);
        result.push_str(
            rstd::format("{}:{}\n", archiver_identity.size(), archiver_identity.as_str()).as_str());
        auto working = working_directory.as_path().to_string_lossy();
        result.push_str(rstd::format("{}:{}\n", working.size(), working.as_str()).as_str());
        for (const auto& argument : arguments) {
            result.push_str(rstd::format("{}:{}\n", argument.size(), argument.as_str()).as_str());
        }
        return result;
    }
};

auto probe_archiver(ref<rstd::path::Path> executable, const ResolvedProcessEnvironment& environment)
    -> ToolchainResult<ArchiverIdentity> {
    auto command = Vec<String>::make();
    rstd_try(toolchain::command::push_path(command, executable));
    toolchain::command::push_option(command, "--version"_str);
    auto version =
        toolchain::command::tool_output(rstd::move(command), "llvm-ar --version"_str, environment);
    if (version.is_err()) return Err(rstd::move(version).unwrap_err());
    if (! version->as_str().contains("LLVM"_str)) {
        return Err(ToolchainError::Message(
            rstd::format("configured archiver '{}' is not llvm-ar", executable)));
    }
    auto metadata = rstd::fs::metadata(executable);
    if (metadata.is_err()) {
        return Err(ToolchainError::Io(String::make("inspect archiver"_str),
                                      PathBuf::from(executable),
                                      rstd::move(metadata).unwrap_err()));
    }
    auto modified = metadata->modified();
    if (modified.is_err()) {
        return Err(ToolchainError::Io(String::make("read archiver modification time"_str),
                                      PathBuf::from(executable),
                                      rstd::move(modified).unwrap_err()));
    }
    auto path = executable.to_str();
    if (path.is_none()) {
        return Err(ToolchainError::Message(
            rstd::format("archiver path '{}' is not valid UTF-8", executable)));
    }
    auto timestamp = modified->as_unix_time();
    auto identity  = rstd::format("lito-archiver-v1\npath:{}\nversion:{}\n{}:{}:{}",
                                  *path,
                                  version->as_str(),
                                  metadata->size(),
                                  timestamp.seconds,
                                  timestamp.nanoseconds);
    return Ok(ArchiverIdentity {
        .executable     = PathBuf::from(executable),
        .version        = rstd::move(version).unwrap(),
        .build_identity = rstd::move(identity),
    });
}

} // namespace lito
