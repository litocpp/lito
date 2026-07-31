export module tenon.formatter;

import rstd;
import tenon.model;
import tenon.source_discovery;
import tenon.workspace_resolver;
import tenon.toolchain.clang_format;

using namespace rstd::prelude;
using namespace rstd::literals;
using StringSet = rstd::collections::BTreeMap<String, empty>;

namespace tenon
{

template<typename T>
auto format_failure(ErrorKind kind, ref<str> message) -> Result<T> {
    return Err(Error::make(kind, message));
}

} // namespace tenon

export namespace tenon
{

auto format(const FormatRequest& request) -> Result<FormatSummary> {
    if (request.selection.root.is_empty()) {
        return format_failure<FormatSummary>(
            ErrorKind::InvalidRequest, "format directory is required"_str);
    }
    if (request.toolchain.formatter.is_empty()) {
        return format_failure<FormatSummary>(
            ErrorKind::InvalidRequest, "clang-format path is required"_str);
    }

    auto resolved = resolve_package_selection(request.selection);
    if (resolved.is_err()) return Err(rstd::move(resolved).unwrap_err());
    auto selection = rstd::move(resolved).unwrap();

    auto selected_roots = StringSet::make();
    for (const auto& id : selection.selected_root_ids) {
        selected_roots.insert(id.clone(), empty {});
    }

    auto created = toolchain::ClangFormat::create(
        request.toolchain.formatter.as_path());
    if (created.is_err()) return Err(rstd::move(created).unwrap_err());
    auto formatter = rstd::move(created).unwrap();

    auto summary = FormatSummary {};
    for (const auto& package : selection.graph.packages) {
        if (! selected_roots.contains_key(package.id.as_str())) continue;
        auto discovered = discover_sources(package.manifest);
        if (discovered.is_err()) return Err(rstd::move(discovered).unwrap_err());
        auto sources = rstd::move(discovered).unwrap();
        for (const auto& source : sources.sources) {
            auto formatted = formatter.format(source.canonical_path.as_path());
            if (formatted.is_err()) return Err(rstd::move(formatted).unwrap_err());
            ++summary.files;
        }
        ++summary.packages;
    }
    if (summary.packages != selection.selected_root_ids.len()) {
        return format_failure<FormatSummary>(
            ErrorKind::Manifest, "selected packages are missing from resolved graph"_str);
    }
    return Ok(summary);
}

} // namespace tenon
