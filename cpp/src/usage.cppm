export module lito.cpp:usage;

import rstd;
import lito.core;
import :compiler.option;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;

export namespace lito::cpp
{

struct LinkArgumentSequence {
    Vec<String> tokens;
    String      source;
    String      identity;

    auto clone() const -> LinkArgumentSequence {
        return LinkArgumentSequence {
            .tokens   = as<Clone>(tokens).clone(),
            .source   = source.clone(),
            .identity = identity.clone(),
        };
    }
};

struct ResolvedExternalTargetUsage {
    String               name;
    DependencyVisibility visibility { DependencyVisibility::Private };
    CppArgumentLayer     compile_arguments;
    String               identity;

    auto clone() const -> ResolvedExternalTargetUsage {
        return ResolvedExternalTargetUsage {
            .name              = name.clone(),
            .visibility        = visibility,
            .compile_arguments = as<Clone>(compile_arguments).clone(),
            .identity          = identity.clone(),
        };
    }
};

struct ResolvedExternalDependency {
    String                           alias;
    String                           provider;
    String                           version;
    Vec<ResolvedExternalTargetUsage> targets;
    LinkArgumentSequence             link_arguments;
    String                           identity;

    auto clone() const -> ResolvedExternalDependency {
        auto copied_targets = Vec<ResolvedExternalTargetUsage>::with_capacity(targets.len());
        for (const auto& target : targets) copied_targets.push(target.clone());
        return ResolvedExternalDependency {
            .alias          = alias.clone(),
            .provider       = provider.clone(),
            .version        = version.clone(),
            .targets        = rstd::move(copied_targets),
            .link_arguments = link_arguments.clone(),
            .identity       = identity.clone(),
        };
    }
};

struct UsageRequirements {
    Vec<PathBuf>                     public_include_directories;
    Vec<PathBuf>                     private_include_directories;
    Vec<String>                      public_definitions;
    Vec<String>                      private_definitions;
    CppArgumentLayer                 public_arguments;
    CppArgumentLayer                 private_arguments;
    Vec<String>                      private_linker_options;
    Vec<IncludeDirectoryRequirement> private_include_directory_requirements;
};

} // namespace lito::cpp
