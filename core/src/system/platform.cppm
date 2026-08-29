module;
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/utsname.h>
#endif

export module lito.system:platform;

import rstd;
import licrypto;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace rstd::literals;

export namespace lito::system
{

enum class Architecture
{
    Unknown,
    Arm,
    Armeb,
    Aarch64,
    Aarch64Be,
    Aarch64_32,
    Arc,
    Avr,
    Bpfel,
    Bpfeb,
    Csky,
    Dxil,
    Hexagon,
    Loongarch32,
    Loongarch64,
    M68k,
    Mips,
    Mipsel,
    Mips64,
    Mips64el,
    Msp430,
    Powerpc,
    Powerpcle,
    Powerpc64,
    Powerpc64le,
    R600,
    Amdgpu,
    Riscv32,
    Riscv64,
    Riscv32be,
    Riscv64be,
    Sparc,
    Sparcv9,
    Sparcel,
    Systemz,
    Tce,
    Tcele,
    Tcele64,
    Thumb,
    Thumbeb,
    X86,
    X86_64,
    Xcore,
    Xtensa,
    Nvptx,
    Nvptx64,
    Amdil,
    Amdil64,
    Hsail,
    Hsail64,
    Spir,
    Spir64,
    Spirv,
    Spirv32,
    Spirv64,
    Kalimba,
    Shave,
    Lanai,
    Wasm32,
    Wasm64,
    Renderscript32,
    Renderscript64,
    Ve,
};

inline constexpr Architecture ARCHITECTURES[] = {
    Architecture::Arm,
    Architecture::Armeb,
    Architecture::Aarch64,
    Architecture::Aarch64Be,
    Architecture::Aarch64_32,
    Architecture::Arc,
    Architecture::Avr,
    Architecture::Bpfel,
    Architecture::Bpfeb,
    Architecture::Csky,
    Architecture::Dxil,
    Architecture::Hexagon,
    Architecture::Loongarch32,
    Architecture::Loongarch64,
    Architecture::M68k,
    Architecture::Mips,
    Architecture::Mipsel,
    Architecture::Mips64,
    Architecture::Mips64el,
    Architecture::Msp430,
    Architecture::Powerpc,
    Architecture::Powerpcle,
    Architecture::Powerpc64,
    Architecture::Powerpc64le,
    Architecture::R600,
    Architecture::Amdgpu,
    Architecture::Riscv32,
    Architecture::Riscv64,
    Architecture::Riscv32be,
    Architecture::Riscv64be,
    Architecture::Sparc,
    Architecture::Sparcv9,
    Architecture::Sparcel,
    Architecture::Systemz,
    Architecture::Tce,
    Architecture::Tcele,
    Architecture::Tcele64,
    Architecture::Thumb,
    Architecture::Thumbeb,
    Architecture::X86,
    Architecture::X86_64,
    Architecture::Xcore,
    Architecture::Xtensa,
    Architecture::Nvptx,
    Architecture::Nvptx64,
    Architecture::Amdil,
    Architecture::Amdil64,
    Architecture::Hsail,
    Architecture::Hsail64,
    Architecture::Spir,
    Architecture::Spir64,
    Architecture::Spirv,
    Architecture::Spirv32,
    Architecture::Spirv64,
    Architecture::Kalimba,
    Architecture::Shave,
    Architecture::Lanai,
    Architecture::Wasm32,
    Architecture::Wasm64,
    Architecture::Renderscript32,
    Architecture::Renderscript64,
    Architecture::Ve,
};

constexpr auto architecture_name(Architecture value) noexcept -> ref<str> {
    switch (value) {
    case Architecture::Unknown: return "unknown"_str;
    case Architecture::Arm: return "arm"_str;
    case Architecture::Armeb: return "armeb"_str;
    case Architecture::Aarch64: return "aarch64"_str;
    case Architecture::Aarch64Be: return "aarch64_be"_str;
    case Architecture::Aarch64_32: return "aarch64_32"_str;
    case Architecture::Arc: return "arc"_str;
    case Architecture::Avr: return "avr"_str;
    case Architecture::Bpfel: return "bpfel"_str;
    case Architecture::Bpfeb: return "bpfeb"_str;
    case Architecture::Csky: return "csky"_str;
    case Architecture::Dxil: return "dxil"_str;
    case Architecture::Hexagon: return "hexagon"_str;
    case Architecture::Loongarch32: return "loongarch32"_str;
    case Architecture::Loongarch64: return "loongarch64"_str;
    case Architecture::M68k: return "m68k"_str;
    case Architecture::Mips: return "mips"_str;
    case Architecture::Mipsel: return "mipsel"_str;
    case Architecture::Mips64: return "mips64"_str;
    case Architecture::Mips64el: return "mips64el"_str;
    case Architecture::Msp430: return "msp430"_str;
    case Architecture::Powerpc: return "powerpc"_str;
    case Architecture::Powerpcle: return "powerpcle"_str;
    case Architecture::Powerpc64: return "powerpc64"_str;
    case Architecture::Powerpc64le: return "powerpc64le"_str;
    case Architecture::R600: return "r600"_str;
    case Architecture::Amdgpu: return "amdgpu"_str;
    case Architecture::Riscv32: return "riscv32"_str;
    case Architecture::Riscv64: return "riscv64"_str;
    case Architecture::Riscv32be: return "riscv32be"_str;
    case Architecture::Riscv64be: return "riscv64be"_str;
    case Architecture::Sparc: return "sparc"_str;
    case Architecture::Sparcv9: return "sparcv9"_str;
    case Architecture::Sparcel: return "sparcel"_str;
    case Architecture::Systemz: return "s390x"_str;
    case Architecture::Tce: return "tce"_str;
    case Architecture::Tcele: return "tcele"_str;
    case Architecture::Tcele64: return "tcele64"_str;
    case Architecture::Thumb: return "thumb"_str;
    case Architecture::Thumbeb: return "thumbeb"_str;
    case Architecture::X86: return "i386"_str;
    case Architecture::X86_64: return "x86_64"_str;
    case Architecture::Xcore: return "xcore"_str;
    case Architecture::Xtensa: return "xtensa"_str;
    case Architecture::Nvptx: return "nvptx"_str;
    case Architecture::Nvptx64: return "nvptx64"_str;
    case Architecture::Amdil: return "amdil"_str;
    case Architecture::Amdil64: return "amdil64"_str;
    case Architecture::Hsail: return "hsail"_str;
    case Architecture::Hsail64: return "hsail64"_str;
    case Architecture::Spir: return "spir"_str;
    case Architecture::Spir64: return "spir64"_str;
    case Architecture::Spirv: return "spirv"_str;
    case Architecture::Spirv32: return "spirv32"_str;
    case Architecture::Spirv64: return "spirv64"_str;
    case Architecture::Kalimba: return "kalimba"_str;
    case Architecture::Shave: return "shave"_str;
    case Architecture::Lanai: return "lanai"_str;
    case Architecture::Wasm32: return "wasm32"_str;
    case Architecture::Wasm64: return "wasm64"_str;
    case Architecture::Renderscript32: return "renderscript32"_str;
    case Architecture::Renderscript64: return "renderscript64"_str;
    case Architecture::Ve: return "ve"_str;
    }
    return "unknown"_str;
}

constexpr auto parse_architecture(ref<str> value) noexcept -> Architecture {
    for (const auto candidate : ARCHITECTURES) {
        if (value == architecture_name(candidate)) return candidate;
    }
    return Architecture::Unknown;
}

auto architecture_choices() -> String {
    auto result = String::make();
    for (auto index = usize {}; index < usize(sizeof(ARCHITECTURES) / sizeof(ARCHITECTURES[0]));
         ++index) {
        if (index != usize {}) result.push_str(", "_str);
        result.push_ascii(u8('\''));
        result.push_str(architecture_name(ARCHITECTURES[index.to_primitive()]));
        result.push_ascii(u8('\''));
    }
    return result;
}

enum class OperatingSystem
{
    Linux,
    Android,
    Macos,
    Windows,
    Freebsd,
    Netbsd,
    Openbsd,
};

inline constexpr OperatingSystem OPERATING_SYSTEMS[] = {
    OperatingSystem::Linux,   OperatingSystem::Android, OperatingSystem::Macos,
    OperatingSystem::Windows, OperatingSystem::Freebsd, OperatingSystem::Netbsd,
    OperatingSystem::Openbsd,
};

constexpr auto operating_system_name(OperatingSystem value) noexcept -> ref<str> {
    switch (value) {
    case OperatingSystem::Linux: return "linux"_str;
    case OperatingSystem::Android: return "android"_str;
    case OperatingSystem::Macos: return "macos"_str;
    case OperatingSystem::Windows: return "windows"_str;
    case OperatingSystem::Freebsd: return "freebsd"_str;
    case OperatingSystem::Netbsd: return "netbsd"_str;
    case OperatingSystem::Openbsd: return "openbsd"_str;
    }
    return ""_str;
}

constexpr auto parse_operating_system(ref<str> value) noexcept -> Option<OperatingSystem> {
    for (const auto candidate : OPERATING_SYSTEMS) {
        if (value == operating_system_name(candidate)) return Some<OperatingSystem>(candidate);
    }
    return None();
}

auto operating_system_choices() -> String {
    auto result = String::make();
    for (auto index = usize {};
         index < usize(sizeof(OPERATING_SYSTEMS) / sizeof(OPERATING_SYSTEMS[0]));
         ++index) {
        if (index != usize {}) result.push_str(", "_str);
        result.push_ascii(u8('\''));
        result.push_str(operating_system_name(OPERATING_SYSTEMS[index.to_primitive()]));
        result.push_ascii(u8('\''));
    }
    return result;
}

class PlatformError {
    String message_;

public:
    explicit PlatformError(String message): message_(rstd::move(message)) {}

