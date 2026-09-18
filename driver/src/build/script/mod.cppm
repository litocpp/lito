module;
#include <rstd/macro.hpp>

module lito.driver:build.script;

import rstd;
import lito.core;
import lito.cpp;
import lito.system;
import :script.build;
import :build.script.declaration;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::system;

namespace lito
{

auto materialize_generated_inputs(cpp::PackageMetadata&             metadata,
                                  Vec<cpp::CompileContext>&         contexts,
                                  const BuildLayout&                layout,
                                  const cpp::SourceTargetSelection& selection)
    -> BuildScriptResult<empty> {
    for (auto target_id : selection.target_order) {
        auto& target         = metadata.targets[target_id];
        auto  generated_root = Option<PathBuf> {};
        for (auto& group : target.source_groups) {
            if (! group.generated) continue;
            if (generated_root.is_none()) {
                generated_root =
                    Some(rstd_try(layout.generated_package_directory(target.id.package.as_str())));
            }
            auto inspected = rstd::fs::metadata(generated_root->as_path());
            if (inspected.is_err() || ! inspected->is_dir()) {
                return build_script_failure<empty>(
                    rstd::format("generated source root '{}' for package '{}' does not exist",
                                 generated_root->as_path(),
                                 target.id.package.as_str()));
            }
            group.root = generated_root->clone();
            group.identity =
                rstd::format("generated:{}:{}", target.id.package.as_str(), layout.output());
        }
        for (const auto& requirement : target.usage.private_include_directory_requirements) {
            if (requirement.root != lito::dependency::IncludeDirectoryRoot::Generated) continue;
            auto generated =
                rstd_try(layout.generated_package_directory(target.id.package.as_str()));
            auto requested = generated.join(requirement.path.as_path());
            auto canonical = rstd::fs::canonicalize(requested.as_path());
            if (canonical.is_err()) {
                return build_script_failure<empty>(
                    rstd::format("generated private include directory '{}' does not exist",
                                 requested.as_path()));
            }
            if (canonical->as_path().strip_prefix(generated.as_path()).is_none()) {
                return build_script_failure<empty>(
                    rstd::format("generated private include directory '{}' escapes package root",
                                 requested.as_path()));
            }
            auto inspected = rstd::fs::metadata(canonical->as_path());
            if (inspected.is_err() || ! inspected->is_dir()) {
                return build_script_failure<empty>(
                    rstd::format("generated private include directory '{}' is not a directory",
                                 canonical->as_path()));
            }
            auto repeated = false;
            for (const auto& include : target.usage.private_include_directories) {
                if (include.as_path() == canonical->as_path()) repeated = true;
            }
            auto include = rstd::move(canonical).unwrap();
            if (! repeated) target.usage.private_include_directories.push(include.clone());
            cpp::add_private_include_directory(contexts[target_id], rstd::move(include));
        }
        target.usage.private_include_directory_requirements.clear();
    }
    return Ok(empty {});
}

auto has_generated_inputs(const cpp::PackageMetadata&       metadata,
                          const cpp::SourceTargetSelection& selection) noexcept -> bool {
    for (auto target : selection.target_order) {
        for (const auto& group : metadata.targets[target].source_groups) {
            if (group.generated) return true;
        }
        if (! metadata.targets[target].usage.private_include_directory_requirements.is_empty()) {
            return true;
        }
    }
    return false;
}

auto build_script_exists(ref<rstd::path::Path> script) -> BuildScriptResult<bool> {
    auto exists = rstd::fs::exists(script);
    if (exists.is_err()) {
        return build_script_io_failure<bool>(
            "inspect build script"_str, script, rstd::move(exists).unwrap_err());
    }
    if (! *exists) return Ok(false);
    auto metadata = rstd::fs::metadata(script);
    if (metadata.is_err()) {
        return build_script_io_failure<bool>(
            "inspect build script"_str, script, rstd::move(metadata).unwrap_err());
    }
    if (! metadata->is_file()) {
        return build_script_failure<bool>(
            rstd::format("build script '{}' is not a regular file", script));
    }
    return Ok(true);
}

auto package_has_script(const Vec<String>& packages, ref<str> package) noexcept -> bool {
    return packages.iter().any([&](auto candidate) {
        return (*candidate) == package;
    });
}

auto evaluate_build_scripts(cpp::PackageMetadata&                    metadata,
                            cpp::ResolvedNativeTargetPlan&           target_plan,
                            const BuildLayout&                       layout,
                            ref<str>                                 profile,
                            const Vec<String>&                       selected_packages,
                            const cpp::SourceTargetSelection&        selection,
                            const Option<BuildEventSink>&            observer,
                            const HostInfo&                          host,
                            const TargetInfo&                        target_info,
                            const ClangToolchain&                    toolchain,
                            lito::tools::ToolResolver&               resolver,
                            const ResolvedProcessEnvironment&        environment,
                            const lito::source::PackageSourceConfig& sources,
                            usize jobs) -> BuildScriptResult<BuildScriptDeclaration> {
    auto invocations       = Vec<BuildScriptInvocation>::make();
    auto scripted_packages = Vec<String>::make();
    auto workspace_script  = false;

    for (usize owner_index {}; owner_index < metadata.build_scripts.len(); ++owner_index) {
        const auto& owner = metadata.build_scripts[owner_index];
        if (owner.kind != cpp::BuildScriptOwnerKind::Workspace) continue;
        if (! rstd_try(build_script_exists(owner.script.as_path()))) continue;
        workspace_script = true;
        invocations.push(BuildScriptInvocation {
            .owner       = "workspace"_Str,
            .owner_index = owner_index,
            .script      = owner.script.clone(),
            .root        = owner.root.clone(),
            .package     = None(),
            .packages    = copy_package_names(selected_packages),
        });
    }
    for (const auto& package : selected_packages) {
        for (usize owner_index {}; owner_index < metadata.build_scripts.len(); ++owner_index) {
            const auto& owner = metadata.build_scripts[owner_index];
            if (owner.kind != cpp::BuildScriptOwnerKind::Package || owner.package.is_none() ||
                owner.package->as_str() != package.as_str()) {
                continue;
            }
            if (! rstd_try(build_script_exists(owner.script.as_path()))) break;
            auto packages = Vec<String>::make();
            packages.push(package.clone());
            invocations.push(BuildScriptInvocation {
                .owner       = rstd::format("package-{}", package.as_str()),
                .owner_index = owner_index,
                .script      = owner.script.clone(),
                .root        = owner.root.clone(),
                .package     = Some(package.clone()),
                .packages    = rstd::move(packages),
            });
            scripted_packages.push(package.clone());
            break;
        }
    }

    if (has_generated_inputs(metadata, selection) && ! workspace_script) {
        for (auto target : selection.target_order) {
            const auto& candidate = metadata.targets[target];
            auto        requires_script =
                ! candidate.usage.private_include_directory_requirements.is_empty();
            for (const auto& group : candidate.source_groups) {
                if (group.generated) requires_script = true;
            }
            if (! requires_script ||
                package_has_script(scripted_packages, candidate.id.package.as_str())) {
                continue;
            }
            auto expected = Option<ref<rstd::path::Path>> {};
            for (const auto& owner : metadata.build_scripts) {
                if (owner.kind == cpp::BuildScriptOwnerKind::Package && owner.package.is_some() &&
                    owner.package->as_str() == candidate.id.package.as_str()) {
                    expected = Some(owner.script.as_path());
                    break;
                }
            }
            if (expected.is_some()) {
                return build_script_failure<BuildScriptDeclaration>(rstd::format(
                    "generated build inputs for package '{}' require build script '{}'",
                    candidate.id.package.as_str(),
                    *expected));
            }
            return build_script_failure<BuildScriptDeclaration>(rstd::format(
                "generated build inputs for package '{}' have no local build-script owner",
                candidate.id.package.as_str()));
        }
    }

    auto declaration = BuildScriptDeclaration::make(observer);
    for (auto& invocation : invocations) {
        auto result = execute_build_script_invocation(metadata,
                                                      target_plan,
                                                      layout,
                                                      profile,
                                                      rstd::move(invocation),
                                                      declaration.output_registry(),
                                                      observer,
                                                      host,
                                                      target_info,
                                                      toolchain,
                                                      resolver,
                                                      environment,
                                                      sources,
                                                      jobs);
        if (result.is_err()) return Err(rstd::move(result).unwrap_err());
        declaration.push(rstd::move(result).unwrap());
    }
    rstd_try(declaration.validate_action_schedule());
    return Ok(rstd::move(declaration));
}

auto materialize_build_script_inputs(cpp::PackageMetadata&             metadata,
                                     Vec<cpp::CompileContext>&         contexts,
                                     const BuildLayout&                layout,
                                     const cpp::SourceTargetSelection& selection)
    -> BuildScriptResult<empty> {
    return materialize_generated_inputs(metadata, contexts, layout, selection);
}

} // namespace lito
