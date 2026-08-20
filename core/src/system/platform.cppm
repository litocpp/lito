module;
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/utsname.h>
#endif

export module lito.system:platform;

import rstd;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace rstd::literals;

export namespace lito::system
{

struct Architecture {
    String name;

    auto as_str() const noexcept -> ref<str> { return name.as_str(); }

    auto clone() const -> Architecture { return Architecture { .name = name.clone() }; }

    auto operator==(const Architecture& other) const noexcept -> bool { return name == other.name; }
};

class PlatformError {
    String message_;

public:
    explicit PlatformError(String message): message_(rstd::move(message)) {}

    auto message() const noexcept -> ref<str> { return message_.as_str(); }
};

template<typename T>
using PlatformResult = Result<T, PlatformError>;

enum class TargetFamily
{
    Unix,
    Windows,
    Unknown,
};

enum class TargetEnvironment
{
    Msvc,
    Gnu,
    Unknown,
};

struct TargetInfo {
    String            triple;
    Architecture      architecture;
    String            os;
    TargetFamily      family { TargetFamily::Unknown };
    TargetEnvironment environment { TargetEnvironment::Unknown };

    auto clone() const -> TargetInfo {
        return TargetInfo {
            .triple       = triple.clone(),
            .architecture = architecture.clone(),
            .os           = os.clone(),
            .family       = family,
            .environment  = environment,
        };
    }

    auto family_name() const noexcept -> ref<str> {
        switch (family) {
        case TargetFamily::Unix: return "unix"_str;
        case TargetFamily::Windows: return "windows"_str;
        case TargetFamily::Unknown: return "unknown"_str;
        }
        return "unknown"_str;
    }

    auto environment_name() const noexcept -> ref<str> {
        switch (environment) {
        case TargetEnvironment::Msvc: return "msvc"_str;
        case TargetEnvironment::Gnu: return "gnu"_str;
        case TargetEnvironment::Unknown: return "unknown"_str;
        }
        return "unknown"_str;
    }
};

struct HostInfo {
    Architecture architecture;
    String       os;

    auto clone() const -> HostInfo {
        return HostInfo {
            .architecture = architecture.clone(),
            .os           = os.clone(),
        };
    }
};

enum class BuildTargetIntent
{
    Native,
    ExplicitTarget,
};

struct BuildPlatform {
    HostInfo          host;
    TargetInfo        compiler_default;
    TargetInfo        effective_target;
    BuildTargetIntent intent { BuildTargetIntent::Native };
    bool              cross { false };
    Option<PathBuf>   sysroot;
    Option<String>    android_abi;
    Option<u32>       android_minimum_api;
    Option<String>    sdk_kind;
    Option<String>    sdk_version;
    Option<String>    sdk_identity;
    String            output_key;

    auto clone() const -> BuildPlatform {
        return BuildPlatform {
            .host                = host.clone(),
            .compiler_default    = compiler_default.clone(),
            .effective_target    = effective_target.clone(),
            .intent              = intent,
            .cross               = cross,
            .sysroot             = as<Clone>(sysroot).clone(),
            .android_abi         = as<Clone>(android_abi).clone(),
            .android_minimum_api = android_minimum_api,
            .sdk_kind            = as<Clone>(sdk_kind).clone(),
            .sdk_version         = as<Clone>(sdk_version).clone(),
            .sdk_identity        = as<Clone>(sdk_identity).clone(),
            .output_key          = output_key.clone(),
        };
    }
};

struct TargetPredicate {
    Vec<String> families;
    Vec<String> operating_systems;
    Vec<String> excluded_families;
    Vec<String> excluded_operating_systems;

    auto matches(const TargetInfo& target) const noexcept -> bool {
        const auto contains = [](const Vec<String>& values, ref<str> value) {
            if (values.is_empty()) return true;
            for (const auto& candidate : values) {
                if (candidate.as_str() == value) return true;
            }
            return false;
        };
        const auto excludes = [](const Vec<String>& values, ref<str> value) {
            for (const auto& candidate : values) {
                if (candidate.as_str() == value) return true;
            }
            return false;
        };
        return contains(families, target.family_name()) &&
               contains(operating_systems, target.os.as_str()) &&
               ! excludes(excluded_families, target.family_name()) &&
               ! excludes(excluded_operating_systems, target.os.as_str());
    }
};

} // namespace lito::system

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::system::PlatformError> : ImplBase<lito::system::PlatformError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_str(this->self().message());
    }
};

