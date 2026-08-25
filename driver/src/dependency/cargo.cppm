module;
#include <rstd/macro.hpp>

export module lito.driver:dependency.cargo;

import rstd;
import lito.crypto;
import lito.core;
import lito.cpp;
import lito.system;
import lito.tools;
import lito.tools.cargo;
import :build.event;
import :build.layout;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::system;
using PathBuf = rstd::path::PathBuf;

namespace lito
{

auto cargo_error(ref<str> context, lito::tools::ToolError error)
    -> lito::dependency::DependencyError {
    return lito::dependency::DependencyError::Provider(
        String::make(context), Box<dyn<rstd::error::Error>>::make(rstd::move(error)));
}

auto cargo_profile_source(const Option<String>& source, const cpp::ProfileSpec& profile)
    -> ref<str> {
    return source.is_some() ? source->as_str() : profile.name.as_str();
}

auto cargo_optimization(ref<str>                                            owner,
                        const lito::dependency::CargoDependencyRequirement& declaration,
                        const cpp::ProfileSpec&                             profile,
                        const cpp::EffectiveNativeProfile&                  native)
    -> lito::dependency::DependencyResult<Option<lito::tools::cargo::ProfileOptimization>> {
    if (native.optimization.is_none() ||
        *native.optimization == lito::manifest::Optimization::Default) {
        return Ok(Option<lito::tools::cargo::ProfileOptimization> {});
    }
    using Native = lito::manifest::Optimization;
    using Cargo  = lito::tools::cargo::ProfileOptimization;
    switch (*native.optimization) {
    case Native::None: return Ok(Some(Cargo::None));
    case Native::Level1: return Ok(Some(Cargo::Level1));
    case Native::Level2: return Ok(Some(Cargo::Level2));
    case Native::Level3: return Ok(Some(Cargo::Level3));
    case Native::Size: return Ok(Some(Cargo::Size));
    case Native::SizeMin: return Ok(Some(Cargo::SizeMin));
    case Native::Level4:
    case Native::Debug:
    case Native::Fast:
        return lito::dependency::dependency_failure<
            Option<lito::tools::cargo::ProfileOptimization>>(rstd::format(
            "Cargo dependency '{}:{}' cannot represent Lito optimization '{}' selected by {}",
            owner,
            declaration.alias.as_str(),
            cpp::cpp_optimization_option(*native.optimization),
            cargo_profile_source(native.sources.optimization, profile)));
    case Native::Default: break;
    }
    __builtin_unreachable();
}

auto cargo_debug_info(const cpp::EffectiveNativeProfile& native)
    -> Option<lito::tools::cargo::ProfileDebugInfo> {
    if (native.debug_info.is_none()) return None();
    using Native = lito::manifest::DebugInfo;
    using Cargo  = lito::tools::cargo::ProfileDebugInfo;
    switch (*native.debug_info) {
    case Native::None: return Some(Cargo::None);
    case Native::Limited: return Some(Cargo::Limited);
    case Native::Full: return Some(Cargo::Full);
    case Native::LineDirectivesOnly: return Some(Cargo::LineDirectivesOnly);
    case Native::LineTablesOnly: return Some(Cargo::LineTablesOnly);
    }
    __builtin_unreachable();
}

auto cargo_lto(const cpp::EffectiveNativeProfile& native)
    -> Option<lito::tools::cargo::ProfileLto> {
    auto value = native.compile_lto.is_some() ? native.compile_lto : native.link_lto;
    if (value.is_none()) return None();
    switch (*value) {
    case lito::manifest::Lto::Off: return Some(lito::tools::cargo::ProfileLto::Off);
    case lito::manifest::Lto::Thin: return Some(lito::tools::cargo::ProfileLto::Thin);
    case lito::manifest::Lto::Fat: return Some(lito::tools::cargo::ProfileLto::Fat);
    }
    __builtin_unreachable();
}

auto cargo_strip(const cpp::EffectiveNativeProfile& native)
    -> Option<lito::tools::cargo::ProfileStrip> {
    if (native.strip.is_none()) return None();
    switch (*native.strip) {
    case lito::artifact::StripMode::None: return Some(lito::tools::cargo::ProfileStrip::None);
    case lito::artifact::StripMode::DebugInfo:
        return Some(lito::tools::cargo::ProfileStrip::DebugInfo);
    case lito::artifact::StripMode::Symbols: return Some(lito::tools::cargo::ProfileStrip::Symbols);
    }
    __builtin_unreachable();
}

} // namespace lito

