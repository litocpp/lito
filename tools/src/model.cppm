export module lito.tools:model;

import rstd;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace rstd::literals;

export namespace lito::tools
{

enum class Tool
{
    Cargo,
    CMake,
    Tar,
    BsdTar,
    ClangFormat,
    Curl,
    Git,
    PkgConfig,
    Strip,
};

enum class HostToolCapability
{
    CargoBuild,
    CMakeProject,
    PkgConfigQuery,
    GitCheckout,
    HttpDownload,
    RegistryPublish,
    ArchiveExtraction,
    SourceFormatting,
    ArtifactStripping,
};

constexpr auto host_tool_capability_name(HostToolCapability capability) noexcept -> ref<str> {
    switch (capability) {
    case HostToolCapability::CargoBuild: return "Cargo build"_str;
    case HostToolCapability::CMakeProject: return "CMake project"_str;
    case HostToolCapability::PkgConfigQuery: return "pkg-config query"_str;
    case HostToolCapability::GitCheckout: return "Git checkout"_str;
    case HostToolCapability::HttpDownload: return "archive download"_str;
    case HostToolCapability::RegistryPublish: return "Registry publish"_str;
    case HostToolCapability::ArchiveExtraction: return "archive extraction"_str;
    case HostToolCapability::SourceFormatting: return "source formatting"_str;
    case HostToolCapability::ArtifactStripping: return "artifact stripping"_str;
    }
    return "host tool"_str;
}

enum class HostToolRequirementOriginKind
{
    Command,
    ExternalDependency,
    ExternalSource,
    BuildProfile,
    InstallEntry,
};

struct HostToolRequirementOrigin {
    HostToolRequirementOriginKind kind { HostToolRequirementOriginKind::Command };
    String                        owner;
    String                        subject;

    auto clone() const -> HostToolRequirementOrigin {
        return HostToolRequirementOrigin {
            .kind    = kind,
            .owner   = owner.clone(),
            .subject = subject.clone(),
        };
    }
};

struct HostToolRequirement {
    HostToolCapability        capability { HostToolCapability::CMakeProject };
    HostToolRequirementOrigin origin;

    auto clone() const -> HostToolRequirement {
        return HostToolRequirement {
            .capability = capability,
            .origin     = origin.clone(),
        };
    }
};

auto command_tool_requirement(HostToolCapability capability, ref<str> command)
    -> HostToolRequirement {
    return HostToolRequirement {
        .capability = capability,
        .origin =
            HostToolRequirementOrigin {
                .kind  = HostToolRequirementOriginKind::Command,
                .owner = String::make(command),
            },
    };
}

auto external_dependency_tool_requirement(HostToolCapability capability,
                                          ref<str>           package,
                                          ref<str>           alias) -> HostToolRequirement {
    return HostToolRequirement {
        .capability = capability,
        .origin =
            HostToolRequirementOrigin {
                .kind    = HostToolRequirementOriginKind::ExternalDependency,
                .owner   = String::make(package),
                .subject = String::make(alias),
            },
    };
}

auto external_source_tool_requirement(HostToolCapability capability,
                                      ref<str>           package,
                                      ref<str>           source) -> HostToolRequirement {
    return HostToolRequirement {
        .capability = capability,
        .origin =
            HostToolRequirementOrigin {
                .kind    = HostToolRequirementOriginKind::ExternalSource,
                .owner   = String::make(package),
                .subject = String::make(source),
            },
    };
}

auto build_profile_tool_requirement(HostToolCapability capability, ref<str> profile, ref<str> field)
    -> HostToolRequirement {
    return HostToolRequirement {
        .capability = capability,
        .origin =
            HostToolRequirementOrigin {
                .kind    = HostToolRequirementOriginKind::BuildProfile,
                .owner   = String::make(profile),
                .subject = String::make(field),
            },
    };
}

auto install_entry_tool_requirement(HostToolCapability capability,
                                    ref<str>           package,
                                    ref<str>           destination) -> HostToolRequirement {
    return HostToolRequirement {
        .capability = capability,
        .origin =
            HostToolRequirementOrigin {
                .kind    = HostToolRequirementOriginKind::InstallEntry,
                .owner   = String::make(package),
                .subject = String::make(destination),
            },
    };
}

auto host_tool_requirement_origin_text(const HostToolRequirementOrigin& origin) -> String {
    switch (origin.kind) {
    case HostToolRequirementOriginKind::Command:
        return rstd::format("command '{}'", origin.owner.as_str());
    case HostToolRequirementOriginKind::ExternalDependency:
        return rstd::format(
            "external dependency '{}:{}'", origin.owner.as_str(), origin.subject.as_str());
    case HostToolRequirementOriginKind::ExternalSource:
        return rstd::format(
            "external source '{}:{}'", origin.owner.as_str(), origin.subject.as_str());
    case HostToolRequirementOriginKind::BuildProfile:
        return rstd::format("profile '{}.{}'", origin.owner.as_str(), origin.subject.as_str());
    case HostToolRequirementOriginKind::InstallEntry:
        return rstd::format(
            "install entry '{}:{}'", origin.owner.as_str(), origin.subject.as_str());
    }
    return String::make("current operation"_str);
}

struct HostToolResolution {
    enum class Kind
    {
        Selected,
        CandidateMissing,
        NotRequired,
    };

