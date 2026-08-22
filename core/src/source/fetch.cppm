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
              (RegistryBlob, (lito::registry::BlobDigest digest;)))
};

auto git_fetch_identity(ref<str> url, ref<str> commit) -> FetchIdentity {
    return FetchIdentity::Git(String::make(url), String::make(commit));
}

auto archive_fetch_identity(lito::parse::FetchUrl url, lito::crypto::Sha256Digest sha256)
    -> FetchIdentity {
    return FetchIdentity::Archive(rstd::move(url), rstd::move(sha256));
}

auto registry_blob_fetch_identity(lito::registry::BlobDigest digest) -> FetchIdentity {
    return FetchIdentity::RegistryBlob(rstd::move(digest));
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
    return rstd::format("lito-fetch-v1\nregistry-blob\n{}",
                        identity.as_RegistryBlob().digest.text());
}

auto fetch_identity_stable_key(const FetchIdentity& identity) -> String {
    auto serialized = fetch_identity_text(identity);
    return lito::crypto::sha256_hex(serialized.as_str());
}

} // namespace lito::source
