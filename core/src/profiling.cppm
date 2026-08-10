export module lito.profiling;

import rstd;
import rstd.bench;
import lito.frontend.preprocessor;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

auto probe_error_message(const rstd::bench::probe::ProbeError& error) -> String {
    if (error.is_ProbeIdExhausted()) return String::make("scan probe id exhausted"_str);
    if (error.is_SchemaMismatch()) return String::make("scan probe schema mismatch"_str);
    const auto& diagnostic = error.as_Diagnostic().reason;
    if (diagnostic.is_InvalidProbe()) return String::make("invalid scan probe"_str);
    if (diagnostic.is_WrongThread()) return String::make("scan probe used from another thread"_str);
    if (diagnostic.is_ActiveSpanOverflow()) {
        return String::make("scan probe active span capacity exceeded"_str);
    }
    if (diagnostic.is_NonLifo()) return String::make("scan probe spans ended out of order"_str);
    if (diagnostic.is_ActiveSpansPending()) {
        return String::make("scan probe has active spans at completion"_str);
    }
    if (diagnostic.is_FrameAlreadyActive()) return String::make("scan probe frame is active"_str);
    if (diagnostic.is_NoActiveFrame()) return String::make("scan probe frame is not active"_str);
    if (diagnostic.is_FrameStillActive()) return String::make("scan probe frame is pending"_str);
    if (diagnostic.is_SequenceExhausted()) return String::make("scan probe sequence exhausted"_str);
    return String::make("scan probe clock stalled"_str);
}

} // namespace lito

export namespace lito
{

enum class ScanProbe
{
    Total,
    Plan,
    Discovery,
    PrepareUnits,
    ClassifyUnits,
    Conventions,
    ModuleGraph,
    Frontend,
    Environment,
    Preprocessor,
    PredefinedMacros,
    TranslationUnit,
    SourceResolve,
    SourceRead,
    Lex,
    Count,
};

enum class ScanSourceOrigin
{
    Discovery,
    Classify,
};

enum class BuildOperation
{
    Compile,
    Archive,
    Link,
    Strip,
};

enum class ScanTimingCategory
{
    Orchestration,
    Environment,
    Preprocessor,
    Source,
};

auto scan_timing_category_label(ScanTimingCategory category) noexcept -> ref<str> {
    switch (category) {
    case ScanTimingCategory::Orchestration: return "scan.orchestration"_str;
    case ScanTimingCategory::Environment: return "scan.environment"_str;
    case ScanTimingCategory::Preprocessor: return "scan.preprocessor"_str;
    case ScanTimingCategory::Source: return "scan.source"_str;
    }
    return "scan.unknown"_str;
}

auto build_operation_label(BuildOperation operation) noexcept -> ref<str> {
    switch (operation) {
    case BuildOperation::Compile: return "build.compile"_str;
    case BuildOperation::Archive: return "build.archive"_str;
    case BuildOperation::Link: return "build.link"_str;
    case BuildOperation::Strip: return "build.strip"_str;
    }
    return "build.unknown"_str;
}

struct BuildOperationTiming {
    usize                count {};
    rstd::time::Duration total;
};

struct CompileExecutionStatistics {
    usize                jobs {};
    usize                max_in_flight {};
    usize                tasks {};
    usize                reused {};
    usize                failed {};
    usize                blocked {};
    usize                max_active {};
    rstd::time::Duration ready_wait;
    rstd::time::Duration completion_wait;
    rstd::time::Duration task_work;
    rstd::time::Duration coordinator_work;
    rstd::time::Duration wall;
};

class BuildTimingReport {
    BuildOperationTiming compile_;
    BuildOperationTiming archive_;
    BuildOperationTiming link_;
    BuildOperationTiming strip_;

    auto timing_mut(BuildOperation operation) noexcept -> BuildOperationTiming& {
        switch (operation) {
        case BuildOperation::Compile: return compile_;
        case BuildOperation::Archive: return archive_;
        case BuildOperation::Link: return link_;
        case BuildOperation::Strip: return strip_;
        }
        return link_;
    }

public:
    void record(BuildOperation operation, rstd::time::Duration elapsed) noexcept {
        auto& timing = timing_mut(operation);
        if (timing.count != usize::MAX) ++timing.count;
        timing.total = timing.total.saturating_add(elapsed);
    }

