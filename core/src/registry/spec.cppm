module;
#include <rstd/enum.hpp>

export module lito.core:registry.spec;

import rstd;
import :registry.error;
import :registry.identity;
import :registry.version;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::registry
{

class RegistryPackageSelector {
    RSTD_ENUM(RegistryPackageSelector,
              (Requirement, (VersionRequirement requirement;)),
              (NamedTag, (String tag;)))

public:
    auto clone() const -> RegistryPackageSelector {
        if (is_Requirement()) {
            return RegistryPackageSelector::Requirement(as_Requirement().requirement.clone());
        }
        return RegistryPackageSelector::NamedTag(as_NamedTag().tag.clone());
    }
};

struct RegistryPackageSpec {
    RegistryPackageName     package;
    RegistryPackageSelector selector;

    static auto parse(ref<str> value) -> RegistryValueResult<RegistryPackageSpec>;

    auto clone() const -> RegistryPackageSpec {
        return RegistryPackageSpec {
            .package  = package.clone(),
            .selector = selector.clone(),
        };
    }
};

auto valid_registry_tag(ref<str> value) noexcept -> bool;

} // namespace lito::registry

auto lito::registry::valid_registry_tag(ref<str> value) noexcept -> bool {
    if (value.is_empty() || value.len() > usize(64) || value[usize()] == u8('v') ||
        (value[usize()] >= u8('0') && value[usize()] <= u8('9'))) {
        return false;
    }
    for (auto byte : value.as_bytes()) {
        const auto ascii = byte.to_primitive();
        if ((ascii >= 'a' && ascii <= 'z') || (ascii >= '0' && ascii <= '9') || ascii == '-' ||
            ascii == '_') {
            continue;
        }
        return false;
    }
    return true;
}

auto lito::registry::RegistryPackageSpec::parse(ref<str> value)
    -> RegistryValueResult<RegistryPackageSpec> {
    auto separated    = value.split_once("@"_str);
    auto package_text = separated.is_some() ? separated->template get<0>() : value;
    auto package      = RegistryPackageName::parse(package_text);
    if (package.is_err()) return Err(rstd::move(package).unwrap_err());
    if (separated.is_none()) {
        return Ok(RegistryPackageSpec {
            .package  = rstd::move(package).unwrap(),
            .selector = RegistryPackageSelector::Requirement(VersionRequirement::any()),
        });
    }
    auto selector = separated->template get<1>();
    if (selector.is_empty() || selector.contains("@"_str)) {
        return registry_value_failure<RegistryPackageSpec>(
            "Registry package selector must not be empty or contain '@'"_str);
    }
    auto requirement = VersionRequirement::parse(selector);
    if (requirement.is_ok()) {
        return Ok(RegistryPackageSpec {
            .package  = rstd::move(package).unwrap(),
            .selector = RegistryPackageSelector::Requirement(rstd::move(requirement).unwrap()),
        });
    }
    if (! valid_registry_tag(selector)) {
        return registry_value_failure<RegistryPackageSpec>(
            "Registry package selector is neither a version requirement nor a valid tag"_str);
    }
    return Ok(RegistryPackageSpec {
        .package  = rstd::move(package).unwrap(),
        .selector = RegistryPackageSelector::NamedTag(String::make(selector)),
    });
}
