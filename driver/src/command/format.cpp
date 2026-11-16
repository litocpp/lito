module;
#include <rstd/macro.hpp>

module lito.driver;

import rstd;
import lito.core;
import :command.error;
import :command.format_execution;
import :build.discovery;
import lito.tools;
import lito.system;
import lito.frontend.lexical;
import lito.frontend.format;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;
using StringSet = rstd::collections::BTreeMap<String, empty>;

namespace lito
{

auto format_builtin_source(ref<rstd::path::Path> path, FormatMode mode) -> CommandResult<bool> {
    auto contents = rstd::fs::read(path);
    if (contents.is_err()) {
        return Err(CommandError::System(SystemError::Io(
            "read format source"_Str, PathBuf::from(path), rstd::move(contents).unwrap_err())));
    }
    auto source = frontend::lexical::SourceFile {
        .snapshot = frontend::lexical::make_source_snapshot(
            PathBuf::from(path),
            frontend::lexical::SourceText::from_bytes(rstd::move(contents).unwrap())),
    };
    auto formatted = frontend::format::format_source(source);
    if (formatted.is_err()) {
        auto error = rstd::move(formatted).unwrap_err();
        if (error.path.is_none()) error.path = Some(PathBuf::from(path));
        return Err(CommandError::Message(rstd::format("{}", error)));
    }
    auto unchanged = source.contents() == formatted->as_str().as_bytes();
    if (mode == FormatMode::Check) return Ok(unchanged);
    if (unchanged) return Ok(true);
    auto written = rstd::fs::write_atomic(path, formatted->as_str().as_bytes());
    if (written.is_err()) {
        return Err(CommandError::System(SystemError::Io(
            "write formatted source"_Str, PathBuf::from(path), rstd::move(written).unwrap_err())));
    }
    return Ok(true);
}

auto format(const FormatRequest& request) -> CommandResult<FormatSummary> {
    if (request.root.is_empty()) {
        return Err(CommandError::Message("format directory is required"_Str));
    }
    auto resolved = lito::workspace::resolve_local_project(request.root.as_path());
    if (resolved.is_err()) {
        auto package_error =
            rstd::into<lito::package::PackageError>(rstd::move(resolved).unwrap_err());
        return Err(rstd::into<CommandError>(rstd::move(package_error)));
    }
    auto project = rstd::move(resolved).unwrap();

    auto available = StringSet::make();
    for (const auto& name : project.primary.names()) available.insert(name.clone(), empty {});
    if (project.tests.is_some()) {
        for (const auto& name : project.tests->names()) available.insert(name.clone(), empty {});
    }
    auto selected = StringSet::make();
    if (request.packages.is_empty()) {
        auto keys = available.keys();
        for (auto key : keys) {
            selected.insert((*key).clone(), empty {});
        }
    } else {
        for (const auto& name : request.packages) {
            if (! lito::manifest::valid_package_name(name.as_str())) {
                return Err(CommandError::Message(rstd::format(
                    "package selection '{}' must contain only ASCII letters, digits, '-' or '_'",
                    name.as_str())));
            }
            if (selected.contains_key(name.as_str())) {
                return Err(CommandError::Message(rstd::format(
                    "project package '{}' was selected more than once", name.as_str())));
            }
            if (! available.contains_key(name.as_str())) {
                return Err(CommandError::Message(
                    rstd::format("project has no local package named '{}'", name.as_str())));
            }
            selected.insert(name.clone(), empty {});
        }
    }
    if (selected.is_empty()) {
        return Err(CommandError::Message("project has no selected format package"_Str));
    }

    auto environment = Option<ResolvedProcessEnvironment> {};
    auto formatter   = Option<tools::ClangFormat> {};
    if (request.backend == FormatBackend::ClangFormat) {
        auto resolved_environment = ResolvedProcessEnvironment::resolve(request.environment);
        if (resolved_environment.is_err()) {
            return Err(rstd::into<CommandError>(rstd::move(resolved_environment).unwrap_err()));
        }
        environment = Some(rstd::move(resolved_environment).unwrap());
        auto tool_resolver =
            lito::tools::ToolResolver(*environment, request.tools.clone(), request.tool_reporter);
        const auto tool_requirement = lito::tools::command_tool_requirement(
            lito::tools::HostToolCapability::SourceFormatting, "lito format"_str);
        auto resolved_formatter =
            tool_resolver.require(lito::tools::Tool::ClangFormat, tool_requirement);
        if (resolved_formatter.is_err()) {
            return Err(rstd::into<CommandError>(rstd::move(resolved_formatter).unwrap_err()));
        }
        auto created =
            tools::ClangFormat::create(resolved_formatter->executable.as_path(), *environment);
        if (created.is_err()) {
            return Err(rstd::into<CommandError>(rstd::move(created).unwrap_err()));
        }
        formatter = Some(rstd::move(created).unwrap());
    }

    auto       summary = FormatSummary {};
    auto       paths   = Vec<PathBuf>::make();
    auto       seen    = StringSet::make();
    const auto discover_packages =
        [&](lito::workspace::WorkspaceCatalog& packages) -> CommandResult<empty> {
        auto names = packages.names()
                         .iter()
                         .map([](auto name) {
                             return name->clone();
                         })
                         .collect<Vec<String>>();
        for (const auto& name : names) {
            if (! selected.contains_key(name.as_str())) continue;
            auto package = packages.take_package(name.as_str());
            if (package.is_none()) {
                return Err(CommandError::Message(
                    rstd::format("local project is missing package '{}'", name.as_str())));
            }
            auto discovered = discover_format_sources(*package);
            if (discovered.is_err()) {
                return Err(rstd::into<CommandError>(rstd::move(discovered).unwrap_err()));
            }
            auto sources = rstd::move(discovered).unwrap();
            for (const auto& source : sources.sources) {
                auto key = source.canonical_path.as_path().to_str();
                if (key.is_none())
                    return Err(CommandError::Message(rstd::format(
                        "source path '{}' is not valid UTF-8", source.canonical_path.as_path())));
                if (seen.contains_key(*key)) continue;
                seen.insert(String::make(*key), empty {});
                paths.push(source.canonical_path.clone());
            }
            ++summary.packages;
        }
        return Ok(empty {});
    };
    rstd_try(discover_packages(project.primary));
    if (project.tests.is_some()) {
        rstd_try(discover_packages(*project.tests));
    }
    if (summary.packages != selected.len()) {
        return Err(CommandError::Message("selected packages are missing from local project"_Str));
    }
    summary.files = paths.len();
    if (paths.is_empty()) return Ok(rstd::move(summary));
    auto parallelism = rstd::thread::available_parallelism();
    auto jobs        = format_execution::worker_count(
        paths.len(), parallelism.is_ok() ? Some(parallelism->get()) : None());
    auto executed =
        format_execution::run(paths.len(), jobs, [&](usize index) -> CommandResult<bool> {
            if (request.backend == FormatBackend::Builtin)
                return format_builtin_source(paths[index].as_path(), request.mode);
            if (request.mode == FormatMode::Check) {
                auto result = formatter->is_formatted(paths[index].as_path());
                if (result.is_err())
                    return Err(rstd::into<CommandError>(rstd::move(result).unwrap_err()));
                return Ok(*result);
            }
            auto result = formatter->format(paths[index].as_path());
            if (result.is_err())
                return Err(rstd::into<CommandError>(rstd::move(result).unwrap_err()));
            return Ok(true);
        });
    if (executed.is_err()) return Err(rstd::move(executed).unwrap_err());
    for (usize index {}; index < paths.len(); ++index) {
        if (! (*executed)[index]) summary.unformatted_files.push(rstd::move(paths[index]));
    }
    return Ok(rstd::move(summary));
}

} // namespace lito
