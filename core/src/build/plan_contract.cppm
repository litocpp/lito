module;
#include <rstd/enum.hpp>

export module lito.build.plan_contract;

import rstd;
import lito.error;
import lito.frontend;
import lito.cpp.bmi;
import lito.dependency.contract;
import lito.manifest.contract;
import lito.package.identity;
import lito.package.target_contract;
import lito.build.profile_contract;
import lito.build.identity;

using namespace rstd::prelude;

export namespace lito
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

struct UnitSpec {
    UnitId                 id {};
    TargetId               target {};
    PathBuf                relative_source;
    PathBuf                source;
    PathBuf                object;
    PathBuf                cache_record;
    Option<PathBuf>        compile_test_record;
    Option<BmiArtifact>    bmi;
    const CompileContext*  context {};
    const CompileTestCase* compile_test {};
};

struct PreparedUnit {
    UnitSpec                           unit;
    PathBuf                            working_directory;
    Option<frontend::FrontendAnalysis> frontend_analysis;
};

struct ScanResult {
    UnitId                           unit {};
    Option<frontend::ProvidedModule> provided;
    Option<String>                   implementation_module;
    Vec<String>                      required_modules;
    Vec<PathBuf>                     header_inputs;
    String                           preprocessor_environment;
};

struct ModulePlan {
    Vec<UnitId>      compile_order;
    Vec<Vec<UnitId>> direct_inputs;
    Vec<Vec<UnitId>> resolved_inputs;
};

} // namespace lito
