module;
#include <rstd/enum.hpp>

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
    bool                  safety { false };

    auto clone() const -> ProjectBuildOptions {
        auto clone_inputs = [](const Vec<BuildOptionInput>& inputs) {
            return inputs.iter()
                .map([](auto input) {
                    return input->clone();
                })
                .collect<Vec<BuildOptionInput>>();
        };
        return ProjectBuildOptions {
            .cpp    = clone_inputs(cpp),
            .c      = clone_inputs(c),
            .linker = clone_inputs(linker),
            .safety = safety,
        };
    }
};

enum class EnvironmentFlagPolicy
{
    Ignore,
    Append,
};

struct AndroidTargetRequest {
    String abi;
    u32    minimum_api {};

    auto clone() const -> AndroidTargetRequest {
        return AndroidTargetRequest {
            .abi         = abi.clone(),
            .minimum_api = minimum_api,
        };
    }
};

class BuildTargetRequest : public DefaultInClass<BuildTargetRequest, Clone> {
    RSTD_ENUM_DEFAULT(BuildTargetRequest,
                      (Default),
                      (Default),
                      (Android, (AndroidTargetRequest target;)))

public:
    auto clone() const -> BuildTargetRequest {
        if (is_Default()) return Default();
        return Android(as_Android().target.clone());
    }
};

} // namespace lito::config
