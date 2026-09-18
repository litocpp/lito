module;
#include <rstd/macro.hpp>

module lito.driver:script.build;

import rstd;
import lito.core;
import lito.cpp;
import lito.system;
import luato;
import :script.build_request;
import :build.script.declaration;
import :package.module_catalog;
import :script.runtime;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::system;

namespace lito
{

constexpr auto build_host_lua_api = R"lua(local native_external_source = lito._external_source
local native_external_source_file = lito._external_source_file

lito.external_source = function(target, alias)
  local handle = native_external_source(target, alias)
  return setmetatable({ _handle = handle }, {
    __index = function(_, key)
      if key == "file" then
        return function(path)
          return native_external_source_file(handle, path)
        end
      end
      return nil
    end,
  })
end
lito._external_source = nil
lito._external_source_file = nil
)lua"_str;

auto binding_error(BuildScriptError error) -> luato::Error {
    return luato::Error::binding(rstd::format("{}", error));
}

auto configure_binding(ConfigureSession& session, luato::Table request)
    -> luato::Result<luato::Table> {
    auto table = rstd::move(request);
    auto known = Vec<String>::make();
    known.push(String::make("package"_str));
    known.push(String::make("input"_str));
    known.push(String::make("output"_str));
    known.push(String::make("values"_str));
    auto checked = table.reject_unknown_fields(known.as_slice());
    if (checked.is_err()) return Err(rstd::move(checked).unwrap_err_unchecked());
    auto package = Result<String, luato::Error>(
        Err(luato::Error::binding(String::make("configure_file.package is required"_str))));
    if (table.contains("package"_str)) {
        package = table.required<String>("package"_str);
    } else {
        auto implicit = session.default_package();
        if (implicit.is_some()) package = Ok(String::make(*implicit));
    }
    auto input      = table.required<String>("input"_str);
    auto output     = table.required<String>("output"_str);
    auto raw_values = table.required<luato::Table>("values"_str);
    if (package.is_err()) return Err(rstd::move(package).unwrap_err_unchecked());
    if (input.is_err()) return Err(rstd::move(input).unwrap_err_unchecked());
    if (output.is_err()) return Err(rstd::move(output).unwrap_err_unchecked());
    if (raw_values.is_err()) return Err(rstd::move(raw_values).unwrap_err_unchecked());
    auto scalars = raw_values->scalar_entries();
    if (scalars.is_err()) return Err(rstd::move(scalars).unwrap_err_unchecked());

    auto values = ConfigureValues::make();
    for (auto& entry : *scalars) {
        if (! configure_placeholder_name_is_valid(entry.key.as_str())) {
            return Err(luato::Error::binding(
                rstd::format("{} is not a valid placeholder name", entry.path.as_str())));
        }
        if (entry.value.is_String()) {
            values.insert(rstd::move(entry.key),
                          ConfigureValue::from_string(entry.value.as_String().value.clone()));
        } else if (entry.value.is_Integer()) {
            values.insert(rstd::move(entry.key),
                          ConfigureValue::from_integer(entry.value.as_Integer().value));
        } else {
            values.insert(rstd::move(entry.key),
                          ConfigureValue::from_boolean(entry.value.as_Boolean().value));
        }
    }
    auto configured = session.configure(package->as_str(),
                                        rstd::move(input).unwrap_unchecked(),
                                        rstd::move(output).unwrap_unchecked(),
                                        values);
    if (configured.is_err()) return Err(binding_error(rstd::move(configured).unwrap_err()));
    auto result = luato::Table::make();
    auto path   = configured->output.as_path().to_str();
    if (path.is_none()) {
        return Err(luato::Error::binding(
            String::make("configure_file output path is not valid UTF-8"_str)));
    }
    auto inserted = result.set(String::make("output"_str), String::make(*path));
    if (inserted.is_err()) return Err(rstd::move(inserted).unwrap_err_unchecked());
    inserted = result.set(String::make("changed"_str),
                          configured->write != rstd::fs::WriteOutcome::Unchanged);
    if (inserted.is_err()) return Err(rstd::move(inserted).unwrap_err_unchecked());
    return Ok(rstd::move(result));
}

