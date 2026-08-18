module;
#include <rstd/macro.hpp>

export module lito.cpp:package.adapter;

import rstd;
import lito.core;
import :build.configuration;
import lito.system;
import :compiler.option;
import :compiler.identity;
import :package.metadata;
import :package.spec;
import :package.target;
import :compiler.parser;
import :compiler.policy;
import :build.profile;
import :source.discovery;
import :usage;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;

export namespace lito::cpp
{

auto make_package_condition_context(const lito::package::ResolvedPackage& package,
                                    const BuildConfiguration&             configuration,
                                    const ProfileSpec&                    profile,
                                    const BuildPlatform& platform) -> lito::condition::Context {
    auto context = lito::condition::Context {};
    context.set_string(String::make("target.os"_str), platform.effective_target.os.clone());
    context.set_string(String::make("target.family"_str),
                       String::make(platform.effective_target.family_name()));
    context.set_string(String::make("target.arch"_str),
                       platform.effective_target.architecture.name.clone());
    context.set_string(String::make("target.triple"_str), platform.effective_target.triple.clone());
    context.set_string(String::make("host.os"_str), platform.host.os.clone());
    context.set_string(String::make("host.arch"_str), platform.host.architecture.name.clone());
    context.set_bool(String::make("build.cross"_str), platform.cross);
    context.set_string(String::make("profile.name"_str), profile.name.clone());
    context.set_string(String::make("toolchain.compiler"_str), String::make("clang"_str));
    context.set_string(
        String::make("toolchain.stdlib"_str),
        String::make(configuration.standard_library == lito::config::StandardLibrary::Libstdcxx
                         ? "libstdc++"_str
                         : "libc++"_str));
    for (const auto& feature : package.features) {
        auto key = String::make("feature."_str);
        key.push_str(feature.name.as_str());
        context.set_bool(rstd::move(key), feature.enabled);
    }
    return context;
}

auto resolve_target_source_groups(lito::package::ResolvedPackage& package,
                                  const lito::condition::Context& context)
    -> lito::package::PackageResult<empty> {
    for (auto& target : package.manifest.targets) {
        auto& source = lito::manifest::package_target_source(target);
        for (const auto& conditional : source.conditions) {
            auto matched = lito::condition::evaluate(conditional.condition, context);
            if (matched.is_err()) {
                return Err(lito::package::PackageError::Message(rstd::format(
                    "package '{}' manifest '{}' target '{}::{}' condition '{}' is invalid: {}",
                    package.manifest.name.as_str(),
                    package.manifest.manifest_path.as_path(),
                    package.manifest.name.as_str(),
                    lito::manifest::package_target_name(target),
                    conditional.source.as_str(),
                    rstd::move(matched).unwrap_err())));
            }
            if (! *matched) continue;
            for (const auto& group : conditional.source_groups) {
                auto present = false;
                for (const auto& existing : source.source_groups) {
                    if (existing == group.as_str()) present = true;
                }
                if (! present) source.source_groups.push(group.clone());
            }
        }
        source.conditions.clear();
    }
    return Ok(empty {});
}

} // namespace lito::cpp

