module;
#include <rstd/macro.hpp>

export module lito.dependency.cmake.executor;

import rstd;
import lito.error;
import lito.build.configuration;
import lito.build.profile_contract;
import lito.build.contract;
import lito.platform.contract;
import lito.dependency.contract;
import lito.cpp;
import lito.system.process;
import lito.system.environment;
import lito.dependency.cmake.model;
import lito.dependency.cmake.invocation;
import lito.dependency.cmake.file_api;
import lito.dependency.cmake.snapshot;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

auto plan_cmake_package(const ResolvedCMakeDependencyRequirement& requirement,
                        const CMakeProviderConfig&                provider,
                        const BuildConfiguration&                 configuration,
                        const ProfileSpec&                        profile,
                        const TargetInfo&                         default_target,
                        ref<str>                                  effective_target,
                        usize jobs = usize(1)) -> Result<CMakePackagePlan> {
    if (jobs == usize {}) {
        return cmake_failure<CMakePackagePlan>("CMake build jobs must be greater than zero"_str);
    }
    if (effective_target != default_target.triple.as_str()) {
        return cmake_failure<CMakePackagePlan>(rstd::format(
            "CMake dependency '{}' cannot resolve cross target '{}' without an explicit CMake "
            "toolchain contract",
            requirement.alias.as_str(),
            effective_target));
    }
    if (requirement.source.is_Archive()) {
        return cmake_failure<CMakePackagePlan>(rstd::format(
            "CMake dependency '{}' archive source must be materialized before planning",
            requirement.alias.as_str()));
    }
    auto area = work_area(requirement, provider, configuration, profile, effective_target);
    if (area.is_err()) return Err(rstd::move(area).unwrap_err());
    auto operations = Vec<CMakePackageOperation>::make();
    if (requirement.integration == CMakeIntegration::Install &&
        ! requirement.source.is_Installed()) {
        operations.push(CMakePackageOperation::ConfigureSource);
        operations.push(CMakePackageOperation::BuildSource);
        operations.push(CMakePackageOperation::InstallSource);
    }
    operations.push(CMakePackageOperation::WriteQuery);
    operations.push(CMakePackageOperation::ConfigureQuery);
    operations.push(CMakePackageOperation::BuildQuery);
    operations.push(CMakePackageOperation::ReadUsage);
    return Ok(CMakePackagePlan {
        .requirement      = clone_cmake_requirement(requirement),
        .provider         = provider.clone(),
        .configuration    = configuration.clone(),
        .profile          = clone_profile(profile),
        .area             = rstd::move(area).unwrap(),
        .effective_target = String::make(effective_target),
        .operations       = rstd::move(operations),
        .jobs             = jobs,
    });
}

auto identify_cmake_provider(CMakeProviderConfig               provider,
                             const ResolvedProcessEnvironment& environment)
    -> Result<CMakeProviderConfig> {
    auto executable = path_text(provider.executable.as_path(), "CMake executable"_str);
    if (executable.is_err()) return Err(rstd::move(executable).unwrap_err());
    auto arguments = Vec<String>::make();
    arguments.push(rstd::move(executable).unwrap());
    arguments.push(String::make("--version"_str));
    auto output = run_command(arguments, environment);
    if (output.is_err()) {
        return cmake_failure<CMakeProviderConfig>(
            rstd::format("CMake provider identity could not execute: {}",
                         rstd::move(output).unwrap_err().message.as_str()));
    }
    if (output->exit_code != i32 {}) {
        return cmake_failure<CMakeProviderConfig>(
            rstd::format("CMake provider identity failed with exit code {}:\n{}{}",
                         output->exit_code,
                         output->standard_output.as_str(),
                         output->standard_error.as_str()));
    }
    provider.identity = String::make(output->standard_output.as_str().trim_ascii());
    if (provider.identity.is_empty()) {
        return cmake_failure<CMakeProviderConfig>("CMake provider returned an empty identity"_str);
    }
    return Ok(rstd::move(provider));
}

