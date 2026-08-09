export module lito.formatter;

import rstd;
import lito.model;
import lito.source_discovery;
import lito.workspace_resolver;
import lito.lock_store;
import lito.toolchain;

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

    auto lock = load_lock_session(request.selection.root.as_path(), false);
    if (lock.is_err()) return Err(rstd::move(lock).unwrap_err());
    auto lock_session  = rstd::move(lock).unwrap();
    auto resolution    = lock_session.take_resolution_options();
    resolution.sources = request.sources.clone();
    auto resolved      = resolve_package_selection(
        request.selection, PackageSelectionPurpose::All, rstd::move(resolution));
    if (resolved.is_err()) return Err(rstd::move(resolved).unwrap_err());
    auto selection = rstd::move(resolved).unwrap();

    auto selected_roots = StringSet::make();
    for (const auto& name : selection.selected_root_names) {
        selected_roots.insert(name.clone(), empty {});
    }

    auto created = toolchain::ClangFormat::create(request.toolchain.formatter.as_path());
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
