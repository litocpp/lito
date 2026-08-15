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
              (External, (LinkArgumentSequence arguments;)))
};

struct SourceDiscoveryPlan {
    usize                      profile {};
    Vec<PackageTargetId>       target_identities;
    Vec<TargetId>              target_order;
    Vec<CompileContext>        contexts;
    Vec<Vec<TargetId>>         visible_targets;
    Vec<Vec<PlannedLinkInput>> link_inputs;
    Vec<Vec<String>>           linker_options;
};

struct SourceTargetSelection {
    usize         profile {};
    Vec<TargetId> selected_targets;
    Vec<TargetId> target_order;
};

struct PackagePlan {
    const PackageSpec*         package {};
    const ProfileSpec*         profile {};
    Vec<TargetId>              target_order;
    Vec<CompileContext>        contexts;
    Vec<Vec<TargetId>>         visible_targets;
    Vec<Vec<PlannedLinkInput>> link_inputs;
    Vec<Vec<String>>           linker_options;
};

} // namespace lito::cpp
