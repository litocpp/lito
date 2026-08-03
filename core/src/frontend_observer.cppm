export module tenon.frontend_observer;

import rstd;
import tenon.frontend;
import tenon.profiling;

using namespace rstd::prelude;

export namespace tenon {

class FrontendProfileObserver {
  struct ActiveActivity {
    frontend::FrontendActivity activity{
        frontend::FrontendActivity::SourceResolve};
    ScanSpanGuard span;
  };

public:
  static auto make(ScanProfiler &profiler) -> FrontendProfileObserver {
    return FrontendProfileObserver{profiler};
  }

  auto observer() noexcept -> frontend::FrontendObserver {
    return frontend::FrontendObserver{
        .context = this,
        .begin =
            [](void *context, frontend::FrontendActivity activity) noexcept {
              static_cast<FrontendProfileObserver *>(context)->begin(activity);
            },
        .end =
            [](void *context, frontend::FrontendActivity activity) noexcept {
              static_cast<FrontendProfileObserver *>(context)->end(activity);
            },
    };
  }

private:
  explicit FrontendProfileObserver(ScanProfiler &profiler)
      : profiler_(&profiler), active_(Vec<ActiveActivity>::make()) {}

  static auto probe(frontend::FrontendActivity activity) noexcept -> ScanProbe {
    switch (activity) {
    case frontend::FrontendActivity::SourceResolve:
      return ScanProbe::SourceResolve;
    case frontend::FrontendActivity::SourceRead:
      return ScanProbe::SourceRead;
    case frontend::FrontendActivity::Lex:
      return ScanProbe::Lex;
    }
    return ScanProbe::SourceResolve;
  }

  auto begin(frontend::FrontendActivity activity) noexcept -> void {
    active_.push(ActiveActivity{
        .activity = activity,
        .span = profiler_->span(probe(activity)),
    });
  }

  auto end(frontend::FrontendActivity activity) noexcept -> void {
    if (active_.is_empty())
      return;
    auto value = rstd::move(active_.pop()).unwrap();
    if (value.activity != activity)
      return;
  }

  ScanProfiler *profiler_{};
  Vec<ActiveActivity> active_;
};

} // namespace tenon
