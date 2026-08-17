module lito.driver:build.frontend_analysis;

import rstd;
import lito.core;
import lito.cpp;
import :build.error;
import lito.frontend;
import lito.toolchain;
import :cache;
import :build.layout;
import :build.profiling;
import :build.frontend_observer;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

struct FrontendAnalysisTaskOutcome {
    BuildResult<frontend::FrontendAnalysis> analysis;
    frontend::FrontendStatistics            statistics;
    ScanTaskProfile                         profile;
};

class FrontendAnalysisTask {
    const ClangToolchain*                    toolchain_ {};
    frontend::FrontendSourceStore            source_store_;
    ScanCacheTransaction                     cache_;
    PathBuf                                  source_;
    toolchain::PreparedScanInput              input_;
    ScanTaskProfileContext                   profile_;

    FrontendAnalysisTask(const ClangToolchain&                    toolchain,
                         frontend::FrontendSourceStore            source_store,
                         ScanCacheTransaction                     cache,
                         PathBuf                                  source,
                         toolchain::PreparedScanInput              input,
                         ScanTaskProfileContext                   profile)
        : toolchain_(rstd::addressof(toolchain)),
          source_store_(rstd::move(source_store)),
          cache_(rstd::move(cache)),
          source_(rstd::move(source)),
          input_(rstd::move(input)),
          profile_(rstd::move(profile)) {}

    friend class FrontendAnalysisService;

public:
    FrontendAnalysisTask(FrontendAnalysisTask&&) noexcept                    = default;
    auto operator=(FrontendAnalysisTask&&) noexcept -> FrontendAnalysisTask& = default;

    auto run() && -> BuildResult<FrontendAnalysisTaskOutcome> {
        auto created_profiler = ScanTaskProfiler::create(rstd::move(profile_));
        if (created_profiler.is_err()) {
            return Err(BuildError::Message(rstd::move(created_profiler).unwrap_err_unchecked()));
        }
        auto profiler = rstd::move(created_profiler).unwrap_unchecked();
        auto observer = FrontendTaskProfileObserver::make(profiler);
        auto frontend_service =
            frontend::FrontendService::with_store(source_store_, Some(observer.observer()));
        auto analysis = [&]() -> BuildResult<frontend::FrontendAnalysis> {
            auto cached = cache_.lookup();
            if (cached.is_err()) {
                return Err(rstd::into<BuildError>(rstd::move(cached).unwrap_err()));
            }
            if (cached->hit.is_some()) return Ok(rstd::move(cached->hit).unwrap());
            auto frontend_span         = profiler.span(ScanProbe::Frontend);
            auto preprocessor_span     = profiler.span(ScanProbe::Preprocessor);
            auto preprocessor_observer = PreprocessorTaskProfileObserver(profiler);
            auto analyzed              = toolchain_->preprocess_with_environment(
                source_.as_path(), input_, frontend_service, preprocessor_observer);
            auto observer_finished     = preprocessor_observer.finish();
            auto preprocessor_finished = profiler.complete(preprocessor_span);
            auto frontend_finished     = profiler.complete(frontend_span);
            if (observer_finished.is_err()) {
                return Err(
                    BuildError::Message(rstd::move(observer_finished).unwrap_err_unchecked()));
            }
            if (preprocessor_finished.is_err()) {
                return Err(
                    BuildError::Message(rstd::move(preprocessor_finished).unwrap_err_unchecked()));
            }
            if (frontend_finished.is_err()) {
                return Err(
                    BuildError::Message(rstd::move(frontend_finished).unwrap_err_unchecked()));
            }
            frontend_service.record_preprocessor_statistics(preprocessor_observer.statistics());
            if (analyzed.is_err()) {
                return Err(rstd::into<BuildError>(rstd::move(analyzed).unwrap_err()));
            }
            auto published = cache_.publish(rstd::move(analyzed).unwrap());
            if (published.is_err()) {
                return Err(rstd::into<BuildError>(rstd::move(published).unwrap_err()));
            }
            return Ok(rstd::move(published).unwrap());
        }();
        auto finished = profiler.finish();
        if (finished.is_err()) {
            return Err(BuildError::Message(rstd::move(finished).unwrap_err_unchecked()));
        }
        return Ok(FrontendAnalysisTaskOutcome {
            .analysis   = rstd::move(analysis),
            .statistics = frontend_service.statistics(),
            .profile    = rstd::move(finished).unwrap_unchecked(),
        });
    }
};

class FrontendAnalysisService {
public:
    static auto make(const BuildLayout&         layout,
                     const ClangToolchain&      toolchain,
                     frontend::FrontendService& frontend_service,
                     ScanCacheSession&          cache,
                     ScanProfiler&              profiler) -> FrontendAnalysisService {
        return FrontendAnalysisService {
            layout, toolchain, frontend_service.source_store(), cache.clone(), profiler
        };
    }