    auto timing(BuildOperation operation) const noexcept -> const BuildOperationTiming& {
        switch (operation) {
        case BuildOperation::Compile: return compile_;
        case BuildOperation::Archive: return archive_;
        case BuildOperation::Link: return link_;
        case BuildOperation::Strip: return strip_;
        }
        return link_;
    }
};

auto scan_source_origin_label(ScanSourceOrigin origin) noexcept -> ref<str> {
    switch (origin) {
    case ScanSourceOrigin::Discovery: return "discovery"_str;
    case ScanSourceOrigin::Classify: return "classify"_str;
    }
    return "unknown"_str;
}

auto scan_probe_label(ScanProbe probe) noexcept -> ref<str> {
    switch (probe) {
    case ScanProbe::Total: return "scan.total"_str;
    case ScanProbe::Plan: return "scan.plan"_str;
    case ScanProbe::Discovery: return "scan.discovery"_str;
    case ScanProbe::PrepareUnits: return "scan.prepare-units"_str;
    case ScanProbe::ClassifyUnits: return "scan.classify-units"_str;
    case ScanProbe::Conventions: return "scan.conventions"_str;
    case ScanProbe::ModuleGraph: return "scan.module-graph"_str;
    case ScanProbe::Frontend: return "scan.frontend"_str;
    case ScanProbe::Environment: return "scan.environment"_str;
    case ScanProbe::Preprocessor: return "scan.preprocessor"_str;
    case ScanProbe::PredefinedMacros: return "scan.preprocessor.predefined-macros"_str;
    case ScanProbe::TranslationUnit: return "scan.preprocessor.translation-unit"_str;
    case ScanProbe::SourceResolve: return "scan.source-resolve"_str;
    case ScanProbe::SourceRead: return "scan.source-read"_str;
    case ScanProbe::Lex: return "scan.lex"_str;
    case ScanProbe::Count: break;
    }
    return "scan.unknown"_str;
}

struct ScanSourceFrame {
    u64                                            id;
    rstd::string::String                           target;
    rstd::path::PathBuf                            source;
    ScanSourceOrigin                               origin { ScanSourceOrigin::Discovery };
    frontend::preprocessor::PreprocessorStatistics preprocessor;
};

struct ScanSourceTiming {
    u64                              frame;
    rstd::bench::probe::ProbeSummary summary;
};

struct ScanExecutionStatistics {
    usize                jobs {};
    usize                max_in_flight {};
    usize                tasks {};
    usize                max_active {};
    rstd::time::Duration ready_wait;
    rstd::time::Duration completion_wait;
    rstd::time::Duration task_work;
};

struct ScanTaskProfile {
    rstd::bench::probe::ProbeBatch batch;
    ScanSourceFrame                frame;
};

class ScanTaskProfileContext {
    rstd::sync::Arc<rstd::bench::probe::ProbeSession> session_;
    Vec<rstd::bench::probe::ProbeId>                  probes_;
    ScanSourceFrame                                   frame_;

    ScanTaskProfileContext(rstd::sync::Arc<rstd::bench::probe::ProbeSession> session,
                           Vec<rstd::bench::probe::ProbeId>                  probes,
                           ScanSourceFrame                                   frame)
        : session_(rstd::move(session)), probes_(rstd::move(probes)), frame_(rstd::move(frame)) {}

    friend class ScanProfiler;
    friend class ScanTaskProfiler;

public:
    ScanTaskProfileContext(ScanTaskProfileContext&&) noexcept                    = default;
    auto operator=(ScanTaskProfileContext&&) noexcept -> ScanTaskProfileContext& = default;
};

class ScanProfileReport {
    rstd::bench::probe::ProbeReport aggregate_;
    rstd::bench::probe::ProbeReport sources_;
    Vec<ScanSourceFrame>            frames_;
    ScanExecutionStatistics         execution_;