template<>
struct Impl<fmt::Debug, lito::system::PlatformError> : ImplBase<lito::system::PlatformError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::system::PlatformError>
    : DefaultInImpl<error::Error, lito::system::PlatformError> {};

} // namespace rstd

namespace lito::system
{

template<typename T>
auto platform_failure(String message) -> PlatformResult<T> {
    return Err(PlatformError(rstd::move(message)));
}

template<typename T>
auto platform_failure(ref<str> message) -> PlatformResult<T> {
    return platform_failure<T>(String::make(message));
}

auto host_os(ref<str> name) -> PlatformResult<String> {
    if (name == "Linux"_str) return Ok(String::make("linux"_str));
    if (name == "Darwin"_str) return Ok(String::make("macos"_str));
    if (name == "FreeBSD"_str) return Ok(String::make("freebsd"_str));
    if (name == "NetBSD"_str) return Ok(String::make("netbsd"_str));
    if (name == "OpenBSD"_str) return Ok(String::make("openbsd"_str));
    return platform_failure<String>(rstd::format("unsupported host operating system '{}'", name));
}

#if ! defined(_WIN32)
auto c_string(const char* value, ref<str> context) -> PlatformResult<String> {
    auto text = rstd::ffi::CStr::from_ptr(value).to_str();
    if (text.is_err()) {
        return platform_failure<String>(rstd::format("{} is not valid UTF-8", context));
    }
    return Ok(String::make(text.unwrap()));
}
#endif

} // namespace lito::system