    auto prepare(const lito::package::PackageTargetId& target,
                 ref<rstd::path::Path>                 relative_source,
                 ref<rstd::path::Path>                 source,
                 const cpp::CompileContext&            context,
                 const cpp::PackageCompileMetadata&    compile_metadata,
                 ref<rstd::path::Path>                 working_directory,
                 ScanSourceOrigin origin) -> BuildResult<FrontendAnalysisTask> {
        auto record = layout_.cache_scan(target, relative_source);
        if (record.is_err()) {
            return Err(rstd::into<BuildError>(rstd::move(record).unwrap_err()));
        }
        auto environment_span     = profiler_.span(ScanProbe::Environment);
        auto prepared = toolchain_.prepare_scan_input(
            context, compile_metadata, working_directory);
        auto environment_finished = profiler_.complete(environment_span);
        if (environment_finished.is_err()) {
            return Err(
                BuildError::Message(rstd::move(environment_finished).unwrap_err_unchecked()));
        }
        if (prepared.is_err()) {
            return Err(rstd::into<BuildError>(rstd::move(prepared).unwrap_err()));
        }
        auto scan_input      = rstd::move(prepared).unwrap();
        auto target_identity = lito::package::package_target_id_text(target);
        auto profile         = profiler_.task(target_identity.as_str(), source, origin);
        if (profile.is_err()) {
            return Err(BuildError::Message(rstd::move(profile).unwrap_err_unchecked()));
        }
        auto cache_input = ScanCacheInput {
            .record                   = rstd::move(record).unwrap(),
            .target                   = target_identity.clone(),
            .relative_source          = PathBuf::from(relative_source),
            .source                   = PathBuf::from(source),
            .context_identity         = context.scan_id.clone(),
            .working_directory        = PathBuf::from(working_directory),
            .preprocessor_environment = scan_input.environment->identity.clone(),
            .external_macro_schema    = String::make(
                scan_input.external_macros->schema_identity()),
            .external_macros          = scan_input.external_macros.clone(),
        };
        return Ok(FrontendAnalysisTask(toolchain_,
                                       source_store_.clone(),
                                       cache_.begin(rstd::move(cache_input)),
                                       PathBuf::from(source),
                                       rstd::move(scan_input),
                                       rstd::move(profile).unwrap_unchecked()));
    }

    auto commit(FrontendAnalysisTaskOutcome outcome) -> BuildResult<frontend::FrontendAnalysis> {
        statistics_.add(outcome.statistics);
        auto ingested = profiler_.ingest(rstd::move(outcome.profile));
        if (ingested.is_err()) {
            return Err(BuildError::Message(rstd::move(ingested).unwrap_err_unchecked()));
        }
        return rstd::move(outcome.analysis);
    }

    auto analyze(const lito::package::PackageTargetId& target,
                 ref<rstd::path::Path>                 relative_source,
                 ref<rstd::path::Path>                 source,
                 const cpp::CompileContext&            context,
                 const cpp::PackageCompileMetadata&    compile_metadata,
                 ref<rstd::path::Path>                 working_directory)
        -> BuildResult<frontend::FrontendAnalysis> {
        auto task = prepare(target,
                            relative_source,
                            source,
                            context,
                            compile_metadata,
                            working_directory,
                            ScanSourceOrigin::Discovery);
        if (task.is_err()) return Err(rstd::move(task).unwrap_err());
        auto outcome = rstd::move(task).unwrap().run();
        if (outcome.is_err()) return Err(rstd::move(outcome).unwrap_err());
        return commit(rstd::move(outcome).unwrap());
    }

    auto profiler() noexcept -> ScanProfiler& { return profiler_; }

    auto record_in_build_reuse() noexcept -> void { ++statistics_.analyze_hits; }

    auto statistics() const noexcept -> const frontend::FrontendStatistics& { return statistics_; }

    auto release_source_cache() -> void { source_store_.release(); }

private:
    FrontendAnalysisService(const BuildLayout&            layout,
                            const ClangToolchain&         toolchain,
                            frontend::FrontendSourceStore source_store,
                            ScanCacheSession              cache,
                            ScanProfiler&                 profiler)
        : layout_(layout),
          toolchain_(toolchain),
          source_store_(rstd::move(source_store)),
          cache_(rstd::move(cache)),
          profiler_(profiler) {}

    const BuildLayout&            layout_;
    const ClangToolchain&         toolchain_;
    frontend::FrontendSourceStore source_store_;
    ScanCacheSession              cache_;
    ScanProfiler&                 profiler_;
    frontend::FrontendStatistics  statistics_;
};

} // namespace lito
