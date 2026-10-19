export module lito.driver:command.progress;

import rstd;
import :build.event;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

class BuildProgressDisplay {
    bool   active_ {};
    usize  width_ {};
    usize  emitted_ {};
    usize  batch_ {};
    String prefix_;

public:
    auto interrupt() -> String {
        if (! active_) return String::make();
        active_ = false;
        return "]\n"_Str;
    }

    auto update(const BuildEvent& event, bool terminal, usize columns) -> String {
        if (event.progress.is_none() || event.progress->total == usize {}) return String::make();
        const auto& progress = *event.progress;
        const auto  start    = progress.current == usize {} && ! event.completed;
        if (start) ++batch_;
        auto current = progress.current < progress.total ? progress.current : progress.total;
        if (! terminal) {
            if (! start && ! event.completed) return String::make();
            auto status = event.completed
                              ? (event.failed ? "failed"_str : "done"_str)
                              : (event.kind == BuildEventKind::ScanPackages ? "scanning"_str
                                                                            : "checking"_str);
            return rstd::format(
                "[{}] {} {}/{} {}\n", event.kind, event.target, current, progress.total, status);
        }
        auto rendered = start ? interrupt() : String::make();
        if (start || width_ == usize {}) {
            // Reserve the closing bracket and failure suffix before emitting the first cell.
            auto available = columns > usize {} ? columns - usize(1) : usize(79);
            prefix_        = rstd::format("[{} {}] ", event.kind, batch_);
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
