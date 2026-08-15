export module lito.cpp:package.spec;

import rstd;
import lito.core;
import :bmi.artifact;
import :build.profile;
import :compiler.option;
import :package.target;
import :usage;

using namespace rstd::prelude;

export namespace lito::cpp
{

using TargetId = usize;

struct TargetSpec {
    PackageTargetId                 id;
    ArtifactKind                    artifact_kind { ArtifactKind::StaticLibrary };
    String                          artifact_name;
    bool                            link_stdlib { true };
    String                          archive_stem;
    Option<String>                  module_affiliation;
    PathBuf                         root;
    PathBuf                         source_root;
    Vec<TargetSource>               sources;
    Vec<DependencySpec>             dependencies;
    Vec<ResolvedExternalDependency> external_dependencies;
    UsageRequirements               usage;
    Vec<ResolvedCompileTestCase>    compile_tests;
    Option<TestAttachmentTarget>    test_attachment;
};

struct PackageSpec {
    String               name;
    PathBuf              root;
    PathBuf              manifest_path;
    String               default_profile;
    Vec<PackageTargetId> default_targets;
    ToolchainSpec        toolchain;
    Vec<ProfileSpec>     profiles;
    Vec<TargetSpec>      targets;
};

struct CompileContext {
    String                id;
    String                scan_id;
    BmiRequest            bmi;
    CppCompileOptions     cpp;
    CppPublicRequirements public_requirements;
    Vec<String>           external_identities;
};

} // namespace lito::cpp
