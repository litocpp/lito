module;
#include <rstd/macro.hpp>

module lito.driver:script.build_request;

import rstd;
import luato;
import :build.generated.model;
import :build.script.support;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

template<typename T>
auto script_request_failure(String message) -> luato::Result<T> {
    auto error = BuildScriptError::BuildToolAction(
        BuildToolActionError::InvalidRequest(rstd::move(message)));
    return Err(luato::Error::binding(rstd::format("{}", error)));
}

template<typename T>
auto script_field(luato::Result<T> value) -> luato::Result<T> {
    if (value.is_err()) return script_request_failure<T>(rstd::format("{}", value.unwrap_err()));
    return value;
}

auto script_fields(const luato::Table& table, initializer_list<ref<str>> fields)
    -> luato::Result<empty> {
    auto known = Vec<String>::make();
    for (auto field : fields) known.push(String::make(field));
    return script_field(table.reject_unknown_fields(known.as_slice()));
}

auto script_package(const luato::Table& table, const Option<String>& fallback, ref<str> context)
    -> luato::Result<String> {
    if (table.contains("package"_str)) return script_field(table.required<String>("package"_str));
    if (fallback.is_some()) return Ok(fallback->clone());
    return script_request_failure<String>(rstd::format("{}.package is required", context));
}

auto script_array(const luato::Table& table, ref<str> field, bool required)
    -> luato::Result<luato::Array> {
    if (! required && ! table.contains(field)) return Ok(luato::Array::make());
    return script_field(table.required<luato::Array>(field));
}

auto script_strings(const luato::Array& array, ref<str> context) -> luato::Result<Vec<String>> {
    auto result = Vec<String>::make();
    for (usize i {}; i < array.len(); ++i) {
        const auto& value = array.values()[i];
        if (! value.is_String())
            return script_request_failure<Vec<String>>(
                rstd::format("{}[{}] must be a string", context, i + usize(1)));
        result.push(value.as_String().value.clone());
    }
    return Ok(rstd::move(result));
}

auto script_member(const luato::Table& table, ref<str> field, ref<str> context)
    -> luato::Result<ref<luato::Value>> {
    for (const auto& entry : table.entries())
        if (entry.key == field)
            return Ok(ref<luato::Value>::from_raw_parts(rstd::addressof(entry.value)));
    return script_request_failure<ref<luato::Value>>(
        rstd::format("{}.{} is required", context, field));
}

auto script_output(const luato::Value& value, ref<str> context, usize index)
    -> luato::Result<DeclaredActionOutput> {
    if (value.is_String()) {
        auto path = value.as_String().value.clone();
        auto kind = inferred_generated_output_kind(path.as_str());
        return Ok(DeclaredActionOutput { rstd::move(path), kind });
    }
    if (! value.is_Table())
        return script_request_failure<DeclaredActionOutput>(
            rstd::format("{}[{}] must be a string or output table", context, index + usize(1)));
    const auto& table = *value.as_Table().value;
    rstd_try(script_fields(table, { "path"_str, "source"_str }));
    auto path = rstd_try(script_field(table.required<String>("path"_str)));
    auto kind = inferred_generated_output_kind(path.as_str());
    if (table.contains("source"_str))
        kind = rstd_try(script_field(table.required<bool>("source"_str)))
                   ? GeneratedOutputKind::Source
                   : GeneratedOutputKind::Other;
    return Ok(DeclaredActionOutput { rstd::move(path), kind });
}

auto script_outputs(const luato::Array& values, ref<str> context)
    -> luato::Result<Vec<DeclaredActionOutput>> {
    auto result = Vec<DeclaredActionOutput>::make();
    for (usize i {}; i < values.len(); ++i)
        result.push(rstd_try(script_output(values.values()[i], context, i)));
    return Ok(rstd::move(result));
}

auto script_input(const luato::Value& value, ref<str> context, usize index)
    -> luato::Result<DeclaredActionInput> {
    if (value.is_String())
        return Ok(DeclaredActionInput { Some(value.as_String().value.clone()), {} });
    if (value.is_Opaque()) return Ok(DeclaredActionInput { None(), { value.as_Opaque().value } });
    return script_request_failure<DeclaredActionInput>(rstd::format(
        "{}[{}] must be a source path or generated output handle", context, index + usize(1)));
}

