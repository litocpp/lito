module;
#include <rstd/macro.hpp>
#include <initializer_list>

export module lito.install.script;

import rstd;
import luato;
import lito.error;
import lito.configure_template;
import lito.manifest.contract;
import lito.package.identity;
import lito.install.recipe_contract;
import lito.install.package_contract;
import lito.install.script_error_contract;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

template<typename T>
auto script_failure(String message) -> InstallScriptResult<T> {
    return Err(InstallScriptError::Message(rstd::move(message)));
}

template<typename T>
auto script_io_failure(ref<str> operation,
                       ref<rstd::path::Path> path,
                       rstd::io::error::Error source) -> InstallScriptResult<T> {
    return Err(InstallScriptError::Io(
        String::make(operation), PathBuf::from(path), rstd::move(source)));
}

auto recipe_path(String value, ref<str> context) -> luato::Result<PathBuf> {
    auto path = PathBuf::from(rstd::move(value));
    if (path.is_empty() || path.as_path().is_absolute() || path.as_path().has_root()) {
        return Err(luato::Error::binding(
            rstd::format("{} must be a non-empty relative path", context)));
    }
    auto components = path.as_path().components();
    for (auto component = components.next(); component.is_some(); component = components.next()) {
        if (component->is_normal()) continue;
        return Err(luato::Error::binding(
            rstd::format("{} contains a non-normal path component", context)));
    }
    if (path.as_path().starts_with(PathBuf::from(".lito"_str).as_path())) {
        return Err(luato::Error::binding(rstd::format("{} may not target .lito", context)));
    }
    return Ok(rstd::move(path));
}

auto known_fields(luato::Table& table, std::initializer_list<ref<str>> names)
    -> luato::Result<empty> {
    auto known = Vec<String>::with_capacity(usize(names.size()));
    for (auto name : names) known.push(String::make(name));
    return table.reject_unknown_fields(known.as_slice());
}

template<typename F>
auto parse_array(const luato::Table& root, ref<str> field, F&& parse) -> luato::Result<empty> {
    if (! root.contains(field)) return Ok(empty {});
    auto array = root.required<luato::Array>(field);
    if (array.is_err()) return Err(rstd::move(array).unwrap_err_unchecked());
    for (usize index {}; index < array->len(); ++index) {
        const auto& value = array->values()[index];
        if (! value.is_Table()) {
            return Err(luato::Error::binding(
                rstd::format("{}.{}[{}] must be a table", root.path(), field, index + usize(1))));
        }
        auto result = parse(value.as_Table().value->clone(), index);
        if (result.is_err()) return Err(rstd::move(result).unwrap_err_unchecked());
    }
    return Ok(empty {});
}

auto configure_values(luato::Table table) -> luato::Result<ConfigureValues> {
    auto scalars = table.scalar_entries();
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
    return Ok(rstd::move(values));
}

class InstallScriptSession {
public:
    explicit InstallScriptSession(const PackageInstallInput& package) : package_(package) {}

