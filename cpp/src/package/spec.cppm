module;
#include <rstd/enum.hpp>

export module lito.cpp:package.spec;

import rstd;
import lito.core;
import :bmi.artifact;
import :build.profile;
import :compiler.option;
import :package.target;
import :package.metadata;
import :usage;
import :c.compiler;

using namespace rstd::prelude;

export namespace lito::cpp
{

using TargetId = usize;

struct TargetSpec {
    lito::package::PackageTargetId     id;
    String                             package_source_identity;
    ArtifactKind                       artifact_kind { ArtifactKind::StaticLibrary };
    lito::manifest::PackageLanguage    language { lito::manifest::PackageLanguage::Cpp };
    String                             artifact_name;
    bool                               link_stdlib { true };
    String                             archive_stem;
    Option<String>                     module_affiliation;
    PathBuf                            root;
    PathBuf                            source_root;
    Vec<TargetSource>                  sources;
    Vec<DependencySpec>                dependencies;
    Vec<ResolvedExternalDependency>    external_dependencies;
    Vec<GeneratedArtifactContribution> generated_artifacts;
    UsageRequirements                  usage;
    Vec<ResolvedCompileTestCase>       compile_tests;
    Option<TestAttachmentTarget>       test_attachment;
    PackageCompileMetadata             compile_metadata;
};

struct PackageSpec {
    String                              name;
    PathBuf                             root;
    PathBuf                             manifest_path;
    String                              default_profile;
    Vec<lito::package::PackageTargetId> default_targets;
    lito::config::ToolchainSpec         toolchain;
    Vec<ProfileSpec>                    profiles;
    Vec<TargetSpec>                     targets;
};

class LanguageCompileContext : public DefaultInClass<LanguageCompileContext, Clone> {
    RSTD_ENUM_DEFAULT(
        LanguageCompileContext,
        (Cpp),
        (C, (lito::c::CCompileOptions options; lito::c::CPublicRequirements public_requirements;)),
        (Cpp,
         (BmiRequest bmi; CppCompileOptions options; CppPublicRequirements public_requirements;)))

public:
    auto clone() const -> LanguageCompileContext {
        RSTD_MATCH(*this) {
            RSTD_CASE(C, options, public_requirements) {
                return C(options.clone(), public_requirements.clone());
            }
            RSTD_CASE(Cpp, bmi, options, public_requirements) {
                return Cpp(bmi, as<Clone>(options).clone(), as<Clone>(public_requirements).clone());
            }
        }
        __builtin_unreachable();
    }
};

struct CompileContext {
    String                 id;
    String                 scan_id;
    LanguageCompileContext language;
    Vec<String>            external_identities;

    auto clone() const -> CompileContext {
        return CompileContext {
            .id                  = id.clone(),
            .scan_id             = scan_id.clone(),
            .language            = as<Clone>(language).clone(),
            .external_identities = as<Clone>(external_identities).clone(),
        };
    }
};

struct PreprocessorProjection {
    Vec<String> user_include_directories;
    Vec<String> system_include_directories;
    Vec<String> definitions;
    Vec<String> undefinitions;
    String      identity;
};

constexpr auto compile_language(const CompileContext& context) noexcept
    -> lito::manifest::PackageLanguage {
    return context.language.is_C() ? lito::manifest::PackageLanguage::C
                                   : lito::manifest::PackageLanguage::Cpp;
}

auto common_compile_options(const CompileContext& context) noexcept
    -> const lito::compiler::CommonCompileOptions& {
    return context.language.is_C() ? context.language.as_C().options.common
                                   : context.language.as_Cpp().options.common;
}

} // namespace lito::cpp