auto tool_binding(ToolActionSession& session, String alias) -> luato::Result<luato::OpaqueHandle> {
    auto tool = session.tool(alias.as_str());
    if (tool.is_err()) return Err(binding_error(rstd::move(tool).unwrap_err()));
    return Ok(luato::OpaqueHandle { tool->identity });
}

auto target_binding(ToolActionSession& session, luato::Table request)
    -> luato::Result<luato::OpaqueHandle> {
    auto target =
        session.target(rstd_try(parse_target_request(request, session.default_package())));
    if (target.is_err()) return Err(binding_error(rstd::move(target).unwrap_err()));
    return Ok(luato::OpaqueHandle { target->identity });
}

auto external_dependency_binding(ToolActionSession&  session,
                                 luato::OpaqueHandle target,
                                 String              alias) -> luato::Result<luato::OpaqueHandle> {
    auto dependency =
        session.external_dependency(BuildScriptHandle { target.identity }, alias.as_str());
    if (dependency.is_err()) return Err(binding_error(rstd::move(dependency).unwrap_err()));
    return Ok(luato::OpaqueHandle { dependency->identity });
}

auto external_source_binding(ToolActionSession& session, luato::OpaqueHandle target, String alias)
    -> luato::Result<luato::OpaqueHandle> {
    auto source = session.external_source(BuildScriptHandle { target.identity }, alias.as_str());
    if (source.is_err()) return Err(binding_error(rstd::move(source).unwrap_err()));
    return Ok(luato::OpaqueHandle { source->identity });
}

auto external_source_file_binding(ToolActionSession&  session,
                                  luato::OpaqueHandle source,
                                  String relative) -> luato::Result<luato::OpaqueHandle> {
    auto file =
        session.external_source_file(BuildScriptHandle { source.identity }, rstd::move(relative));
    if (file.is_err()) return Err(binding_error(rstd::move(file).unwrap_err()));
    return Ok(luato::OpaqueHandle { file->identity });
}

auto external_tool_binding(ToolActionSession& session, luato::OpaqueHandle dependency, String name)
    -> luato::Result<luato::OpaqueHandle> {
    auto tool = session.external_tool(BuildScriptHandle { dependency.identity }, name.as_str());
    if (tool.is_err()) return Err(binding_error(rstd::move(tool).unwrap_err()));
    return Ok(luato::OpaqueHandle { tool->identity });
}

auto host_tool_binding(ToolActionSession&  session,
                       luato::OpaqueHandle target,
                       String              package,
                       String              name) -> luato::Result<luato::OpaqueHandle> {
    auto tool =
        session.host_tool(BuildScriptHandle { target.identity }, package.as_str(), name.as_str());
    if (tool.is_err()) return Err(binding_error(rstd::move(tool).unwrap_err()));
    return Ok(luato::OpaqueHandle { tool->identity });
}

auto external_dependency_info_binding(ToolActionSession& session, luato::OpaqueHandle dependency)
    -> luato::Result<luato::Table> {
    auto information = session.external_dependency_info(BuildScriptHandle { dependency.identity });
    if (information.is_err()) return Err(binding_error(rstd::move(information).unwrap_err()));
    return dependency_info_table(rstd::move(information).unwrap());
}

auto preprocessor_environment_binding(ToolActionSession& session, luato::OpaqueHandle target)
    -> luato::Result<luato::Table> {
    auto environment = session.preprocessor_environment(BuildScriptHandle { target.identity });
    if (environment.is_err()) return Err(binding_error(rstd::move(environment).unwrap_err()));
    return preprocessor_info_table(rstd::move(environment).unwrap());
}

auto add_generated_source_binding(ToolActionSession&  session,
                                  luato::OpaqueHandle target,
                                  luato::OpaqueHandle output) -> luato::Result<bool> {
    auto added = session.add_generated_source(BuildScriptHandle { target.identity },
                                              BuildScriptHandle { output.identity });
    if (added.is_err()) return Err(binding_error(rstd::move(added).unwrap_err()));
    return Ok(*added);
}