    Kind                kind { Kind::Selected };
    HostToolRequirement requirement;
    String              provider;
    PathBuf             requested;
    Option<PathBuf>     executable;
    String              detail;
};

struct HostToolResolutionSink {
    void* context {};
    void (*notify)(void*, const HostToolResolution&) noexcept {};
};

constexpr auto tool_name(Tool tool) noexcept -> ref<str> {
    switch (tool) {
    case Tool::Cargo: return "cargo"_str;
    case Tool::CMake: return "cmake"_str;
    case Tool::Tar: return "tar"_str;
    case Tool::BsdTar: return "bsdtar"_str;
    case Tool::ClangFormat: return "clang-format"_str;
    case Tool::Curl: return "curl"_str;
    case Tool::Git: return "git"_str;
    case Tool::PkgConfig: return "pkg-config"_str;
    case Tool::Strip: return "strip"_str;
    }
    return ""_str;
}

constexpr auto tool_description(Tool tool) noexcept -> ref<str> {
    switch (tool) {
    case Tool::Cargo: return "Cargo executable"_str;
    case Tool::CMake: return "CMake executable"_str;
    case Tool::Tar: return "tar archive extractor"_str;
    case Tool::BsdTar: return "bsdtar archive extractor"_str;
    case Tool::ClangFormat: return "clang-format executable"_str;
    case Tool::Curl: return "curl executable"_str;
    case Tool::Git: return "Git executable"_str;
    case Tool::PkgConfig: return "pkg-config executable"_str;
    case Tool::Strip: return "LLVM strip executable"_str;
    }
    return "tool executable"_str;
}

struct ToolSpec {
    PathBuf   cargo { PathBuf::from("cargo"_str) };
    PathBuf   cmake { PathBuf::from("cmake"_str) };
    PathBuf   tar { PathBuf::from("tar"_str) };
    PathBuf   bsdtar { PathBuf::from("bsdtar"_str) };
    PathBuf   clang_format { PathBuf::from("clang-format"_str) };
    PathBuf   curl { PathBuf::from("curl"_str) };
    PathBuf   git { PathBuf::from("git"_str) };
    PathBuf   pkg_config { PathBuf::from("pkg-config"_str) };
    PathBuf   strip { PathBuf::from("llvm-strip"_str) };
    Vec<Tool> configured_tools;

    auto requested(Tool tool) const noexcept -> ref<rstd::path::Path> {
        switch (tool) {
        case Tool::Cargo: return cargo.as_path();
        case Tool::CMake: return cmake.as_path();
        case Tool::Tar: return tar.as_path();
        case Tool::BsdTar: return bsdtar.as_path();
        case Tool::ClangFormat: return clang_format.as_path();
        case Tool::Curl: return curl.as_path();
        case Tool::Git: return git.as_path();
        case Tool::PkgConfig: return pkg_config.as_path();
        case Tool::Strip: return strip.as_path();
        }
        return cmake.as_path();
    }

    auto mark_configured(Tool tool) -> void {
        if (! explicitly_configured(tool)) configured_tools.push(rstd::move(tool));
    }

    auto explicitly_configured(Tool tool) const noexcept -> bool {
        for (const auto configured : configured_tools) {
            if (configured == tool) return true;
        }
        return false;
    }

    auto clone() const -> ToolSpec {
        return ToolSpec {
            .cargo            = cargo.clone(),
            .cmake            = cmake.clone(),
            .tar              = tar.clone(),
            .bsdtar           = bsdtar.clone(),
            .clang_format     = clang_format.clone(),
            .curl             = curl.clone(),
            .git              = git.clone(),
            .pkg_config       = pkg_config.clone(),
            .strip            = strip.clone(),
            .configured_tools = configured_tools.clone(),
        };
    }
};

} // namespace lito::tools
