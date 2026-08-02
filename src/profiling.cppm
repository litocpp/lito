export module tenon.profiling;

import rstd;
import rstd.bench;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace tenon
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

} // namespace tenon

export namespace tenon
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
    SourceResolve,
    SourceRead,
    Lex,
    Count,
};

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
    case ScanProbe::SourceResolve: return "scan.source-resolve"_str;
    case ScanProbe::SourceRead: return "scan.source-read"_str;
    case ScanProbe::Lex: return "scan.lex"_str;
    case ScanProbe::Count: break;
    }
    return "scan.unknown"_str;
}

class ScanProfiler {
    rstd::bench::probe::ProbeRecorder recorder_;
    Vec<rstd::bench::probe::ProbeId>  probes_;

    ScanProfiler(rstd::bench::probe::ProbeRecorder recorder,
                 Vec<rstd::bench::probe::ProbeId>  probes)
        : recorder_(rstd::move(recorder)), probes_(rstd::move(probes)) {}

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
        auto schema     = rstd::move(registry).freeze();
        auto session    = rstd::bench::probe::ProbeSession::new_(schema.clone());
        auto config     = rstd::bench::probe::RecorderConfig {};
        config.overflow = rstd::bench::probe::OverflowPolicy::Grow();
        return Ok(ScanProfiler(session.recorder(rstd::move(config)), rstd::move(probes)));
    }

    auto span(ScanProbe value) noexcept -> rstd::bench::probe::SpanGuard {
        return recorder_.span(probe(value));
    }

    template<typename Function>
    decltype(auto) measure(ScanProbe value, Function&& function) {
        return rstd::bench::probe::measure(
            recorder_, probe(value), rstd::forward<Function>(function));
    }

    auto complete(rstd::bench::probe::SpanGuard& span) -> rstd::Result<empty, String> {
        auto result = span.finish();
        if (result.is_err()) {
            return Err(probe_error_message(rstd::move(result).unwrap_err_unchecked()));
        }
        return Ok(empty {});
    }

    auto finish() -> rstd::Result<rstd::bench::probe::ProbeReport, String> {
        auto drained = recorder_.drain();
        if (drained.is_err()) {
            return Err(probe_error_message(rstd::move(drained).unwrap_err_unchecked()));
        }
        auto batch     = rstd::move(drained).unwrap_unchecked();
        auto collector = rstd::bench::probe::ProbeCollector::new_(batch.schema_owner());
        auto ingested  = collector.ingest(batch);
        if (ingested.is_err()) {
            return Err(probe_error_message(rstd::move(ingested).unwrap_err_unchecked()));
        }
        return Ok(rstd::move(collector).finish());
    }
};

} // namespace tenon
