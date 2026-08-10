export module lito.frontend_analysis;

import rstd;
import lito.model;
import lito.frontend;
import lito.toolchain;
import lito.cache;
import lito.build_layout;
import lito.profiling;
import lito.frontend_observer;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

struct FrontendAnalysisTaskOutcome {
    Result<frontend::FrontendAnalysis> analysis;
    frontend::FrontendStatistics       statistics;
    ScanTaskProfile                    profile;
};

class FrontendAnalysisTask {
    const ClangToolchain*                    toolchain_ {};
    frontend::FrontendSourceStore            source_store_;
    ScanCacheTransaction                     cache_;
    PathBuf                                  source_;
    toolchain::SharedPreprocessorEnvironment environment_;
    ScanTaskProfileContext                   profile_;

    FrontendAnalysisTask(const ClangToolchain&                    toolchain,
                         frontend::FrontendSourceStore            source_store,
                         ScanCacheTransaction                     cache,
                         PathBuf                                  source,
                         toolchain::SharedPreprocessorEnvironment environment,
                         ScanTaskProfileContext                   profile)
        : toolchain_(rstd::addressof(toolchain)),
          source_store_(rstd::move(source_store)),
          cache_(rstd::move(cache)),
          source_(rstd::move(source)),
          environment_(rstd::move(environment)),
          profile_(rstd::move(profile)) {}

    friend class FrontendAnalysisService;

public:
    FrontendAnalysisTask(FrontendAnalysisTask&&) noexcept                    = default;
    auto operator=(FrontendAnalysisTask&&) noexcept -> FrontendAnalysisTask& = default;

    auto run() && -> Result<FrontendAnalysisTaskOutcome> {
        auto created_profiler = ScanTaskProfiler::create(rstd::move(profile_));
        if (created_profiler.is_err()) {
            return Err(Error::make(ErrorKind::Artifact,
                                   rstd::move(created_profiler).unwrap_err_unchecked()));
        }
        auto profiler = rstd::move(created_profiler).unwrap_unchecked();
        auto observer = FrontendTaskProfileObserver::make(profiler);
        auto frontend_service =
            frontend::FrontendService::with_store(source_store_, Some(observer.observer()));
        auto analysis = [&]() -> Result<frontend::FrontendAnalysis> {
            auto cached = cache_.lookup();
            if (cached.is_err()) return Err(rstd::move(cached).unwrap_err());
            if (cached->hit.is_some()) return Ok(rstd::move(cached->hit).unwrap());
            auto analyzed = toolchain_->preprocess_with_environment(
                source_.as_path(), environment_, frontend_service, profiler);
            if (analyzed.is_err()) return Err(rstd::move(analyzed).unwrap_err());
            return cache_.publish(rstd::move(analyzed).unwrap());
        }();
        auto finished = profiler.finish();
        if (finished.is_err()) {
            return Err(
                Error::make(ErrorKind::Artifact, rstd::move(finished).unwrap_err_unchecked()));
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

    auto prepare(ref<str>              target,
                 ref<rstd::path::Path> relative_source,
                 ref<rstd::path::Path> source,
                 const CompileContext& context,
                 ref<rstd::path::Path> working_directory,
                 ScanSourceOrigin      origin) -> Result<FrontendAnalysisTask> {
        auto record = layout_.cache_scan(target, relative_source);
        if (record.is_err()) return Err(rstd::move(record).unwrap_err());
        auto environment = toolchain_.prepare_scan_environment(context, working_directory);
        if (environment.is_err()) return Err(rstd::move(environment).unwrap_err());
        auto profile = profiler_.task(target, source, origin);
        if (profile.is_err()) {
            return Err(
                Error::make(ErrorKind::Artifact, rstd::move(profile).unwrap_err_unchecked()));
        }
        auto input = ScanCacheInput {
            .record                   = rstd::move(record).unwrap(),
            .target                   = String::make(target),
            .relative_source          = PathBuf::from(relative_source),
            .source                   = PathBuf::from(source),
            .context_identity         = context.scan_id.clone(),
            .working_directory        = PathBuf::from(working_directory),
            .preprocessor_environment = (*environment)->identity.clone(),
        };
        return Ok(FrontendAnalysisTask(toolchain_,
                                       source_store_.clone(),
                                       cache_.begin(rstd::move(input)),
                                       PathBuf::from(source),
                                       rstd::move(environment).unwrap(),
                                       rstd::move(profile).unwrap_unchecked()));
    }

    auto commit(FrontendAnalysisTaskOutcome outcome) -> Result<frontend::FrontendAnalysis> {
        statistics_.add(outcome.statistics);
        auto ingested = profiler_.ingest(rstd::move(outcome.profile));
        if (ingested.is_err()) {
            return Err(
                Error::make(ErrorKind::Artifact, rstd::move(ingested).unwrap_err_unchecked()));
        }
        return rstd::move(outcome.analysis);
    }

    auto analyze(ref<str>              target,
                 ref<rstd::path::Path> relative_source,
                 ref<rstd::path::Path> source,
                 const CompileContext& context,
                 ref<rstd::path::Path> working_directory) -> Result<frontend::FrontendAnalysis> {
        auto task = prepare(target,
                            relative_source,
                            source,
                            context,
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