auto script_inputs(const luato::Array& values, ref<str> context)
    -> luato::Result<Vec<DeclaredActionInput>> {
    auto result = Vec<DeclaredActionInput>::make();
    for (usize i {}; i < values.len(); ++i)
        result.push(rstd_try(script_input(values.values()[i], context, i)));
    return Ok(rstd::move(result));
}

auto script_handles(const luato::Array& values, bool external_sources)
    -> luato::Result<Vec<BuildScriptHandle>> {
    auto result = Vec<BuildScriptHandle>::make();
    for (usize i {}; i < values.len(); ++i) {
        const auto& value = values.values()[i];
        if (value.is_Opaque())
            result.push(BuildScriptHandle { value.as_Opaque().value });
        else if (external_sources && value.is_Table()) {
            auto handle = value.as_Table().value->required<luato::OpaqueHandle>("_handle"_str);
            if (handle.is_err())
                return script_request_failure<Vec<BuildScriptHandle>>(rstd::format(
                    "lito.run.input_roots[{}] must be an external source object", i + usize(1)));
            result.push(BuildScriptHandle { handle->identity });
        } else
            return script_request_failure<Vec<BuildScriptHandle>>(
                external_sources
                    ? rstd::format("lito.run.input_roots[{}] must be an external source object",
                                   i + usize(1))
                    : rstd::format("lito.run.tools[{}] must be a tool handle", i + usize(1)));
    }
    return Ok(rstd::move(result));
}

auto parse_target_request(const luato::Table& table, const Option<String>& fallback)
    -> luato::Result<TargetScriptRequest> {
    rstd_try(script_fields(table, { "package"_str, "kind"_str, "name"_str }));
    auto package = rstd_try(script_package(table, fallback, "lito.target"_str));
    auto kind    = rstd_try(script_field(table.required<String>("kind"_str)));
    auto name    = rstd_try(script_field(table.required<String>("name"_str)));
    return Ok(TargetScriptRequest { rstd::move(package), rstd::move(kind), rstd::move(name) });
}

auto parse_write_request(const luato::Table& table, const Option<String>& fallback)
    -> luato::Result<WriteScriptRequest> {
    rstd_try(script_fields(table, { "package"_str, "output"_str, "content"_str, "inputs"_str }));
    auto package       = rstd_try(script_package(table, fallback, "lito.write"_str));
    auto output        = rstd_try(script_member(table, "output"_str, "lito.write"_str));
    auto content       = rstd_try(script_field(table.required<String>("content"_str)));
    auto inputs        = rstd_try(script_array(table, "inputs"_str, false));
    auto parsed_inputs = rstd_try(script_inputs(inputs, "lito.write.inputs"_str));
    auto parsed_output = rstd_try(script_output(*output, "lito.write.output"_str, usize {}));
    return Ok(WriteScriptRequest { rstd::move(package),
                                   rstd::move(parsed_output),
                                   rstd::move(content),
                                   rstd::move(parsed_inputs) });
}

auto parse_copy_request(const luato::Table& table, const Option<String>& fallback)
    -> luato::Result<CopyScriptRequest> {
    rstd_try(script_fields(table, { "package"_str, "input"_str, "output"_str }));
    auto package       = rstd_try(script_package(table, fallback, "lito.copy"_str));
    auto output        = rstd_try(script_member(table, "output"_str, "lito.copy"_str));
    auto input         = rstd_try(script_member(table, "input"_str, "lito.copy"_str));
    auto parsed_input  = rstd_try(script_input(*input, "lito.copy.input"_str, usize {}));
    auto parsed_output = rstd_try(script_output(*output, "lito.copy.output"_str, usize {}));
    return Ok(CopyScriptRequest {
        rstd::move(package), rstd::move(parsed_input), rstd::move(parsed_output) });
}

auto parse_transform_request(const luato::Table& table, const Option<String>& fallback)
    -> luato::Result<TransformScriptRequest> {
    rstd_try(script_fields(table, { "package"_str, "kind"_str, "input"_str, "outputs"_str }));
    auto package = rstd_try(script_package(table, fallback, "lito.transform"_str));
    auto kind    = rstd_try(script_field(table.required<String>("kind"_str)));
    auto input   = rstd_try(script_field(table.required<luato::OpaqueHandle>("input"_str)));
    auto outputs = rstd_try(script_array(table, "outputs"_str, true));
    auto parsed  = rstd_try(script_outputs(outputs, "lito.transform.outputs"_str));
    return Ok(TransformScriptRequest {
        rstd::move(package), rstd::move(kind), { input.identity }, rstd::move(parsed) });
}

