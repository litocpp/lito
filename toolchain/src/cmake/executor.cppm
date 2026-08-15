module;
#include <rstd/macro.hpp>

export module lito.toolchain.cmake:executor;

import rstd;
import lito.core;
import lito.toolchain.common;
import lito.system;
import lito.cpp;
import :model;
import :invocation;
import :file_api;
import :snapshot;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;

export namespace lito
{

auto plan_cmake_package(const ResolvedCMakeDependencyRequirement& requirement,
                        const CMakeProviderConfig&                provider,
                        const cpp::BuildConfiguration&            configuration,
                        const cpp::ProfileSpec&                   profile,
                        const TargetInfo&                         default_target,
                        ref<str>                                  effective_target,
                        ref<rstd::path::Path>                     profile_cmake_root,
                        usize jobs = usize(1)) -> DependencyResult<CMakePackagePlan> {
    if (jobs == usize {}) {
        return cmake_failure<CMakePackagePlan>("CMake build jobs must be greater than zero"_str);
    }
    if (effective_target != default_target.triple.as_str()) {
        return cmake_failure<CMakePackagePlan>(rstd::format(
            "CMake dependency '{}' cannot resolve cross target '{}' without an explicit CMake "
            "toolchain file",
            requirement.alias.as_str(),
            effective_target));
    }
    if (requirement.source.is_Archive()) {
        return cmake_failure<CMakePackagePlan>(rstd::format(
            "CMake dependency '{}' archive source must be materialized before planning",
            requirement.alias.as_str()));
    }
    auto area = work_area(
        requirement, provider, configuration, profile, effective_target, profile_cmake_root);
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
    -> DependencyResult<CMakeProviderConfig> {
    auto executable = path_text(provider.executable.as_path(), "CMake executable"_str);
    if (executable.is_err()) return Err(rstd::move(executable).unwrap_err());
    auto arguments = Vec<String>::make();
    arguments.push(rstd::move(executable).unwrap());
    arguments.push(String::make("--version"_str));
    auto output = run_command(arguments, environment);
    if (output.is_err()) {
        return Err(DependencyError::Operation(String::make("CMake provider identity"_str),
                                              rstd::move(output).unwrap_err()));
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
                           const Option<ToolchainEventSink>& observer = None())
    -> DependencyResult<CMakeUsageSnapshot> {
    const auto& requirement   = plan.requirement;
    const auto& provider      = plan.provider;
    const auto& configuration = plan.configuration;
    const auto& profile       = plan.profile;
    const auto& area          = plan.area;
    auto        created       = rstd::fs::create_dir_all(area.root.as_path());
    if (created.is_err()) {
        return cmake_io_failure<CMakeUsageSnapshot>("create CMake work directory"_str,
                                                    area.root.as_path(),
                                                    rstd::move(created).unwrap_err());
    }
    auto lock_path = area.root.join(PathBuf::from("lock"_str).as_path());
    auto lock_file = rstd::fs::File::create(lock_path.as_path());
    if (lock_file.is_err()) {
        return cmake_io_failure<CMakeUsageSnapshot>("open CMake dependency lock"_str,
                                                    lock_path.as_path(),
                                                    rstd::move(lock_file).unwrap_err());
    }
    auto locked = rstd::fs::FileLock::acquire(rstd::move(lock_file).unwrap(),
                                              rstd::fs::FileLockMode::Exclusive);
    if (locked.is_err()) {
        return cmake_io_failure<CMakeUsageSnapshot>(
            rstd::format("lock CMake dependency '{}'", requirement.alias.as_str()).as_str(),
            lock_path.as_path(),
            rstd::move(locked).unwrap_err());
    }
    auto cacheable =
        requirement.source.is_Directory() && requirement.source.as_Directory().cacheable;
    if (cacheable) {
        auto cached = read_usage_snapshot(area, requirement);
        if (cached.is_err()) return Err(rstd::move(cached).unwrap_err());
        if (cached->is_some()) {
            emit_cmake(observer,
                       ToolchainEventKind::CMakeReuse,
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
                        ToolchainEventKind::CMakeConfigure,
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
                        ToolchainEventKind::CMakeBuild,
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
                        ToolchainEventKind::CMakeInstall,
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
                           ToolchainEventKind::CMakeReuse,
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
                    ToolchainEventKind::CMakeQuery,
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
                                                             ToolchainEventKind::CMakeQueryBuild,
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
                                 ToolchainEventKind::CMakeSnapshot,
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
        return cmake_io_failure<CMakeUsageSnapshot>(
            rstd::format("read CMake package '{}' version", requirement.package.as_str()).as_str(),
            version_path.as_path(),
            rstd::move(version).unwrap_err());
    }
    auto normalized_version = String::make(version->as_str().trim_ascii());
    if (normalized_version.is_empty()) normalized_version = String::make("unknown"_str);
    snapshots->version = rstd::move(normalized_version);
    if (cacheable) rstd_try(write_usage_snapshot(area, *snapshots));
    return Ok(rstd::move(snapshots).unwrap());
}

auto materialize_cmake_usage(const CMakePackagePlan&       plan,
                             const CMakeUsageSnapshot&     snapshots,
                             const cpp::CppArgumentParser& parser)
    -> DependencyResult<cpp::ResolvedExternalDependency> {
    const auto& requirement        = plan.requirement;
    const auto& provider           = plan.provider;
    const auto& effective_target   = plan.effective_target;
    const auto& normalized_version = snapshots.version;
    if (snapshots.targets.len() != requirement.targets.len()) {
        return cmake_failure<cpp::ResolvedExternalDependency>(
            rstd::format("CMake package '{}' usage snapshot has {} targets, expected {}",
                         requirement.package.as_str(),
                         snapshots.targets.len(),
                         requirement.targets.len()));
    }
    auto targets = Vec<cpp::ResolvedExternalTargetUsage>::with_capacity(requirement.targets.len());
    for (usize index {}; index < requirement.targets.len(); ++index) {
        const auto& target   = requirement.targets[index];
        const auto& snapshot = snapshots.targets[index];
        auto        source   = rstd::format("CMake dependency '{}' package '{}' target '{}'",
                                            requirement.alias.as_str(),
                                            requirement.package.as_str(),
                                            target.name.as_str());
        auto        compile  = parser.parse(snapshot.compile, source.as_str());
        if (compile.is_err()) {
            return Err(DependencyError::Configuration(
                source.clone(), erase_error(rstd::move(compile).unwrap_err())));
        }
        auto identity = target_snapshot_identity(provider,
                                                 requirement,
                                                 target.name.as_str(),
                                                 normalized_version.as_str(),
                                                 snapshot,
                                                 effective_target.as_str());
        if (identity.is_err()) return Err(rstd::move(identity).unwrap_err());
        targets.push(cpp::ResolvedExternalTargetUsage {
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
    return Ok(cpp::ResolvedExternalDependency {
        .alias    = requirement.alias.clone(),
        .provider = String::make("cmake"_str),
        .version  = normalized_version.clone(),
        .targets  = rstd::move(targets),
        .link_arguments =
            cpp::LinkArgumentSequence {
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
