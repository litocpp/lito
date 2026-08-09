#include <rstd/test/gtest.hpp>

import rstd;
import rstd.bench;
import rstd.test;
import lito.profiling;

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

auto run_profiling_test() -> int {
    auto registry = rstd::bench::probe::ProbeRegistry::new_();
    auto probe =
        registry.register_probe(lito::scan_probe_label(lito::ScanProbe::Preprocessor)).unwrap();
    auto schema   = rstd::move(registry).freeze();
    auto now      = u64();
    auto session  = rstd::bench::probe::BasicProbeSession<ManualClock>(schema.clone(),
                                                                       ManualClock { .now = &now });
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
    auto aggregate_recorder = session.recorder();
    {
        auto span = aggregate_recorder.span(probe);
        now       = u64(750);
    }
    auto aggregate_batch = aggregate_recorder.drain().unwrap();
    auto aggregate       = rstd::bench::probe::ProbeCollector::new_(schema.clone());
    aggregate.ingest(aggregate_batch).unwrap();
    auto frames = Vec<lito::ScanSourceFrame>::make();
    frames.push(lito::ScanSourceFrame {
        .id     = u64(1),
        .target = String::make("first"_str),
        .source = rstd::path::PathBuf::from("/first.cppm"_str),
        .origin = lito::ScanSourceOrigin::Discovery,
    });
    frames.push(lito::ScanSourceFrame {
        .id     = u64(2),
        .target = String::make("second"_str),
        .source = rstd::path::PathBuf::from("/second.cpp"_str),
        .origin = lito::ScanSourceOrigin::Classify,
    });
    auto report = lito::ScanProfileReport(
        rstd::move(aggregate).finish(),
        rstd::move(source_collector).finish(),
        rstd::move(frames),
        lito::ScanExecutionStatistics {});

    auto slow = report.slow_sources(lito::ScanProbe::Preprocessor, usize(1));
    if (slow.len() != usize(1) || slow[usize()].frame != u64(2)) return 1;
    const auto* frame = report.frame(slow[usize()].frame);
    if (frame == nullptr || frame->target.as_str() != "second"_str ||
        frame->origin != lito::ScanSourceOrigin::Classify) {
        return 2;
    }
    if (report.aggregate().overall().len() != usize(1) ||
        report.sources().dropped_samples() != usize() ||
        report.sources().diagnostics().len() != usize()) {
        return 3;
    }
    if (report.category_timing(lito::ScanTimingCategory::Preprocessor).as_nanos() != u128(350) ||
        ! report.category_timing(lito::ScanTimingCategory::Environment).is_zero() ||
        ! report.total().is_zero()) {
        return 4;
    }
    auto build_timing = lito::BuildTimingReport {};
    build_timing.record(lito::BuildOperation::Compile,
                        rstd::time::Duration::from_micros(u64(100)));
    build_timing.record(lito::BuildOperation::Compile,
                        rstd::time::Duration::from_micros(u64(250)));
    build_timing.record(lito::BuildOperation::Archive, rstd::time::Duration::from_micros(u64(50)));
    const auto& compile = build_timing.timing(lito::BuildOperation::Compile);
    const auto& archive = build_timing.timing(lito::BuildOperation::Archive);
    const auto& link    = build_timing.timing(lito::BuildOperation::Link);
    if (compile.count != usize(2) || compile.total.as_micros() != u64(350) ||
        archive.count != usize(1) || archive.total.as_micros() != u64(50) ||
        link.count != usize() || ! link.total.is_zero()) {
        return 5;
    }
    if (lito::build_operation_label(lito::BuildOperation::Compile) != "build.compile"_str ||
        lito::build_operation_label(lito::BuildOperation::Archive) != "build.archive"_str ||
        lito::build_operation_label(lito::BuildOperation::Link) != "build.link"_str) {
        return 6;
    }
    return 0;
}

TEST(Profiling, ReportAndBuildTiming) {
    EXPECT_EQ(run_profiling_test(), 0);
}
