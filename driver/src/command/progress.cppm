export module lito.driver:command.progress;

import rstd;
import :build.event;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

auto render_build_preparation(const BuildEvent& event, bool include_prefix = true) -> String {
    if (event.kind != BuildEventKind::Preparation || ! event.completed || event.failed ||
        event.preparation.is_none())
        return String::make();
    const auto& summary = *event.preparation;
    auto        text    = String::make();
    if (summary.is_ScanFinalize()) {
        const auto& value = summary.as_ScanFinalize();
        text              = rstd::format("{}{} units, {} headers",
                                         include_prefix ? "[scan-finalize] "_str : " "_str,
                                         value.units,
                                         value.headers);
    } else if (summary.is_CompilePlan()) {
        const auto& value = summary.as_CompilePlan();
        text              = rstd::format(
            "[compile-plan] {} units, {} dependencies", value.units, value.dependencies);
    } else {
        const auto& value = summary.as_ExecutionGraph();
        text              = rstd::format(
            "[execution-graph] {} actions, {} artifacts", value.actions, value.artifacts);
    }
    text.push_str(
        rstd::format(" ({:.3} ms)\n", f64(event.elapsed.as_secs_f64() * 1000.0)).as_str());
    return text;
}

class BuildProgressDisplay {
    bool   active_ {};
    bool   preparation_active_ {};
    usize  width_ {};
    usize  emitted_ {};
    String prefix_;

public:
    auto interrupt() -> String {
        if (preparation_active_) {
            preparation_active_ = false;
            return "\n"_Str;
        }
        if (! active_) return String::make();
        active_ = false;
        return "]\n"_Str;
    }

    auto update_preparation(const BuildEvent& event) -> String {
        if (event.kind == BuildEventKind::ScanFinalize && ! event.completed) {
            auto text = interrupt();
            text.push_str("[scan-finalize]"_str);
            preparation_active_ = true;
            return text;
        }
        const auto continues = preparation_active_ && event.preparation.is_some() &&
                               event.preparation->is_ScanFinalize();
        auto       summary   = render_build_preparation(event, ! continues);
        if (summary.is_empty()) return summary;
        auto text           = continues ? String::make() : interrupt();
        preparation_active_ = false;
        text.push_str(summary.as_str());
        return text;
    }

    auto update(const BuildEvent& event, bool terminal, usize columns) -> String {
        if (event.progress.is_none() || event.progress->total == usize {}) return String::make();
        const auto& progress = *event.progress;
        const auto  start    = progress.current == usize {} && ! event.completed;
        auto        current = progress.current < progress.total ? progress.current : progress.total;
        if (! terminal) {
            if (! start && ! event.completed) return String::make();
            auto status   = event.completed
                                ? (event.failed ? "failed"_str : "done"_str)
                                : (event.kind == BuildEventKind::ScanPackages ? "scanning"_str
                                                                              : "checking"_str);
            auto rendered = interrupt();
            rendered.push_str(
                rstd::format(
                    "[{}] {} {}/{} {}\n", event.kind, event.target, current, progress.total, status)
                    .as_str());
            return rendered;
        }
        auto rendered = start ? interrupt() : String::make();
        if (start || width_ == usize {}) {
            // Reserve the closing bracket and failure suffix before emitting the first cell.
            auto available = columns > usize {} ? columns - usize(1) : usize(79);
            prefix_        = rstd::format("[{}] ", event.kind);
            if (available < prefix_.len() + usize(10))
                prefix_ = event.kind == BuildEventKind::ScanPackages ? "scan "_Str : "check "_Str;
            if (available < prefix_.len() + usize(10)) prefix_ = String::make();
            auto overhead = prefix_.len() + usize(9);
            width_        = available > overhead ? available - overhead : usize(1);
            if (width_ > usize(20)) width_ = usize(20);
        }
        if (! active_) {
            rendered.push_str(prefix_.as_str());
            rendered.push_ascii('[');
            emitted_ = usize {};
            active_  = true;
        }
        auto filled = current * width_ / progress.total;
        if ((! event.completed || event.failed) && filled == width_) --filled;
        while (emitted_ < filled) {
            rendered.push_ascii('=');
            ++emitted_;
        }
        if (event.completed) {
            rendered.push_ascii(']');
            if (event.failed) rendered.push_str(" failed"_str);
            rendered.push_ascii('\n');
            active_ = false;
        }
        return rendered;
    }
};

} // namespace lito
