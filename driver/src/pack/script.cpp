module;
#include <rstd/macro.hpp>

module lito.driver;

import rstd;
import luato;
import lito.core;
import :command.error;
import :pack.script;
import :script.runtime;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace
{

using lito::PackArgument;
using lito::PackRecipe;
using lito::manifest::package_name_is_valid;
using lito::package::PackageTargetKind;

auto parse_pack_artifact(ref<str> selector, ref<str> package_name) -> luato::Result<PackArgument> {
    auto parts = selector.split_once(":"_str);
    if (parts.is_none() || ! package_name_is_valid(parts->get<1>()) ||
        (parts->get<0>() != "bin"_str && parts->get<0>() != "cdylib"_str)) {
        return Err(luato::Error::binding(rstd::format(
            "pack artifact selector '{}' must be 'bin:NAME' or 'cdylib:NAME'", selector)));
    }
    return Ok(PackArgument::Artifact(lito::package::PackageTargetId {
        .package = String::make(package_name),
        .kind =
            parts->get<0>() == "bin"_str ? PackageTargetKind::Binary : PackageTargetKind::Cdylib,
        .name = String::make(parts->get<1>()),
    }));
}

auto parse_pack_argument(const luato::Value& value, ref<str> package_name, usize index)
    -> luato::Result<PackArgument> {
    if (value.is_String()) {
        const auto& literal = value.as_String().value;
        if (literal.as_str().contains("\0"_str)) {
            return Err(luato::Error::binding(rstd::format("pack args[{}] contains NUL", index)));
        }
        return Ok(PackArgument::Literal(literal.clone()));
    }
    if (value.is_Table()) {
        const auto& table = *value.as_Table().value;
        if (table.entries().len() == usize(1)) {
            if (table.contains("artifact"_str)) {
                auto selector = rstd_try(table.required<String>("artifact"_str));
                return parse_pack_artifact(selector.as_str(), package_name);
            }
            const auto& entry = table.entries()[usize {}];
            if (entry.value.is_Boolean() && entry.value.as_Boolean().value) {
                if (entry.key == "android_abi"_str) return Ok(PackArgument::AndroidAbi());
                if (entry.key == "android_min_api"_str) return Ok(PackArgument::AndroidMinApi());
                if (entry.key == "output"_str) return Ok(PackArgument::Output());
            }
        }
    }
    return Err(luato::Error::binding(
        rstd::format("pack args[{}] must be a string or a table with one of artifact, android_abi, "
                     "android_min_api, or output",
                     index)));
}

auto parse_pack_recipe(const luato::Table& table, ref<str> package_name)
    -> luato::Result<PackRecipe> {
    const String fields[] = { "name"_Str, "tool"_Str, "args"_Str, "output"_Str };
    rstd_try(table.reject_unknown_fields(slice<String>::from_raw_parts(fields, usize(4))));
    auto name = rstd_try(table.required<String>("name"_str));
    if (! package_name_is_valid(name.as_str())) {
        return Err(luato::Error::binding(rstd::format("invalid pack recipe name '{}'", name)));
    }
    auto tool = rstd_try(table.required<String>("tool"_str));
    if (tool.is_empty() || tool.as_str().contains("\0"_str)) {
        return Err(luato::Error::binding("pack recipe tool must name an executable"_Str));
    }
    auto output = rstd_try(table.required<String>("output"_str));
    if (output.as_str().contains("/"_str) ||
        lito::source::SourcePath::parse(output.as_str()).is_err()) {
        return Err(luato::Error::binding(
            rstd::format("pack recipe output '{}' must be a safe file basename", output)));
    }
    auto args           = rstd_try(table.required<luato::Array>("args"_str));
    auto arguments      = Vec<PackArgument>::with_capacity(args.len());
    auto needs_artifact = false;
    auto output_count   = usize {};
    for (usize index {}; index < args.len(); ++index) {
        auto argument =
            rstd_try(parse_pack_argument(args.values()[index], package_name, index + usize(1)));
        needs_artifact |= argument.is_Artifact();
        if (argument.is_Output()) ++output_count;
        arguments.push(rstd::move(argument));
    }
    if (! needs_artifact || output_count != usize(1)) {
        return Err(
            luato::Error::binding(rstd::format("pack recipe '{}' requires at least one artifact "
                                               "reference and exactly one output reference",
                                               name)));
    }
    return Ok(PackRecipe {
        .name      = rstd::move(name),
        .tool      = rstd::move(tool),
        .arguments = rstd::move(arguments),
        .output    = PathBuf::from(rstd::move(output)),
    });
}

} // namespace

auto lito::load_pack_recipe(ref<rstd::path::Path> package_root,
                            ref<str>              package_name,
                            ref<str>              recipe_name) -> CommandResult<PackRecipe> {
    auto script   = PathBuf::from(package_root).join(PathBuf::from("pack.lua"_str).as_path());
    auto metadata = rstd::fs::symlink_metadata(script.as_path());
    if (metadata.is_err()) {
        return Err(CommandError::Message(rstd::format("cannot read pack recipe '{}' at '{}': {}",
                                                      recipe_name,
                                                      script.as_path(),
                                                      rstd::move(metadata).unwrap_err())));
    }
    if (! metadata->is_file() || metadata->is_symlink()) {
        return Err(CommandError::Message(
            rstd::format("pack script '{}' must be a regular non-symlink file", script.as_path())));
    }
    auto script_error = [&](const luato::Error& error) -> CommandError {
        return CommandError::Message(
            rstd::format("pack recipe '{}' in '{}': {}", recipe_name, script.as_path(), error));
    };
    auto recipes = Vec<PackRecipe>::make();
    auto state =
        rstd_try(luato::State::create(luato::StateOptions::build_script()).map_err(script_error));
    auto module = luato::ModuleSpec("lito"_Str);
    module.function("env"_Str, &environment_value);
    module.function("pack"_Str, [&](luato::Table table) -> luato::Result<empty> {
        auto recipe = rstd_try(parse_pack_recipe(table, package_name));
        for (const auto& existing : recipes) {
            if (existing.name == recipe.name) {
                return Err(
                    luato::Error::binding(rstd::format("duplicate pack recipe '{}'", recipe.name)));
            }
        }
        recipes.push(rstd::move(recipe));
        return Ok(empty {});
    });
    auto native = luato::NativeRequireModuleSpec(
        "@lito"_Str, "lito:pack-host-api:v1"_Str, rstd::move(module));
    native.set_global_alias("lito"_Str);
    rstd_try(state.register_native_require_module(rstd::move(native)).map_err(script_error));
    rstd_try(state.execute_file(script.as_path()).map_err(script_error));
    for (auto& recipe : recipes) {
        if (recipe.name == recipe_name) return Ok(rstd::move(recipe));
    }
    return Err(CommandError::Message(
        rstd::format("unknown pack recipe '{}' in '{}'", recipe_name, script.as_path())));
}
