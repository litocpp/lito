import rstd;
import rstd.bench;
import tenon.profiling;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace
{

struct ManualClock {
    u64* now;

    auto now_ns() const noexcept -> u64 { return *now; }

    auto resolution() const noexcept -> Result<rstd::time::Duration, rstd::bench::ClockError> {
        return Ok(rstd::time::Duration::from_nanos(u64(1)));
    }
};

} // namespace

auto main() -> int {
    auto registry = rstd::bench::probe::ProbeRegistry::new_();
    auto probe = registry.register_probe(tenon::scan_probe_label(tenon::ScanProbe::Preprocessor))
                     .unwrap();
    auto schema = rstd::move(registry).freeze();
    auto now     = u64();
    auto session = rstd::bench::probe::BasicProbeSession<ManualClock>(
        schema.clone(), ManualClock { .now = &now });
    auto recorder = session.recorder();

    recorder.begin_frame(u64(1)).unwrap();
    {
        auto span = recorder.span(probe);
        now       = u64(100);
    }
    auto first = recorder.end_frame().unwrap();
    recorder.begin_frame(u64(2)).unwrap();
    {
        auto span = recorder.span(probe);
        now       = u64(400);
    }
    auto second = recorder.end_frame().unwrap();

    auto source_collector = rstd::bench::probe::ProbeCollector::new_(schema.clone());
    source_collector.ingest(first).unwrap();
    source_collector.ingest(second).unwrap();
    auto aggregate = rstd::bench::probe::ProbeCollector::new_(schema.clone());
    auto frames     = Vec<tenon::ScanSourceFrame>::make();
    frames.push(tenon::ScanSourceFrame {
        .id     = u64(1),
        .target = String::make("first"_str),
        .source = rstd::path::PathBuf::from("/first.cppm"_str),
        .origin = tenon::ScanSourceOrigin::Discovery,
    });
    frames.push(tenon::ScanSourceFrame {
        .id     = u64(2),
        .target = String::make("second"_str),
        .source = rstd::path::PathBuf::from("/second.cpp"_str),
        .origin = tenon::ScanSourceOrigin::Classify,
    });
    auto report = tenon::ScanProfileReport(rstd::move(aggregate).finish(),
                                           rstd::move(source_collector).finish(),
                                           rstd::move(frames));

    auto slow = report.slow_sources(tenon::ScanProbe::Preprocessor, usize(1));
    if (slow.len() != usize(1) || slow[usize()].frame != u64(2)) return 1;
    const auto* frame = report.frame(slow[usize()].frame);
    if (frame == nullptr || frame->target.as_str() != "second"_str ||
        frame->origin != tenon::ScanSourceOrigin::Classify) {
        return 2;
    }
    if (report.aggregate().overall().len() != usize() ||
        report.sources().dropped_samples() != usize() ||
        report.sources().diagnostics().len() != usize()) {
        return 3;
    }
    return 0;
}
