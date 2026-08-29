module;
#include <rstd/macro.hpp>

module lito.driver:project;

import rstd;
import lito.tools;
import lito.tools.cargo;
import lito.core;
import :config.project;
import :config.registry;
import :project.error;
import lito.cpp;
import :build.event;
import :build.setup_report;
import :build.layout;
import :build.artifact;
import :dependency.catalog;
import :dependency.preparation;
import :dependency.external_source;
import :package;
import :registry.blob;
import :registry.graph;
import :registry.http;
import :sdk;
import lito.toolchain;
import lito.system;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;

namespace lito
{

struct ProjectResolution {
    lito::package::ResolvedPackageSelection selection;
    lito::lock::LockStatus                  lock;
    DeclaredExternalDependencySources       external_sources;
    lito::dependency::CMakeBuildOverrideSet cmake_build_overrides;
};

} // namespace lito

namespace lito
{

struct StartedProjectResolution {
    lito::package::ResolvedPackageSelection selection;
    lito::lock::LockSession                 lock;
    lito::source::SourceResolutionOptions   external;
};

struct ProjectRegistryResolver {
    const lito::config::LitoBootstrapConfig* config {};
    lito::registry::RegistryNetworkPolicy network { lito::registry::RegistryNetworkPolicy::Online };
    bool                                  locked_mode {};
    Vec<lito::source::RegistrySourcePin>  locked;
    const Vec<PathBuf>*                   source_bundles {};
    lito::tools::ToolResolver*            tools {};
    const ResolvedProcessEnvironment*     environment {};
    Option<lito::package::EmbeddedRegistryPackages> embedded;

    static auto resolve_builtin(void* raw, ref<str> id) noexcept
        -> lito::registry::RegistryGraphResult<lito::registry::BuiltinRegistryPackage> {
        auto& self = *static_cast<ProjectRegistryResolver*>(raw);
        if (self.embedded.is_none()) {
            return Err(lito::registry::RegistryGraphError {
                .message = rstd::format("builtin package '{}' has no embedded provider", id),
            });
        }
        return self.embedded->resolve(id);
    }

    static auto resolve(void*                                           raw,
                        slice<lito::registry::RegistryGraphRequirement> requirements) noexcept
        -> lito::registry::RegistryGraphResult<Vec<lito::registry::ResolvedRegistryGraphSource>> {
        auto& self = *static_cast<ProjectRegistryResolver*>(raw);
        if (self.config == nullptr || self.environment == nullptr) {
            return Err(lito::registry::RegistryGraphError {
                .message = String::make("Registry resolution has no bootstrap context"_str),
            });
        }
        auto data = lito::system::LitoDataRoot::resolve();
        if (data.is_err()) {
            return Err(lito::registry::RegistryGraphError {
                .message = rstd::format("cannot resolve Registry cache root: {}",
                                        rstd::move(data).unwrap_err()),
            });
        }
        auto http       = lito::registry::RegistryHttpTransport {};
        auto blobs      = lito::registry::RegistryBlobTransport {};
        auto http_owner = Option<lito::registry::CurlRegistryHttpTransport> {};
        auto blob_owner = Option<lito::registry::CurlRegistryBlobTransport> {};
        if (self.network == lito::registry::RegistryNetworkPolicy::Online) {
            if (self.tools == nullptr) {
                return Err(lito::registry::RegistryGraphError {
                    .message = String::make("online Registry resolution has no tool resolver"_str),
                });
            }
            auto curl = self.tools->require(
                lito::tools::Tool::Curl,
                lito::tools::command_tool_requirement(lito::tools::HostToolCapability::HttpDownload,
                                                      "Registry resolve"_str));
            if (curl.is_err()) {
                return Err(lito::registry::RegistryGraphError {
                    .message = rstd::format("cannot resolve curl for Registry downloads: {}",
                                            rstd::move(curl).unwrap_err()),
                });
            }
            http_owner = Some(lito::registry::CurlRegistryHttpTransport(curl->executable.clone(),
                                                                        *self.environment));
            blob_owner = Some(lito::registry::CurlRegistryBlobTransport(curl->executable.clone(),
                                                                        *self.environment));
            http       = http_owner->transport();
            blobs      = blob_owner->transport();
        }
        auto pins = Vec<lito::source::RegistrySourcePin>::with_capacity(self.locked.len());
        for (const auto& pin : self.locked) pins.push(pin.clone());
        auto client = lito::registry::RegistryGraphClient(PathBuf::from(data->root()),
                                                          *self.config,
                                                          self.network,
                                                          http,
                                                          blobs,
                                                          self.locked_mode,
                                                          rstd::move(pins),
                                                          self.source_bundles);
        if (self.embedded.is_some()) self.embedded->add_indices(client);
        return client.resolve(requirements);
    }

