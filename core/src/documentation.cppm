export module tenon.documentation;

import rstd;
import tenon.doc;
import tenon.model;
import tenon.project;
import tenon.package;
import tenon.source_discovery;
import tenon.toolchain;
import tenon.frontend;
import tenon.frontend_analysis;
import tenon.frontend_observer;
import tenon.profiling;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace tenon
{

template<typename T>
auto doc_failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Artifact, rstd::move(message)));
}

template<typename T>
auto doc_failure(ref<str> message) -> Result<T> {
    return Err(Error::make(ErrorKind::InvalidRequest, message));
}

auto default_doc_root(ref<rstd::path::Path> project,
                      ref<str>              profile,
                      ref<rstd::path::Path> requested) -> PathBuf {
    if (! requested.is_empty()) {
        return requested.is_absolute() ? PathBuf::from(requested)
                                       : PathBuf::from(project).join(requested);
    }
    return PathBuf::from(project)
        .join(PathBuf::from("build"_str).as_path())
        .join(PathBuf::from(profile).as_path())
        .join(PathBuf::from("doc"_str).as_path());
}

auto default_doc_data_root(ref<rstd::path::Path> project,
                           ref<str>              profile,
                           ref<rstd::path::Path> requested) -> PathBuf {
    if (! requested.is_empty()) {
        return requested.is_absolute() ? PathBuf::from(requested)
                                       : PathBuf::from(project).join(requested);
    }
    return PathBuf::from(project)
        .join(PathBuf::from("build"_str).as_path())
        .join(PathBuf::from(profile).as_path())
        .join(PathBuf::from("doc-data"_str).as_path());
}

auto resolved_doc_path(ref<rstd::path::Path> root, ref<rstd::path::Path> requested) -> PathBuf {
    return requested.is_absolute() ? PathBuf::from(requested) : PathBuf::from(root).join(requested);
}

auto selected_doc_target(const Vec<String>& names, ref<str> name) -> bool {
    for (const auto& selected : names) {
        if (selected.as_str() == name) return true;
    }
    return false;
}

auto copy_doc_summary(String                       profile,
                      tenon::doc::Summary          rendered,
                      frontend::FrontendStatistics frontend_statistics,
                      ToolchainStatistics          toolchain_statistics) -> DocSummary {
    auto summary = DocSummary {
        .profile        = rstd::move(profile),
        .output         = rstd::move(rendered.output),
        .index          = rstd::move(rendered.index),
        .data           = rstd::move(rendered.data.root),
        .data_manifest  = rstd::move(rendered.data.manifest),
        .site_generated = rendered.site_generated,
        .frontend       = rstd::move(frontend_statistics),
        .toolchain      = rstd::move(toolchain_statistics),
    };
    for (auto& package : rendered.packages) {
        auto package_summary = DocPackageSummary {
            .name         = rstd::move(package.name),
            .directory    = rstd::move(package.directory),
            .json         = rstd::move(package.json),
            .data_json    = rstd::move(package.data_json),
            .index        = rstd::move(package.index),
            .symbols      = package.symbols,
            .documented   = package.documented,
            .undocumented = package.undocumented,
            .unsupported  = package.unsupported,
            .diagnostics  = package.diagnostics,
        };
        for (auto& diagnostic : package.diagnostic_details) {
            package_summary.diagnostic_details.push(DocDiagnosticSummary {
                .severity = diagnostic.severity == frontend::DocumentationSeverity::Error
                                ? DocDiagnosticSeverity::Error
                                : DocDiagnosticSeverity::Warning,
                .code     = rstd::move(diagnostic.code),
                .message  = rstd::move(diagnostic.message),
                .path     = PathBuf::from(diagnostic.path.as_str()),
                .line     = diagnostic.line,
            });
        }
        summary.packages.push(rstd::move(package_summary));
    }
    return summary;
}

} // namespace tenon

