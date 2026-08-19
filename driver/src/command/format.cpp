module lito.driver;

import rstd;
import lito.core;
import :command.error;
import :build.event;
import :build.discovery;
import :project;
import lito.toolchain;
import lito.system;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;
using StringSet = rstd::collections::BTreeMap<String, empty>;

namespace lito
{

template<typename T>
auto format_failure(ref<str> message) -> CommandResult<T> {
    return Err(CommandError::Message(String::make(message)));
}

} // namespace lito

namespace lito
{

auto format(const FormatRequest& request) -> CommandResult<FormatSummary> {
    if (request.selection.root.is_empty()) {
        return format_failure<FormatSummary>("format directory is required"_str);
    }
    auto environment = ResolvedProcessEnvironment::resolve(request.environment);
    if (environment.is_err()) {
        return Err(rstd::into<CommandError>(rstd::move(environment).unwrap_err()));
    }
    auto tool_resolver = ToolResolver(*environment, request.tools.clone(), request.tool_reporter);
    auto resolved      = resolve_project_selection(request.selection,
                                                   lito::package::PackageSelectionPurpose::All,
                                                   request.sources,
                                                   request.lock,
                                                   false,
                                                   tool_resolver,
                                                   *environment,
                                                   usize(1),
                                                   request.observer);
    if (resolved.is_err()) {
        return Err(rstd::into<CommandError>(rstd::move(resolved).unwrap_err()));
    }
    auto selection = rstd::move(resolved).unwrap();

    auto selected_roots = StringSet::make();
    for (const auto& name : selection.selected_root_names) {
        selected_roots.insert(name.clone(), empty {});
    }

    const auto tool_requirement =
        command_tool_requirement(HostToolCapability::SourceFormatting, "lito format"_str);
    auto resolved_formatter = tool_resolver.require(Tool::ClangFormat, tool_requirement);
    if (resolved_formatter.is_err()) {
        return Err(rstd::into<CommandError>(rstd::move(resolved_formatter).unwrap_err()));
    }
    auto created =
        toolchain::ClangFormat::create(resolved_formatter->executable.as_path(), *environment);
    if (created.is_err()) {
        return Err(rstd::into<CommandError>(rstd::move(created).unwrap_err()));
    }
    auto formatter = rstd::move(created).unwrap();

    auto summary = FormatSummary {};
    for (const auto& package : selection.graph.packages) {
        if (! selected_roots.contains_key(package.manifest.name.as_str())) continue;
        auto discovered = discover_format_sources(package.manifest);
        if (discovered.is_err()) {
            return Err(rstd::into<CommandError>(rstd::move(discovered).unwrap_err()));
        }
        auto sources = rstd::move(discovered).unwrap();
        for (const auto& source : sources.sources) {
            if (request.mode == FormatMode::Check) {
                auto formatted = formatter.is_formatted(source.canonical_path.as_path());
                if (formatted.is_err()) {
                    return Err(rstd::into<CommandError>(rstd::move(formatted).unwrap_err()));
                }
                if (! *formatted) summary.unformatted_files.push(source.canonical_path.clone());
            } else {
                auto formatted = formatter.format(source.canonical_path.as_path());
                if (formatted.is_err()) {
                    return Err(rstd::into<CommandError>(rstd::move(formatted).unwrap_err()));
                }
            }
            ++summary.files;
        }
        ++summary.packages;
    }
    if (summary.packages != selection.selected_root_names.len()) {
        return format_failure<FormatSummary>(
            "selected packages are missing from resolved graph"_str);
    }
    return Ok(rstd::move(summary));
}

} // namespace lito
