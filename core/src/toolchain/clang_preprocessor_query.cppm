export module lito.toolchain:clang_preprocessor_query;

import rstd;
import lito.cpp;
import lito.error;
import lito.toolchain.contract;
import lito.package.target_contract;
import lito.system.process;
import lito.system.environment;
import lito.frontend;
import :clang_options;
import :command;
import :clang_preprocessor_model;
import :clang_preprocessor_probe;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::toolchain
{

auto query_clang_builtin_environment_snapshot(const Vec<String>&                base_command,
                                              ref<str>                          key,
                                              const BuiltinSemanticContext&     semantic_context,
                                              ref<rstd::path::Path>             working_directory,
                                              const ResolvedProcessEnvironment& environment)
    -> Result<SharedClangBuiltinEnvironmentSnapshot> {
    auto macro_command = clone_command(base_command);
    command::push_option(macro_command, clang_options::DUMP_MACROS);
    command::push_option(macro_command, clang_options::PREPROCESS);
    command::push_option(macro_command, clang_options::LANGUAGE);
    command::push_option(macro_command, clang_options::CXX_SOURCE);
    command::push_option(macro_command, clang_options::STANDARD_INPUT);
    auto macro_output =
        run_command_with_input(macro_command, ""_str, environment, Some(working_directory));
    if (macro_output.is_err()) return Err(rstd::move(macro_output).unwrap_err());
    if (macro_output->exit_code != i32 {}) {
        return environment_failure<SharedClangBuiltinEnvironmentSnapshot>(
            rstd::format("clang++ -dM failed\n{}\n{}",
                         command_text(macro_command).as_str(),
                         macro_output->standard_error.as_str()));
    }
    auto macros = parse_macro_dump(macro_output->standard_output.as_str());
    if (macros.is_err()) return Err(rstd::move(macros).unwrap_err());
    auto clang_macros = clang_owned_macro_seeds(*macros);
    auto parsed       = parse_macro_seeds(clang_macros, "<built-in>"_str);
    if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
    auto capabilities =
        query_clang_capabilities(base_command, semantic_context, working_directory, environment);
    if (capabilities.is_err()) return Err(rstd::move(capabilities).unwrap_err());
    auto values            = rstd::move(parsed).unwrap();
    auto capability_values = rstd::move(capabilities).unwrap();
    auto identity          = builtin_snapshot_identity(clang_macros, key);
    return Ok(
        rstd::sync::Arc<ClangBuiltinEnvironmentSnapshot>::make(ClangBuiltinEnvironmentSnapshot {
            .key                     = String::make(key),
            .identity                = rstd::move(identity),
            .source                  = rstd::move(values.source),
            .definitions             = rstd::move(values.definitions),
            .capabilities            = rstd::move(capability_values.values),
            .clang_macro_count       = clang_macros.len(),
            .native_macro_count      = usize(4),
            .clang_capability_count  = capability_values.clang_count,
            .native_capability_count = capability_values.native_count,
            .macro_output_bytes      = macro_output->standard_output.len(),
            .capability_input_bytes  = capability_values.input_bytes,
            .capability_output_bytes = capability_values.output_bytes,
        }));
}

struct TextBuiltinValues {
    String date;
    String time;
};

auto query_text_builtins(const Vec<String>&                base_command,
                         ref<rstd::path::Path>             working_directory,
                         const ResolvedProcessEnvironment& environment)
    -> Result<TextBuiltinValues> {
    auto command_line = clone_command(base_command);
    command::push_option(command_line, clang_options::PREPROCESS);
    command::push_option(command_line, clang_options::NO_LINE_MARKERS);
    command::push_option(command_line, clang_options::LANGUAGE);
    command::push_option(command_line, clang_options::CXX_SOURCE);
    command::push_option(command_line, clang_options::STANDARD_INPUT);
    auto output =
        run_command_with_input(command_line,
                               "LITO_BUILTIN_DATE __DATE__\nLITO_BUILTIN_TIME __TIME__\n"_str,
                               environment,
                               Some(working_directory));
    if (output.is_err()) return Err(rstd::move(output).unwrap_err());
    if (output->exit_code != i32 {}) {
        return environment_failure<TextBuiltinValues>(
            rstd::format("clang text builtin query failed: {}", output->standard_error.as_str()));
    }
    auto date   = Option<String> {};
    auto time   = Option<String> {};
    auto parsed = each_line(output->standard_output.as_str(), [&](ref<str> raw) -> Result<empty> {
        auto           line        = raw.trim_ascii();
        auto           value       = Option<ref<str>> {};
        auto           target      = static_cast<Option<String>*>(nullptr);
        constexpr auto date_prefix = "LITO_BUILTIN_DATE "_str;
        constexpr auto time_prefix = "LITO_BUILTIN_TIME "_str;
        if (line.starts_with(date_prefix)) {
            value  = line.get(date_prefix.len(), line.len());
            target = rstd::addressof(date);
        } else if (line.starts_with(time_prefix)) {
            value  = line.get(time_prefix.len(), line.len());
            target = rstd::addressof(time);
        } else if (! line.is_empty()) {
            return environment_failure<empty>(
                rstd::format("unexpected clang text builtin output: {}", line));
        }
        if (target == nullptr) return Ok(empty {});
        auto text = value->trim_ascii();
        if (text.len() < usize(2) || text.as_bytes()[usize {}] != u8('"') ||
            text.as_bytes()[text.len() - usize(1)] != u8('"')) {
            return environment_failure<empty>(
                rstd::format("invalid clang text builtin value: {}", text));
        }
        auto inner = text.get(usize(1), text.len() - usize(1));
        if (inner.is_none()) {
            return environment_failure<empty>("invalid clang text builtin boundary"_str);
        }
        *target = Some(String::make(*inner));
        return Ok(empty {});
    });
    if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
    if (date.is_none() || time.is_none()) {
        return environment_failure<TextBuiltinValues>(
            "clang text builtin query returned an incomplete snapshot"_str);
    }
    return Ok(TextBuiltinValues {
        .date = rstd::move(date).unwrap(),
        .time = rstd::move(time).unwrap(),
    });
}

auto query_preprocessor_environment(const Vec<String>&                    base_command,
                                    PreprocessorEnvironmentKey            key,
                                    SharedClangBuiltinEnvironmentSnapshot builtin_environment,
                                    BuiltinSemanticContext                semantic_context,
                                    const Vec<CppMacroDirective>&         macros,
                                    const ResolvedProcessEnvironment&     environment)
    -> Result<PreprocessorEnvironment> {
    auto working_directory = key.working_directory.as_path();
    auto native_macros =
        parse_macro_seeds(native_predefined_macro_seeds(semantic_context), "<lito-built-in>"_str);
    if (native_macros.is_err()) return Err(rstd::move(native_macros).unwrap_err());
    auto command_line_macros = parse_command_line_macros(macros);
    if (command_line_macros.is_err()) return Err(rstd::move(command_line_macros).unwrap_err());
    auto native_values       = rstd::move(native_macros).unwrap();
    auto command_line_values = rstd::move(command_line_macros).unwrap();

    auto include_command = clone_command(base_command);
    command::push_option(include_command, clang_options::PREPROCESS);
    command::push_option(include_command, clang_options::VERBOSE);
    command::push_option(include_command, clang_options::LANGUAGE);
    command::push_option(include_command, clang_options::CXX_SOURCE);
    command::push_option(include_command, clang_options::STANDARD_INPUT);
    auto include_output =
        run_command_with_input(include_command, ""_str, environment, Some(working_directory));
    if (include_output.is_err()) return Err(rstd::move(include_output).unwrap_err());
    if (include_output->exit_code != i32 {}) {
        return environment_failure<PreprocessorEnvironment>(
            rstd::format("clang++ -E -v failed\n{}\n{}",
                         command_text(include_command).as_str(),
                         include_output->standard_error.as_str()));
    }
    auto includes = parse_include_search(include_output->standard_error.as_str());
    if (includes.is_err()) return Err(rstd::move(includes).unwrap_err());
    auto text_builtins = query_text_builtins(base_command, working_directory, environment);
    if (text_builtins.is_err()) return Err(rstd::move(text_builtins).unwrap_err());
    auto identity = environment_identity(
        builtin_environment->identity.as_str(), *includes, key.context_id.as_str());
    if (identity.is_err()) return Err(rstd::move(identity).unwrap_err());
    return Ok(PreprocessorEnvironment {
        .key                 = rstd::move(key),
        .builtin_environment = rstd::move(builtin_environment),
        .native_source       = rstd::move(native_values.source),
        .native_definitions  = rstd::move(native_values.definitions),
        .command_line_source = rstd::move(command_line_values.source),
        .command_line_macros = rstd::move(command_line_values.operations),
        .semantic_context    = rstd::move(semantic_context),
        .include_search      = rstd::move(includes).unwrap(),
        .query_command       = clone_command(base_command),
        .identity            = rstd::move(identity).unwrap(),
        .date                = rstd::move(text_builtins->date),
        .time                = rstd::move(text_builtins->time),
    });
}

} // namespace lito::toolchain