export namespace lito
{

auto resolve_cargo_profile_configuration(
    ref<str>                                            owner,
    const lito::dependency::CargoDependencyRequirement& declaration,
    const cpp::ProfileSpec&                             profile,
    lito::manifest::PackageLanguage                     language)
    -> lito::dependency::DependencyResult<lito::tools::cargo::ProfileConfiguration> {
    auto inherited =
        declaration.consumption.profile.is_some()
            ? declaration.consumption.profile->clone()
            : lito::dependency::CargoProfileName {
                  .value = String::make(
                      profile.family == lito::manifest::BuildProfileFamily::Release ? "release"_str
                                                                                    : "dev"_str),
              };
    auto native = cpp::effective_native_profile(profile, language);
    auto result = lito::tools::cargo::ProfileConfiguration {
        .inherits         = rstd::move(inherited),
        .optimization     = rstd_try(cargo_optimization(owner, declaration, profile, native)),
        .debug_info       = cargo_debug_info(native),
        .lto              = cargo_lto(native),
        .debug_assertions = native.ndebug.is_some() ? Some(! *native.ndebug) : None(),
        .strip = declaration.consumption.usage == lito::dependency::CargoDependencyUsage::Runtime
                     ? cargo_strip(native)
                     : Option<lito::tools::cargo::ProfileStrip> {},
    };
    auto projection = lito::tools::cargo::profile_configuration_identity(result);
    auto digest     = lito::crypto::sha256_hex(projection.as_str());
    auto selected   = String::make("lito-"_str);
    selected.push_str(digest.as_str().get(usize {}, usize(16)).unwrap());
    result.selected = lito::dependency::CargoProfileName { .value = rstd::move(selected) };
    return Ok(rstd::move(result));
}

} // namespace lito

namespace lito
{

auto cargo_target(const lito::tools::cargo::Provider& provider, const BuildPlatform& platform)
    -> lito::dependency::DependencyResult<String> {
    if (platform.cross || platform.intent != BuildTargetIntent::Native ||
        platform.effective_target.triple != platform.compiler_default.triple.as_str()) {
        return lito::dependency::dependency_failure<String>(
            rstd::format("Cargo external dependencies do not yet support cross target '{}'",
                         platform.effective_target.triple.as_str()));
    }
    auto parsed = parse_target_info(provider.host_target.as_str());
    if (parsed.is_err()) {
        return lito::dependency::dependency_failure<String>(
            rstd::format("Cargo provider host target '{}' is invalid: {}",
                         provider.host_target.as_str(),
                         rstd::move(parsed).unwrap_err()));
    }
    const auto& cargo = *parsed;
    const auto& lito  = platform.effective_target;
    const auto  supported =
        cargo.os.as_str() == "linux"_str || cargo.os.as_str() == "macos"_str ||
        (cargo.os.as_str() == "windows"_str && cargo.environment == TargetEnvironment::Msvc);
    if (! supported || cargo.architecture != lito.architecture || cargo.os != lito.os.as_str() ||
        cargo.environment != lito.environment) {
        return lito::dependency::dependency_failure<String>(
            rstd::format("Cargo host target '{}' does not match Lito target '{}'",
                         provider.host_target.as_str(),
                         lito.triple.as_str()));
    }
    return Ok(provider.host_target.clone());
}

auto cargo_source(const cpp::ExternalSourceRootCatalog& catalog,
                  usize                                 package,
                  ref<str>                              name,
                  ref<str>                              alias)
    -> lito::dependency::DependencyResult<const cpp::ExternalSourceRoot*> {
    const cpp::ExternalSourceRoot* result = nullptr;
    for (const auto& source : catalog.sources) {
        if (source.package != package || source.name != name) continue;
        if (result != nullptr) {
            return lito::dependency::dependency_failure<const cpp::ExternalSourceRoot*>(
                rstd::format(
                    "Cargo dependency '{}' external source '{}' is ambiguous", alias, name));
        }
        result = rstd::addressof(source);
    }
    if (result == nullptr) {
        return lito::dependency::dependency_failure<const cpp::ExternalSourceRoot*>(rstd::format(
            "Cargo dependency '{}' external source '{}' was not materialized", alias, name));
    }
    return Ok(result);
}

auto cargo_request_identity(const lito::tools::cargo::Provider&                 provider,
                            const cpp::ExternalSourceRoot&                      source,
                            const lito::dependency::CargoDependencyRequirement& declaration,
                            const lito::tools::cargo::PackageMetadata&          metadata,
                            const lito::tools::cargo::ProfileConfiguration&     profile,
                            lito::manifest::PackageLanguage                     language,
                            ref<str> target) -> lito::dependency::DependencyResult<String> {
    auto lock = rstd::fs::read(metadata.lock_file.as_path());
    if (lock.is_err()) {
        return Err(lito::dependency::DependencyError::Io(String::make("read Cargo lock file"_str),
                                                         metadata.lock_file.clone(),
                                                         rstd::move(lock).unwrap_err()));
    }
    auto       text   = String::make("lito-cargo-request-v3\n"_str);
    const auto append = [&](ref<str> value) {
        text.push_str(rstd::format("{}:{}\n", value.len(), value).as_str());
    };
    append(provider.identity.as_str());
    append(source.identity.as_str());
    append(lito::crypto::sha256_hex(lock->as_slice()).as_str());
    append(metadata.id.as_str());
    append(declaration.recipe.manifest_path.as_path().to_string_lossy().as_str());
    append(profile.selected.as_str());
    append(lito::tools::cargo::profile_configuration_identity(profile).as_str());
    append(lito::manifest::package_language_name(language));
    append(target);
    append(declaration.consumption.usage == lito::dependency::CargoDependencyUsage::Link
               ? "link"_str
               : "runtime"_str);
    append(declaration.consumption.default_features ? "default-features"_str
                                                    : "no-default-features"_str);
    for (const auto& feature : declaration.consumption.features) append(feature.as_str());
    return Ok(lito::crypto::sha256_hex(text.as_str()));
}

} // namespace lito