export namespace tenon
{

auto generate_documentation(const DocRequest& request) -> Result<DocSummary> {
    if (request.selection.root.is_empty())
        return doc_failure<DocSummary>("doc directory is required"_str);
    auto created_toolchain = ClangToolchain::create(request.configuration.toolchain);
    if (created_toolchain.is_err()) return Err(rstd::move(created_toolchain).unwrap_err());
    auto toolchain = rstd::move(created_toolchain).unwrap();
    auto loaded    = resolve_project_metadata(request.selection,
                                              request.configuration,
                                              request.sources,
                                              request.pkg_config,
                                              request.cmake,
                                              toolchain.target_info(),
                                              toolchain.argument_parser(),
                                              request.locked,
                                              PackageSelectionPurpose::Documentation);
    if (loaded.is_err()) return Err(rstd::move(loaded).unwrap_err());
    auto metadata = rstd::move(loaded).unwrap();
    auto resolved =
        resolve_source_discovery(metadata, metadata.default_profile.as_str(), request.targets);
    if (resolved.is_err()) return Err(rstd::move(resolved).unwrap_err());
    auto discovery = rstd::move(resolved).unwrap();
    auto profile   = metadata.profiles[discovery.profile].name.clone();

    auto created_profiler = ScanProfiler::create();
    if (created_profiler.is_err())
        return doc_failure<DocSummary>(rstd::move(created_profiler).unwrap_err_unchecked());
    auto profiler          = rstd::move(created_profiler).unwrap_unchecked();
    auto frontend_observer = FrontendProfileObserver::make(profiler);
    auto frontend_service  = frontend::FrontendService::make(Some(frontend_observer.observer()));
    auto analysis_service =
        FrontendAnalysisService::make_documentation(toolchain, frontend_service, profiler);
    auto discovered = discover_documentation_sources(metadata, discovery, analysis_service, None());
    if (discovered.is_err()) return Err(rstd::move(discovered).unwrap_err());
    auto source_sets = rstd::move(discovered).unwrap();

    const auto& selected_names =
        request.targets.is_empty() ? metadata.default_targets : request.targets;
    auto frontend_path = Option<PathBuf> {};
    if (request.frontend.is_some()) {
        frontend_path =
            Some(resolved_doc_path(metadata.root.as_path(), request.frontend->as_path()));
    }
    auto site = tenon::doc::SiteInput {
        .title = metadata.name.clone(),
        .output =
            default_doc_root(metadata.root.as_path(), profile.as_str(), request.output.as_path()),
        .data_output = default_doc_data_root(
            metadata.root.as_path(), profile.as_str(), request.data_output.as_path()),
        .frontend  = rstd::move(frontend_path),
        .data_only = request.data_only,
    };
    for (auto& source_set : source_sets) {
        if (! selected_doc_target(selected_names, source_set.package_name.as_str())) continue;
        auto target = Option<usize> {};
        for (auto index = usize {}; index < metadata.targets.len(); ++index) {
            if (metadata.targets[index].manifest.name.as_str() ==
                source_set.package_name.as_str()) {
                target = Some(index);
                break;
            }
        }
        if (target.is_none())
            return doc_failure<DocSummary>(rstd::format("doc target '{}' is missing metadata",
                                                        source_set.package_name.as_str()));
        const auto& manifest = metadata.targets[*target].manifest;
        if (manifest.artifact_kind != ArtifactKind::StaticLibrary) continue;
        auto package = tenon::doc::PackageInput {
            .name = manifest.name.clone(),
            .version =
                manifest.version.value.is_some() ? manifest.version.value->clone() : String::make(),
            .root_module =
                manifest.root_module.is_some() ? manifest.root_module->clone() : String::make(),
            .profile           = profile.clone(),
            .root              = manifest.root.clone(),
            .toolchain_version = toolchain.compiler_identity().version.clone(),
            .toolchain_target  = toolchain.compiler_identity().target.clone(),
            .language_standard = discovery.contexts[*target].cpp.language.standard.clone(),
        };
        for (auto& source : source_set.sources.sources) {
            if (source.documentation.is_none())
                return doc_failure<DocSummary>(
                    rstd::format("doc source '{}' has no documentation product",
                                 source.canonical_path.as_path()));
            package.units.push(rstd::move(source.documentation).unwrap());
        }
        site.packages.push(rstd::move(package));
    }
    if (site.packages.is_empty())
        return doc_failure<DocSummary>("doc selection has no library package"_str);
    auto generated = tenon::doc::generate(rstd::move(site));
    if (generated.is_err()) return doc_failure<DocSummary>(rstd::move(generated).unwrap_err());
    return Ok(copy_doc_summary(rstd::move(profile),
                               rstd::move(generated).unwrap(),
                               frontend_service.statistics(),
                               toolchain.statistics()));
}

auto render_documentation(const DocRenderRequest& request) -> Result<DocSummary> {
    if (request.working_directory.is_empty())
        return doc_failure<DocSummary>("doc working directory is required"_str);
    if (request.data.is_empty())
        return doc_failure<DocSummary>("doc data directory is required"_str);
    auto data = resolved_doc_path(request.working_directory.as_path(), request.data.as_path());
    auto output =
        request.output.is_empty()
            ? request.working_directory.join(PathBuf::from("doc"_str).as_path())
            : resolved_doc_path(request.working_directory.as_path(), request.output.as_path());
    auto frontend_path = Option<PathBuf> {};
    if (request.frontend.is_some()) {
        frontend_path = Some(
            resolved_doc_path(request.working_directory.as_path(), request.frontend->as_path()));
    }
    auto rendered = tenon::doc::render(tenon::doc::RenderInput {
        .data     = rstd::move(data),
        .output   = rstd::move(output),
        .frontend = rstd::move(frontend_path),
    });
    if (rendered.is_err()) return doc_failure<DocSummary>(rstd::move(rendered).unwrap_err());
    return Ok(
        copy_doc_summary(String::make("from-data"_str), rstd::move(rendered).unwrap(), {}, {}));
}

} // namespace tenon
