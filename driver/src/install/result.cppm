module;
#include <rstd/enum.hpp>

export module lito.driver:install.result;

import rstd;
import lito.core;
import :build.result;
import :build.product;
import :install.destination;
import :install.entry;
import :install.store.model;

using namespace rstd::prelude;

export namespace lito
{

class InstallBuildOutcome {
    RSTD_ENUM(InstallBuildOutcome,
              (Built, (BuildSummary summary;)),
              (Reused, (CompletedBuildProduct product;)))

public:
    auto profile() const noexcept -> ref<str> {
        return is_Built() ? as_Built().summary.product.profile.as_str()
                          : as_Reused().product.profile.as_str();
    }

    auto built() const noexcept -> Option<ref<BuildSummary>> {
        if (! is_Built()) return None();
        return Some(ref<BuildSummary>::from_raw_parts(rstd::addressof(as_Built().summary)));
    }
};

struct InstallSummary {
    InstallBuildOutcome build;
    InstallDestination  destination;
    Vec<String>         packages;
    Vec<InstallBinary>  binaries;
    Vec<InstallEntry>   entries;
    Vec<InstallLink>    links;
};

} // namespace lito
