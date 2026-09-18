module;
#include <rstd/macro.hpp>

module lito.driver:build.generated.declaration;

import :build.generated;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::system;

namespace lito
{

auto ToolActionSession::tool(ref<str> alias) const -> BuildScriptResult<BuildScriptHandle> {
    auto identity = default_package_.is_some() ? tools_.identity(default_package_->as_str(), alias)
                                               : tools_.identity(alias);
    if (identity == nullptr) {
        return action_failure<BuildScriptHandle>(
            BuildToolActionError::UnknownTool(String::make(alias)));
    }
    return Ok(BuildScriptHandle { .identity = identity });
}

auto ToolActionSession::target(TargetScriptRequest request) const
    -> BuildScriptResult<BuildScriptHandle> {
    auto package = &request.package;
    auto kind    = &request.kind;
    auto name    = &request.name;
    auto allowed = false;
    for (const auto& candidate : packages_) {
        if (candidate == package->as_str()) allowed = true;
    }
    if (! allowed) {
        return action_request_failure<BuildScriptHandle>(
            rstd::format("package '{}' is not available to this build script", package->as_str()));
    }
    for (const auto& candidate : metadata_->targets) {
        if (candidate.id.package == package->as_str() &&
            lito::package::package_target_kind_name(candidate.id.kind) == kind->as_str() &&
            candidate.id.name == name->as_str()) {
            return Ok(BuildScriptHandle { .identity = rstd::addressof(candidate) });
        }
    }
    return action_request_failure<BuildScriptHandle>(rstd::format(
        "target '{}::{}::{}' is not selected", package->as_str(), kind->as_str(), name->as_str()));
}

auto ToolActionSession::external_dependency(BuildScriptHandle target_handle, ref<str> alias) const
    -> BuildScriptResult<BuildScriptHandle> {
    const cpp::ResolvedTarget* target = nullptr;
    for (const auto& candidate : metadata_->targets) {
        if (rstd::addressof(candidate) == target_handle.identity) {
            target = rstd::addressof(candidate);
            break;
        }
    }
    if (target == nullptr) {
        return action_request_failure<BuildScriptHandle>(
            "target handle does not belong to this build script"_str);
    }
    for (const auto& dependency : target->external_dependencies) {
        if (dependency.alias == alias) {
            return Ok(BuildScriptHandle { .identity = rstd::addressof(dependency) });
        }
    }
    return action_request_failure<BuildScriptHandle>(
        rstd::format("target '{}::{}' has no active external dependency '{}'",
                     target->id.package.as_str(),
                     target->id.name.as_str(),
                     alias));
}

auto ToolActionSession::external_source(BuildScriptHandle target_handle, ref<str> alias) const
    -> BuildScriptResult<BuildScriptHandle> {
    auto target = target_index(target_handle);
    if (target.is_none()) {
        return action_request_failure<BuildScriptHandle>(
            "target handle does not belong to this build script"_str);
    }
    for (const auto& source : metadata_->external_sources) {
        if (source.package_name == metadata_->targets[*target].id.package.as_str() &&
            source.name == alias) {
            return Ok(BuildScriptHandle { .identity = rstd::addressof(source) });
        }
    }
    return action_request_failure<BuildScriptHandle>(
        rstd::format("target '{}::{}' has no active external source '{}'",
                     metadata_->targets[*target].id.package.as_str(),
                     metadata_->targets[*target].id.name.as_str(),
                     alias));
}

auto ToolActionSession::external_source_file(BuildScriptHandle source_handle, String relative)
    -> BuildScriptResult<BuildScriptHandle> {
    auto source = external_source_root(source_handle);
    if (source == nullptr) {
        return action_request_failure<BuildScriptHandle>(
            "external source handle does not belong to this build script"_str);
    }
    auto normalized =
        rstd_try(normal_relative_path(rstd::move(relative), "external source file"_str));
    auto source_root = rstd::fs::canonicalize(source->root.as_path());
    if (source_root.is_err()) {
        return build_script_io_failure<BuildScriptHandle>("resolve external source root"_str,
                                                          source->root.as_path(),
                                                          rstd::move(source_root).unwrap_err());
    }
    auto requested = source->root.join(normalized.as_path());
    auto inspected = rstd::fs::symlink_metadata(requested.as_path());
    if (inspected.is_err() || inspected->is_symlink() || ! inspected->is_file()) {
        return action_failure<BuildScriptHandle>(BuildToolActionError::InvalidInput(
            rstd::move(requested), "external source path is not a regular file"_Str));
    }
    auto canonical = rstd::fs::canonicalize(requested.as_path());
    if (canonical.is_err()) {
        return build_script_io_failure<BuildScriptHandle>("resolve external source file"_str,
                                                          requested.as_path(),
                                                          rstd::move(canonical).unwrap_err());
    }
    if (canonical->as_path().strip_prefix(source_root->as_path()).is_none()) {
        return action_failure<BuildScriptHandle>(BuildToolActionError::InvalidInput(
            canonical->clone(), "external source path escapes source root"_Str));
    }
    auto owned  = Box<ResolvedExternalSourceFile>::make(ResolvedExternalSourceFile {
        .source   = source,
        .relative = rstd::move(normalized),
        .path     = rstd::move(canonical).unwrap(),
    });
    auto handle = BuildScriptHandle { .identity = rstd::addressof(*owned) };
    external_source_files_.push(rstd::move(owned));
    return Ok(handle);
}

auto ToolActionSession::external_tool(BuildScriptHandle dependency_handle, ref<str> name) const
    -> BuildScriptResult<BuildScriptHandle> {
    const cpp::ResolvedExternalDependency* dependency = nullptr;
    for (const auto& target : metadata_->targets) {
        for (const auto& candidate : target.external_dependencies) {
            if (rstd::addressof(candidate) == dependency_handle.identity) {
                dependency = rstd::addressof(candidate);
                break;
            }
        }
    }
    if (dependency == nullptr) {
        return action_request_failure<BuildScriptHandle>(
            "external dependency handle does not belong to this build script"_str);
    }
    for (const auto& tool : dependency->host_tools) {
        if (tool.name == name) {
            return Ok(BuildScriptHandle { .identity = rstd::addressof(tool) });
        }
    }
    return action_request_failure<BuildScriptHandle>(rstd::format(
        "external dependency '{}' has no host tool '{}'", dependency->alias.as_str(), name));
}

auto ToolActionSession::host_tool(BuildScriptHandle target_handle, ref<str> package, ref<str> name)
    -> BuildScriptResult<BuildScriptHandle> {
    auto target = target_index(target_handle);
    if (target.is_none()) {
        return action_request_failure<BuildScriptHandle>(
            "target handle does not belong to this build script"_str);
    }
    auto allowed = false;
    for (const auto& dependency : metadata_->targets[*target].host_tool_dependencies) {
        if (dependency.package == package) {
            allowed = true;
            break;
        }
    }
    if (! allowed) {
        return action_request_failure<BuildScriptHandle>(
            rstd::format("target '{}::{}' has no host-tool dependency '{}'",
                         metadata_->targets[*target].id.package.as_str(),
                         metadata_->targets[*target].id.name.as_str(),
                         package));
    }
    const cpp::ResolvedTarget* provider = nullptr;
    for (const auto& candidate : metadata_->targets) {
        if (candidate.id.package == package && candidate.id.name == name && candidate.host_tool) {
            provider = rstd::addressof(candidate);
            break;
        }
    }
    if (provider == nullptr) {
        return action_request_failure<BuildScriptHandle>(
            rstd::format("host-tool dependency '{}' has no target '{}'", package, name));
    }
    auto bound  = Box<PackageHostToolHandle>::make(PackageHostToolHandle {
        .package = metadata_->targets[*target].id.package.clone(),
        .target  = provider->id.clone(),
    });
    auto handle = BuildScriptHandle { .identity = rstd::addressof(*bound) };
    package_host_tool_handles_.push(rstd::move(bound));
    return Ok(handle);
}

auto ToolActionSession::external_dependency_info(BuildScriptHandle dependency_handle) const
    -> BuildScriptResult<ScriptDependencyInfo> {
    for (const auto& target : metadata_->targets) {
        for (const auto& dependency : target.external_dependencies) {
            if (rstd::addressof(dependency) != dependency_handle.identity) continue;
            auto targets = dependency.targets.iter()
                               .map([](auto target) {
                                   return target->name.clone();
                               })
                               .collect<Vec<String>>();
            return Ok(ScriptDependencyInfo { dependency.alias.clone(),
                                             dependency.provider.clone(),
                                             dependency.version.clone(),
                                             dependency.identity.clone(),
                                             rstd::move(targets) });
        }
    }
    return action_request_failure<ScriptDependencyInfo>(
        "external dependency handle does not belong to this build script"_str);
}

auto ToolActionSession::preprocessor_environment(BuildScriptHandle target_handle) const
    -> BuildScriptResult<ScriptPreprocessorInfo> {
    auto target = target_index(target_handle);
    if (target.is_none()) {
        return action_request_failure<ScriptPreprocessorInfo>(
            "target handle does not belong to this build script"_str);
    }
    if (*target >= target_plan_->contexts.len()) {
        return action_request_failure<ScriptPreprocessorInfo>(
            "target handle has no resolved compile environment"_str);
    }
    auto projection = toolchain_->build_tool_preprocessor_projection(
        target_plan_->contexts[*target], metadata_->targets[*target].root.as_path());
    if (projection.is_err()) {
        return Err(rstd::into<BuildScriptError>(rstd::move(projection).unwrap_err()));
    }
    return Ok(
        ScriptPreprocessorInfo { rstd::move(projection).unwrap(),
                                 String::make(target_info_->is_msvc() ? "msvc"_str : "unix"_str),
                                 target_info_->triple.clone() });
}

auto ToolActionSession::add_generated_source(BuildScriptHandle target_handle,
                                             BuildScriptHandle output_handle)
    -> BuildScriptResult<bool> {
    auto target = target_index(target_handle);
    if (target.is_none()) {
        return action_request_failure<bool>(
            "target handle does not belong to this build script"_str);
    }
    auto output = generated_output(output_handle);
    if (output == nullptr) {
        return action_request_failure<bool>(
            "generated output handle does not belong to this build script"_str);
    }
    if (metadata_->targets[*target].id.package != output->package.as_str()) {
        return action_request_failure<bool>(
            "generated output and target belong to different packages"_str);
    }
    if (metadata_->targets[*target].language != lito::manifest::PackageLanguage::Cpp) {
        return action_request_failure<bool>("generated compile sources require a C++ target"_str);
    }
    if (output->kind != GeneratedOutputKind::Source) {
        return action_request_failure<bool>(
            rstd::format("generated output '{}' is not declared or inferred as a source",
                         output->relative.as_path()));
    }
    if (output->availability == cpp::GeneratedSourceAvailability::BeforeScan &&
        ! lito::manifest::cpp_manifest_source(output->relative.as_path())) {
        return action_request_failure<bool>(
            rstd::format("generated compile source '{}' has an unsupported C++ extension",
                         output->relative.as_path()));
    }
    return Ok(cpp::add_generated_source(
        metadata_->targets[*target], output->relative.clone(), output->availability));
}

auto ToolActionSession::add_generated_include(BuildScriptHandle target_handle, String relative)
    -> BuildScriptResult<bool> {
    auto target = target_index(target_handle);
    if (target.is_none()) {
        return action_request_failure<bool>(
            "target handle does not belong to this build script"_str);
    }
    auto path = PathBuf::make();
    if (relative != "."_str) {
        path =
            rstd_try(normal_relative_path(rstd::move(relative), "generated include directory"_str));
    }
    auto generated = rstd_try(
        layout_->generated_package_directory(metadata_->targets[*target].id.package.as_str()));
    auto requested = generated.join(path.as_path());
    auto metadata_changed =
        cpp::add_generated_include_directory(metadata_->targets[*target], path.clone());
    auto context_changed =
        cpp::add_private_include_directory(target_plan_->contexts[*target], rstd::move(requested));
    return Ok(metadata_changed || context_changed);
}

auto ToolActionSession::add_generated_definition(BuildScriptHandle target_handle, String definition)
    -> BuildScriptResult<bool> {
    auto target = target_index(target_handle);
    if (target.is_none()) {
        return action_request_failure<bool>(
            "target handle does not belong to this build script"_str);
    }
    if (*target >= target_plan_->contexts.len()) {
        return action_request_failure<bool>(
            "target handle has no resolved compile environment"_str);
    }
    auto added = cpp::add_private_definition(
        metadata_->targets[*target], target_plan_->contexts[*target], rstd::move(definition));
    if (added.is_err()) {
        return action_request_failure<bool>(rstd::move(added).unwrap_err());
    }
    return Ok(*added);
}

auto ToolActionSession::add_generated_artifact(BuildScriptHandle          target_handle,
                                               BuildScriptHandle          output_handle,
                                               cpp::GeneratedArtifactRole role)
    -> BuildScriptResult<bool> {
    auto target = target_index(target_handle);
    if (target.is_none()) {
        return action_request_failure<bool>(
            "target handle does not belong to this build script"_str);
    }
    auto output = generated_output(output_handle);
    if (output == nullptr) {
        return action_request_failure<bool>(
            "generated output handle does not belong to this build script"_str);
    }
    if (metadata_->targets[*target].id.package != output->package.as_str()) {
        return action_request_failure<bool>(
            "generated output and target belong to different packages"_str);
    }
    auto contribution = cpp::add_generated_artifact(metadata_->targets[*target],
                                                    role,
                                                    output->relative.clone(),
                                                    output->action_identity.clone());
    auto identity     = cpp::add_generated_artifact_identity(target_plan_->contexts[*target],
                                                             output->action_identity.as_str());
    return Ok(contribution || identity);
}

auto ToolActionSession::write(WriteScriptRequest request) -> BuildScriptResult<ToolActionOutcome> {
    auto  package = &request.package;
    auto& inputs  = request.inputs;
    if (! package_is_selected(packages_, package->as_str())) {
        return action_request_failure<ToolActionOutcome>(
            rstd::format("generated output package '{}' is not available to this build script",
                         package->as_str()));
    }
    auto package_root = find_package_root(*metadata_, package->as_str());
    if (package_root.is_none()) {
        return action_request_failure<ToolActionOutcome>(
            rstd::format("generated output package '{}' has no source root", package->as_str()));
    }
    auto rendered      = rstd::move(request.content);
    auto input_records = Vec<ResolvedActionInput>::with_capacity(inputs.len());
    for (usize index {}; index < inputs.len(); ++index) {
        auto resolved = rstd_try(resolve_action_input(
            inputs[index], package->as_str(), *package_root, index, "lito.write.inputs"_str));
        auto marker   = rstd::format("@INPUT:{}@", index + usize(1));
        replace_all(rendered, marker.as_str(), resolved.path.as_path().to_string_lossy().as_str());
        auto xml_marker = rstd::format("@INPUT_XML:{}@", index + usize(1));
        auto escaped    = xml_text(resolved.path.as_path().to_string_lossy().as_str());
        replace_all(rendered, xml_marker.as_str(), escaped.as_str());
        input_records.push(rstd::move(resolved));
    }
    if (rendered.as_str().contains("@INPUT:"_str) ||
        rendered.as_str().contains("@INPUT_XML:"_str)) {
        return action_request_failure<ToolActionOutcome>(
            "lito.write.content contains an unresolved input marker"_str);
    }
    auto output = rstd::move(request.output);
    auto relative =
        rstd_try(normal_relative_path(rstd::move(output.path), "lito.write.output"_str));
    rstd_try(
        output_registry_->claim(package->as_str(), relative.as_path(), script_owner_.as_str()));
    auto identity_text = rstd::format("lito-write-action-v1\n{}\n{}\n{}\n{}",
                                      package->as_str(),
                                      profile_.as_str(),
                                      relative.as_path(),
                                      licrypto::sha256_hex(rendered.as_str().as_bytes()).as_str());
    identity_text.push_str("\noutput-kind:"_str);
    identity_text.push_str(generated_output_kind_name(output.kind));
    for (const auto& input : input_records) {
        identity_text.push_str("\ninput:"_str);
        identity_text.push_str(input.path.as_path().to_string_lossy().as_str());
        identity_text.push_ascii(':');
        identity_text.push_str(input.digest.as_str());
    }
    {
        auto loaded_identities = module_identities_.clone();
        rstd::slice_::sort_unstable(loaded_identities.as_mut_slice().as_mut_ref());
        for (const auto& module : loaded_identities) {
            identity_text.push_str("\nmodule:"_str);
            identity_text.push_str(module.as_str());
        }
    }
    auto outputs = Vec<PathBuf>::make();
    outputs.push(rstd::move(relative));
    auto output_kinds = Vec<GeneratedOutputKind>::make();
    output_kinds.emplace_back(output.kind);
    auto identity = licrypto::sha256_hex(identity_text.as_str());
    auto producer = actions_.len();
    actions_.push(RegisteredAction {
        .kind              = RegisteredActionKind::Write,
        .package           = package->clone(),
        .identity          = identity.clone(),
        .label             = "lito.write"_Str,
        .working_directory = PathBuf::from(*package_root),
        .inputs            = rstd::move(input_records),
        .outputs           = outputs.clone(),
        .content           = rstd::move(rendered),
    });
    actions_[producer].availability = resolve_action_availability(actions_[producer]);
    return Ok(
        make_outcome(false, package->as_str(), outputs, output_kinds, identity.as_str(), producer));
}

auto ToolActionSession::copy(CopyScriptRequest request) -> BuildScriptResult<ToolActionOutcome> {
    auto package = &request.package;
    auto input   = &request.input;
    if (! package_is_selected(packages_, package->as_str())) {
        return action_request_failure<ToolActionOutcome>(
            rstd::format("generated output package '{}' is not available to this build script",
                         package->as_str()));
    }
    auto package_root = find_package_root(*metadata_, package->as_str());
    if (package_root.is_none()) {
        return action_request_failure<ToolActionOutcome>(
            rstd::format("generated output package '{}' has no source root", package->as_str()));
    }
    auto resolved = rstd_try(resolve_action_input(
        *input, package->as_str(), *package_root, usize {}, "lito.copy.input"_str));
    auto output   = rstd::move(request.output);
    auto relative = rstd_try(normal_relative_path(rstd::move(output.path), "lito.copy.output"_str));
    rstd_try(
        output_registry_->claim(package->as_str(), relative.as_path(), script_owner_.as_str()));
    auto identity =
        licrypto::sha256_hex(rstd::format("lito-copy-action-v1\n{}\n{}\n{}:{}\n{}\noutput-kind:{}",
                                          package->as_str(),
                                          profile_.as_str(),
                                          resolved.path.as_path(),
                                          resolved.digest.as_str(),
                                          relative.as_path(),
                                          generated_output_kind_name(output.kind))
                                 .as_str());
    auto inputs = Vec<ResolvedActionInput>::make();
    inputs.push(rstd::move(resolved));
    auto outputs = Vec<PathBuf>::make();
    outputs.push(rstd::move(relative));
    auto output_kinds = Vec<GeneratedOutputKind>::make();
    output_kinds.emplace_back(output.kind);
    auto producer = actions_.len();
    actions_.push(RegisteredAction {
        .kind              = RegisteredActionKind::Copy,
        .package           = package->clone(),
        .identity          = identity.clone(),
        .label             = "lito.copy"_Str,
        .working_directory = PathBuf::from(*package_root),
        .inputs            = rstd::move(inputs),
        .outputs           = outputs.clone(),
    });
    actions_[producer].availability = resolve_action_availability(actions_[producer]);
    return Ok(
        make_outcome(false, package->as_str(), outputs, output_kinds, identity.as_str(), producer));
}

auto ToolActionSession::transform(TransformScriptRequest request)
    -> BuildScriptResult<ToolActionOutcome> {
    auto package = &request.package;
    auto kind    = &request.kind;
    auto input   = &request.input;
    if (kind->as_str() != "cpp-leading-preamble"_str) {
        return action_request_failure<ToolActionOutcome>(
            rstd::format("unknown lito.transform kind '{}'", kind->as_str()));
    }
    auto source = generated_output(*input);
    if (source == nullptr || source->package != package->as_str()) {
        return action_request_failure<ToolActionOutcome>(
            "lito.transform.input is not an output owned by the selected package"_str);
    }
    auto declared_outputs = rstd::move(request.outputs);
    if (declared_outputs.len() != usize(2)) {
        return action_request_failure<ToolActionOutcome>(
            "cpp-leading-preamble transform requires exactly two outputs"_str);
    }
    auto output_paths = Vec<PathBuf>::make();
    auto output_kinds = Vec<GeneratedOutputKind>::make();
    for (auto& output : declared_outputs) {
        auto relative =
            rstd_try(normal_relative_path(rstd::move(output.path), "transform output"_str));
        rstd_try(
            output_registry_->claim(package->as_str(), relative.as_path(), script_owner_.as_str()));
        output_paths.push(rstd::move(relative));
        output_kinds.emplace_back(output.kind);
    }
    auto generated     = rstd_try(layout_->create_generated_package_directory(package->as_str()));
    auto source_path   = generated.join(source->relative.as_path());
    auto identity_text = rstd::format("lito-transform-v2\n{}\n{}\n{}",
                                      kind->as_str(),
                                      source->action_identity.as_str(),
                                      profile_.as_str());
    for (usize index {}; index < output_paths.len(); ++index) {
        identity_text.push_str("\noutput:"_str);
        identity_text.push_str(output_paths[index].as_path().to_string_lossy().as_str());
        identity_text.push_ascii(':');
        identity_text.push_str(generated_output_kind_name(output_kinds[index]));
    }
    {
        auto loaded_identities = module_identities_.clone();
        rstd::slice_::sort_unstable(loaded_identities.as_mut_slice().as_mut_ref());
        for (const auto& module : loaded_identities) {
            identity_text.push_str("\nmodule:"_str);
            identity_text.push_str(module.as_str());
        }
    }
    auto inputs = Vec<ResolvedActionInput>::make();
    inputs.push(ResolvedActionInput {
        .path     = rstd::move(source_path),
        .digest   = source->action_identity.clone(),
        .producer = Some(usize(source->producer.to_primitive())),
    });
    auto identity = licrypto::sha256_hex(identity_text.as_str());
    auto producer = actions_.len();
    actions_.push(RegisteredAction {
        .kind              = RegisteredActionKind::CppLeadingPreamble,
        .package           = package->clone(),
        .identity          = identity.clone(),
        .label             = "lito.transform"_Str,
        .working_directory = PathBuf::from(*find_package_root(*metadata_, package->as_str())),
        .inputs            = rstd::move(inputs),
        .outputs           = output_paths.clone(),
    });
    actions_[producer].availability = resolve_action_availability(actions_[producer]);
    return Ok(make_outcome(
        false, package->as_str(), output_paths, output_kinds, identity.as_str(), producer));
}

auto ToolActionSession::finalize_module_identity() -> BuildScriptResult<empty> {
    auto closure = "lito-module-closure-v2"_Str;
    closure.push_str("\nentry:"_str);
    closure.push_str(rstd_try(action_file_digest(script_.as_path())).as_str());
    closure.push_str("\nhost:"_str);
    closure.push_str(build_host_api_identity);
    auto loaded_identities = module_identities_.clone();
    rstd::slice_::sort_unstable(loaded_identities.as_mut_slice().as_mut_ref());
    for (const auto& module : loaded_identities) {
        closure.push_str("\nmodule:"_str);
        closure.push_str(module.as_str());
    }
    return refresh_action_identities(closure.as_str(), None());
}

auto ToolActionSession::host_tool_targets() const -> Vec<lito::package::PackageTargetId> {
    auto       result = Vec<lito::package::PackageTargetId>::make();
    const auto append = [&result](const ResolvedActionTool& tool) {
        if (tool.target.is_none()) return;
        for (const auto& existing : result) {
            if (existing == *tool.target) return;
        }
        result.push(tool.target->clone());
    };
    for (const auto& action : actions_) {
        if (action.tool.is_some()) append(*action.tool);
        for (const auto& tool : action.tools) append(tool);
    }
    return result;
}

auto ToolActionSession::validate_action_schedule() const -> BuildScriptResult<empty> {
    for (const auto& output : generated_outputs_) {
        if (output->availability != cpp::GeneratedSourceAvailability::AfterScan ||
            output->kind == GeneratedOutputKind::Other) {
            continue;
        }
        auto extension = output->relative.as_path().extension();
        auto text      = extension.is_some() ? extension->to_str() : None();
        if (output->kind == GeneratedOutputKind::Header || text == Some("h"_str) ||
            text == Some("hpp"_str)) {
            return action_request_failure<empty>(
                rstd::format("generated header '{}' cannot be produced after source scan",
                             output->relative.as_path()));
        }
        if (text == Some("cppm"_str)) {
            return action_request_failure<empty>(rstd::format(
                "generated module interface source '{}' cannot be produced after source scan",
                output->relative.as_path()));
        }
    }
    return Ok(empty {});
}

auto ToolActionSession::bind_host_tools(const ResolvedPackageHostTools& tools)
    -> BuildScriptResult<empty> {
    auto       closure = "lito-package-host-tools-v1"_Str;
    const auto bind    = [&tools, &closure](ResolvedActionTool& tool) -> BuildScriptResult<empty> {
        if (tool.target.is_none()) return Ok(empty {});
        auto artifact = tools.get(*tool.target);
        if (artifact.is_none()) {
            return action_request_failure<empty>(
                rstd::format("host-tool target '{}' is not ready",
                             lito::package::package_target_id_text(*tool.target).as_str()));
        }
        auto digest     = rstd_try(action_file_digest((**artifact).executable.as_path()));
        tool.identity   = (**artifact).identity.clone();
        tool.digest     = rstd::move(digest);
        tool.executable = (**artifact).executable.clone();
        tool.artifact   = (**artifact).artifact;
        closure.push_str("\ntarget:"_str);
        closure.push_str(lito::package::package_target_id_text(*tool.target).as_str());
        closure.push_ascii(':');
        closure.push_str(tool.identity.as_str());
        closure.push_ascii(':');
        closure.push_str(tool.digest.as_str());
        return Ok(empty {});
    };
    auto bound = false;
    for (auto& action : actions_) {
        if (action.tool.is_some() && action.tool->target.is_some()) {
            rstd_try(bind(*action.tool));
            bound = true;
        }
        for (auto& tool : action.tools) {
            if (tool.target.is_none()) continue;
            rstd_try(bind(tool));
            bound = true;
        }
    }
    return bound ? refresh_action_identities(closure.as_str(),
                                             Some(cpp::GeneratedSourceAvailability::AfterScan))
                 : Ok(empty {});
}

void ToolActionSession::synchronize_generated_identities(cpp::PackageSpec& package,
                                                         cpp::PackagePlan& plan) const noexcept {
    for (const auto& replacement : identity_replacements_) {
        if (replacement.target >= package.targets.len() ||
            replacement.target >= plan.contexts.len()) {
            continue;
        }
        cpp::replace_generated_artifact_identity(package.targets[replacement.target],
                                                 plan.contexts[replacement.target],
                                                 replacement.previous.as_str(),
                                                 replacement.replacement.as_str());
    }
}

auto ToolActionSession::run(RunScriptRequest request) -> BuildScriptResult<ToolActionOutcome> {
    auto  package            = &request.package;
    auto  handle             = &request.tool;
    auto  inputs             = &request.inputs;
    auto& secondary_handles  = request.tools;
    auto& input_root_handles = request.input_roots;
    auto  primary_tool       = rstd_try(resolve_action_tool(*handle));
    if (primary_tool.package != package->as_str()) {
        return action_request_failure<ToolActionOutcome>(
            rstd::format("build-tool '{}' belongs to package '{}', not '{}'",
                         primary_tool.alias.as_str(),
                         primary_tool.package.as_str(),
                         package->as_str()));
    }
    auto secondary_tools = Vec<ResolvedActionTool>::with_capacity(secondary_handles.len());
    for (usize index {}; index < secondary_handles.len(); ++index) {
        auto tool = rstd_try(resolve_action_tool(secondary_handles[index]));
        if (tool.package != package->as_str()) {
            return action_request_failure<ToolActionOutcome>(
                rstd::format("build-tool '{}' belongs to package '{}', not '{}'",
                             tool.alias.as_str(),
                             tool.package.as_str(),
                             package->as_str()));
        }
        secondary_tools.push(rstd::move(tool));
    }
    auto input_roots = Vec<ResolvedActionInputRoot>::with_capacity(input_root_handles.len());
    for (usize index {}; index < input_root_handles.len(); ++index) {
        auto source = rstd_try(
            action_external_source(input_root_handles[index], "lito.run.input_roots"_str, index));
        if (source->package_name != package->as_str()) {
            return action_request_failure<ToolActionOutcome>(
                rstd::format("external source '{}' belongs to package '{}', not '{}'",
                             source->name.as_str(),
                             source->package_name.as_str(),
                             package->as_str()));
        }
        auto canonical = rstd::fs::canonicalize(source->root.as_path());
        if (canonical.is_err()) {
            return build_script_io_failure<ToolActionOutcome>("resolve build-tool input root"_str,
                                                              source->root.as_path(),
                                                              rstd::move(canonical).unwrap_err());
        }
        auto inspected = rstd::fs::symlink_metadata(canonical->as_path());
        if (inspected.is_err() || ! inspected->is_dir()) {
            return action_failure<ToolActionOutcome>(BuildToolActionError::InvalidInput(
                canonical->clone(), "build-tool input root is not a directory"_Str));
        }
        input_roots.push(ResolvedActionInputRoot {
            .source = source,
            .path   = rstd::move(canonical).unwrap(),
        });
    }
    auto package_root = find_package_root(*metadata_, package->as_str());
    if (package_root.is_none()) {
        return action_request_failure<ToolActionOutcome>(
            rstd::format("build-tool action package '{}' is not selected", package->as_str()));
    }
    auto cwd_text     = rstd::move(request.cwd);
    auto cwd_relative = PathBuf::make();
    if (cwd_text != "."_str) {
        cwd_relative =
            rstd_try(normal_relative_path(rstd::move(cwd_text), "build-tool action cwd"_str));
    }
    auto cwd_requested     = PathBuf::from(*package_root).join(cwd_relative.as_path());
    auto working_directory = rstd::fs::canonicalize(cwd_requested.as_path());
    if (working_directory.is_err()) {
        return build_script_io_failure<ToolActionOutcome>(
            "resolve build-tool action cwd"_str,
            cwd_requested.as_path(),
            rstd::move(working_directory).unwrap_err());
    }
    if (working_directory->as_path().strip_prefix(*package_root).is_none()) {
        return action_request_failure<ToolActionOutcome>(
            "build-tool action cwd escapes package root"_str);
    }
    auto arguments        = rstd::move(request.args);
    auto declared_outputs = rstd::move(request.outputs);
    if (arguments.is_empty() || inputs->is_empty() || declared_outputs.is_empty()) {
        return action_request_failure<ToolActionOutcome>(
            "build-tool action requires args, inputs, and outputs"_str);
    }
    auto input_records = Vec<ResolvedActionInput>::make();
    for (usize input_index {}; input_index < inputs->len(); ++input_index) {
        auto resolved = rstd_try(resolve_action_input((*inputs)[input_index],
                                                      package->as_str(),
                                                      working_directory->as_path(),
                                                      input_index,
                                                      "lito.run.inputs"_str));
        input_records.push(rstd::move(resolved));
    }
    auto output_paths = Vec<PathBuf>::make();
    auto output_kinds = Vec<GeneratedOutputKind>::make();
    for (auto& output : declared_outputs) {
        auto relative =
            rstd_try(normal_relative_path(rstd::move(output.path), "build-tool action output"_str));
        for (const auto& existing : output_paths) {
            if (existing.as_path() == relative.as_path()) {
                return action_failure<ToolActionOutcome>(BuildToolActionError::InvalidOutput(
                    relative.clone(), "path is declared more than once"_Str));
            }
        }
        rstd_try(
            output_registry_->claim(package->as_str(), relative.as_path(), script_owner_.as_str()));
        output_paths.push(rstd::move(relative));
        output_kinds.emplace_back(output.kind);
    }
    auto output_working_directory = Option<usize> {};
    if (request.output_cwd.is_some()) {
        auto selected = request.output_cwd;
        auto index    = usize(static_cast<size_t>(selected->to_primitive()));
        if (*selected < i64(1) || index > output_paths.len()) {
            return action_request_failure<ToolActionOutcome>(
                "lito.run.output_cwd must identify a declared output"_str);
        }
        output_working_directory = Some(index - usize(1));
        auto selected_parent     = output_paths[index - usize(1)].as_path().parent().unwrap();
        for (const auto& candidate : output_paths) {
            if (candidate.as_path().parent().unwrap() != selected_parent) {
                return action_request_failure<ToolActionOutcome>(
                    "lito.run.output_cwd requires all outputs to share one directory"_str);
            }
        }
    }
    auto depfile_output_index = Option<usize> {};
    auto depfile_roots        = Vec<PathBuf>::make();
    auto generated_root = rstd_try(layout_->create_generated_package_directory(package->as_str()));
    if (request.depfile.is_some()) {
        auto  output       = &request.depfile->output;
        auto& roots        = request.depfile->roots;
        auto  output_index = usize(static_cast<size_t>(output->to_primitive()));
        if (*output < i64(1) || output_index > output_paths.len()) {
            return action_request_failure<ToolActionOutcome>(
                "lito.run.depfile.output must identify a declared output"_str);
        }
        depfile_output_index = Some(output_index - usize(1));
        for (usize index {}; index < roots.len(); ++index) {
            auto path = PathBuf::from(roots[index].clone());
            if (! path.as_path().is_absolute()) {
                path = PathBuf::from(working_directory->as_path()).join(path.as_path());
            }
            auto canonical = rstd::fs::canonicalize(path.as_path());
            if (canonical.is_err()) {
                if (path.as_path().strip_prefix(generated_root.as_path()).is_some()) continue;
                return build_script_io_failure<ToolActionOutcome>(
                    "resolve build-tool depfile root"_str,
                    path.as_path(),
                    rstd::move(canonical).unwrap_err());
            }
            auto metadata = rstd::fs::symlink_metadata(canonical->as_path());
            if (metadata.is_err() || ! metadata->is_dir()) {
                return action_failure<ToolActionOutcome>(BuildToolActionError::InvalidInput(
                    canonical->clone(), "depfile root is not a directory"_Str));
            }
            depfile_roots.push(rstd::move(canonical).unwrap());
        }
    }
    for (const auto& root : input_roots) depfile_roots.push(root.path.clone());
    depfile_roots.push(PathBuf::from(*package_root));
    depfile_roots.push(generated_root.clone());
    auto script_digest = rstd_try(action_file_digest(script_.as_path()));
    auto identity_text = rstd::format("build-tool-action-v1\n{}\n{}\n{}\n{}\n{}",
                                      package->as_str(),
                                      profile_.as_str(),
                                      primary_tool.identity.as_str(),
                                      cwd_relative.as_path(),
                                      script_digest.as_str());
    identity_text.push_str("\nmodule-resolver=lito-restricted-v1"_str);
    {
        auto loaded_identities = module_identities_.clone();
        rstd::slice_::sort_unstable(loaded_identities.as_mut_slice().as_mut_ref());
        for (const auto& module : loaded_identities) {
            identity_text.push_str("\nmodule:"_str);
            identity_text.push_str(module.as_str());
        }
    }
    for (const auto& argument : arguments) {
        identity_text.push_ascii('\n');
        identity_text.push_str(argument.as_str());
    }
    for (const auto& tool : secondary_tools) {
        identity_text.push_str("\nsecondary-tool:"_str);
        identity_text.push_str(tool.alias.as_str());
        identity_text.push_ascii(':');
        identity_text.push_str(tool.identity.as_str());
        identity_text.push_ascii(':');
        identity_text.push_str(tool.digest.as_str());
        identity_text.push_ascii(':');
        identity_text.push_str(tool.executable.as_path().to_string_lossy().as_str());
    }
    for (const auto& root : input_roots) {
        identity_text.push_str("\ninput-root:"_str);
        identity_text.push_str(root.source->identity.as_str());
        identity_text.push_ascii(':');
        identity_text.push_str(root.path.as_path().to_string_lossy().as_str());
    }
    for (usize index {}; index < input_records.len(); ++index) {
        identity_text.push_ascii('\n');
        identity_text.push_str(input_records[index].path.as_path().to_string_lossy().as_str());
        identity_text.push_ascii(':');
        identity_text.push_str(input_records[index].digest.as_str());
    }
    for (usize index {}; index < output_paths.len(); ++index) {
        const auto& output = output_paths[index];
        identity_text.push_str("\noutput:"_str);
        identity_text.push_str(output.as_path().to_string_lossy().as_str());
        identity_text.push_ascii(':');
        identity_text.push_str(generated_output_kind_name(output_kinds[index]));
    }
    if (depfile_output_index.is_some()) {
        identity_text.push_str("\ndepfile:"_str);
        identity_text.push_str(rstd::format("{}", *depfile_output_index + usize(1)).as_str());
        for (const auto& root : depfile_roots) {
            identity_text.push_str("\ndepfile-root:"_str);
            identity_text.push_str(root.as_path().to_string_lossy().as_str());
        }
    }
    if (output_working_directory.is_some()) {
        identity_text.push_str("\noutput-cwd:"_str);
        identity_text.push_str(rstd::format("{}", *output_working_directory + usize(1)).as_str());
    }
    identity_text.push_ascii('\n');
    identity_text.push_str(environment_->child_path().to_string_lossy().as_str());
    auto identity = licrypto::sha256_hex(identity_text.as_str());
    auto producer = actions_.len();
    actions_.push(RegisteredAction {
        .kind                     = RegisteredActionKind::Process,
        .package                  = package->clone(),
        .identity                 = identity.clone(),
        .label                    = rstd::move(primary_tool.alias),
        .working_directory        = rstd::move(working_directory).unwrap(),
        .tool                     = Some(rstd::move(primary_tool)),
        .tools                    = rstd::move(secondary_tools),
        .input_roots              = rstd::move(input_roots),
        .arguments                = rstd::move(arguments),
        .inputs                   = rstd::move(input_records),
        .outputs                  = output_paths.clone(),
        .output_working_directory = output_working_directory,
        .depfile_output           = depfile_output_index,
        .depfile_roots            = rstd::move(depfile_roots),
    });
    actions_[producer].availability = resolve_action_availability(actions_[producer]);
    return Ok(make_outcome(
        false, package->as_str(), output_paths, output_kinds, identity.as_str(), producer));
}

auto ToolActionSession::action_availability(usize index) const noexcept
    -> cpp::GeneratedSourceAvailability {
    if (index >= actions_.len()) return cpp::GeneratedSourceAvailability::AfterScan;
    return actions_[index].availability;
}

auto ToolActionSession::resolve_action_availability(const RegisteredAction& action) const noexcept
    -> cpp::GeneratedSourceAvailability {
    if ((action.tool.is_some() && action.tool->target.is_some())) {
        return cpp::GeneratedSourceAvailability::AfterScan;
    }
    for (const auto& tool : action.tools) {
        if (tool.target.is_some()) return cpp::GeneratedSourceAvailability::AfterScan;
    }
    for (const auto& input : action.inputs) {
        if (input.producer.is_some() &&
            action_availability(*input.producer) == cpp::GeneratedSourceAvailability::AfterScan) {
            return cpp::GeneratedSourceAvailability::AfterScan;
        }
    }
    return cpp::GeneratedSourceAvailability::BeforeScan;
}

auto ToolActionSession::refresh_action_identities(
    ref<str>                                 closure,
    Option<cpp::GeneratedSourceAvailability> availability) -> BuildScriptResult<empty> {
    auto previous = Vec<String>::with_capacity(actions_.len());
    for (usize index {}; index < actions_.len(); ++index) {
        auto& action = actions_[index];
        previous.push(action.identity.clone());
        if (availability.is_some() && action_availability(index) != *availability) continue;
        action.identity = licrypto::sha256_hex(
            rstd::format("{}\n{}", action.identity.as_str(), closure).as_str());
    }
    for (auto& action : actions_) {
        for (auto& input : action.inputs) {
            if (input.producer.is_none() || *input.producer >= actions_.len()) continue;
            replace_all(input.digest,
                        previous[*input.producer].as_str(),
                        actions_[*input.producer].identity.as_str());
        }
    }
    for (auto& output : generated_outputs_) {
        if (output->producer >= actions_.len()) {
            return action_request_failure<empty>(
                "generated output refers to an unknown producer"_str);
        }
        output->action_identity = actions_[output->producer].identity.clone();
    }
    for (auto target = cpp::TargetId {}; target < metadata_->targets.len(); ++target) {
        for (usize action {}; action < actions_.len(); ++action) {
            if (cpp::replace_generated_artifact_identity(metadata_->targets[target],
                                                         target_plan_->contexts[target],
                                                         previous[action].as_str(),
                                                         actions_[action].identity.as_str())) {
                identity_replacements_.push(GeneratedIdentityReplacement {
                    .target      = target,
                    .previous    = previous[action].clone(),
                    .replacement = actions_[action].identity.clone(),
                });
            }
        }
    }
    return Ok(empty {});
}

auto ToolActionSession::resolve_action_tool(BuildScriptHandle handle) const
    -> BuildScriptResult<ResolvedActionTool> {
    auto resolved = tools_.from_identity(handle.identity);
    if (resolved.is_some()) {
        auto digest = rstd_try(action_file_digest((**resolved).executable.as_path()));
        return Ok(ResolvedActionTool {
            .package    = (**resolved).package.clone(),
            .alias      = (**resolved).alias.clone(),
            .identity   = (**resolved).identity.clone(),
            .digest     = rstd::move(digest),
            .executable = (**resolved).executable.clone(),
        });
    }
    for (const auto& target : metadata_->targets) {
        for (const auto& dependency : target.external_dependencies) {
            for (const auto& candidate : dependency.host_tools) {
                if (rstd::addressof(candidate) != handle.identity) continue;
                return Ok(ResolvedActionTool {
                    .package = target.id.package.clone(),
                    .alias =
                        rstd::format("{}:{}", dependency.alias.as_str(), candidate.name.as_str()),
                    .identity   = candidate.identity.clone(),
                    .digest     = candidate.digest.clone(),
                    .executable = candidate.executable.clone(),
                });
            }
        }
    }
    for (const auto& binding : package_host_tool_handles_) {
        if (rstd::addressof(*binding) != handle.identity) continue;
        return Ok(ResolvedActionTool {
            .package = binding->package.clone(),
            .alias   = rstd::format(
                "{}:{}", binding->target.package.as_str(), binding->target.name.as_str()),
            .identity =
                rstd::format("package-host-tool:{}",
                             lito::package::package_target_id_text(binding->target).as_str()),
            .target = Some(binding->target.clone()),
        });
    }
    return action_request_failure<ResolvedActionTool>(
        "tool handle does not belong to this build script"_str);
}

auto ToolActionSession::external_source_root(BuildScriptHandle handle) const noexcept
    -> const cpp::ExternalSourceRoot* {
    for (const auto& source : metadata_->external_sources) {
        if (rstd::addressof(source) == handle.identity) return rstd::addressof(source);
    }
    return nullptr;
}

auto ToolActionSession::external_source_file(BuildScriptHandle handle) const noexcept
    -> const ResolvedExternalSourceFile* {
    for (const auto& file : external_source_files_) {
        if (rstd::addressof(*file) == handle.identity) return rstd::addressof(*file);
    }
    return nullptr;
}

auto ToolActionSession::action_external_source(BuildScriptHandle handle,
                                               ref<str>          context,
                                               usize             index) const
    -> BuildScriptResult<const cpp::ExternalSourceRoot*> {
    auto source = external_source_root(handle);
    if (source == nullptr) {
        return action_request_failure<const cpp::ExternalSourceRoot*>(
            rstd::format("{}[{}] does not belong to this build script", context, index + usize(1)));
    }
    return Ok(source);
}

auto ToolActionSession::resolve_action_input(const DeclaredActionInput& input,
                                             ref<str>                   package,
                                             ref<rstd::path::Path>      working_directory,
                                             usize                      index,
                                             ref<str>                   context) const
    -> BuildScriptResult<ResolvedActionInput> {
    auto canonical = PathBuf::make();
    auto producer  = Option<usize> {};
    auto digest    = String::make();
    if (input.path.is_some()) {
        auto relative =
            rstd_try(normal_relative_path(input.path->clone(), "generated action input"_str));
        auto requested = PathBuf::from(working_directory).join(relative.as_path());
        auto resolved  = rstd::fs::canonicalize(requested.as_path());
        if (resolved.is_err()) {
            return build_script_io_failure<ResolvedActionInput>(
                "resolve generated action input"_str,
                requested.as_path(),
                rstd::move(resolved).unwrap_err());
        }
        auto package_root = find_package_root(*metadata_, package);
        if (package_root.is_none() || resolved->as_path().strip_prefix(*package_root).is_none()) {
            return action_failure<ResolvedActionInput>(BuildToolActionError::InvalidInput(
                requested.clone(), "path escapes package root"_Str));
        }
        canonical = rstd::move(resolved).unwrap();
    } else if (input.handle.identity != nullptr) {
        auto handle = input.handle;
        auto file   = external_source_file(handle);
        if (file != nullptr) {
            if (file->source->package_name != package) {
                return action_request_failure<ResolvedActionInput>(
                    rstd::format("{}[{}] is not an external source file owned by package '{}'",
                                 context,
                                 index + usize(1),
                                 package));
            }
            auto metadata = rstd::fs::symlink_metadata(file->path.as_path());
            if (metadata.is_err() || metadata->is_symlink() || ! metadata->is_file()) {
                return action_failure<ResolvedActionInput>(BuildToolActionError::InvalidInput(
                    file->path.clone(), "external source path is not a regular file"_Str));
            }
            auto file_digest = rstd_try(action_file_digest(file->path.as_path()));
            return Ok(ResolvedActionInput {
                .path     = file->path.clone(),
                .digest   = rstd::format("external:{}:{}:{}",
                                         file->source->identity.as_str(),
                                         file->relative.as_path(),
                                         file_digest.as_str()),
                .producer = None(),
            });
        }
        auto output = generated_output(handle);
        if (output == nullptr || output->package != package) {
            return action_request_failure<ResolvedActionInput>(rstd::format(
                "{}[{}] is not a source file or generated output owned by package '{}'",
                context,
                index + usize(1),
                package));
        }
        auto generated = rstd_try(layout_->generated_package_directory(package));
        canonical      = generated.join(output->relative.as_path());
        producer       = Some(usize(output->producer.to_primitive()));
        digest =
            rstd::format("{}:{}", output->action_identity.as_str(), output->relative.as_path());
    } else {
        return action_request_failure<ResolvedActionInput>(rstd::format(
            "{}[{}] must be a source path or generated output handle", context, index + usize(1)));
    }
    if (producer.is_some()) {
        return Ok(ResolvedActionInput {
            .path     = rstd::move(canonical),
            .digest   = rstd::move(digest),
            .producer = producer,
        });
    }
    auto metadata = rstd::fs::symlink_metadata(canonical.as_path());
    if (metadata.is_err() || metadata->is_symlink() || ! metadata->is_file()) {
        return action_failure<ResolvedActionInput>(BuildToolActionError::InvalidInput(
            canonical.clone(), "path is not a regular file"_Str));
    }
    digest = rstd_try(action_file_digest(canonical.as_path()));
    return Ok(ResolvedActionInput {
        .path     = rstd::move(canonical),
        .digest   = rstd::move(digest),
        .producer = None(),
    });
}

auto ToolActionSession::target_index(BuildScriptHandle handle) const noexcept
    -> Option<cpp::TargetId> {
    for (auto index = cpp::TargetId {}; index < metadata_->targets.len(); ++index) {
        if (rstd::addressof(metadata_->targets[index]) == handle.identity) return Some(index);
    }
    return None();
}

auto ToolActionSession::generated_output(BuildScriptHandle handle) const noexcept
    -> const GeneratedActionOutput* {
    for (const auto& output : generated_outputs_) {
        if (rstd::addressof(*output) == handle.identity) return rstd::addressof(*output);
    }
    return nullptr;
}

auto ToolActionSession::make_outcome(bool                            changed,
                                     ref<str>                        package,
                                     const Vec<PathBuf>&             outputs,
                                     const Vec<GeneratedOutputKind>& output_kinds,
                                     ref<str>                        identity,
                                     usize producer) -> ToolActionOutcome {
    auto handles = Vec<BuildScriptHandle>::with_capacity(outputs.len());
    for (usize index {}; index < outputs.len(); ++index) {
        const auto& output = outputs[index];
        auto        owned  = Box<GeneratedActionOutput>::make(GeneratedActionOutput {
            .package         = String::make(package),
            .relative        = output.clone(),
            .action_identity = String::make(identity),
            .producer        = producer,
            .kind            = output_kinds[index],
            .availability    = action_availability(producer),
        });
        handles.push(BuildScriptHandle { .identity = rstd::addressof(*owned) });
        generated_outputs_.push(rstd::move(owned));
    }
    return ToolActionOutcome {
        .changed = changed,
        .outputs = rstd::move(handles),
    };
}

void ToolActionSession::emit(BuildEventKind        kind,
                             ref<str>              target,
                             ref<rstd::path::Path> path) const noexcept {
    if (observer_->is_some() && (*observer_)->notify != nullptr)
        (*observer_)->notify((*observer_)->context, BuildEvent { kind, target, path });
}

} // namespace lito
