export module lito.core:config.project;

import rstd;

using namespace rstd::prelude;

export namespace lito::config
{

enum class ConfigLoadMode
{
    Enabled,
    LocalDisabled,
    Disabled = LocalDisabled,
};

struct BuildOptionInput {
    Vec<String> arguments;
    String      source;

    auto clone() const -> BuildOptionInput {
        return BuildOptionInput {
            .arguments = arguments.clone(),
            .source    = source.clone(),
        };
    }
};

struct ProjectBuildOptions {
    Vec<BuildOptionInput> cpp;
    Vec<BuildOptionInput> c;
    Vec<BuildOptionInput> linker;

    auto clone() const -> ProjectBuildOptions {
        auto clone_inputs = [](const Vec<BuildOptionInput>& inputs) {
            auto result = Vec<BuildOptionInput>::with_capacity(inputs.len());
            for (const auto& input : inputs) result.push(input.clone());
            return result;
        };
        return ProjectBuildOptions {
            .cpp    = clone_inputs(cpp),
            .c      = clone_inputs(c),
            .linker = clone_inputs(linker),
        };
    }
};

enum class EnvironmentFlagPolicy
{
    Ignore,
    Append,
};

} // namespace lito::config
