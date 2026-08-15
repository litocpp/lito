module lito.driver:build.toolchain_observer;

import rstd;
import :build.event;
import lito.toolchain;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

auto emit_build_toolchain(const Option<BuildEventSink>& observer,
                          const ClangToolchain&         toolchain) noexcept -> void {
    if (observer.is_none() || observer->notify == nullptr) return;
    observer->notify(observer->context,
                     BuildEvent { BuildEventKind::Toolchain, "cc"_str, toolchain.cc_path() });
    observer->notify(observer->context,
                     BuildEvent { BuildEventKind::Toolchain, "cxx"_str, toolchain.cxx_path() });
    observer->notify(observer->context,
                     BuildEvent { BuildEventKind::Toolchain, "ld"_str, toolchain.ld_path() });
    observer->notify(observer->context,
                     BuildEvent { BuildEventKind::Toolchain, "ar"_str, toolchain.ar_path() });
}

} // namespace lito
