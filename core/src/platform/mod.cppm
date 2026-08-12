module;
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/utsname.h>
#endif

export module lito.platform;

import rstd;
import lito.error;
export import lito.platform.contract;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

template<typename T>
auto platform_failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Toolchain, rstd::move(message)));
}

template<typename T>
auto platform_failure(ref<str> message) -> Result<T> {
    return platform_failure<T>(String::make(message));
}

auto host_os(ref<str> name) -> Result<String> {
    if (name == "Linux"_str) return Ok(String::make("linux"_str));
    if (name == "Darwin"_str) return Ok(String::make("macos"_str));
    if (name == "FreeBSD"_str) return Ok(String::make("freebsd"_str));
    if (name == "NetBSD"_str) return Ok(String::make("netbsd"_str));
    if (name == "OpenBSD"_str) return Ok(String::make("openbsd"_str));
    return platform_failure<String>(rstd::format("unsupported host operating system '{}'", name));
}

#if ! defined(_WIN32)
auto c_string(const char* value, ref<str> context) -> Result<String> {
    auto text = rstd::ffi::CStr::from_ptr(value).to_str();
    if (text.is_err()) {
        return platform_failure<String>(rstd::format("{} is not valid UTF-8", context));
    }
    return Ok(String::make(text.unwrap()));
}
#endif

} // namespace lito

export namespace lito
{

auto canonical_architecture(ref<str> value) -> Result<Architecture> {
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

auto parse_target_info(ref<str> triple) -> Result<TargetInfo> {
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

    auto os     = String::make("unknown"_str);
    auto family = TargetFamily::Unknown;
    if (triple.contains("-windows"_str) || triple.contains("-win32"_str) ||
        triple.contains("-mingw"_str)) {
        os     = String::make("windows"_str);
        family = TargetFamily::Windows;
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
    });
}

auto detect_host_info() -> Result<HostInfo> {
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
                            Option<ref<str>>  explicit_target) -> Result<BuildPlatform> {
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
            "an explicit target/toolchain contract for cross compilation",
            compiler_default.triple.as_str(),
            host.architecture.as_str(),
            host.os.as_str()));
    }
    auto cross = host.architecture != effective.architecture || host.os != effective.os.as_str();
    return Ok(BuildPlatform {
        .host             = host.clone(),
        .compiler_default = compiler_default.clone(),
        .effective_target = rstd::move(effective),
        .intent           = intent,
        .cross            = cross,
    });
}

} // namespace lito