    auto install(luato::Table table) -> luato::Result<empty> {
        if (recipe_.is_some()) {
            return Err(luato::Error::binding(
                String::make("lito.install may only be called once"_str)));
        }
        rstd_try(known_fields(table,
                              { "artifacts"_str, "external_assets"_str, "files"_str,
                                "templates"_str, "inventories"_str }));
        auto recipe = InstallRecipe {
            .owner   = package_.name.clone(),
            .version = package_.version.clone(),
            .root    = package_.root.clone(),
            .source  = package_.source.clone(),
        };
        for (const auto& dependency : package_.runtime_dependencies) {
            recipe.runtime_dependencies.push(InstallRuntimeDependency {
                .name            = dependency.name.clone(),
                .source_identity = dependency.source_identity.clone(),
            });
        }
        rstd_try(parse_array(table, "artifacts"_str, [&](luato::Table item, usize) -> luato::Result<empty> {
            rstd_try(known_fields(item, { "target"_str, "destination"_str }));
            auto target = rstd_try(item.required<luato::Table>("target"_str));
            rstd_try(known_fields(target, { "kind"_str, "name"_str }));
            auto kind = rstd_try(target.required<String>("kind"_str));
            if (kind != "bin"_str) {
                return Err(luato::Error::binding(
                    String::make("install artifact target.kind must be 'bin'"_str)));
            }
            auto name = rstd_try(target.required<String>("name"_str));
            auto destination = rstd_try(recipe_path(
                rstd_try(item.required<String>("destination"_str)),
                "install artifact destination"_str));
            recipe.artifacts.push(InstallArtifactRecipe {
                .target = PackageTargetId {
                    .package = package_.name.clone(),
                    .kind    = PackageTargetKind::Binary,
                    .name    = rstd::move(name),
                },
                .destination = rstd::move(destination),
            });
            return Ok(empty {});
        }));
        rstd_try(parse_array(table, "external_assets"_str, [&](luato::Table item, usize) -> luato::Result<empty> {
            rstd_try(known_fields(item, { "dependency"_str, "set"_str, "destination"_str }));
            recipe.external_assets.push(InstallExternalAssetRecipe {
                .dependency = rstd_try(item.required<String>("dependency"_str)),
                .set        = rstd_try(item.required<String>("set"_str)),
                .destination = rstd_try(recipe_path(
                    rstd_try(item.required<String>("destination"_str)),
                    "external asset destination"_str)),
            });
            return Ok(empty {});
        }));
        rstd_try(parse_array(table, "files"_str, [&](luato::Table item, usize) -> luato::Result<empty> {
            rstd_try(known_fields(item, { "source"_str, "destination"_str }));
            recipe.files.push(InstallFileRecipe {
                .source = rstd_try(recipe_path(rstd_try(item.required<String>("source"_str)),
                                               "package file source"_str)),
                .destination = rstd_try(recipe_path(
                    rstd_try(item.required<String>("destination"_str)),
                    "package file destination"_str)),
            });
            return Ok(empty {});
        }));
        rstd_try(parse_array(table, "templates"_str, [&](luato::Table item, usize) -> luato::Result<empty> {
            rstd_try(known_fields(item, { "input"_str, "destination"_str, "values"_str }));
            recipe.templates.push(InstallTemplateRecipe {
                .input = rstd_try(recipe_path(rstd_try(item.required<String>("input"_str)),
                                              "install template input"_str)),
                .destination = rstd_try(recipe_path(
                    rstd_try(item.required<String>("destination"_str)),
                    "install template destination"_str)),
                .values = rstd_try(configure_values(
                    rstd_try(item.required<luato::Table>("values"_str)))),
            });
            return Ok(empty {});
        }));
        rstd_try(parse_array(table, "inventories"_str, [&](luato::Table item, usize) -> luato::Result<empty> {
            rstd_try(known_fields(item, { "destination"_str, "relative_to"_str }));
            recipe.inventories.push(InstallInventoryRecipe {
                .destination = rstd_try(recipe_path(
                    rstd_try(item.required<String>("destination"_str)),
                    "inventory destination"_str)),
                .relative_to = rstd_try(recipe_path(
                    rstd_try(item.required<String>("relative_to"_str)),
                    "inventory relative_to"_str)),
            });
            return Ok(empty {});
        }));
        recipe_ = Some(rstd::move(recipe));
        return Ok(empty {});
    }

    auto render(luato::Table table) -> InstallScriptResult<String> {
        auto checked = known_fields(table, { "input"_str, "values"_str });
        if (checked.is_err()) {
            return Err(InstallScriptError::Binding(
                package_.script->clone(), rstd::move(checked).unwrap_err_unchecked()));
        }
        auto input_text = table.required<String>("input"_str);
        auto raw_values = table.required<luato::Table>("values"_str);
        if (input_text.is_err()) {
            return Err(InstallScriptError::Binding(
                package_.script->clone(), rstd::move(input_text).unwrap_err_unchecked()));
        }
        if (raw_values.is_err()) {
            return Err(InstallScriptError::Binding(
                package_.script->clone(), rstd::move(raw_values).unwrap_err_unchecked()));
        }
        auto input = recipe_path(rstd::move(input_text).unwrap_unchecked(),
                                 "render_template.input"_str);
        if (input.is_err()) {
            return Err(InstallScriptError::Binding(
                package_.script->clone(), rstd::move(input).unwrap_err_unchecked()));
        }
        auto source = package_.root.join(input->as_path());
        auto metadata = rstd::fs::symlink_metadata(source.as_path());
        if (metadata.is_err()) {
            return script_io_failure<String>(
                "inspect install template"_str, source.as_path(), rstd::move(metadata).unwrap_err());
        }
        if (! metadata->is_file() || metadata->is_symlink()) {
            return script_failure<String>(
                rstd::format("install template '{}' is not a regular non-symlink file",
                             source.as_path()));
        }
        auto canonical = rstd::fs::canonicalize(source.as_path());
        if (canonical.is_err()) {
            return script_io_failure<String>(
                "resolve install template"_str, source.as_path(), rstd::move(canonical).unwrap_err());
        }
        if (canonical->as_path().strip_prefix(package_.root.as_path()).is_none()) {
            return script_failure<String>(
                rstd::format("install template '{}' escapes package root", source.as_path()));
        }
        auto contents = rstd::fs::read_to_string(canonical->as_path());
        if (contents.is_err()) {
            return script_io_failure<String>("read install template"_str,
                                              canonical->as_path(),
                                              rstd::move(contents).unwrap_err());
        }
        auto values = configure_values(rstd::move(raw_values).unwrap_unchecked());
        if (values.is_err()) {
            return Err(InstallScriptError::Binding(
                package_.script->clone(), rstd::move(values).unwrap_err_unchecked()));
        }
        auto rendered = render_configure_template(contents->as_str(), *values, canonical->as_path());
        if (rendered.is_err()) {
            return Err(InstallScriptError::Template(
                rstd::move(canonical).unwrap(), rstd::move(rendered).unwrap_err()));
        }
        return Ok(rstd::move(rendered).unwrap());
    }

