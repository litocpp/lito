module;
#include <rstd/enum.hpp>

export module lito.cpp:standard_library.model;

import rstd;
import lito.core;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::cpp
{

struct StandardLibraryModuleManifestCandidate
    : DefaultInClass<StandardLibraryModuleManifestCandidate, Clone> {
    Vec<PathBuf> paths;

    auto clone() const -> StandardLibraryModuleManifestCandidate {
        return StandardLibraryModuleManifestCandidate { .paths = paths.clone() };
    }
};

struct StandardLibraryModuleEntry : DefaultInClass<StandardLibraryModuleEntry, Clone> {
    String       logical_name;
    PathBuf      source;
    String       source_identity;
    Vec<PathBuf> system_include_directories;

    auto clone() const -> StandardLibraryModuleEntry {
        return StandardLibraryModuleEntry {
            .logical_name               = logical_name.clone(),
            .source                     = source.clone(),
            .source_identity            = source_identity.clone(),
            .system_include_directories = system_include_directories.clone(),
        };
    }
};

struct StandardLibraryModuleCatalog : DefaultInClass<StandardLibraryModuleCatalog, Clone> {
    lito::config::StandardLibrary   family { lito::config::StandardLibrary::Libstdcxx };
    PathBuf                         manifest;
    String                          manifest_identity;
    u64                             version {};
    u64                             revision {};
    Vec<StandardLibraryModuleEntry> modules;

    auto clone() const -> StandardLibraryModuleCatalog {
        return StandardLibraryModuleCatalog {
            .family            = family,
            .manifest          = manifest.clone(),
            .manifest_identity = manifest_identity.clone(),
            .version           = version,
            .revision          = revision,
            .modules           = modules.clone(),
        };
    }

    auto get(ref<str> logical_name) const noexcept -> Option<ref<StandardLibraryModuleEntry>> {
        for (const auto& module : modules) {
            if (module.logical_name.as_str() == logical_name) {
                return Some(
                    ref<StandardLibraryModuleEntry>::from_raw_parts(rstd::addressof(module)));
            }
        }
        return None();
    }
};

struct StandardLibraryModuleUnit : DefaultInClass<StandardLibraryModuleUnit, Clone> {
    String logical_name;
    String manifest_identity;
    String context_identity;

    auto clone() const -> StandardLibraryModuleUnit {
        return StandardLibraryModuleUnit {
            .logical_name      = logical_name.clone(),
            .manifest_identity = manifest_identity.clone(),
            .context_identity  = context_identity.clone(),
        };
    }
};

class CompileUnitOwner : public DefaultInClass<CompileUnitOwner, Clone> {
    RSTD_ENUM_DEFAULT(CompileUnitOwner,
                      (Project),
                      (Project, (usize target;)),
                      (StandardLibrary, (StandardLibraryModuleUnit module;)))

public:
    auto clone() const -> CompileUnitOwner {
        return is_Project() ? Project(as_Project().target)
                            : StandardLibrary(as<Clone>(as_StandardLibrary().module).clone());
    }
};

auto is_standard_library_module_name(ref<str> name) noexcept -> bool {
    return name == "std"_str || name == "std.compat"_str;
}

auto standard_library_name(lito::config::StandardLibrary family) noexcept -> ref<str> {
    return family == lito::config::StandardLibrary::Libstdcxx ? "libstdc++"_str : "libc++"_str;
}

} // namespace lito::cpp