namespace lito::cpp
{

template<typename T>
auto adapter_failure(String message) -> lito::package::PackageResult<T> {
    return Err(lito::package::PackageError::Message(rstd::move(message)));
}

auto parse_options(const CppArgumentParser&        parser,
                   const Vec<String>&              options,
                   String                          source,
                   lito::manifest::PackageLanguage language)
    -> lito::package::PackageResult<LanguageArgumentLayer> {
    if (language == lito::manifest::PackageLanguage::C) {
        auto parsed = parser.parse_c(options, source.as_str());
        if (parsed.is_err()) {
            return Err(lito::package::PackageError::Configuration(
                erase_error(rstd::move(parsed).unwrap_err())));
        }
        return Ok(LanguageArgumentLayer::C(rstd::move(parsed).unwrap()));
    }
    auto parsed = parser.parse(options, source.as_str());
    if (parsed.is_err()) {
        return Err(lito::package::PackageError::Configuration(
            erase_error(rstd::move(parsed).unwrap_err())));
    }
    return Ok(LanguageArgumentLayer::Cpp(rstd::move(parsed).unwrap()));
}

auto usage_source(const lito::manifest::PackageManifest& package, ref<str> field) -> String {
    return rstd::format("package '{}' manifest '{}' {}",
                        package.name.as_str(),
                        package.manifest_path.as_path(),
                        field);
}

auto definition_name(ref<str> value) -> ref<str> {
    auto separator = value.find("="_str);
    return separator.is_some() ? value.split_at(*separator).template get<0>() : value;
}

auto package_metadata_macro(ref<str> name) -> bool {
    return name == "LITO_PKG_VERSION"_str || name.starts_with("LITO_FEAT_"_str);
}

auto valid_system_library_name(ref<str> name) -> bool {
    return ! name.is_empty() && ! name.starts_with("-"_str) && ! name.starts_with("/"_str) &&
           ! name.contains("/"_str) && ! name.contains("\\"_str) && ! name.contains(":"_str) &&
           ! name.contains(" "_str) && ! name.contains("\t"_str) && ! name.contains("\n"_str) &&
           ! name.contains("\r"_str);
}

auto validate_usage(const lito::manifest::PackageManifest& package, bool has_link_action)
    -> lito::package::PackageResult<empty> {
    const auto& usage                = package.usage;
    auto        validate_definitions = [&](const Vec<String>& definitions,
                                           ref<str> field) -> lito::package::PackageResult<empty> {
        for (const auto& definition : definitions) {
            auto name = definition_name(definition.as_str());
            if (! is_profile_owned_definition(definition.as_str()) &&
                ! package_metadata_macro(name)) {
                continue;
            }
            return adapter_failure<empty>(
                rstd::format("{} definition '{}' overrides a Lito-owned setting",
                             usage_source(package, field).as_str(),
                             definition.as_str()));
        }
        return Ok(empty {});
    };
    rstd_try(validate_definitions(usage.public_definitions, "public-definitions"_str));
    rstd_try(validate_definitions(usage.private_definitions, "private-definitions"_str));
    if (! has_link_action && ! usage.linker_options.is_empty()) {
        return adapter_failure<empty>(
            rstd::format("{} requires a binary, test, or benchmark target",
                         usage_source(package, "usage.linker-options"_str).as_str()));
    }
    for (auto index = usize {}; index < usage.linker_options.len(); ++index) {
        const auto& option = usage.linker_options[index];
        if (option.as_str() == "-pthread"_str) {
            return adapter_failure<empty>(
                rstd::format("{} option '-pthread' must be declared as usage.threads",
                             usage_source(package, "usage.linker-options"_str).as_str()));
        }
        if (! option.as_str().starts_with("-stdlib="_str) && option.as_str() != "-nostdlib++"_str &&
            ! is_profile_owned_linker_option(option.as_str())) {
            continue;
        }
        return adapter_failure<empty>(
            rstd::format("{} option '{}' overrides a Lito-owned setting",
                         usage_source(package, "usage.linker-options"_str).as_str(),
                         option.as_str()));
    }
    return Ok(empty {});
}

auto output_name(ArtifactKind kind, ref<str> declared_name) -> String {
    if (kind != ArtifactKind::StaticLibrary && kind != ArtifactKind::TestAttachmentArchive) {
        return String::make(declared_name);
    }
    auto result = String::make("lib"_str);
    result.push_str(declared_name);
    result.push_str(kind == ArtifactKind::StaticLibrary ? ".a"_str : ".test.a"_str);
    return result;
}

auto contains_source(const Vec<PathBuf>& sources, ref<rstd::path::Path> candidate) -> bool {
    for (const auto& source : sources) {
        if (source.as_path() == candidate) return true;
    }
    return false;
}

auto append_conditional_unique(Vec<String>& output, const Vec<String>& input) -> void {
    for (const auto& value : input) {
        auto present = false;
        for (const auto& existing : output) {
            if (existing.as_str() == value.as_str()) {
                present = true;
                break;
            }
        }
        if (! present) output.push(value.clone());
    }
}

auto append_conditional_unique(Vec<PathBuf>& output, const Vec<PathBuf>& input) -> void {
    for (const auto& value : input) {
        auto present = false;
        for (const auto& existing : output) {
            if (existing.as_path() == value.as_path()) {
                present = true;
                break;
            }
        }
        if (! present) output.push(value.clone());
    }
}

auto append_conditional_unique(Vec<lito::dependency::IncludeDirectoryRequirement>&       output,
                               const Vec<lito::dependency::IncludeDirectoryRequirement>& input)
    -> void {
    for (const auto& value : input) {
        auto present = false;
        for (const auto& existing : output) {
            if (existing.root == value.root && existing.path.as_path() == value.path.as_path()) {
                present = true;
                break;
            }
        }
        if (! present) output.push(value.clone());
    }
}

struct DefinitionRecord {
    String name;
    String value;
    String source;
};

auto resolve_package_configuration(lito::package::ResolvedPackage& package,
                                   const BuildConfiguration&       configuration,
                                   const ProfileSpec&              profile,
                                   const BuildPlatform&            platform,
                                   bool has_library) -> lito::package::PackageResult<empty> {
    auto context = make_package_condition_context(package, configuration, profile, platform);

    auto       matched_threads        = Option<bool> {};
    auto       matched_threads_source = Option<String> {};
    auto       definitions            = Vec<DefinitionRecord>::make();
    const auto record_definitions = [&](const Vec<String>& values,
                                        ref<str> source) -> lito::package::PackageResult<empty> {
        for (const auto& value : values) {
            const auto name      = definition_name(value.as_str());
            auto       duplicate = false;
            for (const auto& existing : definitions) {
                if (existing.name.as_str() != name) continue;
                if (existing.value.as_str() == value.as_str()) {
                    duplicate = true;
                    break;
                }
                return adapter_failure<empty>(
                    rstd::format("package '{}' defines macro '{}' as '{}' from {} and '{}' from {}",
                                 package.manifest.name.as_str(),
                                 name,
                                 existing.value.as_str(),
                                 existing.source.as_str(),
                                 value.as_str(),
                                 source));
            }
            if (duplicate) continue;
            definitions.push(DefinitionRecord {
                .name   = String::make(name),
                .value  = value.clone(),
                .source = String::make(source),
            });
        }
        return Ok(empty {});
    };
    rstd_try(record_definitions(
        package.manifest.usage.public_definitions,
        usage_source(package.manifest, "usage.public-definitions"_str).as_str()));
    rstd_try(record_definitions(
        package.manifest.usage.private_definitions,
        usage_source(package.manifest, "usage.private-definitions"_str).as_str()));
    for (const auto& conditional : package.manifest.conditions) {
        auto matched = lito::condition::evaluate(conditional.condition, context);
        if (matched.is_err()) {
            return adapter_failure<empty>(
                rstd::format("package '{}' manifest '{}' condition '{}' is invalid: {}",
                             package.manifest.name.as_str(),
                             package.manifest.manifest_path.as_path(),
                             conditional.source.as_str(),
                             rstd::move(matched).unwrap_err()));
        }
        if (! *matched) continue;
        const auto& overlay            = conditional.usage;
        auto&       usage              = package.manifest.usage;
        const auto  conditional_source = rstd::format("manifest '{}' condition '{}'",
                                                      package.manifest.manifest_path.as_path(),
                                                      conditional.source.as_str());
        rstd_try(
            record_definitions(overlay.values.public_definitions, conditional_source.as_str()));
        rstd_try(
            record_definitions(overlay.values.private_definitions, conditional_source.as_str()));
        append_conditional_unique(usage.public_include_directories,
                                  overlay.values.public_include_directories);
        append_conditional_unique(usage.private_include_directories,
                                  overlay.values.private_include_directories);
        append_conditional_unique(usage.public_definitions, overlay.values.public_definitions);
        append_conditional_unique(usage.private_definitions, overlay.values.private_definitions);
        append_conditional_unique(usage.options, overlay.values.options);
        append_conditional_unique(usage.linker_options, overlay.values.linker_options);
        append_conditional_unique(usage.system_libraries, overlay.values.system_libraries);
        append_conditional_unique(usage.private_include_directory_requirements,
                                  overlay.values.private_include_directory_requirements);
        append_conditional_unique(usage.public_include_directory_requirements,
                                  overlay.values.public_include_directory_requirements);
        if (overlay.declares_threads) {
            if (matched_threads.is_some() && *matched_threads != overlay.values.threads) {
                return adapter_failure<empty>(
                    rstd::format("package '{}' has conflicting usage.threads values from {} and {}",
                                 package.manifest.name.as_str(),
                                 matched_threads_source->as_str(),
                                 conditional_source.as_str()));
            }
            matched_threads        = Some<bool>(overlay.values.threads);
            matched_threads_source = Some(conditional_source.clone());
            usage.threads          = overlay.values.threads;
        }
    }
    package.manifest.conditions.clear();

    rstd_try(resolve_target_source_groups(package, context));

    if (! has_library &&
        (! package.manifest.usage.public_include_directories.is_empty() ||
         ! package.manifest.usage.public_include_directory_requirements.is_empty() ||
         ! package.manifest.usage.public_definitions.is_empty())) {
        return adapter_failure<empty>(
            rstd::format("package '{}' conditional public usage requires a library target",
                         package.manifest.name.as_str()));
    }
    return Ok(empty {});
}

auto validate_package_metadata_arguments(const lito::manifest::PackageManifest& package,
                                         const LanguageArgumentLayer&           arguments)
    -> lito::package::PackageResult<empty> {
    if (arguments.is_C()) {
        for (const auto& occurrence : arguments.as_C().layer.occurrences) {
            if (! occurrence.argument.is_Macro()) continue;
            const auto& value = occurrence.argument.as_Macro().directive.value;
            auto        name  = definition_name(value.as_str());
            if (! package_metadata_macro(name)) continue;
            return adapter_failure<empty>(
                rstd::format("package '{}' option from {} overrides Lito-owned macro '{}'",
                             package.name.as_str(),
                             occurrence.source.as_str(),
                             name));
        }
        return Ok(empty {});
    }
    for (const auto& occurrence : arguments.as_Cpp().layer.occurrences) {
        if (! occurrence.argument.is_Macro()) continue;
        const auto& value = occurrence.argument.as_Macro().directive.value;
        auto        name  = definition_name(value.as_str());
        if (! package_metadata_macro(name)) continue;
        return adapter_failure<empty>(
            rstd::format("package '{}' option from {} overrides Lito-owned macro '{}'",
                         package.name.as_str(),
                         occurrence.source.as_str(),
                         name));
    }
    return Ok(empty {});
}

auto package_compile_metadata(const lito::package::ResolvedPackage& package)
    -> PackageCompileMetadata {
    auto result = PackageCompileMetadata {};
    if (package.manifest.version.value.is_some()) {
        result.version = Some(package.manifest.version.value->clone());
    }
    result.features = Vec<PackageFeatureState>::with_capacity(package.features.len());
    for (const auto& feature : package.features) {
        result.features.push(PackageFeatureState {
            .name       = feature.name.clone(),
            .macro_name = feature.macro_name.clone(),
            .enabled    = feature.enabled,
        });
    }
    return result;
}

auto promoted_arguments(const LanguageArgumentLayer& arguments) -> LanguageArgumentLayer {
    if (arguments.is_C()) {
        auto result = lito::c::CArgumentLayer {};
        for (const auto& occurrence : arguments.as_C().layer.occurrences) {
            auto promoted = occurrence.argument.is_Common() &&
                            occurrence.argument.as_Common().argument.is_Threading();
            if (promoted) result.occurrences.push(occurrence.clone());
        }
        return LanguageArgumentLayer::C(rstd::move(result));
    }
    auto result = CppArgumentLayer {};
    for (const auto& occurrence : arguments.as_Cpp().layer.occurrences) {
        auto promoted = occurrence.argument.is_Common() &&
                        occurrence.argument.as_Common().argument.is_Threading();
        if (occurrence.argument.is_Macro()) {
            promoted = is_cpp_standard_library_mode_macro(
                occurrence.argument.as_Macro().directive.value.as_str());
        }
        if (promoted) result.occurrences.push(as<Clone>(occurrence).clone());
    }
    return LanguageArgumentLayer::Cpp(rstd::move(result));
}

auto usage_link_requirements(const lito::manifest::PackageManifest& package,
                             const LanguageArgumentLayer&           arguments)
    -> lito::package::PackageResult<CppLinkRequirements> {
    const auto& usage  = package.usage;
    auto        result = CppLinkRequirements {};
    if (usage.threads) {
        result.posix_threads = true;
        result.thread_sources.push(usage_source(package, "usage.threads"_str));
    }
    if (arguments.is_C()) {
        for (const auto& occurrence : arguments.as_C().layer.occurrences) {
            if (! occurrence.argument.is_Common() ||
                ! occurrence.argument.as_Common().argument.is_Threading())
                continue;
            result.posix_threads = true;
            result.thread_sources.push(occurrence.source.clone());
        }
    } else {
        for (const auto& occurrence : arguments.as_Cpp().layer.occurrences) {
            if (! occurrence.argument.is_Common() ||
                ! occurrence.argument.as_Common().argument.is_Threading())
                continue;
            result.posix_threads = true;
            result.thread_sources.push(occurrence.source.clone());
        }
    }
    for (const auto& library : usage.system_libraries) {
        if (! valid_system_library_name(library.as_str())) {
            return adapter_failure<CppLinkRequirements>(
                rstd::format("{} contains invalid logical library name '{}'",
                             usage_source(package, "usage.system-libraries"_str).as_str(),
                             library.as_str()));
        }
        result.system_libraries.push(CppSystemLibraryRequirement {
            .name   = library.clone(),
            .source = usage_source(package, "usage.system-libraries"_str),
        });
    }
    return Ok(rstd::move(result));
}

auto materialize_external_include_requirements(usize                            package_index,
                                               lito::manifest::PackageManifest& manifest,
                                               const ExternalSourceRootCatalog& catalog)
    -> lito::package::PackageResult<empty> {
    const auto materialize = [&](Vec<PathBuf>&                                       output,
                                 Vec<lito::dependency::IncludeDirectoryRequirement>& requirements)
        -> lito::package::PackageResult<empty> {
        auto retained = Vec<lito::dependency::IncludeDirectoryRequirement>::make();
        for (const auto& requirement : requirements) {
            if (requirement.root != lito::dependency::IncludeDirectoryRoot::ExternalSource) {
                retained.push(requirement.clone());
                continue;
            }
            const ExternalSourceRoot* source = nullptr;
            for (const auto& candidate : catalog.sources) {
                if (candidate.package == package_index && requirement.external_source.is_some() &&
                    candidate.name == requirement.external_source->as_str()) {
                    source = rstd::addressof(candidate);
                    break;
                }
            }
            if (source == nullptr) {
                return adapter_failure<empty>(rstd::format(
                    "package '{}' external include source '{}' was not prepared",
                    manifest.name.as_str(),
                    requirement.external_source.is_some() ? requirement.external_source->as_str()
                                                          : "<none>"_str));
            }
            auto requested = source->root.join(requirement.path.as_path());
            auto canonical = rstd::fs::canonicalize(requested.as_path());
            if (canonical.is_err()) {
                return adapter_failure<empty>(rstd::format(
                    "package '{}' cannot resolve external include directory '{}' from source '{}'",
                    manifest.name.as_str(),
                    requirement.path.as_path(),
                    source->name.as_str()));
            }
            if (canonical->as_path().strip_prefix(source->root.as_path()).is_none()) {
                return adapter_failure<empty>(
                    rstd::format("package '{}' external include directory '{}' escapes source '{}'",
                                 manifest.name.as_str(),
                                 requirement.path.as_path(),
                                 source->name.as_str()));
            }
            auto metadata = rstd::fs::metadata(canonical->as_path());
            if (metadata.is_err() || ! metadata->is_dir()) {
                return adapter_failure<empty>(
                    rstd::format("package '{}' external include directory '{}' is not a directory",
                                 manifest.name.as_str(),
                                 canonical->as_path()));
            }
            auto repeated = false;
            for (const auto& existing : output) {
                if (existing.as_path() == canonical->as_path()) repeated = true;
            }
            if (! repeated) output.push(rstd::move(canonical).unwrap());
        }
        requirements = rstd::move(retained);
        return Ok(empty {});
    };
    rstd_try(materialize(manifest.usage.public_include_directories,
                         manifest.usage.public_include_directory_requirements));
    return materialize(manifest.usage.private_include_directories,
                       manifest.usage.private_include_directory_requirements);
}

auto clone_usage(const lito::dependency::DeclaredUsageRequirements& usage,
                 const LanguageArgumentLayer&                       arguments,
                 const LanguageArgumentLayer&                       interface_arguments,
                 const CppLinkRequirements& link_requirements) -> UsageRequirements {
    auto include_requirements = Vec<lito::dependency::IncludeDirectoryRequirement>::with_capacity(
        usage.private_include_directory_requirements.len());
    for (const auto& requirement : usage.private_include_directory_requirements) {
        include_requirements.push(requirement.clone());
    }
    auto public_definitions = as<Clone>(usage.public_definitions).clone();
    for (const auto& definition : usage.private_definitions) {
        if (is_cpp_standard_library_mode_macro(definition.as_str())) {
            public_definitions.push(definition.clone());
        }
    }
    return UsageRequirements {
        .public_include_directories  = as<Clone>(usage.public_include_directories).clone(),
        .private_include_directories = as<Clone>(usage.private_include_directories).clone(),
        .public_definitions          = rstd::move(public_definitions),
        .private_definitions         = as<Clone>(usage.private_definitions).clone(),
        .arguments                   = as<Clone>(arguments).clone(),
        .interface_arguments         = as<Clone>(interface_arguments).clone(),
        .link_requirements           = link_requirements.clone(),
        .linker_options              = as<Clone>(usage.linker_options).clone(),
        .private_include_directory_requirements = rstd::move(include_requirements),
    };
}

auto clone_dependencies(const Vec<DependencySpec>& dependencies) -> Vec<DependencySpec> {
    auto result = Vec<DependencySpec>::with_capacity(dependencies.len());
    for (const auto& dependency : dependencies) {
        result.push(DependencySpec {
            .target     = dependency.target.clone(),
            .visibility = dependency.visibility,
        });
    }
    return result;
}

auto clone_external_dependencies(const Vec<ResolvedExternalDependency>& dependencies)
    -> Vec<ResolvedExternalDependency> {
    auto result = Vec<ResolvedExternalDependency>::with_capacity(dependencies.len());
    for (const auto& dependency : dependencies) result.push(dependency.clone());
    return result;
}

auto target_artifact_kind(lito::package::PackageTargetKind kind) -> ArtifactKind {
    switch (kind) {
    case lito::package::PackageTargetKind::Library: return ArtifactKind::StaticLibrary;
    case lito::package::PackageTargetKind::Binary: return ArtifactKind::Executable;
    case lito::package::PackageTargetKind::Test: return ArtifactKind::TestExecutable;
    case lito::package::PackageTargetKind::Benchmark: return ArtifactKind::BenchmarkExecutable;
    case lito::package::PackageTargetKind::TestAttachment:
        return ArtifactKind::TestAttachmentArchive;
    case lito::package::PackageTargetKind::CompileTest: return ArtifactKind::CompileTest;
    }
    return ArtifactKind::Executable;
}

auto development_target(lito::package::PackageTargetKind kind) noexcept -> bool {
    return kind == lito::package::PackageTargetKind::Test ||
           kind == lito::package::PackageTargetKind::Benchmark ||
           kind == lito::package::PackageTargetKind::CompileTest;
}

auto library_targets(const lito::package::ResolvedPackageGraph& graph)
    -> rstd::collections::BTreeMap<String, lito::package::PackageTargetId> {
    auto result = rstd::collections::BTreeMap<String, lito::package::PackageTargetId>::make();
    for (const auto& package : graph.packages) {
        for (const auto& target : package.manifest.targets) {
            if (lito::manifest::package_target_kind(target) !=
                lito::package::PackageTargetKind::Library)
                continue;
            result.insert(package.manifest.name.clone(),
                          lito::package::PackageTargetId {
                              .package = package.manifest.name.clone(),
                              .kind    = lito::package::PackageTargetKind::Library,
                              .name    = String::make(lito::manifest::package_target_name(target)),
                          });
            break;
        }
    }
    return result;
}

auto selected_target(const Vec<lito::package::PackageTargetId>& selected,
                     const lito::package::PackageTargetId&      target) -> bool {
    for (const auto& candidate : selected) {
        if (candidate == target) return true;
    }
    return false;
}

auto resolve_source_groups(usize                                       package_index,
                           const lito::package::ResolvedPackage&       package,
                           const lito::manifest::TargetSourceManifest& target,
                           const ExternalSourceRootCatalog&            catalog)
    -> lito::package::PackageResult<Vec<ResolvedSourceGroup>> {
    auto result = Vec<ResolvedSourceGroup>::make();
    for (const auto& name : target.source_groups) {
        const lito::manifest::SourceGroupManifest* declaration = nullptr;
        for (const auto& group : package.manifest.source_groups) {
            if (group.name == name.as_str()) {
                declaration = rstd::addressof(group);
                break;
            }
        }
        if (declaration == nullptr) {
            return adapter_failure<Vec<ResolvedSourceGroup>>(
                rstd::format("package '{}' target source group '{}' is not declared",
                             package.manifest.name.as_str(),
                             name.as_str()));
        }
        auto root      = package.manifest.root.clone();
        auto identity  = package.source_identity.clone();
        auto generated = declaration->root == lito::manifest::SourceGroupRoot::Generated;
        auto external  = false;
        if (declaration->external_source.is_some()) {
            const ExternalSourceRoot* source = nullptr;
            for (const auto& candidate : catalog.sources) {
                if (candidate.package == package_index &&
                    candidate.name == declaration->external_source->as_str()) {
                    source = rstd::addressof(candidate);
                    break;
                }
            }
            if (source == nullptr) {
                return adapter_failure<Vec<ResolvedSourceGroup>>(rstd::format(
                    "package '{}' source group '{}' external source '{}' was not prepared",
                    package.manifest.name.as_str(),
                    declaration->name.as_str(),
                    declaration->external_source->as_str()));
            }
            root     = source->root.clone();
            identity = source->identity.clone();
            external = true;
        }
        result.push(ResolvedSourceGroup {
            .name      = declaration->name.clone(),
            .root      = rstd::move(root),
            .identity  = rstd::move(identity),
            .sources   = as<Clone>(declaration->sources).clone(),
            .generated = generated,
            .external  = external,
        });
    }
    return Ok(rstd::move(result));
}

} // namespace lito::cpp