    auto exclusive(ScanProbe probe) const noexcept -> rstd::time::Duration {
        const auto* value = timing(probe);
        return value == nullptr ? rstd::time::Duration {} : value->exclusive_total;
    }

public:
    ScanProfileReport(rstd::bench::probe::ProbeReport aggregate,
                      rstd::bench::probe::ProbeReport sources,
                      Vec<ScanSourceFrame>            frames,
                      ScanExecutionStatistics         execution)
        : aggregate_(rstd::move(aggregate)),
          sources_(rstd::move(sources)),
          frames_(rstd::move(frames)),
          execution_(execution) {}

    auto aggregate() const noexcept -> const rstd::bench::probe::ProbeReport& { return aggregate_; }

    auto sources() const noexcept -> const rstd::bench::probe::ProbeReport& { return sources_; }

    auto frames() const noexcept -> slice<ScanSourceFrame> { return frames_.as_slice(); }

    auto execution() const noexcept -> const ScanExecutionStatistics& { return execution_; }

    auto frame(u64 id) const noexcept -> const ScanSourceFrame* {
        for (const auto& value : frames_) {
            if (value.id == id) return rstd::addressof(value);
        }
        return nullptr;
    }

    auto timing(ScanProbe probe) const noexcept -> const rstd::bench::probe::ProbeSummary* {
        for (const auto& value : aggregate_.overall()) {
            auto label = aggregate_.schema()->label(value.probe);
            if (label.is_some() && *label == scan_probe_label(probe)) {
                return rstd::addressof(value);
            }
        }
        return nullptr;
    }

    auto total() const noexcept -> rstd::time::Duration {
        const auto* value = timing(ScanProbe::Total);
        return value == nullptr ? rstd::time::Duration {} : value->inclusive_total;
    }

    auto category_timing(ScanTimingCategory category) const noexcept -> rstd::time::Duration {
        auto result = rstd::time::Duration {};
        auto add    = [&](ScanProbe probe) {
            result = result.saturating_add(exclusive(probe));
        };
        switch (category) {
        case ScanTimingCategory::Orchestration:
            add(ScanProbe::Total);
            add(ScanProbe::Plan);
            add(ScanProbe::Discovery);
            add(ScanProbe::PrepareUnits);
            add(ScanProbe::ClassifyUnits);
            add(ScanProbe::Conventions);
            add(ScanProbe::ModuleGraph);
            add(ScanProbe::Frontend);
            break;
        case ScanTimingCategory::Environment: add(ScanProbe::Environment); break;
        case ScanTimingCategory::Preprocessor:
            add(ScanProbe::Preprocessor);
            add(ScanProbe::PredefinedMacros);
            add(ScanProbe::TranslationUnit);
            break;
        case ScanTimingCategory::Source:
            add(ScanProbe::SourceResolve);
            add(ScanProbe::SourceRead);
            add(ScanProbe::Lex);
            break;
        }
        return result;
    }

    auto slow_sources(ScanProbe probe, usize limit) const -> Vec<ScanSourceTiming> {
        auto result = Vec<ScanSourceTiming>::make();
        for (const auto& value : sources_.by_frame()) {
            auto label = sources_.schema()->label(value.summary.probe);
            if (label.is_none() || *label != scan_probe_label(probe)) continue;
            result.push(ScanSourceTiming {
                .frame   = value.frame,
                .summary = value.summary,
            });
        }
        rstd::slice_::sort_unstable_by(
            result.as_mut_slice().as_mut_ref(),
            [](const ScanSourceTiming& left, const ScanSourceTiming& right) {
                return left.summary.inclusive_total > right.summary.inclusive_total;
            });
        if (result.len() > limit) result.truncate(limit);
        return result;
    }
};

class ScanSpanGuard {
    rstd::bench::probe::SpanGuard aggregate_;
    rstd::bench::probe::SpanGuard source_;

public:
    ScanSpanGuard(rstd::bench::probe::SpanGuard aggregate,
                  rstd::bench::probe::SpanGuard source) noexcept
        : aggregate_(rstd::move(aggregate)), source_(rstd::move(source)) {}

