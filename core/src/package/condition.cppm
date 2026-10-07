module;
#include <rstd/macro.hpp>

export module lito.core:package.condition;

import rstd;
import lito.system;
import :condition;
import :package.graph;
import :package.error;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::package
{

struct PackageConditionEnvironment {
    lito::condition::Context target;
    lito::condition::Context host;
    void*                    host_state {};
    PackageResult<lito::condition::Context> (*resolve_host)(void*) {};
};

auto make_target_condition_environment(const lito::system::BuildPlatform& platform)
    -> lito::condition::Context {
    auto context = lito::condition::Context {};
    context.set_string("target.os"_Str, platform.effective_target.platform_name().into());
    context.set_string("target.vendor"_Str, platform.effective_target.vendor.clone());
    context.set_string("target.family"_Str, platform.effective_target.family_name().into());
    context.set_string("target.environment"_Str,
                       platform.effective_target.environment_name().into());
    context.set_string(
        "target.arch"_Str,
        lito::system::architecture_name(platform.effective_target.architecture).into());
    context.set_string("target.triple"_Str, platform.effective_target.triple.clone());
    context.set_string("host.os"_Str, platform.host.os.clone());
    context.set_string("host.arch"_Str,
                       lito::system::architecture_name(platform.host.architecture).into());
    context.set_bool("build.cross"_Str, platform.cross);
    return context;
}

auto make_condition_environment(const lito::system::BuildPlatform& platform,
                                ref<str>                           profile,
                                ref<str>                           standard_library,
                                ref<str> standard_library_runtime) -> lito::condition::Context {
    auto context = make_target_condition_environment(platform);
    context.set_string("profile.name"_Str, profile.into());
    context.set_string("toolchain.compiler"_Str, "clang"_Str);
    context.set_string("toolchain.stdlib"_Str, standard_library.into());
    context.set_string("toolchain.stdlib-runtime"_Str, standard_library_runtime.into());
    return context;
}

auto make_package_condition_context(const ResolvedPackage&          package,
                                    const lito::condition::Context& environment)
    -> lito::condition::Context {
    auto context = environment.clone();
    for (const auto& feature : package.features) {
        auto key = "feature."_Str;
        key.push_str(feature.name.as_str());
        context.set_bool(rstd::move(key), feature.enabled);
    }
    return context;
}

auto dependency_condition_matches(const ResolvedPackage&          owner,
                                  ref<str>                        name,
                                  const DependencyActivation&     activation,
                                  const lito::condition::Context& context) -> PackageResult<bool> {
    if (activation.condition.is_none()) return Ok(true);
    auto matched = lito::condition::evaluate(activation.condition->expression, context);
    if (matched.is_err()) {
        return Err(PackageError::Message(
            rstd::format("package '{}' manifest '{}' dependency '{}' condition '{}' is invalid: {}",
                         owner.manifest.name.as_str(),
                         owner.manifest.manifest_path.as_path(),
                         name,
                         activation.condition->source.as_str(),
                         rstd::move(matched).unwrap_err())));
    }
    return Ok(*matched);
}

auto resolve_external_dependency_conditions(lito::package::ResolvedPackage& package,
                                            const lito::condition::Context& context)
    -> lito::package::PackageResult<empty> {
    auto pkg_config = Vec<lito::dependency::PkgConfigExternalDependency>::with_capacity(
        package.manifest.pkg_config_external_dependencies.len());
    for (const auto& dependency : package.manifest.pkg_config_external_dependencies) {
        if (dependency.condition.is_some()) {
            auto matched = lito::condition::evaluate(dependency.condition->expression, context);
            if (matched.is_err()) {
                return Err(lito::package::PackageError::Message(rstd::format(
                    "package '{}' manifest '{}' pkg-config external dependency '{}' condition "
                    "'{}' is invalid: {}",
                    package.manifest.name.as_str(),
                    package.manifest.manifest_path.as_path(),
                    dependency.alias.as_str(),
                    dependency.condition->source.as_str(),
                    rstd::move(matched).unwrap_err())));
            }
            if (! *matched) continue;
        }
        pkg_config.push(dependency.clone());
    }
    package.selected_pkg_config_dependencies = Some(rstd::move(pkg_config));

    auto cmake = Vec<lito::dependency::CMakeDependencyRequirement>::with_capacity(
        package.manifest.cmake_external_dependencies.len());
    for (const auto& dependency : package.manifest.cmake_external_dependencies) {
        if (dependency.condition.is_some()) {
            auto matched = lito::condition::evaluate(dependency.condition->expression, context);
            if (matched.is_err()) {
                return Err(lito::package::PackageError::Message(rstd::format(
                    "package '{}' manifest '{}' CMake external dependency '{}' condition '{}' "
                    "is invalid: {}",
                    package.manifest.name.as_str(),
                    package.manifest.manifest_path.as_path(),
                    dependency.alias.as_str(),
                    dependency.condition->source.as_str(),
                    rstd::move(matched).unwrap_err())));
            }
            if (! *matched) continue;
        }
        cmake.push(dependency.clone());
    }
    package.selected_cmake_dependencies = Some(rstd::move(cmake));

    auto cargo = Vec<lito::dependency::CargoDependencyRequirement>::with_capacity(
        package.manifest.cargo_external_dependencies.len());
    for (const auto& dependency : package.manifest.cargo_external_dependencies) {
        if (dependency.consumption.condition.is_some()) {
            auto matched =
                lito::condition::evaluate(dependency.consumption.condition->expression, context);
            if (matched.is_err()) {
                return Err(lito::package::PackageError::Message(rstd::format(
                    "package '{}' manifest '{}' Cargo external dependency '{}' condition '{}' "
                    "is invalid: {}",
                    package.manifest.name.as_str(),
                    package.manifest.manifest_path.as_path(),
                    dependency.alias.as_str(),
                    dependency.consumption.condition->source.as_str(),
                    rstd::move(matched).unwrap_err())));
            }
            if (! *matched) continue;
        }
        cargo.push(dependency.clone());
    }
    package.selected_cargo_dependencies = Some(rstd::move(cargo));
    return Ok(empty {});
}

} // namespace lito::package
