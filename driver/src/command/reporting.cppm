module;
#include <rstd/enum.hpp>

export module lito.driver:command.reporting;

import rstd;
import rstd.bench;
import lito.core;
import :build.result;
import lito.toolchain;
import :build.profiling;
import lito.frontend.preprocessor;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::timing_output
{

class OutputError {
    RSTD_ENUM(OutputError,
              (InvalidPath, (PathBuf path;)),
              (Io, (String operation; PathBuf path; rstd::io::error::Error source;)))
};

template<typename T>
using OutputResult = Result<T, OutputError>;

} // namespace lito::timing_output

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::timing_output::OutputError>
    : ImplBase<lito::timing_output::OutputError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_InvalidPath()) {
            return formatter.write_fmt(fmt::Arguments::make("timing report path '{}' has no parent",
                                                            error.as_InvalidPath().path.as_path()));
        }
        const auto& value = error.as_Io();
        return formatter.write_fmt(
            fmt::Arguments::make("cannot {} '{}'", value.operation, value.path.as_path()));
    }
};

template<>
struct Impl<fmt::Debug, lito::timing_output::OutputError>
    : ImplBase<lito::timing_output::OutputError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::timing_output::OutputError>
    : ImplBase<lito::timing_output::OutputError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (! error.is_Io()) return None();
        return Some(dyn<error::Error>::from_ref(error.as_Io().source));
    }
};

} // namespace rstd