    ScanSpanGuard()                                            = default;
    ScanSpanGuard(const ScanSpanGuard&)                        = delete;
    auto operator=(const ScanSpanGuard&) -> ScanSpanGuard&     = delete;
    ScanSpanGuard(ScanSpanGuard&&) noexcept                    = default;
    auto operator=(ScanSpanGuard&&) noexcept -> ScanSpanGuard& = default;

    auto finish() noexcept -> rstd::Result<empty, rstd::bench::probe::ProbeError> {
        auto source_result    = source_.finish();
        auto aggregate_result = aggregate_.finish();
        if (source_result.is_err()) return source_result;
        return aggregate_result;
    }
};

class ScanTaskProfiler {
    rstd::bench::probe::ProbeRecorder recorder_;
    Vec<rstd::bench::probe::ProbeId>  probes_;
    ScanSourceFrame                   frame_;
    bool                              active_ { true };

    ScanTaskProfiler(rstd::bench::probe::ProbeRecorder recorder,
                     Vec<rstd::bench::probe::ProbeId>  probes,
                     ScanSourceFrame                   frame)
        : recorder_(rstd::move(recorder)), probes_(rstd::move(probes)), frame_(rstd::move(frame)) {}

    auto probe(ScanProbe value) const noexcept -> rstd::bench::probe::ProbeId {
        return probes_[usize(static_cast<rstd::size_t>(value))];
    }

public:
    static auto create(ScanTaskProfileContext context) -> rstd::Result<ScanTaskProfiler, String> {
        auto config     = rstd::bench::probe::RecorderConfig {};
        config.overflow = rstd::bench::probe::OverflowPolicy::Grow();
        auto recorder   = context.session_->recorder(rstd::move(config));
        auto begun      = recorder.begin_frame(context.frame_.id);
        if (begun.is_err()) {
            return Err(probe_error_message(rstd::move(begun).unwrap_err_unchecked()));
        }
        return Ok(ScanTaskProfiler(
            rstd::move(recorder), rstd::move(context.probes_), rstd::move(context.frame_)));
    }

    auto span(ScanProbe value) noexcept -> ScanSpanGuard {
        return ScanSpanGuard(recorder_.span(probe(value)), {});
    }

    template<typename Function>
    decltype(auto) measure(ScanProbe value, Function&& function) {
        auto guard = span(value);
        return rstd::forward<Function>(function)();
    }

    auto complete(ScanSpanGuard& span) -> rstd::Result<empty, String> {
        auto result = span.finish();
        if (result.is_err()) {
            return Err(probe_error_message(rstd::move(result).unwrap_err_unchecked()));
        }
        return Ok(empty {});
    }

    auto record_preprocessor_statistics(
        const frontend::preprocessor::PreprocessorStatistics& statistics) noexcept -> void {
        frame_.preprocessor = statistics;
    }

    auto finish() -> rstd::Result<ScanTaskProfile, String> {
        if (! active_) return Err(String::make("scan task profile is already complete"_str));
        auto ended = recorder_.end_frame();
        if (ended.is_err()) {
            return Err(probe_error_message(rstd::move(ended).unwrap_err_unchecked()));
        }
        active_ = false;
        return Ok(ScanTaskProfile {
            .batch = rstd::move(ended).unwrap_unchecked(),
            .frame = rstd::move(frame_),
        });
    }
};

class ScanProfiler {
    rstd::sync::Arc<rstd::bench::probe::ProbeSession> session_;
    rstd::bench::probe::ProbeRecorder                 aggregate_recorder_;
    rstd::bench::probe::ProbeRecorder                 source_recorder_;
    rstd::bench::probe::ProbeCollector                aggregate_collector_;
    rstd::bench::probe::ProbeCollector                source_collector_;
    Vec<rstd::bench::probe::ProbeId>                  probes_;
    Vec<ScanSourceFrame>                              source_frames_;
    u64                                               next_source_frame_ { u64(1) };
    bool                                              source_frame_active_ {};
    ScanExecutionStatistics                           execution_;

