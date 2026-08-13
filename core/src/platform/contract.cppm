export module lito.platform.contract;

import rstd;
import lito.error;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

struct Architecture {
    String name;

    auto as_str() const noexcept -> ref<str> { return name.as_str(); }

    auto clone() const -> Architecture { return Architecture { .name = name.clone() }; }

    auto operator==(const Architecture& other) const noexcept -> bool { return name == other.name; }
};

class PlatformError {
    rstd::string::String message_;

public:
    explicit PlatformError(rstd::string::String message): message_(rstd::move(message)) {}

    auto message() const noexcept -> ref<str> { return message_.as_str(); }
};

template<typename T>
using PlatformResult = rstd::Result<T, PlatformError>;

enum class TargetFamily
{
    Unix,
    Windows,
    Unknown,
};

struct TargetInfo {
    String       triple;
    Architecture architecture;
    String       os;
    TargetFamily family { TargetFamily::Unknown };

    auto clone() const -> TargetInfo {
        return TargetInfo {
            .triple       = triple.clone(),
            .architecture = architecture.clone(),
            .os           = os.clone(),
            .family       = family,
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

} // namespace lito

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::PlatformError> : ImplBase<lito::PlatformError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_str(this->self().message());
    }
};

template<>
struct Impl<fmt::Debug, lito::PlatformError> : ImplBase<lito::PlatformError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::PlatformError> : DefaultInImpl<error::Error, lito::PlatformError> {};

}
