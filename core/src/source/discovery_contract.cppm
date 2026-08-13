module;
#include <rstd/enum.hpp>

export module lito.source.discovery_contract;

import rstd;
import lito.error;
import lito.frontend;
import lito.source.contract;
import lito.package.identity;

using namespace rstd::prelude;

export namespace lito
{

enum class SourceOrigin
{
    Explicit,
    Convention,
};

using ProvidedModule = frontend::ProvidedModule;
using SourceLocation = frontend::DependencyLocation;
using ModuleImport   = frontend::ModuleImport;
using FrontendResult = frontend::FrontendResult;

struct ResolvedSource {
    PathBuf                            relative_path;
    PathBuf                            canonical_path;
    SourceOrigin                       origin { SourceOrigin::Explicit };
    Option<String>                     expected_module;
    Option<frontend::FrontendAnalysis> frontend_analysis;
};

struct ResolvedSourceSet {
    Vec<ResolvedSource> sources;
};

struct ResolvedTargetSources {
    PackageTargetId   target;
    ResolvedSourceSet sources;
};

class SourceDiscoveryError {
    RSTD_ENUM(SourceDiscoveryError,
              (Io,
               (String operation; PathBuf path; rstd::io::error::Error source;)),
              (Message, (String message;)))
};

template<typename T>
using SourceDiscoveryResult = rstd::Result<T, SourceDiscoveryError>;

} // namespace lito

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::SourceDiscoveryError> : ImplBase<lito::SourceDiscoveryError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Io()) {
            const auto& value = error.as_Io();
            return formatter.write_fmt(
                fmt::Arguments::make("cannot {} source path '{}'", value.operation, value.path.as_path()));
        }
        return formatter.write_str(error.as_Message().message.as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::SourceDiscoveryError> : ImplBase<lito::SourceDiscoveryError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::SourceDiscoveryError>
    : ImplBase<lito::SourceDiscoveryError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (! error.is_Io()) return None();
        return Some(dyn<error::Error>::from_ref(error.as_Io().source));
    }
};

}