export namespace lito
{

struct ResolvedCargoDependencies {
    Vec<cpp::ExternalDependencyUsage>       usage;
    Vec<lito::dependency::ExternalAssetSet> assets;
};

auto resolve_cargo_dependencies(
    const Vec<lito::dependency::CargoDependencyRequirement>& declarations,
    usize                                                    package_index,
    ref<str>                                                 owner,
    lito::manifest::PackageLanguage                          language,
    const cpp::ExternalSourceRootCatalog&                    sources,
    const cpp::ProfileSpec&                                  profile,
    const BuildPlatform&                                     platform,
    const BuildLayout&                                       layout,
    const lito::tools::cargo::Configuration&                 configuration,
    lito::tools::ToolResolver&                               tool_resolver,
    const ResolvedProcessEnvironment&                        environment,
    usize                                                    jobs,
    Option<lito::tools::cargo::Provider>&                    provider_cache,
    const Option<BuildEventSink>&                            observer = None())
    -> lito::dependency::DependencyResult<ResolvedCargoDependencies> {
    auto result = ResolvedCargoDependencies {};
    if (declarations.is_empty()) return Ok(rstd::move(result));
    if (provider_cache.is_none()) {
        const auto requirement = lito::tools::external_dependency_tool_requirement(
            lito::tools::HostToolCapability::CargoBuild,
            owner,
            declarations[usize {}].alias.as_str());
        auto resolved = tool_resolver.require(lito::tools::Tool::Cargo, requirement);
        if (resolved.is_err()) {
            return Err(
                cargo_error("resolve Cargo provider"_str, rstd::move(resolved).unwrap_err()));
        }
        auto identified = lito::tools::cargo::identify_provider(
            rstd::move(resolved).unwrap().executable, environment);
        if (identified.is_err()) {
            return Err(
                cargo_error("identify Cargo provider"_str, rstd::move(identified).unwrap_err()));
        }
        provider_cache = Some(rstd::move(identified).unwrap());
    }
    auto target = rstd_try(cargo_target(*provider_cache, platform));
    for (const auto& declaration : declarations) {
        auto source   = rstd_try(cargo_source(sources,
                                              package_index,
                                              declaration.recipe.source.as_str(),
                                              declaration.alias.as_str()));
        auto manifest = source->root.join(declaration.recipe.manifest_path.as_path());
        auto metadata =
            lito::tools::cargo::query_metadata(*provider_cache,
                                               lito::tools::cargo::MetadataRequest {
                                                   .source_root = source->root.clone(),
                                                   .manifest    = rstd::move(manifest),
                                                   .package = declaration.recipe.package.clone(),
                                                   .offline = configuration.offline,
                                               },
                                               environment,
                                               cargo_observer(observer));
        if (metadata.is_err()) {
            return Err(cargo_error(rstd::format("query Cargo dependency '{}:{}' metadata",
                                                owner,
                                                declaration.alias.as_str())
                                       .as_str(),
                                   rstd::move(metadata).unwrap_err()));
        }
        auto resolved_profile =
            rstd_try(resolve_cargo_profile_configuration(owner, declaration, profile, language));
        auto request_identity = rstd_try(cargo_request_identity(*provider_cache,
                                                                *source,
                                                                declaration,
                                                                *metadata,
                                                                resolved_profile,
                                                                language,
                                                                target.as_str()));
        auto work_root        = layout.cargo_work_root(request_identity.as_str());
        auto target_directory = work_root.join(PathBuf::from("target"_str).as_path());
        auto request          = lito::tools::cargo::BuildRequest {
            .alias            = declaration.alias.clone(),
            .source_root      = source->root.clone(),
            .manifest         = metadata->manifest.clone(),
            .package          = declaration.recipe.package.clone(),
            .features         = as<Clone>(declaration.consumption.features).clone(),
            .default_features = declaration.consumption.default_features,
            .profile          = rstd::move(resolved_profile),
            .target           = target.clone(),
            .request_identity = request_identity.clone(),
            .work_root        = rstd::move(work_root),
            .target_directory = rstd::move(target_directory),
            .jobs             = jobs,
            .offline          = configuration.offline,
        };
        if (declaration.consumption.usage == lito::dependency::CargoDependencyUsage::Runtime) {
            auto snapshot = lito::tools::cargo::build_binaries(
                *provider_cache, *metadata, request, environment, cargo_observer(observer));
            if (snapshot.is_err()) {
                return Err(cargo_error(
                    rstd::format("build Cargo runtime dependency '{}:{}' with profile '{}' for "
                                 "target '{}'",
                                 owner,
                                 declaration.alias.as_str(),
                                 request.profile.selected.as_str(),
                                 target.as_str())
                        .as_str(),
                    rstd::move(snapshot).unwrap_err()));
            }
            for (auto& artifact : snapshot->artifacts) {
                auto name = artifact.executable.as_path().file_name();
                if (name.is_none()) {
                    return lito::dependency::dependency_failure<ResolvedCargoDependencies>(
                        rstd::format("Cargo binary artifact '{}' has no file name",
                                     artifact.executable.as_path()));
                }
                auto entries = Vec<lito::dependency::ExternalAssetEntry>::make();
                entries.push(lito::dependency::ExternalAssetEntry {
                    .logical_path = PathBuf::from(*name),
                    .source       = rstd::move(artifact.executable),
                });
                result.assets.push(lito::dependency::ExternalAssetSet {
                    .alias       = declaration.alias.clone(),
                    .name        = rstd::move(artifact.name),
                    .disposition = lito::dependency::ExternalAssetDisposition::Materialized,
                    .entries     = rstd::move(entries),
                });
            }
            continue;
        }
        auto suffix =
            platform.effective_target.family == TargetFamily::Windows ? ".lib"_str : ".a"_str;
        auto snapshot = lito::tools::cargo::build_static_library(
            *provider_cache, *metadata, request, suffix, environment, cargo_observer(observer));
        if (snapshot.is_err()) {
            return Err(cargo_error(rstd::format("build Cargo dependency '{}:{}' with profile '{}' "
                                                "for target '{}'",
                                                owner,
                                                declaration.alias.as_str(),
                                                request.profile.selected.as_str(),
                                                target.as_str())
                                       .as_str(),
                                   rstd::move(snapshot).unwrap_err()));
        }
        auto archive = snapshot->archive.as_path().to_str();
        if (archive.is_none()) {
            return lito::dependency::dependency_failure<ResolvedCargoDependencies>(rstd::format(
                "Cargo artifact '{}' is not valid UTF-8", snapshot->archive.as_path()));
        }
        auto link_arguments =
            Vec<String>::with_capacity(snapshot->native_link_arguments.len() + usize(1));
        link_arguments.push(String::make(*archive));
        for (const auto& argument : snapshot->native_link_arguments) {
            link_arguments.push(argument.clone());
        }
        auto source_text = rstd::format("Cargo dependency '{}' package '{}'",
                                        declaration.alias.as_str(),
                                        declaration.recipe.package.as_str());
        if (declaration.consumption.visibility.is_none()) {
            return lito::dependency::dependency_failure<ResolvedCargoDependencies>(rstd::format(
                "Cargo link dependency '{}' is missing visibility", declaration.alias.as_str()));
        }
        auto targets = Vec<cpp::ExternalTargetUsage>::make();
        targets.push(cpp::ExternalTargetUsage {
            .name           = snapshot->package.library->name.clone(),
            .visibility     = *declaration.consumption.visibility,
            .compile_source = source_text.clone(),
            .identity       = snapshot->identity.clone(),
        });
        result.usage.push(cpp::ExternalDependencyUsage {
            .alias    = declaration.alias.clone(),
            .provider = String::make("cargo"_str),
            .version  = snapshot->package.version.clone(),
            .targets  = rstd::move(targets),
            .link_arguments =
                lito::link::ArgumentSequence {
                    .tokens   = rstd::move(link_arguments),
                    .source   = rstd::move(source_text),
                    .identity = snapshot->identity.clone(),
                },
            .link_compatibility =
                lito::link::Compatibility {
                    .rust_static_runtime = Some(lito::link::RustStaticRuntimeUsage {
                        .artifact_identity = snapshot->identity.clone(),
                        .source            = source_text.clone(),
                    }),
                },
            .identity = snapshot->identity.clone(),
        });
    }
    return Ok(rstd::move(result));
}

} // namespace lito