    auto finish() -> InstallScriptResult<InstallRecipe> {
        if (recipe_.is_none()) {
            return script_failure<InstallRecipe>(rstd::format(
                "install script '{}' did not call lito.install", package_.script->as_path()));
        }
        return Ok(rstd::move(recipe_).unwrap());
    }

    auto defer_error(InstallScriptError error) -> void {
        deferred_error_ = Some(rstd::move(error));
    }

    auto take_deferred_error() -> Option<InstallScriptError> {
        return rstd::move(deferred_error_);
    }

private:
    const PackageInstallInput& package_;
    Option<InstallRecipe>      recipe_;
    Option<InstallScriptError> deferred_error_;
};

} // namespace lito

export namespace lito
{

auto execute_install_script(const PackageInstallInput& package,
                            const InstallScriptContext& context)
    -> InstallScriptResult<InstallRecipe> {
    if (package.script.is_none()) {
        return script_failure<InstallRecipe>(
            rstd::format("package '{}' has no install script", package.name.as_str()));
    }
    auto session = InstallScriptSession(package);
    auto state = luato::State::create(luato::StateOptions::base());
    if (state.is_err()) {
        return Err(InstallScriptError::Lua(
            package.script->clone(), rstd::move(state).unwrap_err_unchecked()));
    }
    auto lua = rstd::move(state).unwrap_unchecked();
    auto module = luato::ModuleSpec(String::make("lito"_str));
    module.set(String::make("package_name"_str), package.name.clone());
    module.set(String::make("package_version"_str), package.version.clone());
    module.set(String::make("profile"_str), context.profile.clone());
    module.set(String::make("target"_str), context.target.clone());
    module.set(String::make("target_arch"_str), context.target_arch.clone());
    module.add(luato::NativeFunctionSpec::make(
        String::make("install"_str), usize(1),
        [&session](luato::CallFrame& frame) -> luato::BindingResult {
            auto table = frame.required<luato::Table>(usize {});
            if (table.is_err()) return Err(rstd::move(table).unwrap_err_unchecked());
            auto installed = session.install(rstd::move(table).unwrap_unchecked());
            if (installed.is_err()) return Err(rstd::move(installed).unwrap_err_unchecked());
            return Ok(usize {});
        }));
    module.add(luato::NativeFunctionSpec::make(
        String::make("render_template"_str), usize(1),
        [&session](luato::CallFrame& frame) -> luato::BindingResult {
            auto table = frame.required<luato::Table>(usize {});
            if (table.is_err()) return Err(rstd::move(table).unwrap_err_unchecked());
            auto rendered = session.render(rstd::move(table).unwrap_unchecked());
            if (rendered.is_err()) {
                auto error = rstd::move(rendered).unwrap_err();
                auto text  = rstd::format("{}", error);
                session.defer_error(rstd::move(error));
                return Err(luato::Error::binding(rstd::move(text)));
            }
            frame.push(rstd::move(rendered).unwrap());
            return Ok(usize(1));
        }));
    auto registered = lua.register_module(rstd::move(module));
    if (registered.is_err()) {
        return Err(InstallScriptError::Lua(
            package.script->clone(), rstd::move(registered).unwrap_err_unchecked()));
    }
    auto executed = lua.execute_file(package.script->as_path());
    if (executed.is_err()) {
        auto deferred = session.take_deferred_error();
        if (deferred.is_some()) return Err(rstd::move(deferred).unwrap());
        auto error = rstd::move(executed).unwrap_err_unchecked();
        if (error.kind == luato::ErrorKind::Binding || error.kind == luato::ErrorKind::Type) {
            return Err(InstallScriptError::Binding(
                package.script->clone(), rstd::move(error)));
        }
        return Err(InstallScriptError::Lua(package.script->clone(), rstd::move(error)));
    }
    return session.finish();
}

} // namespace lito
