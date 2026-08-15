module;
#include <rstd/macro.hpp>

export module lito.driver:install.identity;

import rstd;
import :install.error;
import :install.package;
import :install.source;
import lito.core;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

template<typename T>
auto identity_failure(String message) -> InstallStoreResult<T> {
    return Err(InstallStoreError::Cause(InstallStoreCause::Message(rstd::move(message))));
}

} // namespace lito

export namespace lito
{

auto install_package_id(ref<str> name, ref<str> source_identity) -> InstallStoreResult<String> {
    if (! valid_package_name(name) || source_identity.is_empty()) {
        return identity_failure<String>(String::make("install package identity is invalid"_str));
    }
    auto key = String::make(name);
    key.push_ascii('\n');
    key.push_str(source_identity);
    return Ok(rstd::format("{}-{}", name, rstd::crypto::sha256_hex(key.as_str()).as_str()));
}

auto resolve_install_package_identity(ref<str> name, const InstallSourceProvenance& provenance)
    -> InstallStoreResult<InstallPackageIdentity> {
    auto source_identity = rstd_try(install_source_identity(provenance));
    auto id              = rstd_try(install_package_id(name, source_identity.as_str()));
    return Ok(InstallPackageIdentity {
        .id              = rstd::move(id),
        .name            = String::make(name),
        .source_identity = rstd::move(source_identity),
    });
}

} // namespace lito
