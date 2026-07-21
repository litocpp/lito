export module tenon.source_discovery;

import rstd;
import tenon.model;
import tenon.module_convention;

using namespace rstd::literals;

namespace tenon::source_discovery_detail
{

using StringSet = rstd::collections::BTreeMap<String, rstd::empty>;

template<typename T>
auto failure(String message) -> Result<T> {
    return rstd::Err(Error::make(ErrorKind::Manifest, rstd::move(message)));
}

auto supported_extension(rstd::ref<rstd::path::Path> path) -> bool {
    auto extension = path.extension();
    if (extension.is_none()) return false;
    auto text = (*extension).to_str();
    if (text.is_none()) return false;
    return *text == "cppm"_str || *text == "cpp"_str || *text == "cc"_str ||
           *text == "cxx"_str;
}

auto path_text(rstd::ref<rstd::path::Path> path) -> Result<String> {
    auto text = path.to_str();
    if (text.is_none()) {
        return failure<String>(rstd::format("source path '{}' is not valid UTF-8", path));
    }
    return rstd::Ok(String::make(*text));
}

struct SourceEntry {
    String         key;
    ResolvedSource source;
};

} // namespace tenon::source_discovery_detail

export namespace tenon
{

auto discover_sources(const PackageManifest& manifest) -> Result<ResolvedSourceSet> {
    using namespace source_discovery_detail;

    if (manifest.discovery == SourceDiscoveryMode::Module) {
        return discover_module_sources(manifest);
    }
    auto seen = StringSet::make();
    auto entries = Vec<SourceEntry>::make();
    for (const auto& declared : manifest.declared_sources) {
        auto requested = manifest.root.join(declared.as_path());
        auto canonical = rstd::fs::canonicalize(requested.as_path());
        if (canonical.is_err()) {
            return failure<ResolvedSourceSet>(rstd::format(
                "cannot resolve declared source '{}': {}",
                declared.as_path(),
                rstd::move(canonical).unwrap_err()));
        }
        auto resolved = rstd::move(canonical).unwrap();
        auto relative = resolved.as_path().strip_prefix(manifest.root.as_path());
        if (relative.is_none() || (*relative).is_empty()) {
            return failure<ResolvedSourceSet>(rstd::format(
                "declared source '{}' resolves outside package root '{}'",
                declared.as_path(),
                manifest.root.as_path()));
        }
        auto metadata = rstd::fs::metadata(resolved.as_path());
        if (metadata.is_err()) {
            return failure<ResolvedSourceSet>(rstd::format(
                "cannot inspect declared source '{}': {}",
                declared.as_path(),
                rstd::move(metadata).unwrap_err()));
        }
        if (! metadata->is_file()) {
            return failure<ResolvedSourceSet>(
                rstd::format("declared source '{}' is not a file", declared.as_path()));
        }
        if (! supported_extension(resolved.as_path())) {
            return failure<ResolvedSourceSet>(rstd::format(
                "unsupported C++ source extension: {}", declared.as_path()));
        }
        auto key = path_text(*relative);
        if (key.is_err()) return rstd::Err(rstd::move(key).unwrap_err());
        auto source_key = rstd::move(key).unwrap();
        if (seen.contains_key(source_key.as_str())) {
            return failure<ResolvedSourceSet>(rstd::format(
                "library.sources repeats source '{}'", source_key.as_str()));
        }
        seen.insert(source_key.clone(), rstd::empty {});
        entries.push(SourceEntry {
            .key = rstd::move(source_key),
            .source = ResolvedSource {
                .relative_path = PathBuf::from(*relative),
                .canonical_path = rstd::move(resolved),
                .origin = SourceOrigin::Explicit,
            },
        });
    }
    rstd::slice_::sort_unstable_by(
        entries.as_mut_slice().as_mut_ref(),
        [](const SourceEntry& left, const SourceEntry& right) { return left.key < right.key; });

    auto sources = Vec<ResolvedSource>::with_capacity(entries.len());
    for (auto& entry : entries) sources.push(rstd::move(entry.source));
    return rstd::Ok(ResolvedSourceSet { .sources = rstd::move(sources) });
}

} // namespace tenon
