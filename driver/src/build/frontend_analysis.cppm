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

struct HeaderClassifierContext {
    const cpp::HeaderOwnershipIndex* ownership {};
};

auto classify_header(const void* context, ref<rstd::path::Path> path)
    -> frontend::HeaderCacheClassification {
    const auto& classifier = *static_cast<const HeaderClassifierContext*>(context);
    if (classifier.ownership == nullptr) return frontend::HeaderCacheClassification {};
    auto classification = classifier.ownership->classify(path);
    return frontend::HeaderCacheClassification {
        .retention_domain = cpp::header_retention_domain(classification),
    };
}

struct FrontendAnalysisTaskOutcome {
    BuildResult<frontend::FrontendAnalysis> analysis;
    frontend::FrontendStatistics            statistics;
    ScanTaskProfile                         profile;
};

class FrontendAnalysisTask {
    const ClangToolchain*            toolchain_ {};
    const cpp::HeaderOwnershipIndex* header_ownership_ {};
    frontend::FrontendSourceStore    source_store_;
    ScanCacheTransaction             cache_;
    PathBuf                          source_;
    toolchain::PreparedScanInput     input_;
    ScanTaskProfileContext           profile_;

    FrontendAnalysisTask(const ClangToolchain&            toolchain,
                         const cpp::HeaderOwnershipIndex& header_ownership,
                         frontend::FrontendSourceStore    source_store,
                         ScanCacheTransaction             cache,
                         PathBuf                          source,
                         toolchain::PreparedScanInput     input,
                         ScanTaskProfileContext           profile)
        : toolchain_(rstd::addressof(toolchain)),
          header_ownership_(rstd::addressof(header_ownership)),
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
        auto profiler           = rstd::move(created_profiler).unwrap_unchecked();
        auto observer           = FrontendTaskProfileObserver::make(profiler);
        auto classifier_context = HeaderClassifierContext { .ownership = header_ownership_ };
        auto frontend_service   = frontend::FrontendService::with_store(
            source_store_,
            Some(observer.observer()),
            Some(frontend::FrontendHeaderClassifier {
                .context  = rstd::addressof(classifier_context),
                .classify = classify_header,
            }));
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
    auto prepare_with_identity(PathBuf                      record,
                               String                       target_identity,
                               ref<rstd::path::Path>        relative_source,
                               ref<str>                     source_origin_identity,
                               ref<rstd::path::Path>        source,
                               const cpp::CompileContext&   context,
                               toolchain::PreparedScanInput scan_input,
                               ref<rstd::path::Path>        working_directory,
                               ScanSourceOrigin origin) -> BuildResult<FrontendAnalysisTask> {
        auto profile = profiler_.task(target_identity.as_str(), source, origin);
        if (profile.is_err()) {
            return Err(BuildError::Message(rstd::move(profile).unwrap_err_unchecked()));
        }
        auto cache_input = ScanCacheInput {
            .record                   = rstd::move(record),
            .target                   = target_identity.clone(),
            .relative_source          = PathBuf::from(relative_source),
            .source_origin_identity   = String::make(source_origin_identity),
            .source                   = PathBuf::from(source),
            .context_identity         = context.scan_id.clone(),
            .working_directory        = PathBuf::from(working_directory),
            .preprocessor_environment = scan_input.environment->identity.clone(),
            .external_macro_schema    = String::make(scan_input.external_macros->schema_identity()),
            .external_macros          = scan_input.external_macros.clone(),
        };
        return Ok(FrontendAnalysisTask(toolchain_,
                                       header_ownership_,
                                       source_store_.clone(),
                                       cache_.begin(rstd::move(cache_input)),
                                       PathBuf::from(source),
                                       rstd::move(scan_input),
                                       rstd::move(profile).unwrap_unchecked()));
    }

public:
    static auto make(const BuildLayout&               layout,
                     const ClangToolchain&            toolchain,
                     const cpp::HeaderOwnershipIndex& header_ownership,
                     frontend::FrontendService&       frontend_service,
                     ScanCacheSession&                cache,
                     ScanProfiler&                    profiler) -> FrontendAnalysisService {
        return FrontendAnalysisService { layout,           toolchain,
                                         header_ownership, frontend_service.source_store(),
                                         cache.clone(),    profiler };
    }

