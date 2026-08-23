export module lito.core:manifest.edit;

import rstd;
import :registry.identity;
import :registry.version;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;

export namespace lito::manifest
{

struct ManifestEditError {
    PathBuf path;
    String  message;
};

template<typename T>
using ManifestEditResult = Result<T, ManifestEditError>;

struct ManifestDependencyEdit {
    PathBuf path;
    String  package;
};

auto add_registry_dependency(ref<rstd::path::Path>                      requested_directory,
                             const lito::registry::RegistryPackageName& package,
                             const lito::registry::VersionRequirement&  requirement,
                             Option<String>                             registry = None())
    -> ManifestEditResult<ManifestDependencyEdit>;

} // namespace lito::manifest

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::manifest::ManifestEditError>
    : ImplBase<lito::manifest::ManifestEditError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_fmt(fmt::Arguments::make(
            "cannot edit manifest '{}': {}", this->self().path.as_path(), this->self().message));
    }
};

template<>
struct Impl<fmt::Debug, lito::manifest::ManifestEditError>
    : ImplBase<lito::manifest::ManifestEditError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::manifest::ManifestEditError>
    : ImplBase<lito::manifest::ManifestEditError> {};

} // namespace rstd