auto add_generated_include_binding(ToolActionSession&  session,
                                   luato::OpaqueHandle target,
                                   String              relative) -> luato::Result<bool> {
    auto added =
        session.add_generated_include(BuildScriptHandle { target.identity }, rstd::move(relative));
    if (added.is_err()) return Err(binding_error(rstd::move(added).unwrap_err()));
    return Ok(*added);
}

auto add_generated_definition_binding(ToolActionSession&  session,
                                      luato::OpaqueHandle target,
                                      String              definition) -> luato::Result<bool> {
    auto added = session.add_generated_definition(BuildScriptHandle { target.identity },
                                                  rstd::move(definition));
    if (added.is_err()) return Err(binding_error(rstd::move(added).unwrap_err()));
    return Ok(*added);
}

auto add_generated_artifact_binding(ToolActionSession&         session,
                                    luato::OpaqueHandle        target,
                                    luato::OpaqueHandle        output,
                                    cpp::GeneratedArtifactRole role) -> luato::Result<bool> {
    auto added = session.add_generated_artifact(
        BuildScriptHandle { target.identity }, BuildScriptHandle { output.identity }, role);
    if (added.is_err()) return Err(binding_error(rstd::move(added).unwrap_err()));
    return Ok(*added);
}

auto run_binding(ToolActionSession& session, luato::Table request) -> luato::Result<luato::Table> {
    auto ran = session.run(rstd_try(parse_run_request(request, session.default_package())));
    if (ran.is_err()) return Err(binding_error(rstd::move(ran).unwrap_err()));
    auto result   = luato::Table::make();
    auto inserted = result.set(String::make("changed"_str), ran->changed);
    if (inserted.is_err()) return Err(rstd::move(inserted).unwrap_err_unchecked());
    auto values = Vec<luato::Value>::with_capacity(ran->outputs.len());
    for (auto output : ran->outputs) values.push(luato::Value::Opaque(output.identity));
    inserted = result.set(String::make("outputs"_str), luato::Array::from(rstd::move(values)));
    if (inserted.is_err()) return Err(rstd::move(inserted).unwrap_err_unchecked());
    return Ok(rstd::move(result));
}

auto write_binding(ToolActionSession& session, luato::Table request)
    -> luato::Result<luato::Table> {
    auto written = session.write(rstd_try(parse_write_request(request, session.default_package())));
    if (written.is_err()) return Err(binding_error(rstd::move(written).unwrap_err()));
    auto result   = luato::Table::make();
    auto inserted = result.set(String::make("changed"_str), written->changed);
    if (inserted.is_err()) return Err(rstd::move(inserted).unwrap_err_unchecked());
    if (written->outputs.len() != usize(1)) {
        return Err(luato::Error::binding(
            String::make("lito.write did not produce exactly one output"_str)));
    }
    inserted = result.set(String::make("output"_str),
                          luato::OpaqueHandle { written->outputs[usize {}].identity });
    if (inserted.is_err()) return Err(rstd::move(inserted).unwrap_err_unchecked());
    return Ok(rstd::move(result));
}

auto copy_binding(ToolActionSession& session, luato::Table request) -> luato::Result<luato::Table> {
    auto copied = session.copy(rstd_try(parse_copy_request(request, session.default_package())));
    if (copied.is_err()) return Err(binding_error(rstd::move(copied).unwrap_err()));
    if (copied->outputs.len() != usize(1)) {
        return Err(luato::Error::binding(
            String::make("lito.copy did not produce exactly one output"_str)));
    }
    auto result   = luato::Table::make();
    auto inserted = result.set(String::make("changed"_str), copied->changed);
    if (inserted.is_err()) return Err(rstd::move(inserted).unwrap_err_unchecked());
    inserted = result.set(String::make("output"_str),
                          luato::OpaqueHandle { copied->outputs[usize {}].identity });
    if (inserted.is_err()) return Err(rstd::move(inserted).unwrap_err_unchecked());
    return Ok(rstd::move(result));
}

