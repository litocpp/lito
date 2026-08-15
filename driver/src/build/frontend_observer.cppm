module lito.driver:build.frontend_observer;

import rstd;
import lito.frontend;
import :build.profiling;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
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

template<typename Profiler>
class BasicPreprocessorProfileObserver {
    struct ActiveActivity {
        frontend::preprocessor::PreprocessorActivity activity;
        ScanSpanGuard                                span;
    };

public:
    explicit BasicPreprocessorProfileObserver(Profiler& profiler)
        : profiler_(&profiler), active_(Vec<ActiveActivity>::make()) {}

    auto begin(frontend::preprocessor::PreprocessorActivity activity) -> void {
        active_.push(ActiveActivity {
            .activity = activity,
            .span     = profiler_->span(probe(activity)),
        });
    }

    auto end(frontend::preprocessor::PreprocessorActivity activity) -> void {
        auto current = active_.pop();
        if (current.is_none()) {
            remember(String::make("preprocessor profiling activity stack is empty"_str));
            return;
        }
        auto value = rstd::move(current).unwrap_unchecked();
        if (value.activity != activity) {
            remember(String::make("preprocessor profiling activities ended out of order"_str));
        }
        auto completed = profiler_->complete(value.span);
        if (completed.is_err()) {
            remember(rstd::move(completed).unwrap_err_unchecked());
        }
    }

    auto record(const frontend::preprocessor::PreprocessorStatistics& statistics) -> void {
        statistics_ = statistics;
        profiler_->record_preprocessor_statistics(statistics);
    }

    auto finish() -> Result<empty, String> {
        if (! active_.is_empty()) {
            return Err(String::make("preprocessor profiling activities remain active"_str));
        }
        if (error_.is_some()) return Err(rstd::move(error_).unwrap_unchecked());
        return Ok(empty {});
    }

    auto statistics() const noexcept -> const frontend::preprocessor::PreprocessorStatistics& {
        return statistics_;
    }

private:
    static auto probe(frontend::preprocessor::PreprocessorActivity activity) noexcept -> ScanProbe {
        switch (activity) {
        case frontend::preprocessor::PreprocessorActivity::PredefinedMacros:
            return ScanProbe::PredefinedMacros;
        case frontend::preprocessor::PreprocessorActivity::TranslationUnit:
            return ScanProbe::TranslationUnit;
        }
        return ScanProbe::Preprocessor;
    }

    auto remember(String error) -> void {
        if (error_.is_none()) error_ = Some(rstd::move(error));
    }

    Profiler*                                      profiler_ {};
    Vec<ActiveActivity>                            active_;
    frontend::preprocessor::PreprocessorStatistics statistics_;
    Option<String>                                 error_;
};

using PreprocessorTaskProfileObserver = BasicPreprocessorProfileObserver<ScanTaskProfiler>;

} // namespace lito