    auto provider() noexcept -> lito::registry::RegistryGraphProvider {
        return lito::registry::RegistryGraphProvider {
            .context         = this,
            .resolve         = resolve,
            .resolve_builtin = embedded.is_some() ? resolve_builtin : nullptr,
        };
    }
};

auto observer_value(const Option<BuildEventSink>& observer) -> BuildEventSink {
    return observer.is_some() ? *observer : BuildEventSink {};
}

auto start_project_resolution(
    const lito::package::PackageSelection&    selection,
    lito::package::PackageSelectionPurpose    purpose,
    const lito::source::PackageSourceConfig&  sources,
    const lito::lock::LockConfig&             lock,
    bool                                      locked,
    lito::source::GitResolutionMode           git,
    const TargetInfo*                         target,
    lito::tools::ToolResolver*                tool_resolver,
    const ResolvedProcessEnvironment&         environment,
    usize                                     jobs     = usize(1),
    BuildEventSink                            observer = {},
    Option<lito::workspace::WorkspaceCatalog> catalog  = None(),
    lito::source::SourceMaterializationPolicy materialization =
        lito::source::SourceMaterializationPolicy::Materialize,
    lito::lock::InvalidLockPolicy            invalid_lock = lito::lock::InvalidLockPolicy::Reject,
    const lito::config::LitoBootstrapConfig* registries   = nullptr,
    lito::registry::RegistryGraphProvider    registry_provider = {},
    Option<ref<str>> artifact_processor = None()) -> ProjectResult<StartedProjectResolution> {
    auto lock_session = rstd_try(
        lito::lock::load_lock_session(selection.root.as_path(), lock, locked, git, invalid_lock));
    auto resolution            = lock_session.take_resolution_options();
    resolution.sources         = sources.clone();
    resolution.materialization = materialization;
    auto external_resolution   = resolution.clone();
    auto registry_resolver     = Option<ProjectRegistryResolver> {};
    if (registry_provider.resolve == nullptr && registries != nullptr) {
        auto pins =
            Vec<lito::source::RegistrySourcePin>::with_capacity(resolution.registry_sources.len());
        for (const auto& pin : resolution.registry_sources) pins.push(pin.clone());
        auto embedded          = Option<lito::package::EmbeddedRegistryPackages> {};
        auto embedded_provider = registries->embedded_packages();
        if (embedded_provider.resolve != nullptr) {
            auto data = lito::system::LitoDataRoot::resolve();
            if (data.is_err()) {
                return Err(ProjectError::Message(
                    rstd::format("cannot resolve embedded Registry cache root: {}",
                                 rstd::move(data).unwrap_err())));
            }
            embedded = Some(lito::package::EmbeddedRegistryPackages(
                PathBuf::from(data->root()), *registries, embedded_provider));
        }
        registry_resolver = Some(ProjectRegistryResolver {
            .config  = registries,
            .network = materialization == lito::source::SourceMaterializationPolicy::ExistingOnly ||
                               sources.network == lito::source::NetworkPolicy::Offline
                           ? lito::registry::RegistryNetworkPolicy::Offline
                           : lito::registry::RegistryNetworkPolicy::Online,
            .locked_mode    = resolution.locked,
            .locked         = rstd::move(pins),
            .source_bundles = rstd::addressof(sources.source_bundles),
            .tools          = tool_resolver,
            .environment    = rstd::addressof(environment),
            .embedded       = rstd::move(embedded),
        });
        registry_provider = registry_resolver->provider();
    }
    auto project =
        tool_resolver != nullptr
            ? lito::package::resolve_package_selection_with_environment(selection,
                                                                        purpose,
                                                                        rstd::move(resolution),
                                                                        target,
                                                                        *tool_resolver,
                                                                        environment,
                                                                        jobs,
                                                                        source_observer(observer),
                                                                        rstd::move(catalog),
                                                                        registry_provider,
                                                                        artifact_processor)
            : lito::package::resolve_existing_package_selection_with_environment(
                  selection,
                  purpose,
                  rstd::move(resolution),
                  *target,
                  environment,
                  jobs,
                  source_observer(observer),
                  rstd::move(catalog),
                  registry_provider,
                  artifact_processor);
    if (project.is_err()) {
        return Err(rstd::into<ProjectError>(rstd::move(project).unwrap_err()));
    }
    return Ok(StartedProjectResolution {
        .selection = rstd::move(project).unwrap(),
        .lock      = rstd::move(lock_session),
        .external  = rstd::move(external_resolution),
    });
}

auto resolve_project(
    const lito::package::PackageSelection&         selection,
    lito::package::PackageSelectionPurpose         purpose,
    const lito::source::PackageSourceConfig&       sources,
    const lito::lock::LockConfig&                  lock_config,
    bool                                           locked,
    lito::source::GitResolutionMode                git,
    const TargetInfo*                              target,
    lito::tools::ToolResolver&                     tool_resolver,
    const ResolvedProcessEnvironment&              environment,
    const lito::dependency::CMakeBuildOverrideSet& cmake_build_overrides,
    usize                                          jobs     = usize(1),
    BuildEventSink                                 observer = {},
    Option<lito::workspace::WorkspaceCatalog>      catalog  = None(),
    lito::lock::InvalidLockPolicy            invalid_lock   = lito::lock::InvalidLockPolicy::Reject,
    const lito::config::LitoBootstrapConfig* registries     = nullptr,
    lito::registry::RegistryGraphProvider    registry       = {},
    Option<ref<str>> artifact_processor = None()) -> ProjectResult<ProjectResolution> {
    auto started =
        rstd_try(start_project_resolution(selection,
                                          purpose,
                                          sources,
                                          lock_config,
                                          locked,
                                          git,
                                          target,
                                          rstd::addressof(tool_resolver),
                                          environment,
                                          jobs,
                                          observer,
                                          rstd::move(catalog),
                                          lito::source::SourceMaterializationPolicy::Materialize,
                                          invalid_lock,
                                          registries,
                                          registry,
                                          artifact_processor));
    auto declared_sources =
        rstd_try(resolve_external_dependency_sources(started.selection.graph,
                                                     rstd::move(started.external),
                                                     tool_resolver,
                                                     environment,
                                                     observer));
    auto lock = rstd_try(lito::lock::sync_lock(started.selection.graph, rstd::move(started.lock)));
    return Ok(ProjectResolution {
        .selection             = rstd::move(started.selection),
        .lock                  = lock,
        .external_sources      = rstd::move(declared_sources),
        .cmake_build_overrides = cmake_build_overrides.clone(),
    });
}

} // namespace lito

