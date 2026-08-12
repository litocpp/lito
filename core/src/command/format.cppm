export module lito.command.format;

import rstd;
import lito.error;
export import lito.command.project_contract;
import lito.workspace.contract;
import lito.package.graph_contract;
import lito.build.discovery;
import lito.project;
import lito.toolchain;
import lito.system.environment;

using namespace rstd::prelude;
using namespace rstd::literals;
using StringSet = rstd::collections::BTreeMap<String, empty>;

namespace lito
{

template<typename T>
auto format_failure(ErrorKind kind, ref<str> message) -> Result<T> {
    return Err(Error::make(kind, message));
}

} // namespace lito

export namespace lito
{

auto format(const FormatRequest& request) -> Result<FormatSummary> {
    if (request.selection.root.is_empty()) {
        return format_failure<FormatSummary>(ErrorKind::InvalidRequest,
                                             "format directory is required"_str);
    }
    if (request.toolchain.formatter.is_empty()) {
        return format_failure<FormatSummary>(ErrorKind::InvalidRequest,
                                             "clang-format path is required"_str);
    }

    auto environment = ResolvedProcessEnvironment::resolve(request.environment);
    if (environment.is_err()) return Err(rstd::move(environment).unwrap_err());
    auto tool_resolver = ToolResolver(*environment);
    auto resolved      = resolve_project_selection(request.selection,
                                                   PackageSelectionPurpose::All,
                                                   request.sources,
                                                   false,
                                                   tool_resolver,
                                                   *environment);
    if (resolved.is_err()) return Err(rstd::move(resolved).unwrap_err());
    auto selection = rstd::move(resolved).unwrap();

    auto selected_roots = StringSet::make();
    for (const auto& name : selection.selected_root_names) {
        selected_roots.insert(name.clone(), empty {});
    }

    auto created = toolchain::ClangFormat::create(
        request.toolchain.formatter.as_path(), tool_resolver, *environment);
    if (created.is_err()) return Err(rstd::move(created).unwrap_err());
    auto formatter = rstd::move(created).unwrap();

    auto summary = FormatSummary {};
    for (const auto& package : selection.graph.packages) {
        if (! selected_roots.contains_key(package.manifest.name.as_str())) continue;
        auto discovered = discover_format_sources(package.manifest);
        if (discovered.is_err()) return Err(rstd::move(discovered).unwrap_err());
        auto sources = rstd::move(discovered).unwrap();
        for (const auto& source : sources.sources) {
            auto formatted = formatter.format(source.canonical_path.as_path());
            if (formatted.is_err()) return Err(rstd::move(formatted).unwrap_err());
            ++summary.files;
        }
        ++summary.packages;
    }
    if (summary.packages != selection.selected_root_names.len()) {
        return format_failure<FormatSummary>(
            ErrorKind::Manifest, "selected packages are missing from resolved graph"_str);
    }
    return Ok(summary);
}

} // namespace lito