namespace lito::timing_output
{

inline constexpr BuildOperation BUILD_OPERATIONS[] = {
    BuildOperation::Compile,
    BuildOperation::Archive,
    BuildOperation::Link,
    BuildOperation::Strip,
};

inline constexpr BuildStage BUILD_STAGES[] = {
    BuildStage::Total,
    BuildStage::ProductBegin,
    BuildStage::Environment,
    BuildStage::ProjectPrepare,
    BuildStage::TargetPrepare,
    BuildStage::Script,
    BuildStage::RuntimeResource,
    BuildStage::Scan,
    BuildStage::CompilePlan,
    BuildStage::CompileExecute,
    BuildStage::CompileCacheFinish,
    BuildStage::Archive,
    BuildStage::Link,
    BuildStage::ProductFinalize,
    BuildStage::ProductPublish,
};

inline constexpr ExternalPreparationOperation EXTERNAL_PREPARATION_OPERATIONS[] = {
    ExternalPreparationOperation::CMakeConfigure,  ExternalPreparationOperation::CMakeBuild,
    ExternalPreparationOperation::CMakeInstall,    ExternalPreparationOperation::CMakeQuery,
    ExternalPreparationOperation::CMakeQueryBuild, ExternalPreparationOperation::CMakeSnapshot,
    ExternalPreparationOperation::CargoMetadata,   ExternalPreparationOperation::CargoBuild,
    ExternalPreparationOperation::CargoReuse,
};

inline constexpr ScanTimingCategory SCAN_CATEGORIES[] = {
    ScanTimingCategory::Orchestration,
    ScanTimingCategory::Environment,
    ScanTimingCategory::Preprocessor,
    ScanTimingCategory::Source,
};

auto display_duration(rstd::time::Duration duration) -> String {
    const auto micros = duration.as_micros();
    if (micros < u64(1'000)) return rstd::format("{} us", micros);
    if (micros < u64(1'000'000)) {
        return rstd::format("{:.3} ms", f64(static_cast<double>(micros.to_primitive()) / 1'000.0));
    }
    return rstd::format("{:.3} s", f64(static_cast<double>(micros.to_primitive()) / 1'000'000.0));
}

auto display_share(rstd::time::Duration value, rstd::time::Duration total) -> String {
    if (total.is_zero()) return String::make("0.0%"_str);
    return rstd::format("{:.1}%", f64(value.as_secs_f64() / total.as_secs_f64() * 100.0));
}

void append_line(String& output, ref<str> line) {
    output.push_str(line);
    output.push_ascii('\n');
}

void append_line(String& output, String line) {
    append_line(output, line.as_str());
}

void append_metric(String& output, ref<str> name, usize value) {
    append_line(output, rstd::format("  {:<38} {}", name, value));
}

void append_preprocessor(String&                                               output,
                         const frontend::preprocessor::PreprocessorStatistics& statistics) {
    append_metric(output, "files"_str, statistics.files);
    append_metric(output, "source tokens"_str, statistics.source_tokens);
    append_metric(output, "token clones"_str, statistics.token_clones);
    append_metric(output, "synthetic tokens"_str, statistics.synthetic_tokens);
    append_metric(output, "directives"_str, statistics.directives);
    append_metric(output, "conditionals"_str, statistics.conditionals);
    append_metric(output, "macro lookups"_str, statistics.macro_lookups);
    append_metric(output, "macro lookup hits"_str, statistics.macro_lookup_hits);
    append_metric(output, "macro expansions"_str, statistics.macro_expansions);
    append_metric(output, "include attempts"_str, statistics.include_attempts);
    append_metric(output, "include hits"_str, statistics.include_hits);
    append_metric(output, "consumer batches"_str, statistics.consumer_batches);
    append_metric(output, "consumer tokens"_str, statistics.consumer_tokens);
}

auto detailed_report(const BuildSummary& summary) -> String {
    auto output = String::make();

    append_line(output, "frontend"_str);
    append_metric(output, "source requests"_str, summary.frontend.source_requests);
    append_metric(output, "source hits"_str, summary.frontend.source_hits);
    append_metric(output, "source stats"_str, summary.frontend.source_stats);
    append_metric(output, "source reads"_str, summary.frontend.source_reads);
    append_metric(output, "source bytes"_str, summary.frontend.source_bytes);
    append_metric(output, "source waits"_str, summary.frontend.source_waits);
    append_line(output,
                rstd::format("  {:<38} {}",
                             "source wait"_str,
                             display_duration(summary.frontend.source_wait).as_str()));
    append_metric(output, "source ready entries"_str, summary.frontend.source_ready_entries);
    append_metric(output, "source ready peak"_str, summary.frontend.source_ready_peak);
    append_metric(output, "source live payloads"_str, summary.frontend.source_live_payloads);
    append_metric(
        output, "source live payload peak"_str, summary.frontend.source_live_payload_peak);
    append_metric(output, "source retained bytes"_str, summary.frontend.source_retained_bytes);
    append_metric(
        output, "source retained bytes peak"_str, summary.frontend.source_retained_bytes_peak);
    append_metric(
        output, "source in-flight entries"_str, summary.frontend.source_in_flight_entries);
    append_metric(output, "source in-flight peak"_str, summary.frontend.source_in_flight_peak);
    append_metric(output, "source weak hits"_str, summary.frontend.source_weak_hits);
    append_metric(output, "source flight waits"_str, summary.frontend.source_flight_waits);
    append_metric(output, "source expired entries"_str, summary.frontend.source_expired_entries);
    append_metric(output, "lexed sources"_str, summary.frontend.lex_builds);
    append_metric(output, "analyzed sources"_str, summary.frontend.analyze_builds);
    append_metric(output, "analysis hits"_str, summary.frontend.analyze_hits);
    append_metric(output, "full analyses current"_str, summary.frontend.full_analyses);
    append_metric(output, "full analysis peak"_str, summary.frontend.full_analysis_peak);
    append_metric(output, "compacted analyses"_str, summary.frontend.compacted_analyses);
    append_metric(
        output, "compacted analysis bytes"_str, summary.frontend.compacted_analysis_bytes);
    append_metric(output, "persistent scan hits"_str, summary.frontend.persistent_scan_hits);
    append_metric(output, "persistent scan misses"_str, summary.frontend.persistent_scan_misses);
    append_metric(
        output, "persistent scan uncacheable"_str, summary.frontend.persistent_scan_uncacheable);
    append_metric(output, "scan miss absent"_str, summary.frontend.persistent_scan_absent);
    append_metric(output, "scan miss refresh"_str, summary.frontend.persistent_scan_refresh);
    append_metric(output, "scan miss version"_str, summary.frontend.persistent_scan_version);
    append_metric(output, "scan miss recipe"_str, summary.frontend.persistent_scan_recipe);
    append_metric(output, "scan miss corrupt"_str, summary.frontend.persistent_scan_corrupt);
    append_metric(
        output, "scan miss environment"_str, summary.frontend.persistent_scan_environment);
    append_metric(output, "scan miss context"_str, summary.frontend.persistent_scan_context);
    append_metric(output, "scan miss source"_str, summary.frontend.persistent_scan_source);
    append_metric(
        output, "scan miss file dependency"_str, summary.frontend.persistent_scan_file_dependency);
    append_metric(
        output, "scan miss include lookup"_str, summary.frontend.persistent_scan_include_lookup);
    append_metric(
        output, "scan miss embed lookup"_str, summary.frontend.persistent_scan_embed_lookup);
    append_metric(
        output, "scan miss external macro"_str, summary.frontend.persistent_scan_external_macro);
    append_metric(output, "scan miss receipt"_str, summary.frontend.persistent_scan_receipt);
    append_metric(
        output, "fingerprint requests"_str, summary.frontend.persistent_fingerprint_requests);
    append_metric(output, "fingerprint hits"_str, summary.frontend.persistent_fingerprint_hits);
    append_metric(output, "fingerprint builds"_str, summary.frontend.persistent_fingerprint_builds);
    append_metric(output, "fingerprint waits"_str, summary.frontend.persistent_fingerprint_waits);
    append_line(
        output,
        rstd::format("  {:<38} {}",
                     "fingerprint wait"_str,
                     display_duration(summary.frontend.persistent_fingerprint_wait).as_str()));

    append_line(output, "\nscan execution"_str);
    const auto& execution = summary.scan_profile.execution();
    append_metric(output, "jobs"_str, execution.jobs);
    append_metric(output, "max in flight"_str, execution.max_in_flight);
    append_metric(output, "tasks"_str, execution.tasks);
    append_metric(output, "max active"_str, execution.max_active);
    append_line(output,
                rstd::format("  {:<38} {}",
                             "ready wait"_str,
                             display_duration(execution.ready_wait).as_str()));
    append_line(output,
                rstd::format("  {:<38} {}",
                             "completion wait"_str,
                             display_duration(execution.completion_wait).as_str()));
    append_line(output,
                rstd::format("  {:<38} {}",
                             "task work"_str,
                             display_duration(execution.task_work).as_str()));

    append_line(output, "\ncompile execution"_str);
    const auto& compile_execution = summary.compile_execution;
    append_metric(output, "jobs"_str, compile_execution.jobs);
    append_metric(output, "max in flight"_str, compile_execution.max_in_flight);
    append_metric(output, "tasks"_str, compile_execution.tasks);
    append_metric(output, "reused"_str, compile_execution.reused);
    append_metric(output, "failed"_str, compile_execution.failed);
    append_metric(output, "blocked"_str, compile_execution.blocked);
    append_metric(output, "max active"_str, compile_execution.max_active);
    append_line(output,
                rstd::format("  {:<38} {}",
                             "ready wait"_str,
                             display_duration(compile_execution.ready_wait).as_str()));
    append_line(output,
                rstd::format("  {:<38} {}",
                             "completion wait"_str,
                             display_duration(compile_execution.completion_wait).as_str()));
    append_line(output,
                rstd::format("  {:<38} {}",
                             "task work"_str,
                             display_duration(compile_execution.task_work).as_str()));
    append_line(output,
                rstd::format("  {:<38} {}",
                             "coordinator work"_str,
                             display_duration(compile_execution.coordinator_work).as_str()));
    append_line(
        output,
        rstd::format("  {:<38} {}", "wall"_str, display_duration(compile_execution.wall).as_str()));

    append_line(output, "\ntoolchain"_str);
    append_metric(output,
                  "preprocessor environment entries"_str,
                  summary.toolchain.preprocessor_environment_entries);
    append_metric(output,
                  "preprocessor environment queries"_str,
                  summary.toolchain.preprocessor_environment_queries);
    append_metric(output,
                  "preprocessor environment hits"_str,
                  summary.toolchain.preprocessor_environment_hits);
    append_metric(output, "target queries"_str, summary.toolchain.target_queries);
    append_line(output, "  builtin cache key version              3"_str);
    append_line(output,
                rstd::format("  {:<38} {}",
                             "stdlib capability catalog"_str,
                             toolchain::CLANG_STANDARD_LIBRARY_CAPABILITY_ID));
    append_metric(output, "builtin snapshots"_str, summary.toolchain.builtin_snapshots);
    append_metric(output, "builtin refreshes"_str, summary.toolchain.builtin_refreshes);
    append_metric(output, "builtin hits"_str, summary.toolchain.builtin_hits);
    append_metric(output, "builtin macro processes"_str, summary.toolchain.builtin_macro_processes);
    append_metric(
        output, "builtin capability processes"_str, summary.toolchain.builtin_capability_processes);
    append_metric(output, "clang macros"_str, summary.toolchain.clang_macros);
    append_metric(output, "native macro owners"_str, summary.toolchain.native_macro_owners);
    append_metric(output, "clang capabilities"_str, summary.toolchain.clang_capabilities);
    append_metric(output, "native capabilities"_str, summary.toolchain.native_capabilities);
    append_metric(
        output, "builtin macro output bytes"_str, summary.toolchain.builtin_macro_output_bytes);
    append_metric(output,
                  "builtin capability input bytes"_str,
                  summary.toolchain.builtin_capability_input_bytes);
    append_metric(output,
                  "builtin capability output bytes"_str,
                  summary.toolchain.builtin_capability_output_bytes);
    append_metric(output, "ignored builtin options"_str, summary.toolchain.ignored_builtin_options);

    append_line(output, "\npreprocessor"_str);
    append_preprocessor(output, summary.frontend.preprocessor);

    append_line(output, "\nbuild stage timing"_str);
    append_line(output, "  name | calls | total_us"_str);
    for (const auto stage : BUILD_STAGES) {
        const auto& timing = summary.stage_timing.timing(stage);
        append_line(output,
                    rstd::format("  {} | {} | {}",
                                 build_stage_label(stage),
                                 timing.count,
                                 timing.total.as_micros()));
    }
    append_line(output,
                rstd::format("  build.unattributed | 0 | {}",
                             summary.stage_timing.unattributed().as_micros()));

    append_line(output, "\nexternal preparation timing"_str);
    append_line(output, "  name | calls | total_us"_str);
    for (const auto operation : EXTERNAL_PREPARATION_OPERATIONS) {
        const auto& timing = summary.external_preparation.timing(operation);
        append_line(output,
                    rstd::format("  {} | {} | {}",
                                 external_preparation_operation_label(operation),
                                 timing.count,
                                 timing.total.as_micros()));
    }

    append_line(output, "\nbuild action timing"_str);
    append_line(output, "  name | calls | total_us"_str);
    for (const auto operation : BUILD_OPERATIONS) {
        const auto& timing = summary.build_timing.timing(operation);
        append_line(output,
                    rstd::format("  {} | {} | {}",
                                 build_operation_label(operation),
                                 timing.count,
                                 timing.total.as_micros()));
    }

    append_line(output, "\naggregate timing"_str);
    append_line(output,
                "  name | calls | total_us | self_us | min_us | mean_us "
                "| median_us | p95_us | "
                "max_us"_str);
    const auto& aggregate = summary.scan_profile.aggregate();
    for (const auto& timing : aggregate.overall()) {
        auto label = aggregate.schema()->label(timing.probe);
        if (label.is_none()) continue;
        append_line(output,
                    rstd::format("  {} | {} | {} | {} | {:.3} | {:.3} | {:.3} | {:.3} | {:.3}",
                                 *label,
                                 timing.count,
                                 timing.inclusive_total.as_micros(),
                                 timing.exclusive_total.as_micros(),
                                 timing.minimum_ns / f64(1000.0),
                                 timing.mean_ns / f64(1000.0),
                                 timing.median_ns / f64(1000.0),
                                 timing.p95_ns / f64(1000.0),
                                 timing.maximum_ns / f64(1000.0)));
    }
    append_line(output,
                rstd::format("  dropped: {}; diagnostics: {}",
                             aggregate.dropped_samples(),
                             aggregate.diagnostics().len()));

    append_line(output, "\nslow preprocessor sources"_str);
    auto slow_sources = summary.scan_profile.slow_sources(ScanProbe::Preprocessor, usize(10));
    for (const auto& timing : slow_sources) {
        const auto* frame = summary.scan_profile.frame(timing.frame);
        if (frame == nullptr) continue;
        append_line(output,
                    rstd::format("  {} | {} | {} | {}",
                                 display_duration(timing.summary.inclusive_total).as_str(),
                                 scan_source_origin_label(frame->origin),
                                 frame->target.as_str(),
                                 frame->source.as_path()));
        append_preprocessor(output, frame->preprocessor);
    }
    const auto& sources = summary.scan_profile.sources();
    append_line(output,
                rstd::format("  frames: {}; dropped: {}; diagnostics: {}",
                             summary.scan_profile.frames().len(),
                             sources.dropped_samples(),
                             sources.diagnostics().len()));
    return output;
}

void print_summary(const BuildSummary& summary) {
    rstd::io::println("timing summary");
    rstd::io::println("  scan");
    rstd::io::println("    {:<24} {:>12} {:>8}", "category"_str, "time"_str, "share"_str);
    auto scan_total = summary.scan_profile.total();
    for (const auto category : SCAN_CATEGORIES) {
        auto elapsed = summary.scan_profile.category_timing(category);
        auto time    = display_duration(elapsed);
        auto share   = display_share(elapsed, scan_total);
        rstd::io::println("    {:<24} {:>12} {:>8}",
                          scan_timing_category_label(category),
                          time.as_str(),
                          share.as_str());
    }
    auto scan_total_time  = display_duration(scan_total);
    auto scan_total_share = display_share(scan_total, scan_total);
    rstd::io::println("    {:<24} {:>12} {:>8}",
                      "scan.total"_str,
                      scan_total_time.as_str(),
                      scan_total_share.as_str());
    const auto& aggregate = summary.scan_profile.aggregate();
    const auto& sources   = summary.scan_profile.sources();
    rstd::io::println("    sources: {}; dropped: {}; diagnostics: {}",
                      summary.scan_profile.frames().len(),
                      sources.dropped_samples(),
                      sources.diagnostics().len() + aggregate.diagnostics().len());
    const auto& execution = summary.scan_profile.execution();
    rstd::io::println("    jobs: {}; max in flight: {}; max active: {}; task work: {}",
                      execution.jobs,
                      execution.max_in_flight,
                      execution.max_active,
                      display_duration(execution.task_work).as_str());

    rstd::io::println("  build");
    rstd::io::println("    stages");
    rstd::io::println(
        "      {:<30} {:>12} {:>8} {:>8}", "stage"_str, "wall"_str, "calls"_str, "share"_str);
    auto build_total = summary.stage_timing.timing(BuildStage::Total).total;
    for (const auto stage : BUILD_STAGES) {
        const auto& timing = summary.stage_timing.timing(stage);
        auto        total  = display_duration(timing.total);
        auto        share  = display_share(timing.total, build_total);
        rstd::io::println("      {:<30} {:>12} {:>8} {:>8}",
                          build_stage_label(stage),
                          total.as_str(),
                          timing.count,
                          share.as_str());
    }
    auto unattributed = summary.stage_timing.unattributed();
    rstd::io::println("      {:<30} {:>12} {:>8} {:>8}",
                      "build.unattributed"_str,
                      display_duration(unattributed).as_str(),
                      usize {},
                      display_share(unattributed, build_total).as_str());
    rstd::io::println("    external preparation (nested)");
    rstd::io::println("      {:<30} {:>12} {:>8}", "operation"_str, "total"_str, "calls"_str);
    for (const auto operation : EXTERNAL_PREPARATION_OPERATIONS) {
        const auto& timing = summary.external_preparation.timing(operation);
        auto        total  = display_duration(timing.total);
        rstd::io::println("      {:<30} {:>12} {:>8}",
                          external_preparation_operation_label(operation),
                          total.as_str(),
                          timing.count);
    }
    const auto& compile_execution = summary.compile_execution;
    rstd::io::println("    jobs: {}; max in flight: {}; max active: {}; task work: {}",
                      compile_execution.jobs,
                      compile_execution.max_in_flight,
                      compile_execution.max_active,
                      display_duration(compile_execution.task_work).as_str());
    rstd::io::println("    actions (nested/parallel)");
    rstd::io::println("      {:<30} {:>12} {:>8}", "operation"_str, "total"_str, "calls"_str);
    for (const auto operation : BUILD_OPERATIONS) {
        const auto& timing = summary.build_timing.timing(operation);
        auto        total  = display_duration(timing.total);
        rstd::io::println("      {:<30} {:>12} {:>8}",
                          build_operation_label(operation),
                          total.as_str(),
                          timing.count);
    }
}

auto write_details(ref<rstd::path::Path> path, const BuildSummary& summary) -> OutputResult<empty> {
    auto parent = path.parent();
    if (parent.is_none()) {
        return Err(OutputError::InvalidPath(PathBuf::from(path)));
    }
    auto created = rstd::fs::create_dir_all(*parent);
    if (created.is_err()) {
        return Err(OutputError::Io(String::make("create timing report directory"_str),
                                   PathBuf::from(*parent),
                                   rstd::move(created).unwrap_err()));
    }
    auto report  = detailed_report(summary);
    auto written = rstd::fs::write_atomic(path, report.as_str().as_bytes());
    if (written.is_err()) {
        return Err(OutputError::Io(String::make("write timing report"_str),
                                   PathBuf::from(path),
                                   rstd::move(written).unwrap_err()));
    }
    return Ok(empty {});
}

} // namespace lito::timing_output

export namespace lito::timing_output
{

struct OutputOptions {
    bool            standard_output {};
    Option<PathBuf> file;
};

auto emit(const BuildSummary& summary, const OutputOptions& options) -> OutputResult<empty> {
    if (options.standard_output) print_summary(summary);
    if (options.file.is_some()) return write_details(options.file->as_path(), summary);
    return Ok(empty {});
}

} // namespace lito::timing_output
