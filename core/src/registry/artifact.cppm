export module lito.core:registry.artifact;

import rstd;
import :registry.identity;

using namespace rstd::prelude;

export namespace lito::registry
{

enum class RegistryArtifactErrorKind
{
    OfflineCacheMiss,
    Network,
    Io,
    Size,
    Digest,
    Archive,
    Source,
    Manifest,
    Projection,
};

enum class RegistryArtifactFailureCode
{
    PackageInvalid,
    ExternalInputsNotAllowed,
};

struct RegistryArtifactError {
    RegistryArtifactErrorKind   kind { RegistryArtifactErrorKind::Io };
    RegistryArtifactFailureCode code { RegistryArtifactFailureCode::PackageInvalid };
    RegistryPackageId           package;
    String                      message;
};

template<typename T>
using RegistryArtifactResult = Result<T, RegistryArtifactError>;

} // namespace lito::registry