auto execute_cmake_package(const CMakePackagePlan&           plan,
                           const ResolvedProcessEnvironment& environment,
                           const Option<BuildObserver>&      observer = None())
    -> Result<CMakeUsageSnapshot> {
    const auto& requirement   = plan.requirement;
    const auto& provider      = plan.provider;
    const auto& configuration = plan.configuration;
    const auto& profile       = plan.profile;
    const auto& area          = plan.area;
    auto        created       = rstd::fs::create_dir_all(area.root.as_path());
    if (created.is_err()) {
        return cmake_failure<CMakeUsageSnapshot>(
            rstd::format("cannot create CMake work directory '{}': {}",
                         area.root.as_path(),
                         rstd::move(created).unwrap_err()));
    }
    auto lock_path = area.root.join(PathBuf::from("lock"_str).as_path());
    auto lock_file = rstd::fs::File::create(lock_path.as_path());
    if (lock_file.is_err()) {
        return cmake_failure<CMakeUsageSnapshot>(
            rstd::format("cannot open CMake dependency lock '{}': {}",
                         lock_path.as_path(),
                         rstd::move(lock_file).unwrap_err()));
    }
    auto locked = lock_file->lock();
    if (locked.is_err()) {
        return cmake_failure<CMakeUsageSnapshot>(
            rstd::format("cannot lock CMake dependency '{}': {}",
                         requirement.alias.as_str(),
                         rstd::move(locked).unwrap_err()));
    }
    auto cacheable =
        requirement.source.is_Directory() && requirement.source.as_Directory().cacheable;
    if (cacheable) {
        auto cached = read_usage_snapshot(area, requirement);
        if (cached.is_err()) return Err(rstd::move(cached).unwrap_err());
        if (cached->is_some()) {
            emit_cmake(observer,
                       BuildEventKind::CMakeReuse,
                       requirement.alias.as_str(),
                       area.query_root.as_path());
            return Ok(rstd::move(cached).unwrap().unwrap());
        }
    }
    auto install_current = cacheable ? rstd_try(source_install_current(area)) : false;
    auto snapshots       = Option<CMakeUsageSnapshot> {};
    for (auto operation : plan.operations) {
        switch (operation) {
        case CMakePackageOperation::ConfigureSource:
            if (! install_current) {
                rstd_try(with_operation_context(
                    execute_observed(
                        observer,
                        BuildEventKind::CMakeConfigure,
                        requirement.alias.as_str(),
                        area.build.as_path(),
                        [&] {
                            return configure_source(
                                requirement, provider, configuration, profile, area, environment);
                        }),
                    plan,
                    operation));
            }
            break;
        case CMakePackageOperation::BuildSource:
            if (! install_current) {
                rstd_try(with_operation_context(
                    execute_observed(
                        observer,
                        BuildEventKind::CMakeBuild,
                        requirement.alias.as_str(),
                        area.build.as_path(),
                        [&] {
                            return build_source(
                                requirement, provider, profile, area, plan.jobs, environment);
                        }),
                    plan,
                    operation));
            }
            break;
        case CMakePackageOperation::InstallSource:
            if (! install_current) {
                rstd_try(with_operation_context(
                    execute_observed(
                        observer,
                        BuildEventKind::CMakeInstall,
                        requirement.alias.as_str(),
                        area.install.as_path(),
                        [&] {
                            return install_source(
                                requirement, provider, profile, area, cacheable, environment);
                        }),
                    plan,
                    operation));
                install_current = true;
            } else {
                emit_cmake(observer,
                           BuildEventKind::CMakeReuse,
                           requirement.alias.as_str(),
                           area.install.as_path());
            }
            break;
        case CMakePackageOperation::WriteQuery:
            rstd_try(with_operation_context(write_probe_files(requirement, area), plan, operation));
            break;
        case CMakePackageOperation::ConfigureQuery:
            rstd_try(with_operation_context(
                execute_observed(
                    observer,
                    BuildEventKind::CMakeQuery,
                    requirement.alias.as_str(),
                    area.query_build.as_path(),
                    [&] {
                        return configure_probe(
                            requirement, provider, configuration, profile, area, environment);
                    }),
                plan,
                operation));
            break;
        case CMakePackageOperation::BuildQuery:
            rstd_try(with_operation_context(execute_observed(observer,
                                                             BuildEventKind::CMakeQueryBuild,
                                                             requirement.alias.as_str(),
                                                             area.query_build.as_path(),
                                                             [&] {
                                                                 return build_probe(requirement,
                                                                                    provider,
                                                                                    configuration,
                                                                                    profile,
                                                                                    area,
                                                                                    plan.jobs,
                                                                                    environment);
                                                             }),
                                            plan,
                                            operation));
            break;
        case CMakePackageOperation::ReadUsage:
            snapshots = Some(rstd_try(with_operation_context(
                execute_observed(observer,
                                 BuildEventKind::CMakeSnapshot,
                                 requirement.alias.as_str(),
                                 area.query_root.as_path(),
                                 [&] {
                                     return read_probe_snapshots(area, requirement);
                                 }),
                plan,
                operation)));
            break;
        }
    }
    if (snapshots.is_none()) {
        return cmake_failure<CMakeUsageSnapshot>(rstd::format(
            "CMake package '{}' plan produced no usage snapshot", requirement.package.as_str()));
    }
    auto version_path =
        area.query_build.join(PathBuf::from("lito-package-version.txt"_str).as_path());
    auto version = rstd::fs::read_to_string(version_path.as_path());
    if (version.is_err()) {
        return cmake_failure<CMakeUsageSnapshot>(
            rstd::format("cannot read CMake package '{}' version: {}",
                         requirement.package.as_str(),
                         rstd::move(version).unwrap_err()));
    }
    auto normalized_version = String::make(version->as_str().trim_ascii());
    if (normalized_version.is_empty()) normalized_version = String::make("unknown"_str);
    snapshots->version = rstd::move(normalized_version);
    if (cacheable) rstd_try(write_usage_snapshot(area, *snapshots));
    return Ok(rstd::move(snapshots).unwrap());
}

