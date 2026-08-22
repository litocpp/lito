module;
#include <rstd/macro.hpp>

module lito.driver;

import rstd;
import lito.core;
import :command.error;
import :build.discovery;
import lito.tools;
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
    if (request.root.is_empty()) {
        return format_failure<FormatSummary>("format directory is required"_str);
    }
    auto environment = ResolvedProcessEnvironment::resolve(request.environment);
    if (environment.is_err()) {
        return Err(rstd::into<CommandError>(rstd::move(environment).unwrap_err()));
    }
    auto tool_resolver =
        lito::tools::ToolResolver(*environment, request.tools.clone(), request.tool_reporter);
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
                return format_failure<FormatSummary>(rstd::format(
                    "package selection '{}' must contain only ASCII letters, digits, '-' or '_'",
                    name.as_str()));
            }
            if (selected.contains_key(name.as_str())) {
                return format_failure<FormatSummary>(rstd::format(
                    "project package '{}' was selected more than once", name.as_str()));
            }
            if (! available.contains_key(name.as_str())) {
                return format_failure<FormatSummary>(
                    rstd::format("project has no local package named '{}'", name.as_str()));
            }
            selected.insert(name.clone(), empty {});
        }
    }
    if (selected.is_empty()) {
        return format_failure<FormatSummary>("project has no selected format package"_str);
    }

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
    auto formatter = rstd::move(created).unwrap();

    auto       summary = FormatSummary {};
    const auto format_catalog =
        [&](lito::workspace::WorkspaceCatalog& catalog) -> CommandResult<empty> {
        auto names = Vec<String>::with_capacity(catalog.names().len());
        for (const auto& name : catalog.names()) names.push(name.clone());
        for (const auto& name : names) {
            if (! selected.contains_key(name.as_str())) continue;
            auto package = catalog.take_package(name.as_str());
            if (package.is_none()) {
                return format_failure<empty>(
                    rstd::format("local project catalog is missing package '{}'", name.as_str()));
            }
            auto discovered = discover_format_sources(*package);
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
        return Ok(empty {});
    };
    rstd_try(format_catalog(project.primary));
    if (project.tests.is_some()) {
        rstd_try(format_catalog(*project.tests));
    }
    if (summary.packages != selected.len()) {
        return format_failure<FormatSummary>(
            "selected packages are missing from local project catalog"_str);
    }
    return Ok(rstd::move(summary));
}

} // namespace lito
