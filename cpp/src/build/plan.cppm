module;
#include <rstd/enum.hpp>

export module lito.cpp:build.plan;

import rstd;
import lito.core;
import :build.profile;
import :package.spec;
import :usage;

using namespace rstd::prelude;

export namespace lito::cpp
{

class PlannedLinkInput {
    RSTD_ENUM(PlannedLinkInput,
              (Target, (TargetId target;)),
              (External, (lito::link::ArgumentSequence arguments;)))
};

struct ResolvedNativeTargetPlan {
    usize                               profile {};
    Vec<lito::package::PackageTargetId> target_identities;
    Vec<TargetId>                       target_order;
    Vec<CompileContext>                 contexts;
    Vec<Vec<TargetId>>                  public_targets;
    Vec<Vec<TargetId>>                  visible_targets;
    Vec<Vec<PlannedLinkInput>>          link_inputs;
    Vec<lito::link::Requirements>       link_requirements;
    Vec<Vec<String>>                    linker_options;
};

struct SourceTargetSelection {
    usize         profile {};
    Vec<TargetId> selected_targets;
    Vec<TargetId> target_order;

    auto clone() const -> SourceTargetSelection {
        return SourceTargetSelection {
            .profile          = profile,
            .selected_targets = as<Clone>(selected_targets).clone(),
            .target_order     = as<Clone>(target_order).clone(),
        };
    }
};

struct PackagePlan {
    const PackageSpec*            package {};
    const ProfileSpec*            profile {};
    Vec<TargetId>                 target_order;
    Vec<CompileContext>           contexts;
    Vec<Vec<TargetId>>            public_targets;
    Vec<Vec<TargetId>>            visible_targets;
    Vec<Vec<PlannedLinkInput>>    link_inputs;
    Vec<lito::link::Requirements> link_requirements;
    Vec<Vec<String>>              linker_options;
};

} // namespace lito::cpp