auto parse_run_request(const luato::Table& table, const Option<String>& fallback)
    -> luato::Result<RunScriptRequest> {
    rstd_try(script_fields(table,
                           { "tool"_str,
                             "tools"_str,
                             "package"_str,
                             "cwd"_str,
                             "args"_str,
                             "inputs"_str,
                             "input_roots"_str,
                             "outputs"_str,
                             "depfile"_str,
                             "output_cwd"_str }));
    auto handle         = rstd_try(script_field(table.required<luato::OpaqueHandle>("tool"_str)));
    auto package        = rstd_try(script_package(table, fallback, "lito.run"_str));
    auto cwd            = rstd_try(script_field(table.required<String>("cwd"_str)));
    auto args           = rstd_try(script_array(table, "args"_str, true));
    auto inputs         = rstd_try(script_array(table, "inputs"_str, true));
    auto outputs        = rstd_try(script_array(table, "outputs"_str, true));
    auto tools          = rstd_try(script_array(table, "tools"_str, false));
    auto roots          = rstd_try(script_array(table, "input_roots"_str, false));
    auto request        = RunScriptRequest {};
    request.tool        = { handle.identity };
    request.package     = rstd::move(package);
    request.cwd         = rstd::move(cwd);
    request.tools       = rstd_try(script_handles(tools, false));
    request.input_roots = rstd_try(script_handles(roots, true));
    request.args        = rstd_try(script_strings(args, "lito.run.args"_str));
    request.outputs     = rstd_try(script_outputs(outputs, "lito.run.outputs"_str));
    request.inputs      = rstd_try(script_inputs(inputs, "lito.run.inputs"_str));
    if (table.contains("output_cwd"_str))
        request.output_cwd = Some(rstd_try(script_field(table.required<i64>("output_cwd"_str))));
    if (table.contains("depfile"_str)) {
        auto depfile = rstd_try(script_field(table.required<luato::Table>("depfile"_str)));
        rstd_try(script_fields(depfile, { "output"_str, "roots"_str }));
        auto output     = rstd_try(script_field(depfile.required<i64>("output"_str)));
        auto dep_roots  = rstd_try(script_array(depfile, "roots"_str, false));
        request.depfile = Some(ActionDepfileRequest {
            output, rstd_try(script_strings(dep_roots, "lito.run.depfile.roots"_str)) });
    }
    return Ok(rstd::move(request));
}

auto script_string_values(const Vec<String>& values) -> luato::Array {
    return luato::Array::from(values.iter()
                                  .map([](auto value) {
                                      return luato::Value::String(value->clone());
                                  })
                                  .collect<Vec<luato::Value>>());
}

auto dependency_info_table(ScriptDependencyInfo info) -> luato::Result<luato::Table> {
    auto table = luato::Table::make();
    rstd_try(table.set("alias"_Str, rstd::move(info.alias)));
    rstd_try(table.set("provider"_Str, rstd::move(info.provider)));
    rstd_try(table.set("version"_Str, rstd::move(info.version)));
    rstd_try(table.set("identity"_Str, rstd::move(info.identity)));
    rstd_try(table.set("targets"_Str, script_string_values(info.targets)));
    return Ok(rstd::move(table));
}

auto preprocessor_info_table(ScriptPreprocessorInfo info) -> luato::Result<luato::Table> {
    auto table = luato::Table::make();
    rstd_try(table.set("include_directories"_Str,
                       script_string_values(info.projection.user_include_directories)));
    rstd_try(table.set("system_include_directories"_Str,
                       script_string_values(info.projection.system_include_directories)));
    rstd_try(table.set("framework_include_directories"_Str,
                       script_string_values(info.projection.framework_include_directories)));
    rstd_try(table.set("definitions"_Str, script_string_values(info.projection.definitions)));
    rstd_try(table.set("undefinitions"_Str, script_string_values(info.projection.undefinitions)));
    rstd_try(table.set("compiler_flavor"_Str, rstd::move(info.compiler_flavor)));
    rstd_try(table.set("target"_Str, rstd::move(info.target)));
    rstd_try(table.set("identity"_Str, rstd::move(info.projection.identity)));
    return Ok(rstd::move(table));
}

} // namespace lito
