module;
#include <rstd/enum.hpp>

export module lito.package.error_contract;

import rstd;
import lito.error;
import lito.source.error_contract;
import lito.dependency.error_contract;
import lito.system.error_contract;
import lito.cpp;
import lito.source.contract;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

enum class PackageDependencyKind
{
    Normal,
    Development,
    Runtime,
};

struct PackageDependencyCycleEdge {
    String                package;
    String                dependency;
    PackageDependencyKind kind { PackageDependencyKind::Normal };
};

struct PackageDependencyCycleError {
    Vec<String>                     packages;
    Vec<PackageDependencyCycleEdge> edges;
};

class PackageError {
    RSTD_ENUM(PackageError,
              (Source, (SourceError source;)),
              (Dependency, (DependencyError source;)),
              (System, (SystemError source;)),
              (Cpp, (CppOptionError source;)),
              (Cycle, (PackageDependencyCycleError cycle;)),
              (Message, (String message;)))
};

template<typename T>
using PackageResult = rstd::Result<T, PackageError>;

} // namespace lito

export namespace rstd
{

template<>
struct Impl<convert::From<lito::SourceError>, lito::PackageError> {
    static auto from(lito::SourceError error) -> lito::PackageError {
        return lito::PackageError::Source(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::DependencyError>, lito::PackageError> {
    static auto from(lito::DependencyError error) -> lito::PackageError {
        return lito::PackageError::Dependency(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::SystemError>, lito::PackageError> {
    static auto from(lito::SystemError error) -> lito::PackageError {
        return lito::PackageError::System(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::CppOptionError>, lito::PackageError> {
    static auto from(lito::CppOptionError error) -> lito::PackageError {
        return lito::PackageError::Cpp(rstd::move(error));
    }
};

template<>
struct Impl<fmt::Display, lito::PackageError> : ImplBase<lito::PackageError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Source()) {
            return formatter.write_raw("package source resolution failed",
                                       sizeof("package source resolution failed") - 1);
        }
        if (error.is_Dependency()) {
            return formatter.write_raw("package dependency resolution failed",
                                       sizeof("package dependency resolution failed") - 1);
        }
        if (error.is_System()) {
            return formatter.write_raw("package environment resolution failed",
                                       sizeof("package environment resolution failed") - 1);
        }
        if (error.is_Cpp()) {
            return formatter.write_raw("package C++ options are invalid",
                                       sizeof("package C++ options are invalid") - 1);
        }
        if (error.is_Cycle()) {
            auto text = String::make("package dependency cycle: "_str);
            const auto& cycle = error.as_Cycle().cycle;
            for (usize index {}; index < cycle.packages.len(); ++index) {
                if (index != usize {}) text.push_str(" -> "_str);
                text.push_str(cycle.packages[index].as_str());
            }
            return formatter.write_str(text.as_str());
        }
        return formatter.write_str(error.as_Message().message.as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::PackageError> : ImplBase<lito::PackageError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::PackageError> : ImplBase<lito::PackageError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (error.is_Source()) {
            return Some(dyn<error::Error>::from_ref(error.as_Source().source));
        }
        if (error.is_Dependency()) {
            return Some(dyn<error::Error>::from_ref(error.as_Dependency().source));
        }
        if (error.is_System()) {
            return Some(dyn<error::Error>::from_ref(error.as_System().source));
        }
        if (error.is_Cpp()) {
            return Some(dyn<error::Error>::from_ref(error.as_Cpp().source));
        }
        return None();
    }
};

} // namespace rstd