export namespace lito::system
{

auto canonical_architecture(ref<str> value) -> PlatformResult<Architecture> {
    if (value.is_empty()) return platform_failure<Architecture>("architecture is empty"_str);
    for (const auto byte : value.as_bytes()) {
        if ((byte >= u8('a') && byte <= u8('z')) || (byte >= u8('A') && byte <= u8('Z')) ||
            (byte >= u8('0') && byte <= u8('9')) || byte == u8('_') || byte == u8('-')) {
            continue;
        }
        return platform_failure<Architecture>(
            rstd::format("architecture '{}' contains unsupported characters", value));
    }
    auto canonical = String::make(value);
    canonical->make_ascii_lowercase();
    if (canonical.as_str() == "amd64"_str || canonical.as_str() == "x64"_str ||
        canonical.as_str() == "x86_64"_str) {
        return Ok(Architecture { .name = String::make("x86_64"_str) });
    }
    if (canonical.as_str() == "arm64"_str || canonical.as_str() == "aarch64"_str) {
        return Ok(Architecture { .name = String::make("aarch64"_str) });
    }
    return Ok(Architecture { .name = rstd::move(canonical) });
}

auto parse_target_info(ref<str> triple) -> PlatformResult<TargetInfo> {
    if (triple.is_empty()) return platform_failure<TargetInfo>("target triple is empty"_str);
    auto arch_end = usize {};
    while (arch_end < triple.len() && triple.as_bytes()[arch_end] != u8('-')) ++arch_end;
    auto arch = triple.get(usize {}, arch_end);
    if (arch.is_none() || arch->is_empty()) {
        return platform_failure<TargetInfo>(
            rstd::format("target triple '{}' has no architecture", triple));
    }
    auto architecture = canonical_architecture(*arch);
    if (architecture.is_err()) return Err(rstd::move(architecture).unwrap_err());

    auto os          = String::make("unknown"_str);
    auto family      = TargetFamily::Unknown;
    auto environment = TargetEnvironment::Unknown;
    if (triple.contains("-windows"_str) || triple.contains("-win32"_str) ||
        triple.contains("-mingw"_str)) {
        os     = String::make("windows"_str);
        family = TargetFamily::Windows;
        if (triple.contains("-msvc"_str)) {
            environment = TargetEnvironment::Msvc;
        } else if (triple.contains("-gnu"_str) || triple.contains("-mingw"_str)) {
            environment = TargetEnvironment::Gnu;
        }
    } else if (triple.contains("-android"_str)) {
        os     = String::make("android"_str);
        family = TargetFamily::Unix;
    } else if (triple.contains("-linux"_str)) {
        os     = String::make("linux"_str);
        family = TargetFamily::Unix;
    } else if (triple.contains("-darwin"_str) || triple.contains("-apple"_str)) {
        os     = String::make("macos"_str);
        family = TargetFamily::Unix;
    } else if (triple.contains("-freebsd"_str)) {
        os     = String::make("freebsd"_str);
        family = TargetFamily::Unix;
    } else if (triple.contains("-netbsd"_str)) {
        os     = String::make("netbsd"_str);
        family = TargetFamily::Unix;
    } else if (triple.contains("-openbsd"_str)) {
        os     = String::make("openbsd"_str);
        family = TargetFamily::Unix;
    }
    return Ok(TargetInfo {
        .triple       = String::make(triple),
        .architecture = rstd::move(architecture).unwrap(),
        .os           = rstd::move(os),
        .family       = family,
        .environment  = environment,
    });
}

auto detect_host_info() -> PlatformResult<HostInfo> {
#if defined(_WIN32)
    auto information = SYSTEM_INFO {};
    ::GetNativeSystemInfo(&information);
    auto machine = String::make();
    switch (information.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64: machine = String::make("amd64"_str); break;
    case PROCESSOR_ARCHITECTURE_ARM64: machine = String::make("arm64"_str); break;
    default:
        return platform_failure<HostInfo>(rstd::format(
            "unsupported Windows processor architecture {}", information.wProcessorArchitecture));
    }
    auto architecture = canonical_architecture(machine.as_str());
    if (architecture.is_err()) return Err(rstd::move(architecture).unwrap_err());
    return Ok(HostInfo {
        .architecture = rstd::move(architecture).unwrap(),
        .os           = String::make("windows"_str),
    });
#else
    auto information = utsname {};
    if (::uname(&information) != 0) {
        return platform_failure<HostInfo>("cannot query host platform with uname"_str);
    }
    auto machine = c_string(information.machine, "host architecture"_str);
    if (machine.is_err()) return Err(rstd::move(machine).unwrap_err());
    auto system = c_string(information.sysname, "host operating system"_str);
    if (system.is_err()) return Err(rstd::move(system).unwrap_err());
    auto architecture = canonical_architecture(machine->as_str());
    if (architecture.is_err()) return Err(rstd::move(architecture).unwrap_err());
    auto os = host_os(system->as_str());
    if (os.is_err()) return Err(rstd::move(os).unwrap_err());
    return Ok(HostInfo {
        .architecture = rstd::move(architecture).unwrap(),
        .os           = rstd::move(os).unwrap(),
    });
#endif
}

auto resolve_build_platform(const HostInfo&   host,
                            const TargetInfo& compiler_default,
                            Option<ref<str>>  explicit_target,
                            Option<ref<str>>  explicit_sysroot) -> PlatformResult<BuildPlatform> {
    auto effective = compiler_default.clone();
    auto intent    = BuildTargetIntent::Native;
    if (explicit_target.is_some()) {
        auto parsed = parse_target_info(**explicit_target);
        if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
        effective = rstd::move(parsed).unwrap();
        intent    = BuildTargetIntent::ExplicitTarget;
    } else if (host.architecture != compiler_default.architecture ||
               host.os != compiler_default.os.as_str()) {
        return platform_failure<BuildPlatform>(rstd::format(
            "compiler default target '{}' is not native-compatible with host '{}-{}'; declare "
            "an explicit target/toolchain configuration for cross compilation",
            compiler_default.triple.as_str(),
            host.architecture.as_str(),
            host.os.as_str()));
    }
    auto cross = host.architecture != effective.architecture || host.os != effective.os.as_str();
    auto output_key = String::make();
    if (intent == BuildTargetIntent::ExplicitTarget) {
        output_key = String::make("target-"_str);
        output_key.push_str(rstd::crypto::sha256_hex(effective.triple.as_str()).as_str());
    }
    return Ok(BuildPlatform {
        .host             = host.clone(),
        .compiler_default = compiler_default.clone(),
        .effective_target = rstd::move(effective),
        .intent           = intent,
        .cross            = cross,
        .sysroot          = explicit_sysroot.is_some() ? Some(PathBuf::from(**explicit_sysroot))
                                                       : Option<PathBuf> {},
        .output_key       = rstd::move(output_key),
    });
}

auto resolve_build_platform(const HostInfo&   host,
                            const TargetInfo& compiler_default,
                            Option<ref<str>>  explicit_target) -> PlatformResult<BuildPlatform> {
    return resolve_build_platform(host, compiler_default, explicit_target, None());
}

} // namespace lito::system