    ScanProfiler(rstd::sync::Arc<rstd::bench::probe::ProbeSession> session,
                 rstd::bench::probe::ProbeRecorder                 aggregate_recorder,
                 rstd::bench::probe::ProbeRecorder                 source_recorder,
                 rstd::bench::probe::ProbeCollector                aggregate_collector,
                 rstd::bench::probe::ProbeCollector                source_collector,
                 Vec<rstd::bench::probe::ProbeId>                  probes)
        : session_(rstd::move(session)),
          aggregate_recorder_(rstd::move(aggregate_recorder)),
          source_recorder_(rstd::move(source_recorder)),
          aggregate_collector_(rstd::move(aggregate_collector)),
          source_collector_(rstd::move(source_collector)),
          probes_(rstd::move(probes)),
          source_frames_(Vec<ScanSourceFrame>::make()) {}

    auto probe(ScanProbe value) const noexcept -> rstd::bench::probe::ProbeId {
        return probes_[usize(static_cast<rstd::size_t>(value))];
    }

public:
    static auto create() -> rstd::Result<ScanProfiler, String> {
        auto registry = rstd::bench::probe::ProbeRegistry::new_();
        auto probes   = Vec<rstd::bench::probe::ProbeId>::with_capacity(
            usize(static_cast<rstd::size_t>(ScanProbe::Count)));
        for (auto index = rstd::size_t {}; index < static_cast<rstd::size_t>(ScanProbe::Count);
             ++index) {
            auto registered =
                registry.register_probe(scan_probe_label(static_cast<ScanProbe>(index)));
            if (registered.is_err()) {
                return Err(probe_error_message(rstd::move(registered).unwrap_err_unchecked()));
            }
            probes.push(rstd::move(registered).unwrap_unchecked());
        }
        auto schema  = rstd::move(registry).freeze();
        auto session = rstd::sync::Arc<rstd::bench::probe::ProbeSession>::make(
            schema.clone(), rstd::bench::SteadyClock {});
        auto config              = rstd::bench::probe::RecorderConfig {};
        config.overflow          = rstd::bench::probe::OverflowPolicy::Grow();
        auto source_config       = config;
        auto aggregate_recorder  = session->recorder(rstd::move(config));
        auto source_recorder     = session->recorder(rstd::move(source_config));
        auto aggregate_collector = rstd::bench::probe::ProbeCollector::new_(schema.clone());
        auto source_collector    = rstd::bench::probe::ProbeCollector::new_(schema.clone());
        return Ok(ScanProfiler(rstd::move(session),
                               rstd::move(aggregate_recorder),
                               rstd::move(source_recorder),
                               rstd::move(aggregate_collector),
                               rstd::move(source_collector),
                               rstd::move(probes)));
    }

    auto task(ref<str> target, ref<rstd::path::Path> source, ScanSourceOrigin origin)
        -> rstd::Result<ScanTaskProfileContext, String> {
        if (next_source_frame_ == u64::MAX) {
            return Err(String::make("scan source frame id exhausted"_str));
        }
        auto task_probes = Vec<rstd::bench::probe::ProbeId>::with_capacity(probes_.len());
        for (auto value : probes_) task_probes.push(rstd::move(value));
        auto context = ScanTaskProfileContext(session_.clone(),
                                              rstd::move(task_probes),
                                              ScanSourceFrame {
                                                  .id     = next_source_frame_,
                                                  .target = String::make(target),
                                                  .source = rstd::path::PathBuf::from(source),
                                                  .origin = origin,
                                              });
        ++next_source_frame_;
        return Ok(rstd::move(context));
    }

