module;
#include <rstd/enum.hpp>
#include <rstd/macro.hpp>

export module lito.cpp:usage;

import rstd;
import lito.core;
import :compiler.option;
import :c.compiler;
import :link;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;

export namespace lito::cpp
{

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

    auto enabled = lito::compiler::uses_posix_threads(left.common) ||
                   lito::compiler::uses_posix_threads(right.common);
    auto model =
        enabled ? lito::compiler::ThreadingModel::Posix : lito::compiler::ThreadingModel::None;
    auto changed           = left.common.threading != model || right.common.threading != model;
    left.common.threading  = model;
    right.common.threading = model;
    return changed;
}

class LanguageArgumentLayer : public DefaultInClass<LanguageArgumentLayer, Clone> {
    RSTD_ENUM_DEFAULT(LanguageArgumentLayer,
                      (Cpp),
                      (C, (lito::c::CArgumentLayer layer;)),
                      (Cpp, (CppArgumentLayer layer;)))

public:
    auto clone() const -> LanguageArgumentLayer {
        RSTD_MATCH(*this) {
            RSTD_CASE(C, layer) {
                return C(layer.clone());
            }
            RSTD_CASE(Cpp, layer) {
                return Cpp(as<Clone>(layer).clone());
            }
        }
        rstd::unreachable();
    }
};

struct ExternalTargetUsage {
    String                                 name;
    lito::dependency::DependencyVisibility visibility {
        lito::dependency::DependencyVisibility::Private
    };
    Vec<String> compile_options;
    String      compile_source;
    String      identity;

    auto clone() const -> ExternalTargetUsage {
        return ExternalTargetUsage {
            .name            = name.clone(),
            .visibility      = visibility,
            .compile_options = as<Clone>(compile_options).clone(),
            .compile_source  = compile_source.clone(),
            .identity        = identity.clone(),
        };
    }
};

struct ExternalDependencyUsage {
    String                       alias;
    String                       provider;
    String                       version;
    Vec<ExternalTargetUsage>     targets;
    lito::link::ArgumentSequence link_arguments;
    lito::link::Requirements     link_requirements;
    String                       identity;

    auto clone() const -> ExternalDependencyUsage {
        auto copied_targets = Vec<ExternalTargetUsage>::with_capacity(targets.len());
        for (const auto& target : targets) copied_targets.push(target.clone());
        return ExternalDependencyUsage {
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

struct ResolvedExternalTargetUsage {
    String                                 name;
    lito::dependency::DependencyVisibility visibility {
        lito::dependency::DependencyVisibility::Private
    };
    LanguageArgumentLayer compile_arguments;
    String                identity;

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
    lito::link::ArgumentSequence     link_arguments;
    lito::link::Requirements         link_requirements;
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

auto empty_language_arguments(lito::manifest::PackageLanguage language) -> LanguageArgumentLayer {
    return language == lito::manifest::PackageLanguage::C
               ? LanguageArgumentLayer::C(lito::c::CArgumentLayer {})
               : LanguageArgumentLayer::Cpp(CppArgumentLayer {});
}

struct UsageRequirements {
    Vec<PathBuf>                                       public_include_directories;
    Vec<PathBuf>                                       private_include_directories;
    Vec<String>                                        public_definitions;
    Vec<String>                                        private_definitions;
    LanguageArgumentLayer                              arguments;
    LanguageArgumentLayer                              interface_arguments;
    lito::link::Requirements                           link_requirements;
    Vec<String>                                        linker_options;
    Vec<lito::dependency::IncludeDirectoryRequirement> private_include_directory_requirements;
};

} // namespace lito::cpp
