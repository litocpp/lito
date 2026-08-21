module;
#include <rstd/macro.hpp>

export module lito.driver:install.script;

import rstd;
import luato;
import lito.core;
import :install.recipe;
import :install.package;
import :install.script_error;
import :package.module_catalog;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

template<typename T>
auto script_failure(String message) -> InstallScriptResult<T> {
    return Err(InstallScriptError::Message(rstd::move(message)));
}

template<typename T>
auto script_io_failure(ref<str>               operation,
                       ref<rstd::path::Path>  path,
                       rstd::io::error::Error source) -> InstallScriptResult<T> {
    return Err(
        InstallScriptError::Io(String::make(operation), PathBuf::from(path), rstd::move(source)));
}

auto recipe_path(String value, ref<str> context) -> luato::Result<PathBuf> {
    auto path = PathBuf::from(rstd::move(value));
    if (path.is_empty() || path.as_path().is_absolute() || path.as_path().has_root()) {
        return Err(
            luato::Error::binding(rstd::format("{} must be a non-empty relative path", context)));
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

auto inventory_base_path(String value) -> luato::Result<PathBuf> {
    if (value.is_empty()) return Ok(PathBuf::from(rstd::move(value)));
    return recipe_path(rstd::move(value), "inventory relative_to"_str);
}

auto strip_mode(String value, ref<str> path) -> luato::Result<lito::artifact::StripMode> {
    if (value == "debuginfo"_str) return Ok(lito::artifact::StripMode::DebugInfo);
    if (value == "symbols"_str) return Ok(lito::artifact::StripMode::Symbols);
    return Err(luato::Error::binding(rstd::format("{} must be 'debuginfo' or 'symbols'", path)));
}

auto strip_files(const luato::Table& root) -> luato::Result<Vec<PathBuf>> {
    auto array = root.required<luato::Array>("files"_str);
    if (array.is_err()) return Err(rstd::move(array).unwrap_err_unchecked());
    if (array->len() == usize {}) {
        return Err(luato::Error::binding(rstd::format("{}.files must not be empty", root.path())));
    }
    auto result = Vec<PathBuf>::with_capacity(array->len());
    for (usize index {}; index < array->len(); ++index) {
        const auto& value = array->values()[index];
        if (! value.is_String()) {
            return Err(luato::Error::binding(
                rstd::format("{}.files[{}] must be a string", root.path(), index + usize(1))));
        }
        auto path =
            rstd_try(recipe_path(value.as_String().value.clone(), "external asset strip file"_str));
        for (const auto& existing : result) {
            if (existing.as_path() == path.as_path()) {
                return Err(luato::Error::binding(rstd::format(
                    "{}.files[{}] repeats '{}'", root.path(), index + usize(1), path.as_path())));
            }
        }
        result.push(rstd::move(path));
    }
    return Ok(rstd::move(result));
}

auto known_fields(luato::Table& table, initializer_list<ref<str>> names) -> luato::Result<empty> {
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

auto environment_value(String name) -> luato::Result<Option<String>> {
    if (name.is_empty()) {
        return Err(
            luato::Error::binding(String::make("environment variable name must not be empty"_str)));
    }
    for (auto byte : name.as_str().as_bytes()) {
        if (byte == u8('=') || byte == u8 {}) {
            return Err(luato::Error::binding(rstd::format(
                "environment variable name '{}' contains an invalid character", name)));
        }
    }
    auto value = rstd::env::var_os(name.as_str());
    if (value.is_none()) return Ok(None());
    auto text = rstd::move(value).unwrap().into_string();
    if (text.is_err()) {
        return Err(luato::Error::binding(
            rstd::format("environment variable '{}' is not valid UTF-8", name)));
    }
    return Ok(Some(rstd::move(text).unwrap()));
}

class InstallScriptSession {
public:
    explicit InstallScriptSession(const PackageInstallInput& package): package_(package) {}

    auto install(luato::Table table) -> luato::Result<empty> {
        if (recipe_.is_some()) {
            return Err(
                luato::Error::binding(String::make("lito.install may only be called once"_str)));
        }
        rstd_try(known_fields(table,
                              { "artifacts"_str,
                                "target_runtimes"_str,
                                "external_assets"_str,
                                "files"_str,
                                "templates"_str,
                                "inventories"_str }));
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
        rstd_try(parse_array(
            table, "artifacts"_str, [&](luato::Table item, usize) -> luato::Result<empty> {
                rstd_try(
                    known_fields(item, { "target"_str, "destination"_str, "runtime_search"_str }));
                auto target = rstd_try(item.required<luato::Table>("target"_str));
                rstd_try(known_fields(target, { "kind"_str, "name"_str }));
                auto kind        = rstd_try(target.required<String>("kind"_str));
                auto target_kind = lito::package::PackageTargetKind::Binary;
                if (kind == "lib"_str) {
                    target_kind = lito::package::PackageTargetKind::Library;
                } else if (kind != "bin"_str) {
                    return Err(luato::Error::binding(
                        String::make("install artifact target.kind must be 'bin' or 'lib'"_str)));
                }
                auto name = rstd_try(target.required<String>("name"_str));
                auto destination =
                    rstd_try(recipe_path(rstd_try(item.required<String>("destination"_str)),
                                         "install artifact destination"_str));
                auto runtime_search = Vec<InstallArtifactRecipe::RuntimeSearchReference>::make();
                rstd_try(parse_array(
                    item,
                    "runtime_search"_str,
                    [&](luato::Table reference, usize index) -> luato::Result<empty> {
                        rstd_try(known_fields(reference, { "external_asset"_str }));
                        auto external =
                            rstd_try(reference.required<luato::Table>("external_asset"_str));
                        rstd_try(known_fields(external, { "dependency"_str, "set"_str }));
                        auto resolved = InstallArtifactRecipe::RuntimeSearchReference {
                            .dependency = rstd_try(external.required<String>("dependency"_str)),
                            .set        = rstd_try(external.required<String>("set"_str)),
                        };
                        for (const auto& prior : runtime_search) {
                            if (prior.dependency == resolved.dependency.as_str() &&
                                prior.set == resolved.set.as_str()) {
                                return Err(luato::Error::binding(rstd::format(
                                    "{}.runtime_search[{}] repeats external asset '{}:{}'",
                                    item.path(),
                                    index + usize(1),
                                    resolved.dependency.as_str(),
                                    resolved.set.as_str())));
                            }
                        }
                        runtime_search.push(rstd::move(resolved));
                        return Ok(empty {});
                    }));
                if (item.contains("runtime_search"_str) && runtime_search.is_empty()) {
                    return Err(luato::Error::binding(
                        rstd::format("{}.runtime_search must not be empty", item.path())));
                }
                recipe.artifacts.push(InstallArtifactRecipe {
                    .target =
                        lito::package::PackageTargetId {
                            .package = package_.name.clone(),
                            .kind    = target_kind,
                            .name    = rstd::move(name),
                        },
                    .destination    = rstd::move(destination),
                    .runtime_search = rstd::move(runtime_search),
                });
                return Ok(empty {});
            }));
        rstd_try(parse_array(
            table, "target_runtimes"_str, [&](luato::Table item, usize) -> luato::Result<empty> {
                rstd_try(known_fields(item, { "name"_str, "destination"_str }));
                auto name = rstd_try(item.required<String>("name"_str));
                if (name.is_empty()) {
                    return Err(luato::Error::binding(
                        String::make("target runtime name must not be empty"_str)));
                }
                for (const auto& existing : recipe.target_runtimes) {
                    if (existing.name == name.as_str()) {
                        return Err(luato::Error::binding(
                            rstd::format("target runtime '{}' is declared more than once", name)));
                    }
                }
                recipe.target_runtimes.push(InstallTargetRuntimeRecipe {
                    .name = rstd::move(name),
                    .destination =
                        rstd_try(recipe_path(rstd_try(item.required<String>("destination"_str)),
                                             "target runtime destination"_str)),
                });
                return Ok(empty {});
            }));
        rstd_try(parse_array(
            table, "external_assets"_str, [&](luato::Table item, usize) -> luato::Result<empty> {
                rstd_try(known_fields(
                    item, { "dependency"_str, "set"_str, "destination"_str, "strip"_str }));
                auto strip = Option<InstallStripRecipe> {};
                if (item.contains("strip"_str)) {
                    auto raw = rstd_try(item.required<luato::Table>("strip"_str));
                    rstd_try(known_fields(raw, { "mode"_str, "files"_str }));
                    auto mode_value = rstd_try(raw.required<String>("mode"_str));
                    auto mode = rstd_try(strip_mode(rstd::move(mode_value),
                                                    rstd::format("{}.mode", raw.path()).as_str()));
                    strip     = Some(InstallStripRecipe {
                        .mode  = mode,
                        .files = rstd_try(strip_files(raw)),
                    });
                }
                recipe.external_assets.push(InstallExternalAssetRecipe {
                    .dependency = rstd_try(item.required<String>("dependency"_str)),
                    .set        = rstd_try(item.required<String>("set"_str)),
                    .destination =
                        rstd_try(recipe_path(rstd_try(item.required<String>("destination"_str)),
                                             "external asset destination"_str)),
                    .strip = rstd::move(strip),
                });
                return Ok(empty {});
            }));
        rstd_try(
            parse_array(table, "files"_str, [&](luato::Table item, usize) -> luato::Result<empty> {
                rstd_try(known_fields(item, { "source"_str, "destination"_str }));
                recipe.files.push(InstallFileRecipe {
                    .source = rstd_try(recipe_path(rstd_try(item.required<String>("source"_str)),
                                                   "package file source"_str)),
                    .destination =
                        rstd_try(recipe_path(rstd_try(item.required<String>("destination"_str)),
                                             "package file destination"_str)),
                });
                return Ok(empty {});
            }));
        rstd_try(parse_array(
            table, "templates"_str, [&](luato::Table item, usize) -> luato::Result<empty> {
                rstd_try(known_fields(item, { "input"_str, "destination"_str, "values"_str }));
                recipe.templates.push(InstallTemplateRecipe {
                    .input = rstd_try(recipe_path(rstd_try(item.required<String>("input"_str)),
                                                  "install template input"_str)),
                    .destination =
                        rstd_try(recipe_path(rstd_try(item.required<String>("destination"_str)),
                                             "install template destination"_str)),
                    .values = rstd_try(
                        configure_values(rstd_try(item.required<luato::Table>("values"_str)))),
                });
                return Ok(empty {});
            }));
        rstd_try(parse_array(
            table, "inventories"_str, [&](luato::Table item, usize) -> luato::Result<empty> {
                rstd_try(known_fields(item, { "destination"_str, "relative_to"_str }));
                recipe.inventories.push(InstallInventoryRecipe {
                    .destination =
                        rstd_try(recipe_path(rstd_try(item.required<String>("destination"_str)),
                                             "inventory destination"_str)),
                    .relative_to = rstd_try(
                        inventory_base_path(rstd_try(item.required<String>("relative_to"_str)))),
                });
                return Ok(empty {});
            }));
        recipe_ = Some(rstd::move(recipe));
        return Ok(empty {});
    }

    auto render(luato::Table table) -> InstallScriptResult<String> {
        auto checked = known_fields(table, { "input"_str, "values"_str });
        if (checked.is_err()) {
            return Err(InstallScriptError::Binding(package_.script->clone(),
                                                   rstd::move(checked).unwrap_err_unchecked()));
        }
        auto input_text = table.required<String>("input"_str);
        auto raw_values = table.required<luato::Table>("values"_str);
        if (input_text.is_err()) {
            return Err(InstallScriptError::Binding(package_.script->clone(),
                                                   rstd::move(input_text).unwrap_err_unchecked()));
        }
        if (raw_values.is_err()) {
            return Err(InstallScriptError::Binding(package_.script->clone(),
                                                   rstd::move(raw_values).unwrap_err_unchecked()));
        }
        auto input =
            recipe_path(rstd::move(input_text).unwrap_unchecked(), "render_template.input"_str);
        if (input.is_err()) {
            return Err(InstallScriptError::Binding(package_.script->clone(),
                                                   rstd::move(input).unwrap_err_unchecked()));
        }
        auto source   = package_.root.join(input->as_path());
        auto metadata = rstd::fs::symlink_metadata(source.as_path());
        if (metadata.is_err()) {
            return script_io_failure<String>("inspect install template"_str,
                                             source.as_path(),
                                             rstd::move(metadata).unwrap_err());
        }
        if (! metadata->is_file() || metadata->is_symlink()) {
            return script_failure<String>(rstd::format(
                "install template '{}' is not a regular non-symlink file", source.as_path()));
        }
        auto canonical = rstd::fs::canonicalize(source.as_path());
        if (canonical.is_err()) {
            return script_io_failure<String>("resolve install template"_str,
                                             source.as_path(),
                                             rstd::move(canonical).unwrap_err());
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
            return Err(InstallScriptError::Binding(package_.script->clone(),
                                                   rstd::move(values).unwrap_err_unchecked()));
        }
        auto rendered =
            render_configure_template(contents->as_str(), *values, canonical->as_path());
        if (rendered.is_err()) {
            return Err(InstallScriptError::Template(rstd::move(canonical).unwrap(),
                                                    rstd::move(rendered).unwrap_err()));
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

    auto take_deferred_error() -> Option<InstallScriptError> { return rstd::move(deferred_error_); }

private:
    const PackageInstallInput& package_;
    Option<InstallRecipe>      recipe_;
    Option<InstallScriptError> deferred_error_;
};

} // namespace lito

export namespace lito
{

auto execute_install_script(const PackageInstallInput& package, const InstallScriptContext& context)
    -> InstallScriptResult<InstallRecipe> {
    if (package.script.is_none()) {
        return script_failure<InstallRecipe>(
            rstd::format("package '{}' has no install script", package.name.as_str()));
    }
    auto session = InstallScriptSession(package);
    auto state   = luato::State::create(luato::StateOptions::build_script());
    if (state.is_err()) {
        return Err(InstallScriptError::Lua(package.script->clone(),
                                           rstd::move(state).unwrap_err_unchecked()));
    }
    auto lua = rstd::move(state).unwrap_unchecked();
    auto catalog =
        lito::package::ScriptModuleCatalog::make(package.root.as_path(),
                                                 package.source.identity.as_str(),
                                                 package.script_dependencies.as_slice(),
                                                 package.script_packages.as_slice(),
                                                 lito::manifest::ScriptHostKind::Install);
    if (catalog.is_err()) {
        return Err(InstallScriptError::Message(rstd::format(
            "cannot configure install script modules: {}", rstd::move(catalog).unwrap_err())));
    }
    auto modules    = rstd::move(catalog).unwrap();
    auto configured = lua.set_module_resolver(luato::ModuleResolverSpec::make(
        [&modules](luato::ModuleRequest request) -> luato::Result<luato::LuaModuleSource> {
            return modules.resolve(rstd::move(request));
        }));
    if (configured.is_err()) {
        return Err(InstallScriptError::Lua(package.script->clone(),
                                           rstd::move(configured).unwrap_err_unchecked()));
    }
    auto module = luato::ModuleSpec(String::make("lito"_str));
    module.set(String::make("package_name"_str), package.name.clone());
    module.set(String::make("package_version"_str), package.version.clone());
    module.set(String::make("profile"_str), context.profile.clone());
    module.set(String::make("target"_str), context.target.clone());
    module.set(String::make("target_arch"_str), context.target_arch.clone());
    module.add(luato::NativeFunctionSpec::make(
        String::make("install"_str),
        usize(1),
        [&session](luato::CallFrame& frame) -> luato::BindingResult {
            auto table = frame.required<luato::Table>(usize {});
            if (table.is_err()) return Err(rstd::move(table).unwrap_err_unchecked());
            auto installed = session.install(rstd::move(table).unwrap_unchecked());
            if (installed.is_err()) return Err(rstd::move(installed).unwrap_err_unchecked());
            return Ok(usize {});
        }));
    module.add(luato::NativeFunctionSpec::make(
        String::make("render_template"_str),
        usize(1),
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
    module.add(luato::NativeFunctionSpec::make(
        String::make("env"_str), usize(1), [](luato::CallFrame& frame) -> luato::BindingResult {
            auto name = frame.required<String>(usize {});
            if (name.is_err()) return Err(rstd::move(name).unwrap_err_unchecked());
            auto value = environment_value(rstd::move(name).unwrap_unchecked());
            if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
            auto resolved = rstd::move(value).unwrap_unchecked();
            if (resolved.is_none()) {
                frame.push_nil();
            } else {
                frame.push(rstd::move(resolved).unwrap());
            }
            return Ok(usize(1));
        }));
    auto native_module =
        luato::NativeRequireModuleSpec(String::make("@lito"_str),
                                       String::make("lito:install-host-api:v1"_str),
                                       rstd::move(module));
    native_module.set_global_alias(String::make("lito"_str));
    auto registered = lua.register_native_require_module(rstd::move(native_module));
    if (registered.is_err()) {
        return Err(InstallScriptError::Lua(package.script->clone(),
                                           rstd::move(registered).unwrap_err_unchecked()));
    }
    auto entry = modules.entry(package.script->as_path(), package.name.as_str());
    if (entry.is_err()) {
        return Err(InstallScriptError::Lua(package.script->clone(),
                                           rstd::move(entry).unwrap_err_unchecked()));
    }
    auto executed = lua.execute_entry(rstd::move(entry).unwrap_unchecked());
    if (executed.is_err()) {
        auto deferred = session.take_deferred_error();
        if (deferred.is_some()) return Err(rstd::move(deferred).unwrap());
        auto error = rstd::move(executed).unwrap_err_unchecked();
        if (error.kind == luato::ErrorKind::Binding || error.kind == luato::ErrorKind::Type) {
            return Err(InstallScriptError::Binding(package.script->clone(), rstd::move(error)));
        }
        return Err(InstallScriptError::Lua(package.script->clone(), rstd::move(error)));
    }
    return session.finish();
}

} // namespace lito
