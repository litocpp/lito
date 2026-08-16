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

constexpr auto cpp_thread_requirement_semantics() -> CppRequirementSemantics {
    return CppRequirementSemantics {
        .applications =
            CppRequirementApplications {
                .preprocess = true,
                .scan       = true,
                .bmi        = true,
                .compile    = true,
                .link       = true,
            },
        .domain = CppCompatibilityDomain::ImportClosure,
        .merge  = CppRequirementMergePolicy::Enable,
    };
}

constexpr auto cpp_system_library_requirement_semantics() -> CppRequirementSemantics {
    return CppRequirementSemantics {
        .applications = CppRequirementApplications { .link = true },
        .domain       = CppCompatibilityDomain::LinkClosure,
        .merge        = CppRequirementMergePolicy::SetUnion,
    };
}

auto merge_cpp_import_requirements(CppCompileOptions& left, CppCompileOptions& right) -> bool {
    constexpr auto semantics = cpp_thread_requirement_semantics();
    static_assert(semantics.domain == CppCompatibilityDomain::ImportClosure);
    static_assert(semantics.merge == CppRequirementMergePolicy::Enable);

    auto enabled          = left.threading.posix || right.threading.posix;
    auto changed          = left.threading.posix != enabled || right.threading.posix != enabled;
    left.threading.posix  = enabled;
    right.threading.posix = enabled;
    return changed;
}

struct CppSystemLibraryRequirement : DefaultInClass<CppSystemLibraryRequirement, Clone> {
    String name;
    String source;

    auto clone() const -> CppSystemLibraryRequirement {
        return CppSystemLibraryRequirement { .name = name.clone(), .source = source.clone() };
    }
};

struct CppLinkRequirements {
    bool                             posix_threads { false };
    Vec<String>                      thread_sources;
    Vec<CppSystemLibraryRequirement> system_libraries;

    auto clone() const -> CppLinkRequirements {
        return CppLinkRequirements {
            .posix_threads    = posix_threads,
            .thread_sources   = as<Clone>(thread_sources).clone(),
            .system_libraries = as<Clone>(system_libraries).clone(),
        };
    }
};

auto cpp_link_requirements_identity(const CppLinkRequirements& requirements) -> String {
    auto result = String::make("lito-cpp-link-requirements-v1\n"_str);
    result.push_str(requirements.posix_threads ? "posix-threads=true\n"_str
                                               : "posix-threads=false\n"_str);
    for (const auto& requirement : requirements.system_libraries) {
        result.push_str("system-library="_str);
        result.push_str(requirement.name.as_str());
        result.push_ascii('\n');
    }
    return result;
}

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
    CppLinkRequirements              link_requirements;
    String                           identity;

    auto clone() const -> ResolvedExternalDependency {
        auto copied_targets = Vec<ResolvedExternalTargetUsage>::with_capacity(targets.len());
        for (const auto& target : targets) copied_targets.push(target.clone());
        return ResolvedExternalDependency {
            .alias             = alias.clone(),
            .provider          = provider.clone(),
            .version           = version.clone(),
            .targets           = rstd::move(copied_targets),
            .link_arguments    = link_arguments.clone(),
            .link_requirements = link_requirements.clone(),
            .identity          = identity.clone(),
        };
    }
};

struct UsageRequirements {
    Vec<PathBuf>                     public_include_directories;
    Vec<PathBuf>                     private_include_directories;
    Vec<String>                      public_definitions;
    Vec<String>                      private_definitions;
    CppArgumentLayer                 arguments;
    CppArgumentLayer                 interface_arguments;
    CppLinkRequirements              link_requirements;
    Vec<String>                      linker_options;
    Vec<IncludeDirectoryRequirement> private_include_directory_requirements;
};

} // namespace lito::cpp