auto materialize_cmake_usage(const CMakePackagePlan&   plan,
                             const CMakeUsageSnapshot& snapshots,
                             const CppArgumentParser&  parser)
    -> Result<ResolvedExternalDependency> {
    const auto& requirement        = plan.requirement;
    const auto& provider           = plan.provider;
    const auto& effective_target   = plan.effective_target;
    const auto& normalized_version = snapshots.version;
    if (snapshots.targets.len() != requirement.targets.len()) {
        return cmake_failure<ResolvedExternalDependency>(
            rstd::format("CMake package '{}' usage snapshot has {} targets, expected {}",
                         requirement.package.as_str(),
                         snapshots.targets.len(),
                         requirement.targets.len()));
    }
    auto targets = Vec<ResolvedExternalTargetUsage>::with_capacity(requirement.targets.len());
    for (usize index {}; index < requirement.targets.len(); ++index) {
        const auto& target   = requirement.targets[index];
        const auto& snapshot = snapshots.targets[index];
        auto        source   = rstd::format("CMake dependency '{}' package '{}' target '{}'",
                                            requirement.alias.as_str(),
                                            requirement.package.as_str(),
                                            target.name.as_str());
        auto        compile  = parser.parse(snapshot.compile, source.as_str());
        if (compile.is_err()) {
            return cmake_failure<ResolvedExternalDependency>(
                rstd::format("{} has invalid compile requirements: {}",
                             source.as_str(),
                             rstd::move(compile).unwrap_err()));
        }
        auto identity = target_snapshot_identity(provider,
                                                 requirement,
                                                 target.name.as_str(),
                                                 normalized_version.as_str(),
                                                 snapshot,
                                                 effective_target.as_str());
        if (identity.is_err()) return Err(rstd::move(identity).unwrap_err());
        targets.push(ResolvedExternalTargetUsage {
            .name              = target.name.clone(),
            .visibility        = target.visibility,
            .compile_arguments = rstd::move(compile).unwrap(),
            .identity          = rstd::move(identity).unwrap(),
        });
    }
    auto identity = dependency_identity(
        provider, requirement, normalized_version.as_str(), snapshots, effective_target.as_str());
    if (identity.is_err()) return Err(rstd::move(identity).unwrap_err());
    auto link_arguments =
        materialize_link_tokens(snapshots.combined.link, plan.area.query_build.as_path());
    if (link_arguments.is_err()) return Err(rstd::move(link_arguments).unwrap_err());
    return Ok(ResolvedExternalDependency {
        .alias    = requirement.alias.clone(),
        .provider = String::make("cmake"_str),
        .version  = normalized_version.clone(),
        .targets  = rstd::move(targets),
        .link_arguments =
            LinkArgumentSequence {
                .tokens   = rstd::move(link_arguments).unwrap(),
                .source   = rstd::format("CMake dependency '{}' package '{}'",
                                         requirement.alias.as_str(),
                                         requirement.package.as_str()),
                .identity = identity->clone(),
            },
        .identity = rstd::move(identity).unwrap(),
    });
}

} // namespace lito
