module;
#include <rstd/enum.hpp>

export module lito.core:source.fetch;

import rstd;
import lito.crypto;
import :parse;
import :registry.digest;

using namespace rstd::prelude;

export namespace lito::source
{

class FetchIdentity {
    RSTD_ENUM(FetchIdentity,
              (Git, (String url; String commit;)),
              (Archive, (lito::parse::FetchUrl url; lito::crypto::Sha256Digest sha256;)),
              (RegistryPackage, (lito::registry::PackageChecksum checksum;)))
};

auto git_fetch_identity(ref<str> url, ref<str> commit) -> FetchIdentity {
    return FetchIdentity::Git(String::make(url), String::make(commit));
}

auto archive_fetch_identity(lito::parse::FetchUrl url, lito::crypto::Sha256Digest sha256)
    -> FetchIdentity {
    return FetchIdentity::Archive(rstd::move(url), rstd::move(sha256));
}

auto registry_package_fetch_identity(lito::registry::PackageChecksum checksum) -> FetchIdentity {
    return FetchIdentity::RegistryPackage(rstd::move(checksum));
}

auto fetch_identity_text(const FetchIdentity& identity) -> String {
    if (identity.is_Git()) {
        return rstd::format("lito-fetch-v1\ngit\n{}\n{}",
                            identity.as_Git().url.as_str(),
                            identity.as_Git().commit.as_str());
    }
    if (identity.is_Archive()) {
        return rstd::format("lito-fetch-v1\narchive\n{}\n{}",
                            identity.as_Archive().url.as_str(),
                            identity.as_Archive().sha256);
    }
    return rstd::format("lito-fetch-v1\nregistry-package\n{}",
                        identity.as_RegistryPackage().checksum.text());
}

auto fetch_identity_stable_key(const FetchIdentity& identity) -> String {
    auto serialized = fetch_identity_text(identity);
    return lito::crypto::sha256_hex(serialized.as_str());
}

} // namespace lito::source
