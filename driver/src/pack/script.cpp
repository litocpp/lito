module;
#include <rstd/macro.hpp>

module lito.driver;

import rstd;
import luato;
import lito.core;
import lito.system;
import lito.toolchain;
import :command.error;
import :package.module_catalog;
import :pack.script;
import :script.runtime;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace
{

using lito::PackArgument;
using lito::PackCallbackContext;
using lito::PackCallbackTarget;
using lito::PackRecipe;
using lito::manifest::package_name_is_valid;
using lito::package::PackageTargetId;
using lito::package::PackageTargetKind;

auto parse_pack_artifact(ref<str> selector, ref<str> package_name)
    -> luato::Result<PackageTargetId> {
    auto parts = selector.split_once(":"_str);
    if (parts.is_none() || ! package_name_is_valid(parts->get<1>()) ||
        (parts->get<0>() != "bin"_str && parts->get<0>() != "cdylib"_str)) {
        return Err(luato::Error::binding(rstd::format(
            "pack artifact selector '{}' must be 'bin:NAME' or 'cdylib:NAME'", selector)));
    }
    return Ok(PackageTargetId {
        .package = String::make(package_name),
        .kind =
            parts->get<0>() == "bin"_str ? PackageTargetKind::Binary : PackageTargetKind::Cdylib,
        .name = String::make(parts->get<1>()),
    });
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
                auto target   = rstd_try(parse_pack_artifact(selector.as_str(), package_name));
                return Ok(PackArgument::Artifact(rstd::move(target)));
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

auto pack_name_and_output(String name, String output) -> luato::Result<PackRecipe> {
    if (! package_name_is_valid(name.as_str())) {
        return Err(luato::Error::binding(rstd::format("invalid pack recipe name '{}'", name)));
    }
    if (output.as_str().contains("/"_str) ||
        lito::source::SourcePath::parse(output.as_str()).is_err()) {
        return Err(luato::Error::binding(
            rstd::format("pack recipe output '{}' must be a safe file basename", output)));
    }
    return Ok(PackRecipe { .name = rstd::move(name), .output = PathBuf::from(rstd::move(output)) });
}

auto parse_tool_recipe(const luato::Table& table, ref<str> package_name)
    -> luato::Result<PackRecipe> {
    const String fields[] = { "name"_Str, "tool"_Str, "args"_Str, "output"_Str };
    rstd_try(table.reject_unknown_fields(slice<String>::from_raw_parts(fields, usize(4))));
    auto name   = rstd_try(table.required<String>("name"_str));
    auto output = rstd_try(table.required<String>("output"_str));
    auto recipe = rstd_try(pack_name_and_output(rstd::move(name), rstd::move(output)));
    auto tool   = rstd_try(table.required<String>("tool"_str));
    if (tool.is_empty() || tool.as_str().contains("\0"_str)) {
        return Err(luato::Error::binding("pack recipe tool must name an executable"_Str));
    }
    auto args         = rstd_try(table.required<luato::Array>("args"_str));
    recipe.tool       = rstd::move(tool);
    recipe.arguments  = Vec<PackArgument>::with_capacity(args.len());
    auto output_count = usize {};
    for (usize index {}; index < args.len(); ++index) {
        auto argument =
            rstd_try(parse_pack_argument(args.values()[index], package_name, index + usize(1)));
        if (argument.is_Artifact()) {
            const auto& target = argument.as_Artifact().target;
            if (! recipe.inputs.iter().any([&](auto input) {
                    return *input == target;
                })) {
                recipe.inputs.push(target.clone());
            }
        }
        if (argument.is_Output()) ++output_count;
        recipe.arguments.push(rstd::move(argument));
    }
    if (recipe.inputs.is_empty() || output_count != usize(1)) {
        return Err(
            luato::Error::binding(rstd::format("pack recipe '{}' requires at least one artifact "
                                               "reference and exactly one output reference",
                                               recipe.name)));
    }
    return Ok(rstd::move(recipe));
}

struct DeclaredPackRecipe {
    PackRecipe                    recipe;
    Option<luato::FunctionHandle> callback;
};

auto recipe_value(luato::TableHandle& table, ref<str> field) -> luato::Result<luato::LuaValue> {
    return table.get_in_call(luato::IntoLua<ref<str>>::into_lua(field));
}

auto parse_pack_android_abis(luato::TableHandle& table) -> luato::Result<Vec<String>> {
    auto abis   = rstd_try(recipe_value(table, "android_abis"_str));
    auto result = Vec<String>::make();
    if (abis.is_Nil()) return Ok(rstd::move(result));
    if (! abis.is_Table()) {
        return Err(luato::Error::binding("pack recipe android_abis must be an array"_Str));
    }
    auto values = rstd_try(abis.as_Table().value.to_value());
    if (! values.is_Array() || values.as_Array().value->len() == usize {}) {
        return Err(luato::Error::binding("pack recipe android_abis must be a non-empty array"_Str));
    }
    for (const auto& value : values.as_Array().value->values()) {
        if (! value.is_String()) {
            return Err(luato::Error::binding("pack recipe Android ABI must be a string"_Str));
        }
        const auto& abi = value.as_String().value;
        if (lito::android_abi_clang_prefix(abi.as_str()).is_none()) {
            return Err(luato::Error::binding(rstd::format("unsupported Android ABI '{}'", abi)));
        }
        if (result.iter().any([&](auto item) {
                return *item == abi;
            })) {
            return Err(luato::Error::binding(rstd::format("duplicate Android ABI '{}'", abi)));
        }
        result.push(abi.clone());
    }
    return Ok(rstd::move(result));
}

auto parse_pack_recipe(luato::TableHandle table, ref<str> package_name)
    -> luato::Result<DeclaredPackRecipe> {
    auto callback = rstd_try(recipe_value(table, "run"_str));
    if (callback.is_Nil()) {
        auto snapshot = rstd_try(table.to_table());
        return Ok(
            DeclaredPackRecipe { .recipe = rstd_try(parse_tool_recipe(snapshot, package_name)) });
    }
    if (! callback.is_Function()) {
        return Err(luato::Error::binding("pack recipe run must be a function"_Str));
    }
    auto keys = rstd_try(table.keys());
    for (const auto& key : keys) {
        auto text = rstd_try(key.as_utf8());
        if (text != "name"_str && text != "output"_str && text != "inputs"_str &&
            text != "run"_str && text != "android_abis"_str) {
            return Err(
                luato::Error::binding(rstd::format("unknown Lua pack recipe field '{}'", text)));
        }
    }
    auto name   = rstd_try(recipe_value(table, "name"_str));
    auto output = rstd_try(recipe_value(table, "output"_str));
    auto recipe = rstd_try(pack_name_and_output(String::make(rstd_try(name.as_utf8())),
                                                String::make(rstd_try(output.as_utf8()))));
    recipe.android_abis = rstd_try(parse_pack_android_abis(table));
    auto inputs = rstd_try(recipe_value(table, "inputs"_str));
    if (! inputs.is_Table()) {
        return Err(luato::Error::binding("pack recipe inputs must be an array"_Str));
    }
    auto values = rstd_try(inputs.as_Table().value.to_value());
    if (! values.is_Array()) {
        return Err(luato::Error::binding("pack recipe inputs must be a non-empty array"_Str));
    }
    for (const auto& value : values.as_Array().value->values()) {
        if (! value.is_String()) {
            return Err(luato::Error::binding("pack recipe input must be a target selector"_Str));
        }
        auto target = rstd_try(parse_pack_artifact(value.as_String().value.as_str(), package_name));
        if (recipe.inputs.iter().any([&](auto input) {
                return *input == target;
            })) {
            return Err(luato::Error::binding(
                rstd::format("duplicate pack recipe input '{}'", value.as_String().value)));
        }
        recipe.inputs.push(rstd::move(target));
    }
    recipe.lua_callback = true;
    return Ok(DeclaredPackRecipe {
        .recipe   = rstd::move(recipe),
        .callback = Some(rstd::move(callback.as_Function().value)),
    });
}

auto path_error(ref<str> action, ref<rstd::path::Path> path, const rstd::io::Error& error)
    -> luato::Error {
    return luato::Error::binding(rstd::format("{} '{}': {}", action, path, error));
}

auto path_text(ref<rstd::path::Path> path) -> luato::Result<String> {
    auto text = path.to_str();
    if (text.is_none()) {
        return Err(luato::Error::binding(rstd::format("path '{}' is not valid UTF-8", path)));
    }
    return Ok(String::make(*text));
}

auto set_pack_target(luato::Table& table, const PackCallbackTarget& target)
    -> luato::Result<empty> {
    auto artifacts = luato::Table::make();
    for (const auto& artifact : target.artifacts) {
        rstd_try(
            artifacts.set(artifact.selector.clone(), rstd_try(path_text(artifact.path.as_path()))));
    }
    rstd_try(table.set("artifacts"_Str, rstd::move(artifacts)));
    rstd_try(table.set("target"_Str, target.target.clone()));
    if (! target.android_abi.is_empty()) {
        rstd_try(table.set("android_abi"_Str, target.android_abi.clone()));
        rstd_try(table.set("android_min_api"_Str, i64(target.android_min_api.to_primitive())));
    }
    return Ok(empty {});
}

} // namespace

namespace lito
{

auto reserve_pack_directory(ref<rstd::path::Path> parent) -> CommandResult<PathBuf> {
    auto created_parent = rstd::fs::create_dir_all(parent);
    if (created_parent.is_err()) {
        return Err(
            CommandError::System(system::SystemError::Io("create pack staging parent"_Str,
                                                         PathBuf::from(parent),
                                                         rstd::move(created_parent).unwrap_err())));
    }
    constexpr auto attempt_limit = usize(64);
    for (auto attempt = usize {}; attempt < attempt_limit; ++attempt) {
        auto name = PathBuf::from(rstd::format(".lito-pack-{}-{}", rstd::process::id(), attempt));
        auto path = PathBuf::from(parent).join(name.as_path());
        auto made = rstd::fs::create_dir(path.as_path());
        if (made.is_ok()) return Ok(rstd::move(path));
        auto error = rstd::move(made).unwrap_err();
        if (error.kind() != rstd::io::ErrorKind { rstd::io::ErrorKind::AlreadyExists }) {
            return Err(CommandError::System(system::SystemError::Io(
                "create pack staging directory"_Str, rstd::move(path), rstd::move(error))));
        }
    }
    return Err(CommandError::Message(
        rstd::format("cannot reserve pack staging directory under '{}'", parent)));
}

struct PackScriptSession::Impl {
    PackScriptDependencies                    dependencies;
    Option<package::ScriptModuleCatalog>      modules;
    luato::State                              state;
    Vec<DeclaredPackRecipe>                   recipes;
    usize                                     selected {};
    PathBuf                                   script;
    const system::ResolvedProcessEnvironment& environment;
    const PackCallbackContext*                active {};
    Vec<PathBuf>                              temporary_directories;

    Impl(PackScriptDependencies                    deps,
         luato::State                              vm,
         PathBuf                                   source,
         const system::ResolvedProcessEnvironment& process_environment)
        : dependencies(rstd::move(deps)),
          state(rstd::move(vm)),
          recipes(Vec<DeclaredPackRecipe>::make()),
          script(rstd::move(source)),
          environment(process_environment),
          temporary_directories(Vec<PathBuf>::make()) {}

    ~Impl() {
        for (const auto& path : temporary_directories) {
            (void)rstd::fs::remove_dir_all(path.as_path());
        }
    }

    auto session_error(ref<str> recipe, const luato::Error& error) const -> CommandError {
        return CommandError::Message(
            rstd::format("pack recipe '{}' in '{}': {}", recipe, script.as_path(), error));
    }

    auto path(ref<str> value) const -> luato::Result<PathBuf> {
        if (active == nullptr) {
            return Err(luato::Error::binding(
                "pack file and process operations require a running callback"_Str));
        }
        if (value.is_empty() || value.contains("\0"_str)) {
            return Err(luato::Error::binding("invalid pack path"_Str));
        }
        auto result = PathBuf::from(value);
        if (result.as_path().is_relative()) result = active->package_root.join(result.as_path());
        return Ok(rstd::move(result));
    }

    auto file_kind(String value) const -> luato::Result<Option<String>> {
        auto resolved = rstd_try(path(value.as_str()));
        auto metadata = rstd::fs::symlink_metadata(resolved.as_path());
        if (metadata.is_err()) {
            auto error = rstd::move(metadata).unwrap_err();
            if (error.kind() == rstd::io::ErrorKind { rstd::io::ErrorKind::NotFound }) {
                return Ok(None());
            }
            return Err(path_error("inspect pack path"_str, resolved.as_path(), error));
        }
        if (metadata->is_symlink()) return Ok(Some("symlink"_Str));
        if (metadata->is_file()) return Ok(Some("file"_Str));
        if (metadata->is_dir()) return Ok(Some("directory"_Str));
        return Ok(Some("other"_Str));
    }

    auto list_dir(String value) const -> luato::Result<luato::Array> {
        auto resolved   = rstd_try(path(value.as_str()));
        auto read_error = [&](const auto& error) {
            return path_error("enumerate pack directory"_str, resolved.as_path(), error);
        };
        auto opened = rstd_try(rstd::fs::read_dir(resolved.as_path()).map_err(read_error));
        auto names  = Vec<String>::make();
        for (auto entry : opened) {
            auto item = rstd_try(rstd::move(entry).map_err(read_error));
            auto name = item.file_name().into_string();
            if (name.is_err()) {
                return Err(luato::Error::binding(rstd::format(
                    "pack directory '{}' contains a non-UTF-8 name", resolved.as_path())));
            }
            names.push(rstd::move(name).unwrap());
        }
        rstd::slice_::sort_unstable_by(names.as_mut_slice().as_mut_ref(),
                                       [](const String& left, const String& right) {
                                           return left < right;
                                       });
        auto values = Vec<luato::Value>::with_capacity(names.len());
        for (auto& name : names) values.push(luato::Value::String(rstd::move(name)));
        return Ok(luato::Array::from(rstd::move(values)));
    }

    auto mkdir(String value) const -> luato::Result<empty> {
        auto resolved = rstd_try(path(value.as_str()));
        return rstd::fs::create_dir_all(resolved.as_path()).map_err([&](const auto& error) {
            return path_error("create pack directory"_str, resolved.as_path(), error);
        });
    }

    auto copy_file(String source, String destination) const -> luato::Result<empty> {
        auto from = rstd_try(path(source.as_str()));
        auto to   = rstd_try(path(destination.as_str()));
        rstd_try(rstd::fs::copy(from.as_path(), to.as_path()).map_err([&](const auto& error) {
            return luato::Error::binding(rstd::format(
                "copy pack file '{}' to '{}': {}", from.as_path(), to.as_path(), error));
        }));
        return Ok(empty {});
    }

    auto link_file(String source, String destination) const -> luato::Result<bool> {
        auto from   = rstd_try(path(source.as_str()));
        auto to     = rstd_try(path(destination.as_str()));
        auto linked = rstd::fs::hard_link(from.as_path(), to.as_path());
        if (linked.is_ok()) return Ok(true);
        auto error = rstd::move(linked).unwrap_err();
        if (error.kind() == rstd::io::ErrorKind { rstd::io::ErrorKind::AlreadyExists }) {
            return Ok(false);
        }
        return Err(luato::Error::binding(
            rstd::format("link pack file '{}' to '{}': {}", from.as_path(), to.as_path(), error)));
    }

    auto temp_dir(String parent) -> luato::Result<String> {
        auto resolved = rstd_try(path(parent.as_str()));
        auto created =
            rstd_try(reserve_pack_directory(resolved.as_path()).map_err([](const auto& error) {
                return luato::Error::binding(rstd::format("{}", error));
            }));
        auto result = path_text(created.as_path());
        temporary_directories.push(rstd::move(created));
        return result;
    }

    auto run(const luato::Table& options) const -> luato::Result<empty> {
        const String fields[] = { "tool"_Str, "args"_Str, "cwd"_Str };
        rstd_try(options.reject_unknown_fields(slice<String>::from_raw_parts(fields, usize(3))));
        auto tool = rstd_try(options.required<String>("tool"_str));
        if (tool.is_empty() || tool.as_str().contains("\0"_str)) {
            return Err(luato::Error::binding("pack run tool must name an executable"_Str));
        }
        auto cwd =
            options.contains("cwd"_str) ? rstd_try(options.required<String>("cwd"_str)) : "."_Str;
        auto directory  = rstd_try(path(cwd.as_str()));
        auto executable = PathBuf::from(tool.as_str());
        if (executable.as_path().is_relative() &&
            ! system::is_searchable_executable_name(executable.as_path())) {
            executable = directory.join(executable.as_path());
        }
        auto resolved =
            rstd_try(environment.locate_executable(executable.as_path(), "pack tool"_str)
                         .map_err([](const auto& error) {
                             return luato::Error::binding(rstd::format("{}", error));
                         }));
        if (resolved.is_none()) {
            return Err(luato::Error::binding(rstd::format("cannot find pack tool '{}'", tool)));
        }
        auto command = rstd::process::Command::make(resolved->as_path().as_os_str());
        if (options.contains("args"_str)) {
            auto args = options.required<luato::Array>("args"_str);
            if (args.is_err()) {
                auto table = options.required<luato::Table>("args"_str);
                if (table.is_err() || ! table->entries().is_empty()) {
                    return Err(rstd::move(args).unwrap_err());
                }
            } else {
                for (const auto& arg : args->values()) {
                    if (! arg.is_String() || arg.as_String().value.as_str().contains("\0"_str)) {
                        return Err(luato::Error::binding(
                            "pack tool arguments must be strings without NUL"_Str));
                    }
                    command.arg(arg.as_String().value.as_str());
                }
            }
        }
        command.current_dir(directory.as_path());
        system::apply_command_environment(command, environment);
        auto status = rstd_try(command.status().map_err([&](const auto& error) {
            return path_error("execute pack tool"_str, resolved->as_path(), error);
        }));
        if (status.success()) return Ok(empty {});
        auto code = status.code();
        return Err(luato::Error::binding(
            rstd::format("pack tool '{}' failed with {} {}",
                         resolved->as_path(),
                         code.is_some() ? "exit code"_str : "signal"_str,
                         code.is_some() ? *code : status.signal().unwrap_or(i32(-1)))));
    }
};

PackScriptSession::PackScriptSession(Box<Impl> implementation): impl_(rstd::move(implementation)) {
}

PackScriptSession::PackScriptSession(PackScriptSession&&) noexcept = default;
PackScriptSession::~PackScriptSession()                            = default;

auto PackScriptSession::load(ref<rstd::path::Path>                     package_root,
                             ref<str>                                  package_name,
                             ref<str>                                  recipe_name,
                             PackScriptDependencies                    dependencies,
                             const system::ResolvedProcessEnvironment& environment)
    -> CommandResult<PackScriptSession> {
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
    auto state = rstd_try(luato::State::create(luato::StateOptions::build_script())
                              .map_err([&](const luato::Error& error) {
                                  return CommandError::Message(rstd::format(
                                      "initialize pack script '{}': {}", script.as_path(), error));
                              }));
    auto impl  = Box<Impl>::make(
        rstd::move(dependencies), rstd::move(state), rstd::move(script), environment);
    auto* session      = rstd::addressof(*impl);
    auto  module_error = [&](const luato::Error& error) {
        return session->session_error(recipe_name, error);
    };
    auto modules =
        rstd_try(package::ScriptModuleCatalog::make(package_root,
                                                    impl->dependencies.identity.as_str(),
                                                    impl->dependencies.dependencies.as_slice(),
                                                    impl->dependencies.packages.as_slice(),
                                                    manifest::ScriptHostKind::Pack));
    impl->modules = Some(rstd::move(modules));
    rstd_try(attach_script_modules(impl->state, *impl->modules).map_err(module_error));
    auto module = luato::ModuleSpec("lito"_Str);
    register_script_utilities(module);
    module.function("env"_Str, &environment_value);
    module.raw_function(
        "pack"_Str,
        usize(1),
        [session,
         owner = String::make(package_name)](luato::CallFrame& frame) -> luato::BindingResult {
            if (session->active != nullptr) {
                return Err(
                    luato::Error::binding("pack recipes must be declared before execution"_Str));
            }
            auto table       = rstd_try(luato::argument<luato::TableHandle>(frame, usize {}));
            auto declaration = rstd_try(parse_pack_recipe(rstd::move(table), owner.as_str()));
            for (const auto& existing : session->recipes) {
                if (existing.recipe.name == declaration.recipe.name) {
                    return Err(luato::Error::binding(
                        rstd::format("duplicate pack recipe '{}'", declaration.recipe.name)));
                }
            }
            session->recipes.push(rstd::move(declaration));
            return Ok(usize {});
        });
    module.function("file_kind"_Str, [session](String path) {
        return session->file_kind(rstd::move(path));
    });
    module.function("list_dir"_Str, [session](String path) {
        return session->list_dir(rstd::move(path));
    });
    module.function("mkdir"_Str, [session](String path) {
        return session->mkdir(rstd::move(path));
    });
    module.function("copy_file"_Str, [session](String source, String destination) {
        return session->copy_file(rstd::move(source), rstd::move(destination));
    });
    module.function("link_file"_Str, [session](String source, String destination) {
        return session->link_file(rstd::move(source), rstd::move(destination));
    });
    module.function("temp_dir"_Str, [session](String path) {
        return session->temp_dir(rstd::move(path));
    });
    module.function("run"_Str, [session](luato::Table options) {
        return session->run(options);
    });
    auto native = luato::NativeRequireModuleSpec(
        "@lito"_Str, "lito:pack-host-api:v2"_Str, rstd::move(module));
    native.set_global_alias("lito"_Str);
    rstd_try(impl->state.register_native_require_module(rstd::move(native)).map_err(module_error));
    auto entry =
        rstd_try(impl->modules->entry(impl->script.as_path(), package_name).map_err(module_error));
    rstd_try(impl->state.execute_entry(rstd::move(entry)).map_err(module_error));
    for (auto index = usize {}; index < impl->recipes.len(); ++index) {
        if (impl->recipes[index].recipe.name == recipe_name) {
            impl->selected = index;
            return Ok(PackScriptSession(rstd::move(impl)));
        }
    }
    return Err(CommandError::Message(
        rstd::format("unknown pack recipe '{}' in '{}'", recipe_name, impl->script.as_path())));
}

auto PackScriptSession::recipe() const -> const PackRecipe& {
    return impl_->recipes[impl_->selected].recipe;
}

auto PackScriptSession::execute(const PackCallbackContext& context) -> CommandResult<empty> {
    auto& selected = impl_->recipes[impl_->selected];
    auto& callback = selected.callback;
    if (callback.is_none()) {
        return Err(CommandError::Message("pack recipe has no Lua callback"_Str));
    }
    auto arguments = [&]() -> luato::Result<luato::TableHandle> {
        auto data = luato::Table::make();
        rstd_try(data.set("package_root"_Str, rstd_try(path_text(context.package_root.as_path()))));
        rstd_try(data.set("work_dir"_Str, rstd_try(path_text(context.work_dir.as_path()))));
        rstd_try(data.set("output"_Str, rstd_try(path_text(context.output.as_path()))));
        if (selected.recipe.android_abis.is_empty()) {
            rstd_try(set_pack_target(data, context.targets[usize {}]));
        } else {
            auto targets = Vec<luato::Value>::with_capacity(context.targets.len());
            for (const auto& target : context.targets) {
                auto variant = luato::Table::make();
                rstd_try(set_pack_target(variant, target));
                targets.push(luato::Value::Table(rstd::move(variant)));
            }
            rstd_try(data.set("variants"_Str, luato::Array::from(rstd::move(targets))));
        }
        auto lua = rstd_try(impl_->state.from_snapshot(luato::Value::Table(rstd::move(data))));
        return Ok(rstd::move(lua.as_Table().value));
    }();
    auto context_table = rstd_try(rstd::move(arguments).map_err([&](const luato::Error& error) {
        return impl_->session_error(selected.recipe.name.as_str(), error);
    }));
    impl_->active      = rstd::addressof(context);
    auto result        = callback->call<luato::IgnoreReturns>(rstd::move(context_table));
    impl_->active      = nullptr;
    rstd_try(rstd::move(result).map_err([&](const luato::Error& error) {
        return impl_->session_error(selected.recipe.name.as_str(), error);
    }));
    return Ok(empty {});
}

} // namespace lito