namespace lito
{

auto resolve_project_selection(const lito::package::PackageSelection&   selection,
                               lito::package::PackageSelectionPurpose   purpose,
                               const lito::source::PackageSourceConfig& sources,
                               const lito::lock::LockConfig&            lock,
                               bool                                     locked,
                               lito::tools::ToolResolver&               tool_resolver,
                               const ResolvedProcessEnvironment&        environment,
                               usize                                    jobs       = usize(1),
                               const Option<BuildEventSink>&            observer   = None(),
                               const lito::config::LitoBootstrapConfig* registries = nullptr)
    -> ProjectResult<lito::package::ResolvedPackageSelection> {
    auto started = start_project_resolution(selection,
                                            purpose,
                                            sources,
                                            lock,
                                            locked,
                                            lito::source::GitResolutionMode::ReuseLocked,
                                            nullptr,
                                            rstd::addressof(tool_resolver),
                                            environment,
                                            jobs,
                                            observer_value(observer),
                                            None(),
                                            lito::source::SourceMaterializationPolicy::Materialize,
                                            lito::lock::InvalidLockPolicy::Reject,
                                            registries);
    if (started.is_err()) return Err(rstd::move(started).unwrap_err());
    return Ok(rstd::move(started).unwrap().selection);
}

auto resolve_existing_project_selection(
    const lito::package::PackageSelection&    selection,
    lito::package::PackageSelectionPurpose    purpose,
    const lito::source::PackageSourceConfig&  sources,
    const lito::lock::LockConfig&             lock,
    const TargetInfo&                         target,
    const ResolvedProcessEnvironment&         environment,
    usize                                     jobs,
    const Option<BuildEventSink>&             observer,
    Option<lito::workspace::WorkspaceCatalog> catalog    = None(),
    const lito::config::LitoBootstrapConfig*  registries = nullptr,
    lito::registry::RegistryGraphProvider     registry   = {})
    -> ProjectResult<lito::package::ResolvedPackageSelection> {
    auto started = start_project_resolution(selection,
                                            purpose,
                                            sources,
                                            lock,
                                            true,
                                            lito::source::GitResolutionMode::ReuseLocked,
                                            rstd::addressof(target),
                                            nullptr,
                                            environment,
                                            jobs,
                                            observer_value(observer),
                                            rstd::move(catalog),
                                            lito::source::SourceMaterializationPolicy::ExistingOnly,
                                            lito::lock::InvalidLockPolicy::Reject,
                                            registries,
                                            registry);
    if (started.is_err()) return Err(rstd::move(started).unwrap_err());
    return Ok(rstd::move(started).unwrap().selection);
}

struct PreparedBuildProject {
    ClangToolchain                    toolchain;
    cpp::BuildConfiguration           configuration;
    BuildPlatform                     platform;
    BuildLayout                       layout;
    cpp::PackageMetadata              metadata;
    ExternalAssetCatalog              external_assets;
    Vec<ExternalSourceProvenance>     external_source_provenance;
    Vec<BuiltTargetRuntime>           target_runtimes;
    Option<PathBuf>                   target_stripper;
    Option<AndroidNdkLease>           android_sdk;
    Option<Box<PreparedBuildProject>> plugin_host;
    Vec<ProcMacroAggregateRequest>    proc_macro_aggregates;
};

struct ResolvedProjectMetadata {
    BuildPlatform                 platform;
    BuildLayout                   layout;
    cpp::PackageMetadata          metadata;
    ExternalAssetCatalog          external_assets;
    Vec<ExternalSourceProvenance> external_source_provenance;
};

struct ResolvedProjectSession {
    ProjectResolution              project;
    cpp::ParsedGlobalBuildOptions  build_arguments;
    BuildPlatform                  platform;
    Option<AndroidCmakeProjection> android_cmake;
};

struct ResolvedPluginBuildProject {
    cpp::BuildConfiguration configuration;
    ClangToolchain          toolchain;
    ResolvedProjectSession  session;
};

struct ResolvedBuildProject {
    cpp::BuildConfiguration                 configuration;
    ClangToolchain                          toolchain;
    ResolvedProjectSession                  session;
    Vec<BuiltTargetRuntime>                 target_runtimes;
    Option<PathBuf>                         target_stripper;
    Option<AndroidNdkLease>                 android_sdk;
    Option<Box<ResolvedPluginBuildProject>> plugin_host;
};

auto proc_macro_aggregate_requests(const cpp::PackageMetadata& metadata)
    -> Vec<ProcMacroAggregateRequest> {
    auto requests = Vec<ProcMacroAggregateRequest>::make();
    for (const auto& target : metadata.targets) {
        if (target.proc_macro_dependencies.is_empty()) continue;
        auto providers =
            Vec<ProcMacroProviderBinding>::with_capacity(target.proc_macro_dependencies.len());
        for (const auto& dependency : target.proc_macro_dependencies) {
            providers.push(ProcMacroProviderBinding {
                .package = dependency.package.clone(),
            });
        }
        auto digest  = proc_macro_aggregate_identity(target.proc_macro_dependencies);
        auto present = false;
        for (const auto& existing : requests) {
            if (existing.identity == digest.as_str()) {
                present = true;
                break;
            }
        }
        if (! present) {
            requests.push(ProcMacroAggregateRequest {
                .identity  = rstd::move(digest),
                .providers = rstd::move(providers),
            });
        }
    }
    return requests;
}

struct ConfiguredToolchainSelection {
    lito::config::ToolchainSpec      tools;
    Option<ResolvedAndroidToolchain> android;
    Option<HostInfo>                 host;
    Option<AndroidNdkLease>          android_sdk;
};

struct AcquisitionPlatform {
    BuildPlatform               platform;
    lito::config::ToolchainSpec toolchain;
    Option<AndroidNdkLease>     android_sdk;
};

auto resolve_configured_toolchain(const config::BuildConfigurationRequest& configuration,
                                  const ResolvedProcessEnvironment&        environment)
    -> ProjectResult<ConfiguredToolchainSelection> {
    if (configuration.target.is_Default()) {
        return Ok(ConfiguredToolchainSelection {
            .tools = configuration.toolchain.clone(),
        });
    }
    auto explicit_standard_library =
        lito::config::explicit_standard_library(configuration.standard_library);
    if (explicit_standard_library.is_some() &&
        *explicit_standard_library != lito::config::StandardLibrary::Libcxx) {
        return Err(ProjectError::Message(
            rstd::format("Android target requires standard library 'libc++'; configured '{}'",
                         lito::config::standard_library_name(*explicit_standard_library))));
    }
    if (configuration.toolchain.sdk.is_none()) {
        return Err(ProjectError::Message(
            String::make("Android target requires toolchain.sdk with kind 'android-ndk'"_str)));
    }
    const auto& selection = *configuration.toolchain.sdk;
    if (selection.kind() != lito::config::SdkKind::AndroidNdk) {
        return Err(ProjectError::Message(
            rstd::format("Android target requires SDK kind 'android-ndk'; configured '{}'",
                         lito::config::sdk_kind_name(selection.kind()))));
    }
    auto host  = rstd_try(detect_host_info());
    auto lease = Option<AndroidNdkLease> {};
    auto root  = PathBuf {};
    if (selection.is_Managed()) {
        auto acquired = acquire_android_ndk(selection.as_Managed().version.as_str());
        if (acquired.is_err()) {
            return Err(
                ProjectError::Message(rstd::format("cannot acquire managed Android NDK {}: {}",
                                                   selection.as_Managed().version.as_str(),
                                                   rstd::move(acquired).unwrap_err())));
        }
        lease = Some(rstd::move(acquired).unwrap());
        root  = PathBuf::from(lease->root());
    } else {
        root = selection.as_Directory().path.clone();
    }
    auto distribution = open_android_ndk(root.as_path(), host);
    if (distribution.is_err()) {
        return Err(rstd::into<ProjectError>(rstd::move(distribution).unwrap_err()));
    }
    if (selection.is_Directory()) {
        auto certified = certify_android_ndk(*distribution, environment);
        if (certified.is_err()) {
            return Err(rstd::into<ProjectError>(rstd::move(certified).unwrap_err()));
        }
    }
    auto android = resolve_android_toolchain(rstd::move(distribution).unwrap(),
                                             configuration.target.as_Android().target,
                                             configuration.standard_library_runtime);
    if (android.is_err()) {
        return Err(rstd::into<ProjectError>(rstd::move(android).unwrap_err()));
    }
    auto resolved = rstd::move(android).unwrap();
    auto tools    = resolved.tools.clone();
    return Ok(ConfiguredToolchainSelection {
        .tools       = rstd::move(tools),
        .android     = Some(rstd::move(resolved)),
        .host        = Some(rstd::move(host)),
        .android_sdk = rstd::move(lease),
    });
}

auto resolve_acquisition_platform(const config::BuildConfigurationRequest& configuration,
                                  const ResolvedProcessEnvironment&        environment)
    -> ProjectResult<AcquisitionPlatform> {
    auto selected = rstd_try(resolve_configured_toolchain(configuration, environment));
    auto target =
        selected.android.is_some()
            ? lito::resolve_clang_target(selected.tools,
                                         configuration.standard_library,
                                         rstd::addressof(selected.android->target.target_info),
                                         environment)
            : lito::resolve_clang_target(
                  selected.tools, configuration.standard_library, environment);
    if (target.is_err()) return Err(rstd::into<ProjectError>(rstd::move(target).unwrap_err()));
    auto resolved = rstd::move(target).unwrap();
    auto platform = Option<BuildPlatform> {};
    if (selected.android.is_some()) {
        platform               = Some(resolve_android_build_platform(
            *selected.host, resolved.target.info, selected.android->target));
        platform->sdk_version  = Some(selected.android->distribution.revision().text.clone());
        platform->sdk_identity = Some(String::make(selected.android->distribution.identity()));
    } else {
        auto host            = rstd_try(detect_host_info());
        auto explicit_target = resolved.target.source == CompileTargetSource::CompilerDefault
                                   ? Option<ref<str>> {}
                                   : Some(resolved.target.info.triple.as_str());
        auto selected_platform =
            resolve_build_platform(host, resolved.compiler_default, explicit_target, None());
        if (selected_platform.is_err()) {
            return Err(ProjectError::Platform(rstd::move(selected_platform).unwrap_err()));
        }
        platform = Some(rstd::move(selected_platform).unwrap());
    }
    return Ok(AcquisitionPlatform {
        .platform    = rstd::move(platform).unwrap(),
        .toolchain   = rstd::move(selected.tools),
        .android_sdk = rstd::move(selected.android_sdk),
    });
}

auto lto_is_enabled(const Option<lito::manifest::Lto>& value) noexcept -> bool {
    return value.is_some() && *value != lito::manifest::Lto::Off;
}

auto validate_linker_profile(const ClangToolchain&                            toolchain,
                             const cpp::ProfileSpec&                          profile,
                             const BuildPlatform&                             platform,
                             const lito::package::EffectiveLanguageStandards& standards)
    -> ProjectResult<empty> {
    const auto& linker = toolchain.linker_identity();
    if (linker.family != LinkerFamily::GnuLd) return Ok(empty {});
    const auto& target = platform.effective_target;
    if (target.family != TargetFamily::Unix || target.platform == TargetPlatform::Macos ||
        target.triple.as_str() != toolchain.target()) {
        return Err(ProjectError::Message(rstd::format(
            "configured GNU ld is only supported for the host ELF target '{}'; effective target "
            "is '{}'",
            toolchain.target(),
            target.triple.as_str())));
    }
    const auto reject_lto = [&](ref<str>                           field,
                                const Option<lito::manifest::Lto>& value,
                                const Option<String>&              source) -> ProjectResult<empty> {
        if (! lto_is_enabled(value)) return Ok(empty {});
        return Err(ProjectError::Message(
            rstd::format("configured GNU ld does not support LLVM LTO '{}' selected for {} by {}",
                         cpp::cpp_lto_option(value),
                         field,
                         source.is_some() ? source->as_str() : profile.name.as_str())));
    };
    if (standards.c.is_some()) {
        rstd_try(
            reject_lto("C compilation"_str, profile.c.common.codegen.lto, profile.c_sources.lto));
    }
    if (standards.cpp.is_some()) {
        rstd_try(reject_lto(
            "C++ compilation"_str, profile.cpp.common.codegen.lto, profile.cpp_sources.lto));
    }
    return reject_lto("linking"_str, profile.link_lto, profile.link_lto_source);
}

struct ResolvedBuildContext {
    cpp::ParsedGlobalBuildOptions build_arguments;
    BuildPlatform                 platform;
};

auto resolve_build_context(const lito::config::ProjectBuildOptions& options,
                           const ClangToolchain&                    toolchain,
                           const BuildPlatform*                     prepared_platform = nullptr)
    -> ProjectResult<ResolvedBuildContext> {
    auto build_arguments = rstd_try(parse_build_arguments(options, toolchain.argument_parser()));
    auto cpp_target      = cpp::explicit_cpp_target_options(build_arguments.cpp);
    auto c_target        = cpp::explicit_c_target_options(build_arguments.c);
    auto platform        = BuildPlatform {};
    if (prepared_platform != nullptr) {
        const auto reject_raw =
            [](ref<str>                          language,
               const cpp::ExplicitTargetOptions& options) -> ProjectResult<empty> {
            if (options.target.is_some()) {
                return Err(ProjectError::Message(
                    rstd::format("{} target '{}' from {} conflicts with typed config.build.target",
                                 language,
                                 options.target->value,
                                 options.target->source)));
            }
            if (options.sysroot.is_some()) {
                return Err(ProjectError::Message(
                    rstd::format("{} sysroot '{}' from {} conflicts with typed config.build.target",
                                 language,
                                 options.sysroot->value,
                                 options.sysroot->source)));
            }
            return Ok(empty {});
        };
        rstd_try(reject_raw("C++"_str, cpp_target));
        rstd_try(reject_raw("C"_str, c_target));
        platform = prepared_platform->clone();
    } else {
        auto host = rstd_try(detect_host_info());
        if (cpp_target.target.is_some() && cpp_target.target->value != toolchain.target()) {
            return Err(ProjectError::Message(rstd::format(
                "C++ target '{}' from {} conflicts with the toolchain compile target '{}'; "
                "configure toolchain.os and toolchain.arch instead",
                cpp_target.target->value,
                cpp_target.target->source,
                toolchain.target())));
        }
        const auto explicit_target =
            toolchain.compile_target().source != CompileTargetSource::CompilerDefault ||
            cpp_target.target.is_some();
        platform = rstd_try(resolve_build_platform(
            host,
            toolchain.compiler_default_target_info(),
            explicit_target ? Some(toolchain.target()) : Option<ref<str>> {},
            cpp_target.sysroot.is_some() ? Some(cpp_target.sysroot->value) : Option<ref<str>> {}));
    }
    if (c_target.target.is_some() &&
        c_target.target->value != platform.effective_target.triple.as_str()) {
        return Err(ProjectError::Message(rstd::format(
            "C build target '{}' from {} conflicts with the C++-owned effective target '{}'; "
            "configure the project target through build.options",
            c_target.target->value,
            c_target.target->source,
            platform.effective_target.triple.as_str())));
    }
    if (c_target.sysroot.is_some()) {
        auto expected =
            platform.sysroot.is_some() ? platform.sysroot->as_path().to_str() : Option<ref<str>> {};
        if (expected.is_none() || c_target.sysroot->value != *expected) {
            return Err(ProjectError::Message(rstd::format(
                "C sysroot '{}' from {} conflicts with the C++-owned effective sysroot '{}'",
                c_target.sysroot->value,
                c_target.sysroot->source,
                expected.is_some() ? *expected : "<none>"_str)));
        }
    }
    return Ok(ResolvedBuildContext {
        .build_arguments = rstd::move(build_arguments),
        .platform        = rstd::move(platform),
    });
}

auto resolve_project_session(const lito::package::PackageSelection&         selection,
                             const lito::source::PackageSourceConfig&       sources,
                             const lito::lock::LockConfig&                  lock,
                             lito::tools::ToolResolver&                     tool_resolver,
                             const ResolvedProcessEnvironment&              environment,
                             const lito::dependency::CMakeBuildOverrideSet& cmake_build_overrides,
                             bool                                           locked,
                             lito::package::PackageSelectionPurpose         purpose,
                             usize                                          jobs,
                             ResolvedBuildContext                           context,
                             const Option<BuildEventSink>&                  observer      = None(),
                             Option<lito::workspace::WorkspaceCatalog>      catalog       = None(),
                             const AndroidCmakeProjection*                  android_cmake = nullptr,
                             const lito::config::LitoBootstrapConfig*       registries    = nullptr,
                             lito::registry::RegistryGraphProvider          registry      = {},
                             Option<ref<str>> artifact_processor                          = None())
    -> ProjectResult<ResolvedProjectSession> {
    auto project = rstd_try(resolve_project(selection,
                                            purpose,
                                            sources,
                                            lock,
                                            locked,
                                            lito::source::GitResolutionMode::ReuseLocked,
                                            rstd::addressof(context.platform.effective_target),
                                            tool_resolver,
                                            environment,
                                            cmake_build_overrides,
                                            jobs,
                                            observer_value(observer),
                                            rstd::move(catalog),
                                            lito::lock::InvalidLockPolicy::Reject,
                                            registries,
                                            registry,
                                            artifact_processor));
    return Ok(ResolvedProjectSession {
        .project         = rstd::move(project),
        .build_arguments = rstd::move(context.build_arguments),
        .platform        = rstd::move(context.platform),
        .android_cmake   = android_cmake != nullptr ? Some(android_cmake->clone())
                                                    : Option<AndroidCmakeProjection> {},
    });
}

auto resolve_project_metadata(ResolvedProjectSession                           session,
                              const cpp::BuildConfiguration&                   configuration,
                              const lito::manifest::BuildProfileName&          profile,
                              ref<rstd::path::Path>                            requested_output,
                              const lito::source::PackageSourceConfig&         sources,
                              const lito::tools::cargo::Configuration&         cargo,
                              const lito::dependency::PkgConfigProviderConfig& pkg_config,
                              const lito::dependency::CMakeProviderConfig&     cmake,
                              const ClangToolchain&                            toolchain,
                              lito::tools::ToolResolver&                       tool_resolver,
                              const ResolvedProcessEnvironment&                environment,
                              usize                                            jobs,
                              const Option<BuildEventSink>&                    observer = None(),
                              const Option<BuildSetupReportSink>& setup_reporter        = None(),
                              const Option<PathBuf>& cmake_find_install_prefix          = None())
    -> ProjectResult<ResolvedProjectMetadata> {
    auto external_sources       = rstd::move(session.project.external_sources);
    auto cmake_build_overrides  = rstd::move(session.project.cmake_build_overrides);
    auto project                = rstd::move(session.project.selection);
    auto resolved_configuration = configuration.clone();
    if (project.standards.cpp.is_some()) {
        resolved_configuration.language_standard =
            String::make(lito::manifest::cpp_standard_name(*project.standards.cpp));
    }
    resolved_configuration.c_standard    = project.standards.c;
    resolved_configuration.toolchain.cc  = PathBuf::from(toolchain.cc_path());
    resolved_configuration.toolchain.cxx = PathBuf::from(toolchain.cxx_path());
    resolved_configuration.toolchain.ld  = PathBuf::from(toolchain.ld_path());
    resolved_configuration.toolchain.ar  = PathBuf::from(toolchain.ar_path());
    auto resolved_profile = rstd_try(cpp::make_profile_spec(resolved_configuration,
                                                            project.graph.profile,
                                                            profile,
                                                            rstd::move(session.build_arguments)));
    rstd_try(cpp::apply_build_platform(resolved_profile, session.platform));
    const auto& target = session.platform.effective_target;
    rstd_try(
        validate_linker_profile(toolchain, resolved_profile, session.platform, project.standards));
    if (resolved_configuration.standard_library == lito::config::StandardLibrary::Msvc &&
        ! target.is_msvc()) {
        return Err(ProjectError::Message(rstd::format(
            "standard library 'msvc' requires a Windows MSVC target; effective target is '{}'",
            target.triple.as_str())));
    }
    if (resolved_configuration.standard_library == lito::config::StandardLibrary::Libstdcxx &&
        target.is_msvc()) {
        return Err(ProjectError::Message(rstd::format(
            "standard library 'libstdc++' is not supported for Windows MSVC target '{}'",
            target.triple.as_str())));
    }
    if (target.platform == TargetPlatform::Android &&
        resolved_configuration.standard_library != lito::config::StandardLibrary::Libcxx) {
        return Err(ProjectError::Message(
            rstd::format("Android target '{}' only supports standard library 'libc++'",
                         target.triple.as_str())));
    }
    if (target.platform != TargetPlatform::Android &&
        resolved_configuration.standard_library_runtime ==
            lito::config::StandardLibraryRuntime::Static) {
        return Err(ProjectError::Message(
            rstd::format("static standard-library runtime is not supported for target '{}'",
                         target.triple.as_str())));
    }
    auto normalize_microsoft_runtime =
        [&target](lito::compiler::CommonCompileOptions& options) -> ProjectResult<empty> {
        if (! target.is_msvc()) {
            if (options.microsoft_runtime_library.is_some()) {
                return Err(ProjectError::Message(rstd::format(
                    "-fms-runtime-lib requires a Windows MSVC target; effective target is '{}'",
                    target.triple.as_str())));
            }
            return Ok(empty {});
        }
        if (options.microsoft_runtime_library.is_none()) {
            options.microsoft_runtime_library =
                Some(lito::compiler::MicrosoftRuntimeLibrary::Dynamic);
        }
        if (! lito::compiler::microsoft_runtime_library_is_dynamic(
                *options.microsoft_runtime_library)) {
            return Err(ProjectError::Message(rstd::format(
                "Microsoft runtime '{}' requires static standard-library runtime support, which "
                "is not supported yet; toolchain.stdlib-runtime is 'dynamic'",
                lito::compiler::microsoft_runtime_library_name(
                    *options.microsoft_runtime_library))));
        }
        return Ok(empty {});
    };
    rstd_try(normalize_microsoft_runtime(resolved_profile.c.common));
    rstd_try(normalize_microsoft_runtime(resolved_profile.cpp.common));
    if (resolved_profile.c.common.microsoft_runtime_library !=
        resolved_profile.cpp.common.microsoft_runtime_library) {
        return Err(ProjectError::Message(String::make(
            "C and C++ compilation selected different Microsoft runtime libraries"_str)));
    }
    auto layout = BuildLayout::create(project.graph.root_directory.as_path(),
                                      requested_output,
                                      resolved_profile.name.as_str(),
                                      session.platform.output_key.as_str());
    if (layout.is_err()) {
        return Err(rstd::into<ProjectError>(rstd::move(layout).unwrap_err()));
    }
    if (project.standards.cpp.is_some()) {
        auto prepared_support = toolchain.prepare_target_cxx_support(
            resolved_profile.cpp, layout->generated_root().as_path());
        if (prepared_support.is_err()) {
            return Err(rstd::into<ProjectError>(rstd::move(prepared_support).unwrap_err()));
        }
        auto standard_library =
            toolchain.resolve_standard_library(resolved_profile.cpp,
                                               session.platform.effective_target,
                                               resolved_profile.linker_options);
        if (standard_library.is_err()) {
            return Err(rstd::into<ProjectError>(rstd::move(standard_library).unwrap_err()));
        }
        resolved_profile.cpp.abi.resolved_standard_library =
            Some(rstd::move(standard_library).unwrap());
    }
    emit_build_setup_report(setup_reporter,
                            configuration.toolchain,
                            toolchain,
                            configuration.global_options,
                            project.standards,
                            resolved_configuration.standard_library_runtime,
                            resolved_profile,
                            session.platform,
                            project.graph,
                            project.selected_package_names);
    for (auto& package : project.graph.packages) {
        auto selected = false;
        for (const auto& name : project.selected_package_names) {
            if (name == package.manifest.name.as_str()) selected = true;
        }
        if (! selected) continue;
        for (auto& target : package.manifest.targets) {
            auto id = lito::package::PackageTargetId {
                .package = package.manifest.name.clone(),
                .kind    = lito::manifest::package_target_kind(target),
                .name    = String::make(lito::manifest::package_target_name(target)),
            };
            auto target_selected = false;
            for (const auto& selected_target : project.effective_targets) {
                if (selected_target == id) target_selected = true;
            }
            if (! target_selected) {
                auto& source = lito::manifest::package_target_source(target);
                source.source_groups.clear();
                source.conditions.clear();
            }
        }
        auto has_library = false;
        for (const auto& target : package.manifest.targets) {
            if (target.is_Library()) has_library = true;
        }
        auto resolved = cpp::apply_package_configuration(
            package, resolved_configuration, resolved_profile, session.platform, has_library);
        if (resolved.is_err()) {
            return Err(rstd::into<ProjectError>(rstd::move(resolved).unwrap_err()));
        }
    }
    auto prepared_external_sources =
        prepare_external_dependency_sources(project.graph,
                                            project.selected_package_names,
                                            rstd::move(external_sources),
                                            cmake_build_overrides,
                                            tool_resolver,
                                            environment,
                                            jobs,
                                            observer_value(observer));
    if (prepared_external_sources.is_err()) {
        return Err(rstd::into<ProjectError>(rstd::move(prepared_external_sources).unwrap_err()));
    }
    auto external_usage = rstd_try(resolve_external_usage_catalog(project.graph,
                                                                  project.selected_package_names,
                                                                  *prepared_external_sources,
                                                                  cargo,
                                                                  pkg_config,
                                                                  cmake,
                                                                  resolved_configuration,
                                                                  resolved_profile,
                                                                  toolchain.linker_identity(),
                                                                  *layout,
                                                                  session.platform,
                                                                  tool_resolver,
                                                                  environment,
                                                                  jobs,
                                                                  observer,
                                                                  sources,
                                                                  session.android_cmake,
                                                                  cmake_find_install_prefix));
    auto assets         = rstd::move(external_usage.assets);
    auto provenance     = rstd::move(external_usage.provenance);
    auto host_only_dependencies = project.artifact_processor_package_names.clone();
    auto metadata = cpp::adapt_package_graph_metadata(rstd::move(project.graph),
                                                      project.selected_package_names,
                                                      project.selected_targets,
                                                      project.effective_targets,
                                                      host_only_dependencies,
                                                      resolved_configuration,
                                                      rstd::move(resolved_profile),
                                                      session.platform,
                                                      rstd::move(external_usage.usage),
                                                      rstd::move(external_usage.sources),
                                                      toolchain.argument_parser());
    if (metadata.is_err()) {
        return Err(rstd::into<ProjectError>(rstd::move(metadata).unwrap_err()));
    }
    return Ok(ResolvedProjectMetadata {
        .platform                   = rstd::move(session.platform),
        .layout                     = rstd::move(layout).unwrap(),
        .metadata                   = rstd::move(metadata).unwrap(),
        .external_assets            = rstd::move(assets),
        .external_source_provenance = rstd::move(provenance),
    });
}

auto prepare_resolved_build_project(ResolvedProjectSession                   session,
                                    const cpp::BuildConfiguration&           configuration,
                                    const lito::manifest::BuildProfileName&  profile,
                                    ref<rstd::path::Path>                    requested_output,
                                    const lito::source::PackageSourceConfig& sources,
                                    const lito::tools::cargo::Configuration& cargo,
                                    const lito::dependency::PkgConfigProviderConfig& pkg_config,
                                    const lito::dependency::CMakeProviderConfig&     cmake,
                                    ClangToolchain                                   toolchain,
                                    Option<Box<ResolvedPluginBuildProject>>          plugin_host,
                                    lito::tools::ToolResolver&                       tool_resolver,
                                    const ResolvedProcessEnvironment&                environment,
                                    usize                                            jobs,
                                    const Option<BuildEventSink>&       observer       = None(),
                                    const Option<BuildSetupReportSink>& setup_reporter = None(),
                                    const Option<PathBuf>& cmake_find_install_prefix   = None())
    -> ProjectResult<PreparedBuildProject> {
    auto metadata = resolve_project_metadata(rstd::move(session),
                                             configuration,
                                             profile,
                                             requested_output,
                                             sources,
                                             cargo,
                                             pkg_config,
                                             cmake,
                                             toolchain,
                                             tool_resolver,
                                             environment,
                                             jobs,
                                             observer,
                                             setup_reporter,
                                             cmake_find_install_prefix);
    if (metadata.is_err()) return Err(rstd::move(metadata).unwrap_err());
    auto resolved           = rstd::move(metadata).unwrap();
    auto aggregate_requests = proc_macro_aggregate_requests(resolved.metadata);
    auto prepared_host      = Option<Box<PreparedBuildProject>> {};
    if (plugin_host.is_some()) {
        auto host        = rstd::move(plugin_host).unwrap();
        auto host_output = PathBuf::from(resolved.layout.output())
                               .join(PathBuf::from("plugin-host"_str).as_path());
        auto prepared    = prepare_resolved_build_project(rstd::move(host->session),
                                                          host->configuration,
                                                          profile,
                                                          host_output.as_path(),
                                                          sources,
                                                          cargo,
                                                          pkg_config,
                                                          cmake,
                                                          rstd::move(host->toolchain),
                                                          None(),
                                                          tool_resolver,
                                                          environment,
                                                          jobs,
                                                          observer,
                                                          None());
        if (prepared.is_err()) return Err(rstd::move(prepared).unwrap_err());
        prepared->proc_macro_aggregates = rstd::move(aggregate_requests);
        prepared_host = Some(Box<PreparedBuildProject>::make(rstd::move(prepared).unwrap()));
    }
    return Ok(PreparedBuildProject {
        .toolchain                  = rstd::move(toolchain),
        .configuration              = configuration.clone(),
        .platform                   = rstd::move(resolved.platform),
        .layout                     = rstd::move(resolved.layout),
        .metadata                   = rstd::move(resolved.metadata),
        .external_assets            = rstd::move(resolved.external_assets),
        .external_source_provenance = rstd::move(resolved.external_source_provenance),
        .target_runtimes            = Vec<BuiltTargetRuntime>::make(),
        .target_stripper            = None(),
        .android_sdk                = None(),
        .plugin_host                = rstd::move(prepared_host),
        .proc_macro_aggregates      = Vec<ProcMacroAggregateRequest>::make(),
    });
}

auto resolve_project_metadata(
    const lito::package::PackageSelection&           selection,
    const cpp::BuildConfiguration&                   configuration,
    const lito::manifest::BuildProfileName&          profile,
    ref<rstd::path::Path>                            requested_output,
    const lito::source::PackageSourceConfig&         sources,
    const lito::lock::LockConfig&                    lock,
    const lito::tools::cargo::Configuration&         cargo,
    const lito::dependency::PkgConfigProviderConfig& pkg_config,
    const lito::dependency::CMakeProviderConfig&     cmake,
    const lito::dependency::CMakeBuildOverrideSet&   cmake_build_overrides,
    const ClangToolchain&                            toolchain,
    lito::tools::ToolResolver&                       tool_resolver,
    const ResolvedProcessEnvironment&                environment,
    bool                                             locked,
    lito::package::PackageSelectionPurpose    purpose = lito::package::PackageSelectionPurpose::All,
    usize                                     jobs    = usize(1),
    const Option<BuildEventSink>&             observer       = None(),
    const Option<BuildSetupReportSink>&       setup_reporter = None(),
    Option<lito::workspace::WorkspaceCatalog> catalog        = None())
    -> ProjectResult<ResolvedProjectMetadata> {
    auto context = rstd_try(resolve_build_context(configuration.global_options, toolchain));
    auto session = rstd_try(resolve_project_session(selection,
                                                    sources,
                                                    lock,
                                                    tool_resolver,
                                                    environment,
                                                    cmake_build_overrides,
                                                    locked,
                                                    purpose,
                                                    jobs,
                                                    rstd::move(context),
                                                    observer,
                                                    rstd::move(catalog)));
    return resolve_project_metadata(rstd::move(session),
                                    configuration,
                                    profile,
                                    requested_output,
                                    sources,
                                    cargo,
                                    pkg_config,
                                    cmake,
                                    toolchain,
                                    tool_resolver,
                                    environment,
                                    jobs,
                                    observer,
                                    setup_reporter);
}

auto resolve_build_project(const lito::package::PackageSelection&         selection,
                           const config::BuildConfigurationRequest&       configuration,
                           const lito::source::PackageSourceConfig&       sources,
                           const lito::lock::LockConfig&                  lock,
                           const lito::dependency::CMakeBuildOverrideSet& cmake_build_overrides,
                           lito::tools::ToolResolver&                     tool_resolver,
                           const ResolvedProcessEnvironment&              environment,
                           bool                                           locked,
                           lito::package::PackageSelectionPurpose         purpose,
                           usize                                          jobs,
                           const Option<BuildEventSink>&                  observer   = None(),
                           Option<lito::workspace::WorkspaceCatalog>      catalog    = None(),
                           const lito::config::LitoBootstrapConfig*       registries = nullptr,
                           lito::registry::RegistryGraphProvider          registry   = {})
    -> ProjectResult<ResolvedBuildProject> {
    auto host_request                     = configuration.clone();
    host_request.target                   = config::BuildTargetRequest::Default();
    host_request.toolchain.sdk            = None();
    host_request.toolchain.target         = config::ToolchainTargetSelection::CompilerDefault();
    host_request.toolchain.wasm           = None();
    host_request.standard_library         = config::StandardLibrarySelection::Auto;
    host_request.standard_library_runtime = config::StandardLibraryRuntime::Dynamic;
    auto selected        = rstd_try(resolve_configured_toolchain(configuration, environment));
    auto target_runtimes = Vec<BuiltTargetRuntime>::make();
    auto target_stripper = Option<PathBuf> {};
    if (selected.android.is_some() && selected.android->shared_runtime.is_some()) {
        target_runtimes.push(BuiltTargetRuntime {
            .name     = selected.android->shared_runtime->name.clone(),
            .path     = selected.android->shared_runtime->path.clone(),
            .identity = selected.android->shared_runtime->identity.clone(),
        });
    }
    if (selected.android.is_some()) {
        target_stripper = Some(selected.android->distribution.paths().strip.clone());
    }
    auto created = selected.android.is_some()
                       ? ClangToolchain::create_for_target(selected.tools.clone(),
                                                           configuration.standard_library,
                                                           selected.android->target.target_info,
                                                           environment)
                       : ClangToolchain::create(
                             selected.tools.clone(), configuration.standard_library, environment);
    if (created.is_err()) {
        return Err(rstd::into<ProjectError>(rstd::move(created).unwrap_err()));
    }
    auto       toolchain   = rstd::move(created).unwrap();
    const auto wasm_target = toolchain.target_info().architecture == Architecture::Wasm32 ||
                             toolchain.target_info().architecture == Architecture::Wasm64;
    if (configuration.toolchain.wasm.is_some() && ! wasm_target) {
        return Err(ProjectError::Message(
            rstd::format("toolchain.wasm requires a WebAssembly target, got '{}'",
                         toolchain.target_info().triple.as_str())));
    }
    host_request.global_options = rstd_try(cpp::project_host_profile_options(
        configuration.global_options, toolchain.argument_parser()));
    auto platform               = Option<BuildPlatform> {};
    auto cmake_projection       = Option<AndroidCmakeProjection> {};
    if (selected.android.is_some()) {
        auto& android = *selected.android;
        auto  resolved =
            resolve_android_build_platform(*selected.host, toolchain.target_info(), android.target);
        resolved.sdk_version  = Some(android.distribution.revision().text.clone());
        resolved.sdk_identity = Some(String::make(android.distribution.identity()));
        platform              = Some(rstd::move(resolved));
        cmake_projection      = Some(android.cmake.clone());
    }
    auto context = resolve_build_context(configuration.global_options,
                                         toolchain,
                                         platform.is_some() ? rstd::addressof(*platform) : nullptr);
    if (context.is_err()) return Err(rstd::move(context).unwrap_err());
    auto resolved_request       = configuration.clone();
    resolved_request.toolchain  = selected.tools.clone();
    auto resolved_configuration = config::resolve_build_configuration(
        rstd::move(resolved_request), toolchain.compile_target().standard_library);
    auto session = resolve_project_session(
        selection,
        sources,
        lock,
        tool_resolver,
        environment,
        cmake_build_overrides,
        locked,
        purpose,
        jobs,
        rstd::move(context).unwrap(),
        observer,
        rstd::move(catalog),
        cmake_projection.is_some() ? rstd::addressof(*cmake_projection) : nullptr,
        registries,
        registry,
        configuration.toolchain.wasm.is_some() && configuration.toolchain.wasm->processor.is_some()
            ? Some(configuration.toolchain.wasm->processor->as_str())
            : Option<ref<str>> {});
    if (session.is_err()) return Err(rstd::move(session).unwrap_err());
    auto plugin_host = Option<Box<ResolvedPluginBuildProject>> {};
    if (! session->project.selection.host_package_names.is_empty()) {
        auto host_selected = rstd_try(resolve_configured_toolchain(host_request, environment));
        auto host_created  = ClangToolchain::create(
            host_selected.tools.clone(), host_request.standard_library, environment);
        if (host_created.is_err()) {
            return Err(rstd::into<ProjectError>(rstd::move(host_created).unwrap_err()));
        }
        auto host_toolchain = rstd::move(host_created).unwrap();
        auto host_context =
            rstd_try(resolve_build_context(host_request.global_options, host_toolchain));
        auto host_session = rstd_try(
            resolve_project_session(selection,
                                    sources,
                                    lock,
                                    tool_resolver,
                                    environment,
                                    cmake_build_overrides,
                                    locked,
                                    purpose,
                                    jobs,
                                    rstd::move(host_context),
                                    observer,
                                    None(),
                                    nullptr,
                                    registries,
                                    registry,
                                    configuration.toolchain.wasm.is_some() &&
                                            configuration.toolchain.wasm->processor.is_some()
                                        ? Some(configuration.toolchain.wasm->processor->as_str())
                                        : Option<ref<str>> {}));
        auto host_selection = lito::package::resolve_plugin_host_selection(
            rstd::move(host_session.project.selection));
        if (host_selection.is_err()) {
            return Err(rstd::into<ProjectError>(rstd::move(host_selection).unwrap_err()));
        }
        host_session.project.selection  = rstd::move(host_selection).unwrap();
        auto resolved_host_request      = host_request.clone();
        resolved_host_request.toolchain = host_selected.tools.clone();
        auto host_configuration         = config::resolve_build_configuration(
            rstd::move(resolved_host_request), host_toolchain.compile_target().standard_library);
        plugin_host = Some(Box<ResolvedPluginBuildProject>::make(ResolvedPluginBuildProject {
            .configuration = rstd::move(host_configuration),
            .toolchain     = rstd::move(host_toolchain),
            .session       = rstd::move(host_session),
        }));
    }
    return Ok(ResolvedBuildProject {
        .configuration   = rstd::move(resolved_configuration),
        .toolchain       = rstd::move(toolchain),
        .session         = rstd::move(session).unwrap(),
        .target_runtimes = rstd::move(target_runtimes),
        .target_stripper = rstd::move(target_stripper),
        .android_sdk     = rstd::move(selected.android_sdk),
        .plugin_host     = rstd::move(plugin_host),
    });
}

auto prepare_build_project(
    const lito::package::PackageSelection&           selection,
    const config::BuildConfigurationRequest&         configuration,
    const lito::manifest::BuildProfileName&          profile,
    ref<rstd::path::Path>                            requested_output,
    const lito::source::PackageSourceConfig&         sources,
    const lito::lock::LockConfig&                    lock,
    const lito::tools::cargo::Configuration&         cargo,
    const lito::dependency::PkgConfigProviderConfig& pkg_config,
    const lito::dependency::CMakeProviderConfig&     cmake,
    const lito::dependency::CMakeBuildOverrideSet&   cmake_build_overrides,
    lito::tools::ToolResolver&                       tool_resolver,
    const ResolvedProcessEnvironment&                environment,
    bool                                             locked,
    lito::package::PackageSelectionPurpose    purpose = lito::package::PackageSelectionPurpose::All,
    usize                                     jobs    = usize(1),
    const Option<BuildEventSink>&             observer       = None(),
    Option<lito::workspace::WorkspaceCatalog> catalog        = None(),
    const Option<BuildSetupReportSink>&       setup_reporter = None(),
    const lito::config::LitoBootstrapConfig*  registries     = nullptr)
    -> ProjectResult<PreparedBuildProject> {
    auto resolved = rstd_try(resolve_build_project(selection,
                                                   configuration,
                                                   sources,
                                                   lock,
                                                   cmake_build_overrides,
                                                   tool_resolver,
                                                   environment,
                                                   locked,
                                                   purpose,
                                                   jobs,
                                                   observer,
                                                   rstd::move(catalog),
                                                   registries));
    auto prepared = prepare_resolved_build_project(rstd::move(resolved.session),
                                                   resolved.configuration,
                                                   profile,
                                                   requested_output,
                                                   sources,
                                                   cargo,
                                                   pkg_config,
                                                   cmake,
                                                   rstd::move(resolved.toolchain),
                                                   rstd::move(resolved.plugin_host),
                                                   tool_resolver,
                                                   environment,
                                                   jobs,
                                                   observer,
                                                   setup_reporter);
    if (prepared.is_err()) return Err(rstd::move(prepared).unwrap_err());
    prepared->target_runtimes = rstd::move(resolved.target_runtimes);
    prepared->target_stripper = rstd::move(resolved.target_stripper);
    prepared->android_sdk     = rstd::move(resolved.android_sdk);
    return prepared;
}

auto update_project_dependencies(
    ref<rstd::path::Path>                              root,
    const ProcessEnvironmentSpec&                      environment_spec,
    const lito::tools::ToolSpec&                       tools,
    const lito::lock::LockConfig&                      lock,
    const lito::source::PackageSourceConfig&           sources,
    const Option<BuildEventSink>&                      observer,
    const Option<lito::tools::HostToolResolutionSink>& tool_reporter = None(),
    const lito::config::LitoBootstrapConfig*           registries    = nullptr)
    -> ProjectResult<lito::lock::LockStatus> {
    if (root.is_empty()) {
        return Err(ProjectError::Message(String::make("update directory is required"_str)));
    }
    auto selection = lito::package::PackageSelection {
        .root = PathBuf::from(root),
    };
    auto environment   = rstd_try(ResolvedProcessEnvironment::resolve(environment_spec));
    auto tool_resolver = lito::tools::ToolResolver(environment, tools.clone(), tool_reporter);
    auto jobs          = usize(1);
    auto available     = rstd::thread::available_parallelism();
    if (available.is_ok()) jobs = available->get();
    auto resolved = rstd_try(resolve_project(selection,
                                             lito::package::PackageSelectionPurpose::All,
                                             sources,
                                             lock,
                                             false,
                                             lito::source::GitResolutionMode::Refresh,
                                             nullptr,
                                             tool_resolver,
                                             environment,
                                             lito::dependency::CMakeBuildOverrideSet {},
                                             jobs,
                                             observer_value(observer),
                                             None(),
                                             lito::lock::InvalidLockPolicy::Replace,
                                             registries));
    return Ok(resolved.lock);
}

} // namespace lito
