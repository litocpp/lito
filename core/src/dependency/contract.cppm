module;
#include <rstd/enum.hpp>

export module lito.dependency.contract;

import rstd;
import lito.error;
import lito.cpp;
import lito.platform.contract;
import lito.source.contract;

using namespace rstd::prelude;

export namespace lito
{

enum class DependencyVisibility
{
    Public,
    Private,
    LinkOnly,
};

enum class PkgConfigVersionOperator
{
    Equal,
    Less,
    Greater,
    LessEqual,
    GreaterEqual,
};

enum class PkgConfigQueryMode
{
    Shared,
    Static,
};

struct PkgConfigVersionRequirement {
    PkgConfigVersionOperator comparison { PkgConfigVersionOperator::Equal };
    String                   value;
};

struct PkgConfigDependencyRequirement {
    String                              module;
    Option<PkgConfigVersionRequirement> version;
    PkgConfigQueryMode                  mode { PkgConfigQueryMode::Shared };
};

struct CMakeCacheEntry {
    String name;
    String value;
};

struct CMakeArchiveVariant {
    Architecture architecture;
    String       url;
    String       sha256;
};

class CMakeDependencySource {
    RSTD_ENUM(CMakeDependencySource,
              (Installed),
              (Path, (PathBuf path;)),
              (Git, (String url; GitReference reference;)),
              (Archive, (String url; String sha256;)),
              (ArchitectureArchives, (Vec<CMakeArchiveVariant> variants;)))

public:
    auto clone() const -> CMakeDependencySource {
        if (is_Path()) return CMakeDependencySource::Path(as_Path().path.clone());
        if (is_Git()) {
            return CMakeDependencySource::Git(as_Git().url.clone(),
                                              GitReference {
                                                  .kind  = as_Git().reference.kind,
                                                  .value = as_Git().reference.value.clone(),
                                              });
        }
        if (is_Archive()) {
            return CMakeDependencySource::Archive(as_Archive().url.clone(),
                                                  as_Archive().sha256.clone());
        }
        if (is_ArchitectureArchives()) {
            auto variants =
                Vec<CMakeArchiveVariant>::with_capacity(as_ArchitectureArchives().variants.len());
            for (const auto& variant : as_ArchitectureArchives().variants) {
                variants.push(CMakeArchiveVariant {
                    .architecture = variant.architecture.clone(),
                    .url          = variant.url.clone(),
                    .sha256       = variant.sha256.clone(),
                });
            }
            return CMakeDependencySource::ArchitectureArchives(rstd::move(variants));
        }
        return CMakeDependencySource::Installed();
    }
};

enum class CMakeIntegration
{
    Install,
    BuildTree,
};

struct CMakeTargetRequirement {
    String               name;
    DependencyVisibility visibility { DependencyVisibility::Private };
};

struct CMakeDependencyRequirement {
    String                      alias;
    String                      package;
    CMakeDependencySource       source;
    CMakeIntegration            integration { CMakeIntegration::Install };
    Option<PathBuf>             adapter;
    Option<PathBuf>             config_directory;
    Vec<CMakeCacheEntry>        cache;
    Vec<CMakeTargetRequirement> targets;
    Option<PathBuf>             declaration_root;
    Option<PathBuf>             adapter_root;
};

class ResolvedExternalSource {
    RSTD_ENUM(ResolvedExternalSource,
              (Path, (PathBuf path;)),
              (Git, (String url; GitReference reference; String commit;)),
              (Archive, (String url; String sha256;)))
};

struct ResolvedExternalSourceRecord {
    String                 package;
    String                 alias;
    String                 provider;
    Vec<Architecture>      architectures;
    ResolvedExternalSource source;
};

struct PkgConfigExternalDependency {
    String                         alias;
    PkgConfigDependencyRequirement requirement;
    DependencyVisibility           visibility { DependencyVisibility::Private };
};

struct PkgConfigProviderConfig {
    PathBuf         executable;
    Vec<PathBuf>    search_paths;
    Vec<PathBuf>    library_paths;
    Option<PathBuf> sysroot;
    bool            target_configured { false };

    auto clone() const -> PkgConfigProviderConfig {
        auto result = PkgConfigProviderConfig {
            .executable        = executable.clone(),
            .search_paths      = as<rstd::clone::Clone>(search_paths).clone(),
            .library_paths     = as<rstd::clone::Clone>(library_paths).clone(),
            .target_configured = target_configured,
        };
        if (sysroot.is_some()) result.sysroot = Some(sysroot->clone());
        return result;
    }
};

struct CMakeProviderConfig {
    PathBuf      executable;
    String       generator;
    String       identity;
    Vec<PathBuf> search_paths;

    auto clone() const -> CMakeProviderConfig {
        return CMakeProviderConfig {
            .executable   = executable.clone(),
            .generator    = generator.clone(),
            .identity     = identity.clone(),
            .search_paths = as<rstd::clone::Clone>(search_paths).clone(),
        };
    }
};

struct LinkArgumentSequence {
    Vec<String> tokens;
    String      source;
    String      identity;

    auto clone() const -> LinkArgumentSequence {
        return LinkArgumentSequence {
            .tokens   = as<rstd::clone::Clone>(tokens).clone(),
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
            .compile_arguments = as<rstd::clone::Clone>(compile_arguments).clone(),
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

enum class IncludeDirectoryRoot
{
    Package,
    Generated,
};

struct IncludeDirectoryRequirement {
    IncludeDirectoryRoot root { IncludeDirectoryRoot::Package };
    PathBuf              path;

    auto clone() const -> IncludeDirectoryRequirement {
        return IncludeDirectoryRequirement { .root = root, .path = path.clone() };
    }
};

struct UsageRequirements {
    Vec<PathBuf>                     public_include_directories;
    Vec<PathBuf>                     private_include_directories;
    Vec<String>                      public_definitions;
    Vec<String>                      private_definitions;
    Vec<String>                      public_options;
    Vec<String>                      private_options;
    CppArgumentLayer                 public_arguments;
    CppArgumentLayer                 private_arguments;
    Vec<String>                      private_linker_options;
    Vec<IncludeDirectoryRequirement> private_include_directory_requirements;
};

class PreparedCMakeDependencySource {
    RSTD_ENUM(PreparedCMakeDependencySource,
              (Installed),
              (Directory, (PathBuf root; String identity; bool cacheable;)),
              (Archive, (String url; String sha256;)),
              (ArchitectureArchives, (Vec<CMakeArchiveVariant> variants;)))
};

struct PreparedCMakeDependencyRequirement {
    String                        alias;
    String                        package;
    PreparedCMakeDependencySource source;
    CMakeIntegration              integration { CMakeIntegration::Install };
    Option<PathBuf>               adapter;
    String                        adapter_identity;
    Option<PathBuf>               config_directory;
    Vec<CMakeCacheEntry>          cache;
    Vec<CMakeTargetRequirement>   targets;
};

class ResolvedCMakeDependencySource {
    RSTD_ENUM(ResolvedCMakeDependencySource,
              (Installed),
              (Directory, (PathBuf root; String identity; bool add_subdirectory; bool cacheable;)),
              (Archive, (String url; String sha256;)))
};

struct ResolvedCMakeDependencyRequirement {
    String                        alias;
    String                        package;
    ResolvedCMakeDependencySource source;
    CMakeIntegration              integration { CMakeIntegration::Install };
    Option<PathBuf>               adapter;
    String                        adapter_identity;
    Option<PathBuf>               config_directory;
    Vec<CMakeCacheEntry>          cache;
    Vec<CMakeTargetRequirement>   targets;
};

} // namespace lito
