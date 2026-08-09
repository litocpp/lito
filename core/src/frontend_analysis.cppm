export module lito.frontend_analysis;

import rstd;
import lito.model;
import lito.frontend;
import lito.toolchain;
import lito.cache;
import lito.build_layout;
import lito.profiling;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

class FrontendAnalysisService {
public:
    static auto make(const BuildLayout&         layout,
                     const ClangToolchain&      toolchain,
                     frontend::FrontendService& frontend_service,
                     ScanCacheSession&          cache,
                     ScanProfiler&              profiler) -> FrontendAnalysisService {
        return FrontendAnalysisService { layout, toolchain, frontend_service, cache, profiler };
    }

    static auto make_documentation(const ClangToolchain&      toolchain,
                                   frontend::FrontendService& frontend_service,
                                   ScanProfiler&              profiler) -> FrontendAnalysisService {
        return FrontendAnalysisService { toolchain, frontend_service, profiler };
    }

    auto analyze(ref<str>              target,
                 ref<rstd::path::Path> relative_source,
                 ref<rstd::path::Path> source,
                 const CompileContext& context,
                 ref<rstd::path::Path> working_directory) -> Result<frontend::FrontendAnalysis> {
        if (layout_ == nullptr || cache_ == nullptr) {
            return Err(
                Error::make(ErrorKind::Artifact, "scan analysis service has no cache state"_str));
        }
        auto record = layout_->cache_scan(target, relative_source);
        if (record.is_err()) return Err(rstd::move(record).unwrap_err());
        auto environment =
            toolchain_->preprocessor_environment_identity(context, working_directory);
        if (environment.is_err()) return Err(rstd::move(environment).unwrap_err());
        auto input = ScanCacheInput {
            .record                   = rstd::move(record).unwrap(),
            .target                   = String::make(target),
            .relative_source          = PathBuf::from(relative_source),
            .source                   = PathBuf::from(source),
            .context_identity         = context.id.clone(),
            .working_directory        = PathBuf::from(working_directory),
            .preprocessor_environment = rstd::move(environment).unwrap(),
        };
        auto cached = cache_->lookup(input);
        if (cached.is_err()) return Err(rstd::move(cached).unwrap_err());
        if (cached->hit.is_some()) {
            return Ok(rstd::move(cached->hit).unwrap());
        }
        auto analyzed = toolchain_->preprocess(
            source, context, working_directory, *frontend_service_, *profiler_);
        if (analyzed.is_err()) return Err(rstd::move(analyzed).unwrap_err());
        auto published = cache_->publish(input, rstd::move(analyzed).unwrap());
        if (published.is_err()) return Err(rstd::move(published).unwrap_err());
        return published;
    }

    auto profiler() noexcept -> ScanProfiler& { return *profiler_; }

    auto analyze_documentation(ref<rstd::path::Path> source,
                               const CompileContext& context,
                               ref<rstd::path::Path> working_directory)
        -> Result<frontend::DocumentationAnalysis> {
        auto analyzed = toolchain_->preprocess_documentation(
            source, context, working_directory, *frontend_service_, *profiler_);
        if (analyzed.is_err()) return Err(rstd::move(analyzed).unwrap_err());
        auto value = rstd::move(analyzed).unwrap();
        frontend_service_->record_documentation_build(value.documentation.declarations.len());
        return Ok(frontend::DocumentationAnalysis {
            .analysis =
                frontend::FrontendAnalysis {
                    .result           = rstd::move(value.result),
                    .context_identity = context.id.clone(),
                    .receipt          = String::make(),
                    .origin           = frontend::FrontendAnalysisOrigin::Uncacheable,
                },
            .documentation = rstd::move(value.documentation),
        });
    }

    auto record_in_build_reuse() noexcept -> void { frontend_service_->record_analysis_hit(); }

private:
    FrontendAnalysisService(const BuildLayout&         layout,
                            const ClangToolchain&      toolchain,
                            frontend::FrontendService& frontend_service,
                            ScanCacheSession&          cache,
                            ScanProfiler&              profiler)
        : layout_(&layout),
          toolchain_(&toolchain),
          frontend_service_(&frontend_service),
          cache_(&cache),
          profiler_(&profiler) {}

    FrontendAnalysisService(const ClangToolchain&      toolchain,
                            frontend::FrontendService& frontend_service,
                            ScanProfiler&              profiler)
        : toolchain_(&toolchain), frontend_service_(&frontend_service), profiler_(&profiler) {}

    const BuildLayout*         layout_ {};
    const ClangToolchain*      toolchain_ {};
    frontend::FrontendService* frontend_service_ {};
    ScanCacheSession*          cache_ {};
    ScanProfiler*              profiler_ {};
};

} // namespace lito
