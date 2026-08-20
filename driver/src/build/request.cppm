export module lito.driver:build.request;

import rstd;
import lito.tools;
import lito.core;
import :package.selection;
import :build.event;
import :build.setup_report;
import lito.cpp;
import lito.system;

using namespace rstd::prelude;
using namespace lito::system;

export namespace lito
{

struct ScanExecutionPolicy {
    Option<usize> jobs;
    Option<usize> max_in_flight;
};

struct CompileExecutionPolicy {
    Option<usize> jobs;
    Option<usize> max_in_flight;
};

struct BuildExecutionPolicy {
    ScanExecutionPolicy    scan;
    CompileExecutionPolicy compile;
};

struct InstallArtifactLinkPolicy {
    lito::artifact::ElfRunpath runtime_search;
    String                     identity;

    auto clone() const -> InstallArtifactLinkPolicy {
        return InstallArtifactLinkPolicy {
            .runtime_search = runtime_search.clone(),
            .identity       = identity.clone(),
        };
    }
};

struct RequestedArtifactLinkVariant {
    lito::package::PackageTargetId target;
    InstallArtifactLinkPolicy      policy;

    auto clone() const -> RequestedArtifactLinkVariant {
        return RequestedArtifactLinkVariant {
            .target = target.clone(),
            .policy = policy.clone(),
        };
    }
};

struct BuildRequest {
    lito::package::PackageSelection           selection;
    Vec<String>                               targets;
    Vec<lito::package::PackageTargetId>       exact_targets;
    Vec<RequestedArtifactLinkVariant>         artifact_link_variants;
    PathBuf                                   output;
    ProcessEnvironmentSpec                    environment;
    lito::tools::ToolSpec                     tools;
    cpp::BuildConfiguration                   configuration;
    lito::lock::LockConfig                    lock;
    Option<lito::manifest::BuildProfileName>  profile;
    lito::source::PackageSourceConfig         sources;
    lito::dependency::PkgConfigProviderConfig pkg_config;
    lito::dependency::CMakeProviderConfig     cmake;
    lito::dependency::CMakeBuildOverrideSet   cmake_build_overrides;
    lito::package::PackageSelectionPurpose    purpose {
        lito::package::PackageSelectionPurpose::Production
    };
    bool                                        locked { false };
    BuildExecutionPolicy                        execution;
    Option<BuildEventSink>                      observer;
    Option<BuildSetupReportSink>                setup_reporter;
    Option<lito::tools::HostToolResolutionSink> tool_reporter;
};

} // namespace lito