export namespace lito::cpp
{

auto apply_package_configuration(lito::package::ResolvedPackage& package,
                                 const BuildConfiguration&       configuration,
                                 const ProfileSpec&              profile,
                                 const BuildPlatform&            platform,
                                 bool has_library) -> lito::package::PackageResult<empty> {
    return resolve_package_configuration(package, configuration, profile, platform, has_library);
}

struct ExternalPackageUsage {
    String                       package;
    Vec<ExternalDependencyUsage> dependencies;
    bool                         consumed { false };
};

struct ExternalUsageCatalog {
    Vec<ExternalPackageUsage> packages;

    auto take(ref<str> package) -> lito::package::PackageResult<Vec<ExternalDependencyUsage>> {
        for (auto& entry : packages) {
            if (entry.package.as_str() != package) continue;
            if (entry.consumed) {
                return adapter_failure<Vec<ExternalDependencyUsage>>(rstd::format(
                    "external usage for package '{}' was consumed more than once", package));
            }
            entry.consumed = true;
            return Ok(rstd::move(entry.dependencies));
        }
        return adapter_failure<Vec<ExternalDependencyUsage>>(
            rstd::format("external usage catalog has no package '{}'", package));
    }

    auto all_consumed() const noexcept -> bool {
        for (const auto& entry : packages) {
            if (! entry.consumed) return false;
        }
        return true;
    }
};

auto resolve_external_usage(Vec<ExternalDependencyUsage>    dependencies,
                            lito::manifest::PackageLanguage language,
                            const CppArgumentParser&        parser)
    -> lito::package::PackageResult<Vec<ResolvedExternalDependency>> {
    auto result = Vec<ResolvedExternalDependency>::with_capacity(dependencies.len());
    for (auto& dependency : dependencies) {
        auto targets = Vec<ResolvedExternalTargetUsage>::with_capacity(dependency.targets.len());
        for (auto& target : dependency.targets) {
            auto arguments = rstd_try(parse_options(
                parser, target.compile_options, target.compile_source.clone(), language));
            targets.push(ResolvedExternalTargetUsage {
                .name              = rstd::move(target.name),
                .visibility        = target.visibility,
                .compile_arguments = rstd::move(arguments),
                .identity          = rstd::move(target.identity),
            });
        }
        result.push(ResolvedExternalDependency {
            .alias             = rstd::move(dependency.alias),
            .provider          = rstd::move(dependency.provider),
            .version           = rstd::move(dependency.version),
            .targets           = rstd::move(targets),
            .link_arguments    = rstd::move(dependency.link_arguments),
            .link_requirements = rstd::move(dependency.link_requirements),
            .identity          = rstd::move(dependency.identity),
        });
    }
    return Ok(rstd::move(result));
}

auto adapt_package_graph_metadata(lito::package::ResolvedPackageGraph        graph,
                                  const Vec<String>&                         selected_package_names,
                                  const Vec<lito::package::PackageTargetId>& selected_targets,
                                  const BuildConfiguration&                  configuration,
                                  ProfileSpec                                profile,
                                  const BuildPlatform&                       platform,
                                  ExternalUsageCatalog                       external_usage,
                                  ExternalSourceRootCatalog                  external_sources,
                                  const CppArgumentParser&                   argument_parser)
    -> lito::package::PackageResult<PackageMetadata> {
    if (! is_supported_cpp_standard(configuration.language_standard.as_str()) ||
        configuration.toolchain.cxx.is_empty() || configuration.toolchain.ld.is_empty() ||
        configuration.toolchain.ar.is_empty()) {
        return adapter_failure<PackageMetadata>(
            String::make("invalid build configuration for package graph"_str));
    }
    auto libraries = library_targets(graph);

    auto selected = rstd::collections::BTreeMap<String, empty>::make();
    for (const auto& name : selected_package_names) selected.insert(name.clone(), empty {});

    auto external_by_package =
        Vec<Vec<ResolvedExternalDependency>>::with_capacity(graph.packages.len());
    for (usize index {}; index < graph.packages.len(); ++index) {
        external_by_package.emplace_back();
        if (! selected.contains_key(graph.packages[index].manifest.name.as_str())) continue;
        auto unresolved =
            rstd_try(external_usage.take(graph.packages[index].manifest.name.as_str()));
        if (graph.packages[index].manifest.standard.is_none()) {
            return adapter_failure<PackageMetadata>(
                rstd::format("selected package '{}' has external usage but no language contract",
                             graph.packages[index].manifest.name.as_str()));
        }
        external_by_package[index] = rstd_try(resolve_external_usage(
            rstd::move(unresolved),
            lito::manifest::package_standard_language(*graph.packages[index].manifest.standard),
            argument_parser));
    }
    if (! external_usage.all_consumed()) {
        return adapter_failure<PackageMetadata>(
            String::make("external usage catalog contains an unselected package"_str));
    }

    for (const auto& package : graph.packages) {
        for (const auto& dependency : package.dependencies) {
            if (! libraries.contains_key(dependency.name.as_str())) {
                return adapter_failure<PackageMetadata>(
                    rstd::format("package '{}' depends on package '{}' which does not expose a "
                                 "library target",
                                 package.manifest.name.as_str(),
                                 dependency.name.as_str()));
            }
        }
        for (const auto& dependency : package.dev_dependencies) {
            if (! libraries.contains_key(dependency.name.as_str())) {
                return adapter_failure<PackageMetadata>(
                    rstd::format("package '{}' has development dependency '{}' which does not "
                                 "expose a library target",
                                 package.manifest.name.as_str(),
                                 dependency.name.as_str()));
            }
        }
    }

    auto targets     = Vec<ResolvedTarget>::make();
    auto build_tools = Vec<PackageBuildToolRequirement>::make();
    for (usize package_index {}; package_index < graph.packages.len(); ++package_index) {
        auto& package = graph.packages[package_index];
        if (! selected.contains_key(package.manifest.name.as_str())) continue;
        if (package.manifest.standard.is_none()) {
            return adapter_failure<PackageMetadata>(
                rstd::format("selected package '{}' has compile targets but no language contract",
                             package.manifest.name.as_str()));
        }
        const auto package_language =
            lito::manifest::package_standard_language(*package.manifest.standard);
        auto has_link_action = false;
        auto has_library     = false;
        for (const auto& target : package.manifest.targets) {
            auto kind = lito::manifest::package_target_kind(target);
            if (kind == lito::package::PackageTargetKind::Library) has_library = true;
            if (kind == lito::package::PackageTargetKind::Binary ||
                kind == lito::package::PackageTargetKind::Test ||
                kind == lito::package::PackageTargetKind::Benchmark) {
                has_link_action = true;
            }
        }
        rstd_try(
            resolve_package_configuration(package, configuration, profile, platform, has_library));
        rstd_try(materialize_external_include_requirements(
            package_index, package.manifest, external_sources));
        rstd_try(validate_usage(package.manifest, has_link_action));
        for (auto& requirement : package.manifest.build_tools) {
            build_tools.push(PackageBuildToolRequirement {
                .package     = package.manifest.name.clone(),
                .root        = package.manifest.root.clone(),
                .requirement = rstd::move(requirement),
            });
        }
        auto arguments = rstd_try(parse_options(argument_parser,
                                                package.manifest.usage.options,
                                                usage_source(package.manifest, "usage.options"_str),
                                                package_language));
        rstd_try(validate_package_metadata_arguments(package.manifest, arguments));
        if (package.manifest.usage.threads) {
            if (arguments.is_C()) {
                arguments.as_C().layer.occurrences.push(lito::c::CCompilerArgumentOccurrence {
                    .argument = lito::c::CCompilerArgument::Common(
                        lito::compiler::CommonCompilerArgument::Threading(
                            lito::compiler::ThreadingModel::Posix)),
                    .source = usage_source(package.manifest, "usage.threads"_str),
                });
            } else {
                arguments.as_Cpp().layer.occurrences.push(CppCompilerArgumentOccurrence {
                    .argument = CppCompilerArgument::Common(
                        lito::compiler::CommonCompilerArgument::Threading(
                            lito::compiler::ThreadingModel::Posix)),
                    .source = usage_source(package.manifest, "usage.threads"_str),
                });
            }
        }
        auto interface_arguments = promoted_arguments(arguments);
        auto link_requirements   = rstd_try(usage_link_requirements(package.manifest, arguments));
        auto compile_metadata    = package_compile_metadata(package);
        auto compile_tests =
            Vec<ResolvedCompileTestCase>::with_capacity(package.manifest.compile_tests.len());
        for (const auto& test : package.manifest.compile_tests) {
            auto arguments = rstd_try(parse_options(argument_parser,
                                                    test.options,
                                                    rstd::format("package '{}'.compile-test '{}'",
                                                                 package.manifest.name.as_str(),
                                                                 test.name.as_str()),
                                                    package_language));
            rstd_try(validate_package_metadata_arguments(package.manifest, arguments));
            if (! arguments.is_Cpp()) {
                return adapter_failure<PackageMetadata>(rstd::format(
                    "C package '{}' cannot declare compile tests", package.manifest.name.as_str()));
            }
            auto cpp_arguments = rstd::move(arguments.as_Cpp().layer);
            compile_tests.push(ResolvedCompileTestCase {
                .name                    = test.name.clone(),
                .source                  = test.source.clone(),
                .outcome                 = test.outcome,
                .arguments               = rstd::move(cpp_arguments),
                .diagnostic_contains     = test.diagnostic_contains.clone(),
                .diagnostic_contains_any = test.diagnostic_contains_any.clone(),
            });
        }
        auto dependencies = Vec<DependencySpec>::with_capacity(package.dependencies.len());
        for (const auto& dependency : package.dependencies) {
            if (! selected.contains_key(dependency.name.as_str())) {
                return adapter_failure<PackageMetadata>(
                    rstd::format("resolved dependency '{}' is missing", dependency.name.as_str()));
            }
            auto library = libraries.get(dependency.name.as_str());
            if (library.is_none()) {
                return adapter_failure<PackageMetadata>(rstd::format(
                    "resolved dependency '{}' has no library target", dependency.name.as_str()));
            }
            dependencies.push(DependencySpec {
                .target     = (**library).clone(),
                .visibility = dependency.visibility,
            });
        }
        auto development_selected = false;
        for (const auto& selected_target : selected_targets) {
            if (selected_target.package == package.manifest.name.as_str() &&
                development_target(selected_target.kind)) {
                development_selected = true;
                break;
            }
        }
        auto dev_dependencies = Vec<DependencySpec>::with_capacity(package.dev_dependencies.len());
        if (development_selected) {
            for (const auto& dependency : package.dev_dependencies) {
                if (! selected.contains_key(dependency.name.as_str())) {
                    return adapter_failure<PackageMetadata>(
                        rstd::format("resolved development dependency '{}' is missing",
                                     dependency.name.as_str()));
                }
                auto library = libraries.get(dependency.name.as_str());
                if (library.is_none()) {
                    return adapter_failure<PackageMetadata>(
                        rstd::format("resolved development dependency '{}' has no library target",
                                     dependency.name.as_str()));
                }
                dev_dependencies.push(DependencySpec {
                    .target     = (**library).clone(),
                    .visibility = lito::dependency::DependencyVisibility::Private,
                });
            }
        }
        auto own_library = libraries.get(package.manifest.name.as_str());
        for (auto& manifest_target : package.manifest.targets) {
            const auto kind = lito::manifest::package_target_kind(manifest_target);
            auto       id   = lito::package::PackageTargetId {
                .package = package.manifest.name.clone(),
                .kind    = kind,
                .name    = String::make(lito::manifest::package_target_name(manifest_target)),
            };
            auto& source = lito::manifest::package_target_source(manifest_target);
            auto  source_groups =
                rstd_try(resolve_source_groups(package_index, package, source, external_sources));
            auto attachments = Vec<lito::manifest::TestAttachmentManifest>::make();
            if (manifest_target.is_Test()) {
                attachments = rstd::move(manifest_target.as_Test().attachments);
            }
            auto runtime_resources = Vec<lito::manifest::RuntimeResourceManifest>::make();
            if (manifest_target.is_Binary()) {
                runtime_resources = rstd::move(manifest_target.as_Binary().resources);
            }
            auto target_dependencies = clone_dependencies(dependencies);
            if (development_target(kind)) {
                for (const auto& dependency : dev_dependencies) {
                    target_dependencies.push(DependencySpec {
                        .target     = dependency.target.clone(),
                        .visibility = lito::dependency::DependencyVisibility::Private,
                    });
                }
            }
            if (kind != lito::package::PackageTargetKind::Library && own_library.is_some()) {
                target_dependencies.push(DependencySpec {
                    .target     = (**own_library).clone(),
                    .visibility = lito::dependency::DependencyVisibility::Private,
                });
            }
            targets.push(ResolvedTarget {
                .id            = rstd::move(id),
                .artifact_kind = target_artifact_kind(kind),
                .language      = package_language,
                .artifact_name =
                    String::make(lito::manifest::package_target_artifact_name(manifest_target)),
                .link_stdlib   = lito::manifest::package_target_links_stdlib(manifest_target),
                .source        = rstd::move(source),
                .source_groups = rstd::move(source_groups),
                .root          = package.manifest.root.clone(),
                .source_root   = package.manifest.source_root.clone(),
                .usage         = clone_usage(
                    package.manifest.usage, arguments, interface_arguments, link_requirements),
                .attachments       = rstd::move(attachments),
                .runtime_resources = rstd::move(runtime_resources),
                .dependencies      = rstd::move(target_dependencies),
                .external_dependencies =
                    clone_external_dependencies(external_by_package[package_index]),
                .compile_metadata = compile_metadata.clone(),
            });
        }
        if (! package.manifest.compile_tests.is_empty()) {
            auto sources = Vec<PathBuf>::with_capacity(package.manifest.compile_tests.len());
            for (const auto& test : package.manifest.compile_tests)
                sources.push(test.source.clone());
            auto compile_dependencies = clone_dependencies(dependencies);
            for (const auto& dependency : dev_dependencies) {
                compile_dependencies.push(DependencySpec {
                    .target     = dependency.target.clone(),
                    .visibility = lito::dependency::DependencyVisibility::Private,
                });
            }
            if (own_library.is_some()) {
                compile_dependencies.push(DependencySpec {
                    .target     = (**own_library).clone(),
                    .visibility = lito::dependency::DependencyVisibility::Private,
                });
            }
            targets.push(ResolvedTarget {
                .id =
                    lito::package::PackageTargetId {
                        .package = package.manifest.name.clone(),
                        .kind    = lito::package::PackageTargetKind::CompileTest,
                        .name    = package.manifest.name.clone(),
                    },
                .artifact_kind = ArtifactKind::CompileTest,
                .language      = package_language,
                .artifact_name = package.manifest.name.clone(),
                .source =
                    lito::manifest::TargetSourceManifest {
                        .discovery        = lito::manifest::SourceDiscoveryMode::Explicit,
                        .declared_sources = rstd::move(sources),
                    },
                .root        = package.manifest.root.clone(),
                .source_root = package.manifest.source_root.clone(),
                .usage       = clone_usage(
                    package.manifest.usage, arguments, interface_arguments, link_requirements),
                .compile_tests = rstd::move(compile_tests),
                .dependencies  = rstd::move(compile_dependencies),
                .external_dependencies =
                    clone_external_dependencies(external_by_package[package_index]),
                .compile_metadata = compile_metadata.clone(),
            });
        }
    }

    const auto real_target_count = targets.len();
    auto       attachments       = Vec<ResolvedTarget>::make();
    for (usize test_index {}; test_index < real_target_count; ++test_index) {
        const auto& test = targets[test_index];
        if (test.artifact_kind != ArtifactKind::TestExecutable ||
            ! selected_target(selected_targets, test.id)) {
            continue;
        }
        for (const auto& declaration : test.attachments) {
            auto direct = false;
            for (const auto& dependency : test.dependencies) {
                if (dependency.target.package == declaration.package.as_str()) {
                    direct = true;
                    break;
                }
            }
            if (! direct) {
                return adapter_failure<PackageMetadata>(rstd::format(
                    "test target '{}::{}' can only attach a direct dependency, but '{}' is not one",
                    test.id.package.as_str(),
                    test.id.name.as_str(),
                    declaration.package.as_str()));
            }
            auto library_id = libraries.get(declaration.package.as_str());
            if (library_id.is_none()) {
                return adapter_failure<PackageMetadata>(rstd::format(
                    "test attachment dependency '{}' is missing", declaration.package.as_str()));
            }
            const ResolvedTarget* library = nullptr;
            for (const auto& candidate : targets) {
                if (candidate.id == **library_id) {
                    library = rstd::addressof(candidate);
                    break;
                }
            }
            if (library == nullptr || library->artifact_kind != ArtifactKind::StaticLibrary) {
                return adapter_failure<PackageMetadata>(
                    rstd::format("test target '{}::{}' cannot attach non-library package '{}'",
                                 test.id.package.as_str(),
                                 test.id.name.as_str(),
                                 declaration.package.as_str()));
            }
            auto sources = Vec<PathBuf>::make();
            for (const auto& source : declaration.sources) {
                if (contains_source(sources, source.as_path())) {
                    return adapter_failure<PackageMetadata>(
                        rstd::format("test attachment '{}' repeats source '{}'",
                                     declaration.package.as_str(),
                                     source.as_path()));
                }
                if (contains_source(test.source.declared_sources, source.as_path())) {
                    return adapter_failure<PackageMetadata>(
                        rstd::format("test source '{}' cannot also attach to package '{}'",
                                     source.as_path(),
                                     declaration.package.as_str()));
                }
                sources.push(source.clone());
            }
            if (sources.is_empty()) continue;
            auto synthetic_name = rstd::format("{}@test-attach@{}@{}",
                                               test.id.name.as_str(),
                                               library->id.package.as_str(),
                                               library->id.name.as_str());
            attachments.push(ResolvedTarget {
                .id =
                    lito::package::PackageTargetId {
                        .package = test.id.package.clone(),
                        .kind    = lito::package::PackageTargetKind::TestAttachment,
                        .name    = rstd::move(synthetic_name),
                    },
                .artifact_kind = ArtifactKind::TestAttachmentArchive,
                .language      = library->language,
                .artifact_name = library->artifact_name.clone(),
                .source =
                    lito::manifest::TargetSourceManifest {
                        .module           = library->source.module.clone(),
                        .discovery        = lito::manifest::SourceDiscoveryMode::Explicit,
                        .declared_sources = rstd::move(sources),
                    },
                .root             = test.root.clone(),
                .source_root      = test.source_root.clone(),
                .test_attachment  = Some(TestAttachmentTarget {
                    .test_target    = test.id.clone(),
                    .library_target = library->id.clone(),
                }),
                .compile_metadata = library->compile_metadata.clone(),
            });
        }
    }
    for (auto& attachment : attachments) targets.push(rstd::move(attachment));

    auto default_targets =
        Vec<lito::package::PackageTargetId>::with_capacity(selected_targets.len());
    for (const auto& target : selected_targets) {
        if (! selected.contains_key(target.package.as_str())) {
            return adapter_failure<PackageMetadata>(
                rstd::format("selected root package '{}' is missing", target.package.as_str()));
        }
        default_targets.push(target.clone());
    }
    auto build_scripts = Vec<BuildScriptOwner>::make();
    if (graph.root_is_workspace) {
        build_scripts.push(BuildScriptOwner {
            .kind            = BuildScriptOwnerKind::Workspace,
            .package         = None(),
            .source_identity = String::make("workspace"_str),
            .root            = graph.root_directory.clone(),
            .script          = graph.root_directory.join(PathBuf::from("build.lua"_str).as_path()),
        });
    }
    for (const auto& root : graph.roots) {
        if (! selected.contains_key(root.name.as_str())) continue;
        for (const auto& package : graph.packages) {
            if (package.manifest.name != root.name.as_str()) continue;
            build_scripts.push(BuildScriptOwner {
                .kind            = BuildScriptOwnerKind::Package,
                .package         = Some(root.name.clone()),
                .source_identity = root.source_identity.clone(),
                .root            = package.manifest.root.clone(),
                .script = package.manifest.root.join(PathBuf::from("build.lua"_str).as_path()),
            });
            break;
        }
    }
    auto selected_packages      = Vec<SelectedPackageMetadata>::make();
    auto selected_root_packages = rstd::collections::BTreeMap<String, empty>::make();
    for (const auto& target : selected_targets) {
        selected_root_packages.insert(target.package.clone(), empty {});
    }
    for (const auto& package : graph.packages) {
        if (! selected_root_packages.contains_key(package.manifest.name.as_str())) continue;
        auto version = Option<String> {};
        if (package.manifest.version.value.is_some()) {
            version = Some(package.manifest.version.value->clone());
        }
        selected_packages.push(SelectedPackageMetadata {
            .name            = package.manifest.name.clone(),
            .version         = rstd::move(version),
            .source_identity = package.source_identity.clone(),
            .root            = package.manifest.root.clone(),
        });
    }
    auto profiles        = Vec<ProfileSpec>::make();
    auto default_profile = profile.name.clone();
    profiles.push(rstd::move(profile));
    return Ok(PackageMetadata {
        .name              = rstd::move(graph.name),
        .root              = rstd::move(graph.root_directory),
        .manifest_path     = rstd::move(graph.manifest_path),
        .build_scripts     = rstd::move(build_scripts),
        .default_profile   = rstd::move(default_profile),
        .default_targets   = rstd::move(default_targets),
        .selected_packages = rstd::move(selected_packages),
        .build_tools       = rstd::move(build_tools),
        .toolchain =
            lito::config::ToolchainSpec {
                .cc     = configuration.toolchain.cc.clone(),
                .cxx    = configuration.toolchain.cxx.clone(),
                .ld     = configuration.toolchain.ld.clone(),
                .ar     = configuration.toolchain.ar.clone(),
                .strip  = configuration.toolchain.strip.clone(),
                .format = configuration.toolchain.format.clone(),
            },
        .profiles = rstd::move(profiles),
        .targets  = rstd::move(targets),
    });
}

auto finalize_package(PackageMetadata metadata, Vec<ResolvedTargetSources> source_sets)
    -> lito::package::PackageResult<PackageSpec> {
    for (usize index {}; index < source_sets.len(); ++index) {
        for (usize prior {}; prior < index; ++prior) {
            if (source_sets[prior].target == source_sets[index].target) {
                return adapter_failure<PackageSpec>(rstd::format(
                    "source discovery repeated target '{}::{}::{}'",
                    source_sets[index].target.package.as_str(),
                    lito::package::package_target_kind_name(source_sets[index].target.kind),
                    source_sets[index].target.name.as_str()));
            }
        }
    }

    auto targets = Vec<TargetSpec>::with_capacity(metadata.targets.len());
    for (auto& target : metadata.targets) {
        auto source_position = Option<usize> {};
        for (usize index {}; index < source_sets.len(); ++index) {
            if (source_sets[index].target == target.id) {
                source_position = Some(index);
                break;
            }
        }
        auto source_set = ResolvedSourceSet {};
        if (source_position.is_some()) {
            source_set = rstd::move(source_sets[*source_position].sources);
        }
        auto sources = Vec<TargetSource>::with_capacity(source_set.sources.len());
        for (auto& source : source_set.sources) {
            sources.push(TargetSource {
                .relative_path     = rstd::move(source.relative_path),
                .path              = rstd::move(source.canonical_path),
                .source_root       = rstd::move(source.source_root),
                .origin_identity   = rstd::move(source.origin_identity),
                .external          = source.external,
                .expected_module   = rstd::move(source.expected_module),
                .frontend_analysis = rstd::move(source.frontend_analysis),
            });
        }
        auto artifact_name = output_name(target.artifact_kind, target.artifact_name.as_str());
        auto archive_stem  = target.artifact_name.clone();
        targets.push(TargetSpec {
            .id                    = rstd::move(target.id),
            .artifact_kind         = target.artifact_kind,
            .language              = target.language,
            .artifact_name         = rstd::move(artifact_name),
            .link_stdlib           = target.link_stdlib,
            .archive_stem          = rstd::move(archive_stem),
            .module_affiliation    = rstd::move(target.source.module),
            .root                  = rstd::move(target.root),
            .source_root           = rstd::move(target.source_root),
            .sources               = rstd::move(sources),
            .dependencies          = rstd::move(target.dependencies),
            .external_dependencies = rstd::move(target.external_dependencies),
            .usage                 = rstd::move(target.usage),
            .compile_tests         = rstd::move(target.compile_tests),
            .test_attachment       = rstd::move(target.test_attachment),
            .compile_metadata      = rstd::move(target.compile_metadata),
        });
    }

    return Ok(PackageSpec {
        .name            = rstd::move(metadata.name),
        .root            = rstd::move(metadata.root),
        .manifest_path   = rstd::move(metadata.manifest_path),
        .default_profile = rstd::move(metadata.default_profile),
        .default_targets = rstd::move(metadata.default_targets),
        .toolchain       = rstd::move(metadata.toolchain),
        .profiles        = rstd::move(metadata.profiles),
        .targets         = rstd::move(targets),
    });
}

} // namespace lito::cpp
