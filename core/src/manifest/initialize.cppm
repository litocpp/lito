export module lito.core:manifest.initialize;

import rstd;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;

export namespace lito::manifest
{

struct ProjectInitializationError {
    PathBuf path;
    String  message;
};

template<typename T>
using ProjectInitializationResult = Result<T, ProjectInitializationError>;

struct ProjectInitialization {
    PathBuf root;
    String  package;
};

auto initialize_project(ref<rstd::path::Path> directory, Option<String> package = None())
    -> ProjectInitializationResult<ProjectInitialization>;

} // namespace lito::manifest

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::manifest::ProjectInitializationError>
    : ImplBase<lito::manifest::ProjectInitializationError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_fmt(fmt::Arguments::make("cannot initialize project '{}': {}",
                                                        this->self().path.as_path(),
                                                        this->self().message));
    }
};

template<>
struct Impl<fmt::Debug, lito::manifest::ProjectInitializationError>
    : ImplBase<lito::manifest::ProjectInitializationError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::manifest::ProjectInitializationError>
    : ImplBase<lito::manifest::ProjectInitializationError> {};

} // namespace rstd
