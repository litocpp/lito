module;
#include <rstd/macro.hpp>

export module lito.tools:git;

import rstd;
import lito.system;
import :command;
import :error;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::system;

namespace lito::tools
{

auto git_command(ref<rstd::path::Path> executable) -> ToolResult<Vec<String>> {
    auto arguments = Vec<String>::make();
    rstd_try(command::push_path(arguments, executable));
#if defined(_WIN32)
    arguments.push("-c"_Str);
    arguments.push("core.longPaths=true"_Str);
#endif
    return Ok(rstd::move(arguments));
}

auto git_run(Vec<String>                       arguments,
             ref<str>                          operation,
             const ResolvedProcessEnvironment& environment) -> ToolResult<CommandOutput> {
    auto output = run_command(arguments, environment);
    if (output.is_err()) return Err(rstd::into<ToolError>(rstd::move(output).unwrap_err()));
    return Ok(rstd::move(output).unwrap());
}

auto git_output(Vec<String>                       arguments,
                ref<str>                          operation,
                const ResolvedProcessEnvironment& environment) -> ToolResult<String> {
    auto output = rstd_try(git_run(rstd::move(arguments), operation, environment));
    if (output.exit_code != i32 {}) {
        return Err(ToolError::Execution(operation.into(),
                                        output.exit_code,
                                        rstd::move(output.standard_output),
                                        rstd::move(output.standard_error)));
    }
    return Ok(trim_ascii(rstd::move(output.standard_output)));
}

auto git_status(Vec<String>                       arguments,
                ref<str>                          operation,
                const ResolvedProcessEnvironment& environment) -> ToolResult<empty> {
    (void)rstd_try(git_output(rstd::move(arguments), operation, environment));
    return Ok(empty {});
}

} // namespace lito::tools

export namespace lito::tools
{

class GitClient {
public:
    GitClient(rstd::path::PathBuf executable, const ResolvedProcessEnvironment& environment)
        : executable_(rstd::move(executable)), environment_(rstd::addressof(environment)) {}

    auto clone(const ResolvedProcessEnvironment& environment) const -> GitClient {
        return GitClient(executable_.clone(), environment);
    }

    auto executable() const noexcept -> ref<rstd::path::Path> { return executable_.as_path(); }

    auto head(ref<rstd::path::Path> worktree, ref<str> operation) const -> ToolResult<String> {
        auto arguments = rstd_try(git_command(executable_.as_path()));
        arguments.push("-C"_Str);
        rstd_try(command::push_path(arguments, worktree));
        arguments.push("rev-parse"_Str);
        arguments.push("--verify"_Str);
        arguments.push("HEAD"_Str);
        return git_output(rstd::move(arguments), operation, *environment_);
    }

    auto try_head(ref<rstd::path::Path> worktree, ref<str> operation) const
        -> ToolResult<Option<String>> {
        auto arguments = rstd_try(git_command(executable_.as_path()));
        arguments.push("-C"_Str);
        rstd_try(command::push_path(arguments, worktree));
        arguments.push("rev-parse"_Str);
        arguments.push("--verify"_Str);
        arguments.push("HEAD"_Str);
        auto output = rstd_try(git_run(rstd::move(arguments), operation, *environment_));
        if (output.exit_code != i32 {}) return Ok(Option<String> {});
        return Ok(Some(trim_ascii(rstd::move(output.standard_output))));
    }

    auto initialize_bare(ref<rstd::path::Path> repository) const -> ToolResult<empty> {
        auto arguments = rstd_try(git_command(executable_.as_path()));
        arguments.push("init"_Str);
        arguments.push("--bare"_Str);
        rstd_try(command::push_path(arguments, repository));
        return git_status(rstd::move(arguments), "Git cache initialization"_str, *environment_);
    }

    auto remote_origin(ref<rstd::path::Path> repository) const -> ToolResult<Option<String>> {
        auto arguments = rstd_try(git_command(executable_.as_path()));
        arguments.push("--git-dir"_Str);
        rstd_try(command::push_path(arguments, repository));
        arguments.push("config"_Str);
        arguments.push("--get"_Str);
        arguments.push("remote.origin.url"_Str);
        auto output = rstd_try(
            git_run(rstd::move(arguments), "Git cache remote inspection"_str, *environment_));
        if (output.exit_code != i32 {}) return Ok(Option<String> {});
        return Ok(Some(trim_ascii(rstd::move(output.standard_output))));
    }

