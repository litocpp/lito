export module lito.build.frontend_observer;

import rstd;
import lito.frontend;
import lito.build.profiling;

using namespace rstd::prelude;

export namespace lito
{

template<typename Profiler>
class BasicFrontendProfileObserver {
    struct ActiveActivity {
        frontend::FrontendActivity activity { frontend::FrontendActivity::SourceResolve };
        ScanSpanGuard              span;
    };

public:
    static auto make(Profiler& profiler) -> BasicFrontendProfileObserver {
        return BasicFrontendProfileObserver { profiler };
    }

    auto observer() noexcept -> frontend::FrontendObserver {
        return frontend::FrontendObserver {
            .context = this,
            .begin =
                [](void* context, frontend::FrontendActivity activity) noexcept {
                    static_cast<BasicFrontendProfileObserver*>(context)->begin(activity);
                },
            .end =
                [](void* context, frontend::FrontendActivity activity) noexcept {
                    static_cast<BasicFrontendProfileObserver*>(context)->end(activity);
                },
        };
    }

private:
    explicit BasicFrontendProfileObserver(Profiler& profiler)
        : profiler_(&profiler), active_(Vec<ActiveActivity>::make()) {}

    static auto probe(frontend::FrontendActivity activity) noexcept -> ScanProbe {
        switch (activity) {
        case frontend::FrontendActivity::SourceResolve: return ScanProbe::SourceResolve;
        case frontend::FrontendActivity::SourceRead: return ScanProbe::SourceRead;
        case frontend::FrontendActivity::Lex: return ScanProbe::Lex;
        }
        return ScanProbe::SourceResolve;
    }

    auto begin(frontend::FrontendActivity activity) noexcept -> void {
        active_.push(ActiveActivity {
            .activity = activity,
            .span     = profiler_->span(probe(activity)),
        });
    }

    auto end(frontend::FrontendActivity activity) noexcept -> void {
        if (active_.is_empty()) return;
        auto value = rstd::move(active_.pop()).unwrap();
        if (value.activity != activity) return;
    }

    Profiler*           profiler_ {};
    Vec<ActiveActivity> active_;
};

using FrontendProfileObserver     = BasicFrontendProfileObserver<ScanProfiler>;
using FrontendTaskProfileObserver = BasicFrontendProfileObserver<ScanTaskProfiler>;

} // namespace lito