    auto ingest(ScanTaskProfile profile) -> rstd::Result<empty, String> {
        auto aggregate = aggregate_collector_.ingest(profile.batch);
        if (aggregate.is_err()) {
            return Err(probe_error_message(rstd::move(aggregate).unwrap_err_unchecked()));
        }
        auto source = source_collector_.ingest(profile.batch);
        if (source.is_err()) {
            return Err(probe_error_message(rstd::move(source).unwrap_err_unchecked()));
        }
        source_frames_.push(rstd::move(profile.frame));
        return Ok(empty {});
    }

    auto record_execution(ScanExecutionStatistics statistics) noexcept -> void {
        execution_ = statistics;
    }

    auto span(ScanProbe value) noexcept -> ScanSpanGuard {
        auto source = rstd::bench::probe::SpanGuard {};
        if (source_frame_active_) source = source_recorder_.span(probe(value));
        return ScanSpanGuard(aggregate_recorder_.span(probe(value)), rstd::move(source));
    }

    template<typename Function>
    decltype(auto) measure(ScanProbe value, Function&& function) {
        auto guard = span(value);
        return rstd::forward<Function>(function)();
    }

    auto complete(ScanSpanGuard& span) -> rstd::Result<empty, String> {
        auto result = span.finish();
        if (result.is_err()) {
            return Err(probe_error_message(rstd::move(result).unwrap_err_unchecked()));
        }
        return Ok(empty {});
    }

    auto begin_source_frame(ref<str> target, ref<rstd::path::Path> source, ScanSourceOrigin origin)
        -> rstd::Result<empty, String> {
        if (next_source_frame_ == u64::MAX) {
            return Err(String::make("scan source frame id exhausted"_str));
        }
        auto begun = source_recorder_.begin_frame(next_source_frame_);
        if (begun.is_err()) {
            return Err(probe_error_message(rstd::move(begun).unwrap_err_unchecked()));
        }
        source_frames_.push(ScanSourceFrame {
            .id     = next_source_frame_,
            .target = rstd::string::String::make(target),
            .source = rstd::path::PathBuf::from(source),
            .origin = origin,
        });
        ++next_source_frame_;
        source_frame_active_ = true;
        return Ok(empty {});
    }

    auto end_source_frame() -> rstd::Result<empty, String> {
        auto ended = source_recorder_.end_frame();
        if (ended.is_err()) {
            return Err(probe_error_message(rstd::move(ended).unwrap_err_unchecked()));
        }
        source_frame_active_ = false;
        auto batch           = rstd::move(ended).unwrap_unchecked();
        auto ingested        = source_collector_.ingest(batch);
        if (ingested.is_err()) {
            return Err(probe_error_message(rstd::move(ingested).unwrap_err_unchecked()));
        }
        auto recycled = source_recorder_.recycle(rstd::move(batch));
        if (recycled.is_err()) {
            return Err(probe_error_message(rstd::move(recycled).unwrap_err_unchecked()));
        }
        return Ok(empty {});
    }

    auto record_preprocessor_statistics(
        const frontend::preprocessor::PreprocessorStatistics& statistics) noexcept -> void {
        if (! source_frame_active_ || source_frames_.is_empty()) return;
        source_frames_[source_frames_.len() - usize(1)].preprocessor = statistics;
    }

    auto finish() -> rstd::Result<ScanProfileReport, String> {
        auto drained = aggregate_recorder_.drain();
        if (drained.is_err()) {
            return Err(probe_error_message(rstd::move(drained).unwrap_err_unchecked()));
        }
        auto batch    = rstd::move(drained).unwrap_unchecked();
        auto ingested = aggregate_collector_.ingest(batch);
        if (ingested.is_err()) {
            return Err(probe_error_message(rstd::move(ingested).unwrap_err_unchecked()));
        }
        rstd::slice_::sort_unstable_by(
            source_frames_.as_mut_slice().as_mut_ref(),
            [](const ScanSourceFrame& left, const ScanSourceFrame& right) {
                return left.id < right.id;
            });
        return Ok(ScanProfileReport(rstd::move(aggregate_collector_).finish(),
                                    rstd::move(source_collector_).finish(),
                                    rstd::move(source_frames_),
                                    execution_));
    }
};

} // namespace lito