auto transform_binding(ToolActionSession& session, luato::Table request)
    -> luato::Result<luato::Table> {
    auto transformed =
        session.transform(rstd_try(parse_transform_request(request, session.default_package())));
    if (transformed.is_err()) return Err(binding_error(rstd::move(transformed).unwrap_err()));
    auto values = Vec<luato::Value>::with_capacity(transformed->outputs.len());
    for (auto output : transformed->outputs) values.push(luato::Value::Opaque(output.identity));
    auto result   = luato::Table::make();
    auto inserted = result.set(String::make("changed"_str), transformed->changed);
    if (inserted.is_err()) return Err(rstd::move(inserted).unwrap_err_unchecked());
    inserted = result.set(String::make("outputs"_str), luato::Array::from(rstd::move(values)));
    if (inserted.is_err()) return Err(rstd::move(inserted).unwrap_err_unchecked());
    return Ok(rstd::move(result));
}

auto copy_package_names(const Vec<String>& packages) -> Vec<String> {
    auto copied = packages.iter()
                      .map([](auto package) {
                          return package->clone();
                      })
                      .collect<Vec<String>>();
    return copied;
}

auto execute_build_script_invocation(cpp::PackageMetadata&                    metadata,
                                     cpp::ResolvedNativeTargetPlan&           target_plan,
                                     const BuildLayout&                       layout,
                                     ref<str>                                 profile,
                                     BuildScriptInvocation                    invocation,
                                     BuildOutputRegistry&                     output_registry,
                                     const Option<BuildEventSink>&            observer,
                                     const HostInfo&                          host,
                                     const TargetInfo&                        target_info,
                                     const ClangToolchain&                    toolchain,
                                     lito::tools::ToolResolver&               resolver,
                                     const ResolvedProcessEnvironment&        environment,
                                     const lito::source::PackageSourceConfig& sources,
                                     usize                                    jobs)
    -> BuildScriptResult<DeclaredBuildScriptInvocation> {
    auto default_package = as<Clone>(invocation.package).clone();
    auto session         = ConfigureSession::create(metadata,
                                                    layout,
                                                    invocation.packages,
                                                    invocation.owner.as_str(),
                                                    as<Clone>(default_package).clone(),
                                                    invocation.owner.as_str(),
                                                    output_registry);
    if (session.is_err()) return Err(rstd::move(session).unwrap_err());
    auto configure = rstd::move(session).unwrap();
    auto resolved_tools =
        resolve_host_build_tools(metadata,
                                 invocation.packages,
                                 host,
                                 layout,
                                 resolver,
                                 environment,
                                 sources,
                                 jobs,
                                 observer.is_some() ? *observer : BuildEventSink {});
    if (resolved_tools.is_err()) {
        return Err(rstd::into<BuildScriptError>(rstd::move(resolved_tools).unwrap_err()));
    }
    auto tools           = rstd::move(resolved_tools).unwrap();
    auto action_packages = copy_package_names(invocation.packages);
    auto actions         = ToolActionSession(metadata,
                                             target_plan,
                                             layout,
                                             profile,
                                             invocation.script.as_path(),
                                             rstd::move(default_package),
                                             invocation.owner.clone(),
                                             rstd::move(action_packages),
                                             output_registry,
                                             toolchain,
                                             rstd::move(tools),
                                             target_info,
                                             environment,
                                             observer);

    auto state = luato::State::create(luato::StateOptions::build_script());
    if (state.is_err()) {
        return Err(BuildScriptError::Lua(String::make("create Lua state"_str),
                                         None(),
                                         rstd::move(state).unwrap_err_unchecked()));
    }
    auto        lua          = rstd::move(state).unwrap_unchecked();
    const auto& script_owner = metadata.build_scripts[invocation.owner_index];
    auto        module_sources =
        lito::package::ScriptModuleCatalog::make(invocation.root.as_path(),
                                                 script_owner.source_identity.as_str(),
                                                 script_owner.script_dependencies.as_slice(),
                                                 script_owner.script_packages.as_slice(),
                                                 lito::manifest::ScriptHostKind::Build);
    if (module_sources.is_err()) {
        return Err(rstd::into<BuildScriptError>(rstd::move(module_sources).unwrap_err()));
    }
    auto modules            = rstd::move(module_sources).unwrap();
    auto configured_modules = attach_script_modules(lua, modules);
    if (configured_modules.is_err()) {
        return Err(BuildScriptError::Lua(String::make("configure build script modules"_str),
                                         Some(invocation.script.clone()),
                                         rstd::move(configured_modules).unwrap_err_unchecked()));
    }
    auto module = luato::ModuleSpec(String::make("lito"_str));
    module.set(String::make("profile"_str), String::make(profile));
    auto project_root = metadata.root.as_path().to_str();
    if (project_root.is_none()) {
        return build_script_failure<DeclaredBuildScriptInvocation>(
            "project root is not valid UTF-8"_str);
    }
    module.set(String::make("project_root"_str), String::make(*project_root));
    if (invocation.package.is_some()) {
        auto package_root = invocation.root.as_path().to_str();
        if (package_root.is_none()) {
            return build_script_failure<DeclaredBuildScriptInvocation>(
                "package root is not valid UTF-8"_str);
        }
        auto generated =
            rstd_try(layout.create_generated_package_directory(invocation.package->as_str()));
        auto generated_root = generated.as_path().to_str();
        if (generated_root.is_none()) {
            return build_script_failure<DeclaredBuildScriptInvocation>(
                "generated root is not valid UTF-8"_str);
        }
        module.set(String::make("package"_str), invocation.package->clone());
        module.set(String::make("package_root"_str), String::make(*package_root));
        module.set(String::make("generated_root"_str), String::make(*generated_root));
    }
    module.function(String::make("configure_file"_str), [&configure](luato::Table request) {
        return configure_binding(configure, rstd::move(request));
    });
    module.function(String::make("tool"_str), [&actions](String alias) {
        return tool_binding(actions, rstd::move(alias));
    });
    module.function(String::make("target"_str), [&actions](luato::Table request) {
        return target_binding(actions, rstd::move(request));
    });
    module.function(String::make("external_dependency"_str),
                    [&actions](luato::OpaqueHandle target, String alias) {
                        return external_dependency_binding(
                            actions, rstd::move(target), rstd::move(alias));
                    });
    module.function(
        String::make("_external_source"_str), [&actions](luato::OpaqueHandle target, String alias) {
            return external_source_binding(actions, rstd::move(target), rstd::move(alias));
        });
    module.function(String::make("_external_source_file"_str),
                    [&actions](luato::OpaqueHandle source, String relative) {
                        return external_source_file_binding(
                            actions, rstd::move(source), rstd::move(relative));
                    });
    module.function(
        String::make("external_tool"_str), [&actions](luato::OpaqueHandle dependency, String name) {
            return external_tool_binding(actions, rstd::move(dependency), rstd::move(name));
        });
    module.function(String::make("host_tool"_str),
                    [&actions](luato::OpaqueHandle target, String package, String name) {
                        return host_tool_binding(
                            actions, rstd::move(target), rstd::move(package), rstd::move(name));
                    });
    module.function(String::make("external_dependency_info"_str),
                    [&actions](luato::OpaqueHandle dependency) {
                        return external_dependency_info_binding(actions, rstd::move(dependency));
                    });
    module.function(String::make("target_preprocessor_environment"_str),
                    [&actions](luato::OpaqueHandle target) {
                        return preprocessor_environment_binding(actions, rstd::move(target));
                    });
    module.function(String::make("target_add_generated_source"_str),
                    [&actions](luato::OpaqueHandle target, luato::OpaqueHandle output) {
                        return add_generated_source_binding(
                            actions, rstd::move(target), rstd::move(output));
                    });
    module.function(String::make("target_add_generated_include"_str),
                    [&actions](luato::OpaqueHandle target, String relative) {
                        return add_generated_include_binding(
                            actions, rstd::move(target), rstd::move(relative));
                    });
    module.function(String::make("target_add_generated_definition"_str),
                    [&actions](luato::OpaqueHandle target, String definition) {
                        return add_generated_definition_binding(
                            actions, rstd::move(target), rstd::move(definition));
                    });
    module.function(String::make("target_add_resource"_str),
                    [&actions](luato::OpaqueHandle target, luato::OpaqueHandle output) {
                        return add_generated_artifact_binding(
                            actions, target, output, cpp::GeneratedArtifactRole::Resource);
                    });
    module.function(String::make("target_add_metadata"_str),
                    [&actions](luato::OpaqueHandle target, luato::OpaqueHandle output) {
                        return add_generated_artifact_binding(
                            actions, target, output, cpp::GeneratedArtifactRole::Metadata);
                    });
    module.function(String::make("target_add_auxiliary_artifact"_str),
                    [&actions](luato::OpaqueHandle target, luato::OpaqueHandle output) {
                        return add_generated_artifact_binding(
                            actions, target, output, cpp::GeneratedArtifactRole::Auxiliary);
                    });
    module.function(String::make("run"_str), [&actions, &lua](luato::Table request) {
        actions.set_module_identities(loaded_script_identities(lua));
        return run_binding(actions, rstd::move(request));
    });
    module.function(String::make("write"_str), [&actions, &lua](luato::Table request) {
        actions.set_module_identities(loaded_script_identities(lua));
        return write_binding(actions, rstd::move(request));
    });
    module.function(String::make("copy"_str), [&actions](luato::Table request) {
        return copy_binding(actions, rstd::move(request));
    });
    module.function(String::make("transform"_str), [&actions, &lua](luato::Table request) {
        actions.set_module_identities(loaded_script_identities(lua));
        return transform_binding(actions, rstd::move(request));
    });
    auto native_module = luato::NativeRequireModuleSpec(
        String::make("@lito"_str), String::make(build_host_api_identity), rstd::move(module));
    native_module.set_global_alias(String::make("lito"_str));
    auto registered = lua.register_native_require_module(rstd::move(native_module));
    if (registered.is_err()) {
        return Err(BuildScriptError::Lua(String::make("register build script API"_str),
                                         None(),
                                         rstd::move(registered).unwrap_err_unchecked()));
    }
    auto initialized = lua.execute_entry(luato::LuaModuleSource {
        .logical_name = String::make("@lito/bootstrap"_str),
        .identity     = String::make(build_host_api_identity),
        .display_path = String::make("@lito/bootstrap.lua"_str),
        .bytes        = Vec<u8>::from(build_host_lua_api.as_bytes()),
    });
    if (initialized.is_err()) {
        return Err(BuildScriptError::Lua(String::make("initialize build script API"_str),
                                         None(),
                                         rstd::move(initialized).unwrap_err_unchecked()));
    }
    auto entry = modules.entry(invocation.script.as_path(), invocation.owner.as_str());
    if (entry.is_err()) {
        return Err(BuildScriptError::Lua(String::make("load build script"_str),
                                         Some(invocation.script.clone()),
                                         rstd::move(entry).unwrap_err_unchecked()));
    }
    auto executed = lua.execute_entry(rstd::move(entry).unwrap_unchecked());
    if (executed.is_err()) {
        return Err(BuildScriptError::Lua(String::make("execute build script"_str),
                                         Some(invocation.script.clone()),
                                         rstd::move(executed).unwrap_err_unchecked()));
    }
    actions.set_module_identities(loaded_script_identities(lua));
    auto finalized = actions.finalize_module_identity();
    if (finalized.is_err()) return Err(rstd::move(finalized).unwrap_err());
    return Ok(DeclaredBuildScriptInvocation {
        .actions   = rstd::move(actions),
        .configure = rstd::move(configure),
        .owner     = invocation.owner.clone(),
        .script    = invocation.script.clone(),
        .elapsed   = executed->elapsed,
    });
}

} // namespace lito
