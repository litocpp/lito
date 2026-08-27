module;
#include <rstd/enum.hpp>

export module lito.tools.cargo:flatpak;

import rstd;
import rstd.toml;
import lito.core;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;

export namespace lito::tools::cargo
{

struct GitSelector {
    lito::source::GitReferenceKind kind { lito::source::GitReferenceKind::DefaultBranch };
    String                         value;

    auto clone() const -> GitSelector { return GitSelector { kind, value.clone() }; }
};

struct GitSource {
    String              original;
    String              url;
    String              commit;
    Option<GitSelector> selector;

    auto clone() const -> GitSource {
        return GitSource {
            .original = original.clone(),
            .url      = url.clone(),
            .commit   = commit.clone(),
            .selector = selector.is_some() ? Some(selector->clone()) : None<GitSelector>(),
        };
    }
};

class LockedSource {
    RSTD_ENUM(LockedSource, (CratesIo), (Git, (GitSource source;)))
};

struct LockedPackage {
    String                             name;
    String                             version;
    Option<LockedSource>               source;
    Option<lito::crypto::Sha256Digest> checksum;
};

struct LockedDocument {
    PathBuf            path;
    u64                version {};
    Vec<LockedPackage> packages;
};

struct GitRequest {
    String url;
    String commit;
};

struct GitCheckout {
    String  url;
    String  commit;
    PathBuf root;
};

class FlatpakExportError {
    RSTD_ENUM(FlatpakExportError,
              (Io, (String operation; PathBuf path; rstd::io::error::Error source;)),
              (Toml, (PathBuf path; rstd::toml::Error source;)),
              (Serialize, (PathBuf path; rstd::toml::SerializeError source;)),
              (Message, (String message;)))
};

template<typename T>
using FlatpakExportResult = Result<T, FlatpakExportError>;

auto parse_locked_document(ref<rstd::path::Path> path) -> FlatpakExportResult<LockedDocument>;
auto locked_git_requests(const LockedDocument& document) -> Vec<GitRequest>;
auto project_flatpak_sources(const LockedDocument& document, const Vec<GitCheckout>& checkouts)
    -> FlatpakExportResult<lito::flatpak::SourceSet>;

} // namespace lito::tools::cargo

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::tools::cargo::FlatpakExportError>
    : ImplBase<lito::tools::cargo::FlatpakExportError> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<fmt::Debug, lito::tools::cargo::FlatpakExportError>
    : ImplBase<lito::tools::cargo::FlatpakExportError> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<error::Error, lito::tools::cargo::FlatpakExportError>
    : ImplBase<lito::tools::cargo::FlatpakExportError> {
    auto source() const noexcept -> Option<error::ErrorRef>;
};

} // namespace rstd