    auto message() const noexcept -> ref<str> { return message_.as_str(); }
};

auto platform_error_ref(const PlatformError& error [[clang::lifetimebound]]) noexcept
    -> rstd::error::ErrorRef;

template<typename T>
using PlatformResult = Result<T, PlatformError>;

enum class TargetFamily
{
    Unix,
    Windows,
    Unknown,
};

enum class TargetPlatform
{
    Linux,
    Android,
    Macos,
    Windows,
    Freebsd,
    Netbsd,
    Openbsd,
    Unknown,
};

struct TargetInfo {
    String         triple;
    Architecture   architecture { Architecture::Unknown };
    String         vendor;
    String         operating_system;
    Option<String> environment;
    TargetFamily   family { TargetFamily::Unknown };
    TargetPlatform platform { TargetPlatform::Unknown };

    auto clone() const -> TargetInfo {
        return TargetInfo {
            .triple           = triple.clone(),
            .architecture     = architecture,
            .vendor           = vendor.clone(),
            .operating_system = operating_system.clone(),
            .environment      = as<Clone>(environment).clone(),
            .family           = family,
            .platform         = platform,
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

    auto platform_name() const noexcept -> ref<str> {
        switch (platform) {
        case TargetPlatform::Linux: return "linux"_str;
        case TargetPlatform::Android: return "android"_str;
        case TargetPlatform::Macos: return "macos"_str;
        case TargetPlatform::Windows: return "windows"_str;
        case TargetPlatform::Freebsd: return "freebsd"_str;
        case TargetPlatform::Netbsd: return "netbsd"_str;
        case TargetPlatform::Openbsd: return "openbsd"_str;
        case TargetPlatform::Unknown: return "unknown"_str;
        }
        return "unknown"_str;
    }

    auto environment_name() const noexcept -> ref<str> {
        return environment.is_some() ? environment->as_str() : "unknown"_str;
    }

    auto is_msvc() const noexcept -> bool {
        return environment.is_some() && environment->as_str().starts_with("msvc"_str);
    }

    auto is_gnu() const noexcept -> bool {
        return (environment.is_some() && environment->as_str().starts_with("gnu"_str)) ||
               operating_system.as_str().starts_with("mingw"_str);
    }

    auto is_abi_compatible_with(const TargetInfo& other) const noexcept -> bool {
        if (architecture == Architecture::Unknown || other.architecture == Architecture::Unknown ||
            platform == TargetPlatform::Unknown || other.platform == TargetPlatform::Unknown ||
            architecture != other.architecture || platform != other.platform ||
            environment.is_some() != other.environment.is_some()) {
            return false;
        }
        return environment.is_none() || environment->as_str() == other.environment->as_str();
    }
};

struct HostInfo {
    Architecture architecture { Architecture::Unknown };
    String       os;

    auto clone() const -> HostInfo {
        return HostInfo {
            .architecture = architecture,
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
               contains(operating_systems, target.platform_name()) &&
               ! excludes(excluded_families, target.family_name()) &&
               ! excludes(excluded_operating_systems, target.platform_name());
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

// Windows Clang ABI workaround: materialize the dyn Error vtable in its owning module.
auto platform_error_ref(const PlatformError& error [[clang::lifetimebound]]) noexcept
    -> rstd::error::ErrorRef {
    return rstd::ptr_::dyn<rstd::error::Error>::from_ref(error);
}

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

auto require_architecture(ref<str> value) -> PlatformResult<Architecture> {
    auto architecture = parse_architecture(value);
    if (architecture != Architecture::Unknown) return Ok(architecture);
    return platform_failure<Architecture>(rstd::format(
        "architecture '{}' is not a canonical Clang architecture name; expected one of {}",
        value,
        architecture_choices().as_str()));
}

auto parse_target_architecture(ref<str> value) noexcept -> Architecture {
    auto architecture = parse_architecture(value);
    if (architecture != Architecture::Unknown) return architecture;
    if (value == "amd64"_str || value == "x86_64h"_str || value == "x86_64_lfi"_str) {
        return Architecture::X86_64;
    }
    if (value == "i486"_str || value == "i586"_str || value == "i686"_str || value == "i786"_str ||
        value == "i886"_str || value == "i986"_str) {
        return Architecture::X86;
    }
    if (value == "arm64"_str || value == "arm64e"_str || value == "arm64ec"_str ||
        value == "aarch64_lfi"_str) {
        return Architecture::Aarch64;
    }
    if (value == "arm64_32"_str) return Architecture::Aarch64_32;
    if (value == "xscale"_str) return Architecture::Arm;
    if (value == "xscaleeb"_str) return Architecture::Armeb;
    if (value.starts_with("aarch64_be"_str)) return Architecture::Aarch64Be;
    if (value.starts_with("aarch64"_str)) return Architecture::Aarch64;
    if (value.starts_with("thumbeb"_str)) return Architecture::Thumbeb;
    if (value.starts_with("thumb"_str)) return Architecture::Thumb;
    if (value.starts_with("armeb"_str)) return Architecture::Armeb;
    if (value.starts_with("arm"_str)) return Architecture::Arm;
    if (value == "bpf_be"_str) return Architecture::Bpfeb;
    if (value == "bpf_le"_str) return Architecture::Bpfel;
    if (value == "powerpcspe"_str || value == "ppc"_str || value == "ppc32"_str) {
        return Architecture::Powerpc;
    }
    if (value == "ppcle"_str || value == "ppc32le"_str) return Architecture::Powerpcle;
    if (value == "ppu"_str || value == "ppc64"_str) return Architecture::Powerpc64;
    if (value == "ppc64le"_str) return Architecture::Powerpc64le;
    if (value == "mipseb"_str || value == "mipsallegrex"_str || value == "mipsisa32r6"_str ||
        value == "mipsr6"_str) {
        return Architecture::Mips;
    }
    if (value == "mipsallegrexel"_str || value == "mipsisa32r6el"_str || value == "mipsr6el"_str) {
        return Architecture::Mipsel;
    }
    if (value == "mips64eb"_str || value == "mipsn32"_str || value == "mipsisa64r6"_str ||
        value == "mips64r6"_str || value == "mipsn32r6"_str) {
        return Architecture::Mips64;
    }
    if (value == "mipsn32el"_str || value == "mipsisa64r6el"_str || value == "mips64r6el"_str ||
        value == "mipsn32r6el"_str) {
        return Architecture::Mips64el;
    }
    if (value == "amdgcn"_str || value.starts_with("amdgpu"_str)) {
        return Architecture::Amdgpu;
    }
    if (value == "systemz"_str) return Architecture::Systemz;
    if (value == "sparc64"_str) return Architecture::Sparcv9;
    if (value.starts_with("spirv32v"_str)) return Architecture::Spirv32;
    if (value.starts_with("spirv64v"_str)) return Architecture::Spirv64;
    if (value.starts_with("spirv"_str)) return Architecture::Spirv;
    if (value.starts_with("kalimba"_str)) return Architecture::Kalimba;
    if (value.starts_with("dxilv"_str)) return Architecture::Dxil;
    return Architecture::Unknown;
}

auto parse_target_info(ref<str> triple) -> PlatformResult<TargetInfo> {
    if (triple.is_empty()) return platform_failure<TargetInfo>("target triple is empty"_str);
    auto architecture_and_rest = triple.split_once("-"_str);
    if (architecture_and_rest.is_none() || architecture_and_rest->get<0>().is_empty()) {
        return platform_failure<TargetInfo>(rstd::format(
            "target triple '{}' must contain architecture, vendor, and operating system", triple));
    }
    auto vendor_and_rest = architecture_and_rest->get<1>().split_once("-"_str);
    if (vendor_and_rest.is_none() || vendor_and_rest->get<0>().is_empty() ||
        vendor_and_rest->get<1>().is_empty()) {
        return platform_failure<TargetInfo>(rstd::format(
            "target triple '{}' must contain architecture, vendor, and operating system", triple));
    }
    auto os_and_environment = vendor_and_rest->get<1>().split_once("-"_str);
    auto vendor             = vendor_and_rest->get<0>();
    auto raw_os =
        os_and_environment.is_some() ? os_and_environment->get<0>() : vendor_and_rest->get<1>();
    auto environment = Option<String> {};
    if (os_and_environment.is_some()) {
        auto raw_environment = os_and_environment->get<1>();
        if (raw_environment.is_empty() || raw_environment.contains("-"_str)) {
            return platform_failure<TargetInfo>(
                rstd::format("target triple '{}' has an invalid environment component", triple));
        }
        environment = Some(String::make(raw_environment));
    } else if ((vendor == "linux"_str &&
                (raw_os.starts_with("gnu"_str) || raw_os.starts_with("android"_str))) ||
               (vendor == "windows"_str &&
                (raw_os.starts_with("msvc"_str) || raw_os.starts_with("gnu"_str)))) {
        environment = Some(String::make(raw_os));
        raw_os      = vendor;
        vendor      = "unknown"_str;
    }

    auto platform = TargetPlatform::Unknown;
    auto family   = TargetFamily::Unknown;
    if (raw_os == "linux"_str && environment.is_some() &&
        environment->as_str().starts_with("android"_str)) {
        platform = TargetPlatform::Android;
        family   = TargetFamily::Unix;
    } else if (raw_os == "linux"_str) {
        platform = TargetPlatform::Linux;
        family   = TargetFamily::Unix;
    } else if (raw_os.starts_with("windows"_str) || raw_os.starts_with("win32"_str) ||
               raw_os.starts_with("mingw"_str)) {
        platform = TargetPlatform::Windows;
        family   = TargetFamily::Windows;
    } else if (raw_os.starts_with("darwin"_str)) {
        platform = TargetPlatform::Macos;
        family   = TargetFamily::Unix;
    } else if (raw_os.starts_with("freebsd"_str)) {
        platform = TargetPlatform::Freebsd;
        family   = TargetFamily::Unix;
    } else if (raw_os.starts_with("netbsd"_str)) {
        platform = TargetPlatform::Netbsd;
        family   = TargetFamily::Unix;
    } else if (raw_os.starts_with("openbsd"_str)) {
        platform = TargetPlatform::Openbsd;
        family   = TargetFamily::Unix;
    }
    return Ok(TargetInfo {
        .triple           = String::make(triple),
        .architecture     = parse_target_architecture(architecture_and_rest->get<0>()),
        .vendor           = String::make(vendor),
        .operating_system = String::make(raw_os),
        .environment      = rstd::move(environment),
        .family           = family,
        .platform         = platform,
    });
}

auto target_operating_system(const TargetInfo& target) -> PlatformResult<OperatingSystem> {
    auto parsed = parse_operating_system(target.platform_name());
    if (parsed.is_some()) return Ok(*parsed);
    return platform_failure<OperatingSystem>(
        rstd::format("target '{}' has unsupported operating system '{}'",
                     target.triple.as_str(),
                     target.operating_system.as_str()));
}

auto encode_target_candidate(ref<str>            os,
                             const Architecture& architecture,
                             Option<ref<str>>    vendor,
                             Option<ref<str>>    environment) -> PlatformResult<String> {
    auto triple = String::make();
    if (architecture == Architecture::Unknown) {
        return platform_failure<String>("cannot encode an unknown target architecture"_str);
    }
    const auto valid_component = [](ref<str> value) {
        return ! value.is_empty() && ! value.contains("-"_str);
    };
    if (! valid_component(os)) {
        return platform_failure<String>(rstd::format("invalid target operating system '{}'", os));
    }
    if (vendor.is_some() && ! valid_component(*vendor)) {
        return platform_failure<String>(rstd::format("invalid target vendor '{}'", *vendor));
    }
    if (environment.is_some() && ! valid_component(*environment)) {
        return platform_failure<String>(
            rstd::format("invalid target environment '{}'", *environment));
    }
    triple.push_str(architecture_name(architecture));
    triple.push_ascii(u8('-'));
    triple.push_str(vendor.is_some() ? *vendor : "unknown"_str);
    if (os == "android"_str) {
        if (environment.is_some() && ! environment->starts_with("android"_str)) {
            return platform_failure<String>(
                "the Android target platform requires an Android environment"_str);
        }
        triple.push_str("-linux-"_str);
        triple.push_str(environment.is_some() ? *environment : "android"_str);
        return Ok(rstd::move(triple));
    }
    triple.push_ascii(u8('-'));
    triple.push_str(os == "macos"_str ? "darwin"_str : os);
    if (environment.is_some()) {
        triple.push_ascii(u8('-'));
        triple.push_str(*environment);
    }
    return Ok(rstd::move(triple));
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
    auto architecture = parse_target_architecture(machine.as_str());
    return Ok(HostInfo {
        .architecture = architecture,
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
    auto architecture = parse_target_architecture(machine->as_str());
    auto os           = host_os(system->as_str());
    if (os.is_err()) return Err(rstd::move(os).unwrap_err());
    return Ok(HostInfo {
        .architecture = architecture,
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
               host.os != compiler_default.platform_name()) {
        return platform_failure<BuildPlatform>(rstd::format(
            "compiler default target '{}' is not native-compatible with host '{}-{}'; declare "
            "an explicit target/toolchain configuration for cross compilation",
            compiler_default.triple.as_str(),
            architecture_name(host.architecture),
            host.os.as_str()));
    }
    auto cross =
        intent == BuildTargetIntent::ExplicitTarget
            ? effective.triple != compiler_default.triple.as_str()
            : host.architecture != effective.architecture || host.os != effective.platform_name();
    auto output_key = String::make();
    if (intent == BuildTargetIntent::ExplicitTarget) {
        output_key = String::make("target-"_str);
        output_key.push_str(licrypto::sha256_hex(effective.triple.as_str()).as_str());
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