    auto prepare(const lito::package::PackageTargetId& target,
                 ref<rstd::path::Path>                 relative_source,
                 ref<str>                              source_origin_identity,
                 ref<rstd::path::Path>                 source,
                 const cpp::CompileContext&            context,
                 const cpp::PackageCompileMetadata&    compile_metadata,
                 ref<rstd::path::Path>                 working_directory,
                 ScanSourceOrigin origin) -> BuildResult<FrontendAnalysisTask> {
        auto record = layout_.cache_scan(target, relative_source);
        if (record.is_err()) {
            return Err(rstd::into<BuildError>(rstd::move(record).unwrap_err()));
        }
        auto target_identity  = lito::package::package_target_id_text(target);
        auto environment_span = profiler_.span(ScanProbe::Environment);
        auto scan_input =
            toolchain_.prepare_scan_input(context, compile_metadata, working_directory);
        auto environment_finished = profiler_.complete(environment_span);
        if (environment_finished.is_err()) {
            return Err(
                BuildError::Message(rstd::move(environment_finished).unwrap_err_unchecked()));
        }
        if (scan_input.is_err()) {
            return Err(rstd::into<BuildError>(rstd::move(scan_input).unwrap_err()));
        }
        return prepare_with_identity(rstd::move(record).unwrap(),
                                     rstd::move(target_identity),
                                     relative_source,
                                     source_origin_identity,
                                     source,
                                     context,
                                     rstd::move(scan_input).unwrap(),
                                     working_directory,
                                     origin);
    }

    auto commit(FrontendAnalysisTaskOutcome outcome) -> BuildResult<frontend::FrontendAnalysis> {
        statistics_.add(outcome.statistics);
        auto ingested = profiler_.ingest(rstd::move(outcome.profile));
        if (ingested.is_err()) {
            return Err(BuildError::Message(rstd::move(ingested).unwrap_err_unchecked()));
        }
        return rstd::move(outcome.analysis);
    }

    auto project(frontend::FrontendAnalysis analysis, lito::manifest::PackageLanguage language)
        -> Result<cpp::SourceScanArtifact, String> {
        ++statistics_.full_analyses;
        if (statistics_.full_analyses > statistics_.full_analysis_peak) {
            statistics_.full_analysis_peak = statistics_.full_analyses;
        }
        auto projected = cpp::project_frontend_analysis(rstd::move(analysis), language);
        --statistics_.full_analyses;
        if (projected.is_ok()) {
            ++statistics_.compacted_analyses;
            statistics_.compacted_analysis_bytes += projected->retained_bytes();
        }
        return projected;
    }

