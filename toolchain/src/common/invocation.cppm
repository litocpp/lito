export module lito.toolchain.common:invocation;

import rstd;
import lito.core;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

struct CompileCommandResult {
    i32                  exit_code {};
    String               standard_output;
    String               standard_error;
    rstd::time::Duration elapsed;
};

struct CompileInvocation {
    Vec<String>     arguments;
    Vec<String>     identity_inputs;
    PathBuf         working_directory;
    String          identity_working_directory;
    PathBuf         staged_object;
    Option<PathBuf> final_object;
    Option<PathBuf> staged_bmi;
    Option<PathBuf> final_bmi;

    auto clone() const -> CompileInvocation {
        return CompileInvocation {
            .arguments                  = arguments.clone(),
            .identity_inputs            = identity_inputs.clone(),
            .working_directory          = working_directory.clone(),
            .identity_working_directory = identity_working_directory.clone(),
            .staged_object              = staged_object.clone(),
            .final_object = final_object.is_some() ? Some(final_object->clone()) : None(),
            .staged_bmi   = staged_bmi.is_some() ? Some(staged_bmi->clone()) : None(),
            .final_bmi    = final_bmi.is_some() ? Some(final_bmi->clone()) : None(),
        };
    }

    auto identity() const -> String {
        auto result = String::make("lito-clang-compile-invocation-v1\n"_str);
        result.push_str(rstd::format("{}:{}\n",
                                     identity_working_directory.size(),
                                     identity_working_directory.as_str())
                            .as_str());
        for (const auto& argument : arguments) {
            result.push_str(rstd::format("{}:{}\n", argument.size(), argument.as_str()).as_str());
        }
        for (const auto& input : identity_inputs) {
            result.push_str(
                rstd::format("identity:{}:{}\n", input.size(), input.as_str()).as_str());
        }
        return result;
    }

    auto retained_bytes() const noexcept -> usize {
        auto result = arguments.capacity() * usize(sizeof(String)) +
                      identity_inputs.capacity() * usize(sizeof(String)) +
                      working_directory.capacity() + identity_working_directory.capacity() +
                      staged_object.capacity();
        for (const auto& argument : arguments) result += argument.capacity();
        for (const auto& input : identity_inputs) result += input.capacity();
        if (final_object.is_some()) result += final_object->capacity();
        if (staged_bmi.is_some()) result += staged_bmi->capacity();
        if (final_bmi.is_some()) result += final_bmi->capacity();
        return result;
    }
};

struct ResolvedCompilerPluginUsage {
    PathBuf     plugin;
    String      name;
    Vec<String> arguments;
    String      identity;
};

struct FrontendPluginInvocation {
    Vec<String> arguments;
    PathBuf     working_directory;
    PathBuf     source;
    String      action;
};

} // namespace lito
