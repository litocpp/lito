export module lito.core:dependency.usage;

import rstd;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;

export namespace lito
{

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

struct DeclaredUsageRequirements {
    Vec<PathBuf>                     public_include_directories;
    Vec<PathBuf>                     private_include_directories;
    Vec<String>                      public_definitions;
    Vec<String>                      private_definitions;
    Vec<String>                      public_options;
    Vec<String>                      private_options;
    Vec<String>                      private_linker_options;
    Vec<IncludeDirectoryRequirement> private_include_directory_requirements;
};

} // namespace lito