    auto set_remote_origin(ref<rstd::path::Path> repository, ref<str> url) const
        -> ToolResult<empty> {
        auto arguments = rstd_try(git_command(executable_.as_path()));
        arguments.push("--git-dir"_Str);
        rstd_try(command::push_path(arguments, repository));
        arguments.push("config"_Str);
        arguments.push("remote.origin.url"_Str);
        arguments.push(String::make(url));
        return git_status(
            rstd::move(arguments), "Git cache remote configuration"_str, *environment_);
    }

    auto commit_exists(ref<rstd::path::Path> repository, ref<str> commit) const
        -> ToolResult<bool> {
        auto arguments = rstd_try(git_command(executable_.as_path()));
        arguments.push("--git-dir"_Str);
        rstd_try(command::push_path(arguments, repository));
        arguments.push("cat-file"_Str);
        arguments.push("-e"_Str);
        arguments.push(rstd::format("{}^{{commit}}", commit));
        auto output =
            rstd_try(git_run(rstd::move(arguments), "Git object inspection"_str, *environment_));
        return Ok(output.exit_code == i32 {});
    }

    auto fetch(ref<rstd::path::Path> repository, ref<str> revision, ref<str> local_reference) const
        -> ToolResult<empty> {
        auto arguments = rstd_try(git_command(executable_.as_path()));
        arguments.push("--git-dir"_Str);
        rstd_try(command::push_path(arguments, repository));
        arguments.push("fetch"_Str);
        arguments.push("--force"_Str);
        arguments.push("--no-tags"_Str);
        arguments.push("origin"_Str);
        arguments.push(rstd::format("{}:{}", revision, local_reference));
        return git_status(rstd::move(arguments), "Git source fetch"_str, *environment_);
    }

    auto rev_parse_commit(ref<rstd::path::Path> repository, ref<str> revision) const
        -> ToolResult<String> {
        auto arguments = rstd_try(git_command(executable_.as_path()));
        arguments.push("--git-dir"_Str);
        rstd_try(command::push_path(arguments, repository));
        arguments.push("rev-parse"_Str);
        arguments.push("--verify"_Str);
        arguments.push(rstd::format("{}^{{commit}}", revision));
        auto commit = rstd_try(
            git_output(rstd::move(arguments), "Git source revision resolution"_str, *environment_));
        if (commit.len() != usize(40)) {
            return Err(ToolError::Message(rstd::format(
                "Git resolved '{}' to non-full object id '{}'", revision, commit.as_str())));
        }
        return Ok(rstd::move(commit));
    }

    auto clone_shared(ref<rstd::path::Path> repository, ref<rstd::path::Path> checkout) const
        -> ToolResult<empty> {
        auto arguments = rstd_try(git_command(executable_.as_path()));
        arguments.push("clone"_Str);
        arguments.push("--no-checkout"_Str);
        arguments.push("--shared"_Str);
        rstd_try(command::push_path(arguments, repository));
        rstd_try(command::push_path(arguments, checkout));
        return git_status(rstd::move(arguments), "Git source checkout creation"_str, *environment_);
    }

    auto export_checkout(ref<rstd::path::Path> source,
                         ref<rstd::path::Path> destination,
                         ref<str>              commit) const -> ToolResult<empty> {
        auto arguments = rstd_try(git_command(executable_.as_path()));
        arguments.push("clone"_Str);
        arguments.push("--no-checkout"_Str);
        arguments.push("--no-hardlinks"_Str);
        rstd_try(command::push_path(arguments, source));
        rstd_try(command::push_path(arguments, destination));
        rstd_try(git_status(rstd::move(arguments), "Git source bundle export"_str, *environment_));
        return checkout_detached(destination, commit);
    }

    auto set_worktree_remote_origin(ref<rstd::path::Path> worktree, ref<str> url) const
        -> ToolResult<empty> {
        auto arguments = rstd_try(git_command(executable_.as_path()));
        arguments.push("-C"_Str);
        rstd_try(command::push_path(arguments, worktree));
        arguments.push("remote"_Str);
        arguments.push("set-url"_Str);
        arguments.push("origin"_Str);
        arguments.push(String::make(url));
        return git_status(
            rstd::move(arguments), "Git worktree remote configuration"_str, *environment_);
    }

    auto checkout_detached(ref<rstd::path::Path> checkout, ref<str> commit) const
        -> ToolResult<empty> {
        auto arguments = rstd_try(git_command(executable_.as_path()));
        arguments.push("-C"_Str);
        rstd_try(command::push_path(arguments, checkout));
        arguments.push("checkout"_Str);
        arguments.push("--detach"_Str);
        arguments.push(String::make(commit));
        return git_status(rstd::move(arguments), "Git source checkout"_str, *environment_);
    }

private:
    rstd::path::PathBuf               executable_;
    const ResolvedProcessEnvironment* environment_ {};
};

} // namespace lito::tools
