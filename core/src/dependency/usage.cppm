export module lito.core:dependency.usage;

import rstd;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;

export namespace lito::dependency
{

enum class IncludeDirectoryRoot
{
    Package,
    Generated,
    ExternalSource,
};

struct IncludeDirectoryRequirement {
    IncludeDirectoryRoot root { IncludeDirectoryRoot::Package };
    PathBuf              path;
    Option<String>       external_source;

    auto clone() const -> IncludeDirectoryRequirement {
        auto result = IncludeDirectoryRequirement { .root = root, .path = path.clone() };
        if (external_source.is_some()) result.external_source = Some(external_source->clone());
        return result;
    }
};

struct DeclaredUsageRequirements {
    Vec<PathBuf>                     public_include_directories;
    Vec<PathBuf>                     private_include_directories;
    Vec<String>                      public_definitions;
    Vec<String>                      private_definitions;
    Vec<String>                      options;
    Vec<String>                      linker_options;
    bool                             threads { false };
    Vec<String>                      system_libraries;
    Vec<IncludeDirectoryRequirement> private_include_directory_requirements;
    Vec<IncludeDirectoryRequirement> public_include_directory_requirements;
};

} // namespace lito::dependency
