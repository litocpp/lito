module;
#include <rstd/enum.hpp>

export module lito.cpp:source.discovery;

import rstd;
import lito.core;
import :build.scan;

using namespace rstd::prelude;

export namespace lito::cpp
{

enum class SourceOrigin
{
    Explicit,
    Convention,
};

struct ResolvedSource {
    PathBuf                    relative_path;
    PathBuf                    canonical_path;
    PathBuf                    source_root;
    String                     origin_identity;
    bool                       external { false };
    SourceOrigin               origin { SourceOrigin::Explicit };
    bool                       module_companion { false };
    bool                       module_context_required { false };
    Option<String>             expected_module;
    Option<SourceScanArtifact> scan_artifact;
};

struct ResolvedSourceSet {
    Vec<ResolvedSource> sources;
};

struct ResolvedTargetSources {
    lito::package::PackageTargetId target;
    ResolvedSourceSet              sources;
};

class SourceDiscoveryError {
    RSTD_ENUM(SourceDiscoveryError,
              (Io, (String operation; PathBuf path; rstd::io::error::Error source;)),
              (Message, (String message;)))
};

template<typename T>
using SourceDiscoveryResult = Result<T, SourceDiscoveryError>;

} // namespace lito::cpp

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::cpp::SourceDiscoveryError>
    : ImplBase<lito::cpp::SourceDiscoveryError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Io()) {
            const auto& value = error.as_Io();
            return formatter.write_fmt(fmt::Arguments::make(
                "cannot {} source path '{}'", value.operation, value.path.as_path()));
        }
        return formatter.write_str(error.as_Message().message.as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::cpp::SourceDiscoveryError>
    : ImplBase<lito::cpp::SourceDiscoveryError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::cpp::SourceDiscoveryError>
    : ImplBase<lito::cpp::SourceDiscoveryError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (! error.is_Io()) return None();
        return Some(dyn<error::Error>::from_ref(error.as_Io().source));
    }
};

} // namespace rstd