    auto analyze(const lito::package::PackageTargetId& target,
                 ref<rstd::path::Path>                 relative_source,
                 ref<str>                              source_origin_identity,
                 ref<rstd::path::Path>                 source,
                 const cpp::CompileContext&            context,
                 const cpp::PackageCompileMetadata&    compile_metadata,
                 ref<rstd::path::Path>                 working_directory)
        -> BuildResult<frontend::FrontendAnalysis> {
        auto task = prepare(target,
                            relative_source,
                            source_origin_identity,
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

    auto analyze_standard_module(ref<str>                   logical_name,
                                 ref<str>                   standard_library_context_identity,
                                 ref<str>                   source_origin_identity,
                                 ref<rstd::path::Path>      source,
                                 const cpp::CompileContext& context,
                                 ref<rstd::path::Path>      working_directory)
        -> BuildResult<frontend::FrontendAnalysis> {
        auto record =
            layout_.cache_standard_module_scan(standard_library_context_identity, logical_name);
        auto target = rstd::format(
            "standard-library::{}::{}",
            cpp::standard_library_name(context.language.as_Cpp().options.abi.standard_library),
            logical_name);
        auto relative         = PathBuf::from(rstd::format("{}.cppm", logical_name));
        auto environment_span = profiler_.span(ScanProbe::Environment);
        auto scan_input =
            toolchain_.prepare_standard_library_scan_input(context, working_directory);
        auto environment_finished = profiler_.complete(environment_span);
        if (environment_finished.is_err()) {
            return Err(
                BuildError::Message(rstd::move(environment_finished).unwrap_err_unchecked()));
        }
        if (scan_input.is_err()) {
            return Err(rstd::into<BuildError>(rstd::move(scan_input).unwrap_err()));
        }
        auto task = prepare_with_identity(rstd::move(record),
                                          rstd::move(target),
                                          relative.as_path(),
                                          source_origin_identity,
                                          source,
                                          context,
                                          rstd::move(scan_input).unwrap(),
                                          working_directory,
                                          ScanSourceOrigin::StandardLibrary);
        if (task.is_err()) return Err(rstd::move(task).unwrap_err());
        auto outcome = rstd::move(task).unwrap().run();
        if (outcome.is_err()) return Err(rstd::move(outcome).unwrap_err());
        return commit(rstd::move(outcome).unwrap());
    }

    auto profiler() noexcept -> ScanProfiler& { return profiler_; }

    auto statistics() const -> frontend::FrontendStatistics {
        auto result                              = statistics_;
        auto store                               = source_store_.statistics();
        result.source_ready_entries              = store.ready_entries;
        result.source_ready_peak                 = store.ready_peak;
        result.source_live_payloads              = store.live_payloads;
        result.source_live_payload_peak          = store.live_payload_peak;
        result.source_retained_bytes             = store.retained_bytes;
        result.source_retained_bytes_peak        = store.retained_bytes_peak;
        result.source_storage_bytes              = store.storage_bytes;
        result.source_storage_bytes_peak         = store.storage_bytes_peak;
        result.source_token_bytes                = store.token_bytes;
        result.source_token_bytes_peak           = store.token_bytes_peak;
        result.source_arena_used_bytes           = store.arena_used_bytes;
        result.source_arena_used_bytes_peak      = store.arena_used_bytes_peak;
        result.source_arena_reserved_bytes       = store.arena_reserved_bytes;
        result.source_arena_reserved_bytes_peak  = store.arena_reserved_bytes_peak;
        result.source_domain_used_bytes          = store.domain_used_bytes;
        result.source_domain_used_bytes_peak     = store.domain_used_bytes_peak;
        result.source_domain_reserved_bytes      = store.domain_reserved_bytes;
        result.source_domain_reserved_bytes_peak = store.domain_reserved_bytes_peak;
        result.source_domain_ordinary_blocks     = store.domain_ordinary_blocks;
        result.source_domain_large_blocks        = store.domain_large_blocks;
        result.source_domain_mapped_bytes        = store.domain_mapped_bytes;
        result.source_domain_mapped_bytes_peak   = store.domain_mapped_bytes_peak;
        result.source_domain_mappings            = store.domain_mappings;
        result.source_domain_mappings_peak       = store.domain_mappings_peak;
        if (source_release_.is_some()) {
            const auto& receipt                  = *source_release_;
            const auto& domain                   = receipt.domain_statistics();
            auto        released                 = receipt.released();
            result.source_domain_used_bytes      = released ? usize {} : domain.used_bytes;
            result.source_domain_reserved_bytes  = released ? usize {} : domain.reserved_bytes;
            result.source_domain_ordinary_blocks = domain.ordinary_blocks;
            result.source_domain_large_blocks    = domain.large_blocks;
            result.source_domain_mapped_bytes    = released ? usize {} : domain.mapped_bytes;
            result.source_domain_mappings        = released ? usize {} : domain.mappings;
            result.source_domain_release_immediate =
                receipt.released_immediately() ? usize(1) : usize {};
            result.source_domain_release_delayed =
                receipt.released_immediately() ? usize {} : usize(1);
        }
        result.source_metadata_reserved_bytes      = store.metadata_reserved_bytes;
        result.source_metadata_reserved_bytes_peak = store.metadata_reserved_bytes_peak;
        result.source_in_flight_entries            = store.in_flight_entries;
        result.source_in_flight_peak               = store.in_flight_peak;
        result.source_active_loads                 = store.active_loads;
        result.source_active_loads_peak            = store.active_loads_peak;
        result.source_cache_hits                   = store.cache_hits;
        result.source_flight_waits                 = store.flight_waits;
        result.source_domain_releases              = store.domain_releases;
        return result;
    }

    auto release_source_cache()
        -> Result<frontend::SourceCacheReleaseReceipt, frontend::SourceCacheReleaseError> {
        return source_store_.release();
    }

    auto record_source_release(frontend::SourceCacheReleaseReceipt receipt) -> void {
        source_release_ = Some(rstd::move(receipt));
    }

    auto release_header_domain(ref<str> domain) -> void { source_store_.release_domain(domain); }

private:
    FrontendAnalysisService(const BuildLayout&               layout,
                            const ClangToolchain&            toolchain,
                            const cpp::HeaderOwnershipIndex& header_ownership,
                            frontend::FrontendSourceStore    source_store,
                            ScanCacheSession                 cache,
                            ScanProfiler&                    profiler)
        : layout_(layout),
          toolchain_(toolchain),
          header_ownership_(header_ownership),
          source_store_(rstd::move(source_store)),
          source_release_(),
          cache_(rstd::move(cache)),
          profiler_(profiler) {}

    const BuildLayout&                          layout_;
    const ClangToolchain&                       toolchain_;
    const cpp::HeaderOwnershipIndex&            header_ownership_;
    frontend::FrontendSourceStore               source_store_;
    Option<frontend::SourceCacheReleaseReceipt> source_release_;
    ScanCacheSession                            cache_;
    ScanProfiler&                               profiler_;
    frontend::FrontendStatistics                statistics_;
};

} // namespace lito
