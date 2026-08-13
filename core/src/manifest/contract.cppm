module;
#include <rstd/enum.hpp>

export module lito.manifest.contract;

export import rstd;
import rstd.toml;
import lito.error;
import lito.cpp;
import lito.build.profile_contract;
import lito.platform.contract;
import lito.source.contract;
import lito.dependency.contract;
import lito.package.identity;

using namespace rstd::prelude;

export namespace lito
{

enum class SourceDiscoveryMode
{
    Explicit,
    Module,
};

enum class PackageVersionSource
{
    Unspecified,
    Explicit,
    Workspace,
};

struct PackageVersion {
    PackageVersionSource source { PackageVersionSource::Unspecified };
    Option<String>       value;
};

struct DeclaredDependency {
    String                   name;
    PackageSourceRequirement source;
    DependencyVisibility     visibility { DependencyVisibility::Private };
    Option<PathBuf>          declaration_root;
};

struct WorkspaceDependencyReference {
    String               name;
    DependencyVisibility visibility { DependencyVisibility::Private };
};

struct DeclaredRuntimeDependency {
    String                   name;
    PackageSourceRequirement source;
    Option<PathBuf>          declaration_root;
};

struct WorkspaceRuntimeDependencyReference {
    String name;
};

struct WorkspacePkgConfigExternalDependencyReference {
    String               alias;
    DependencyVisibility visibility { DependencyVisibility::Private };
};

struct WorkspaceCMakeExternalDependencyReference {
    String                      alias;
    Vec<CMakeTargetRequirement> targets;
};

struct ConditionalSourceGroup {
    TargetPredicate predicate;
    Vec<PathBuf>    sources;
};

struct TestAttachmentManifest {
    String                      package;
    Vec<PathBuf>                sources;
    Vec<ConditionalSourceGroup> conditional_source_groups;
};

struct TargetSourceManifest {
    Option<String>              module;
    SourceDiscoveryMode         discovery { SourceDiscoveryMode::Explicit };
    Vec<PathBuf>                declared_sources;
    Vec<ConditionalSourceGroup> conditional_source_groups;
};

class PackageTargetManifest {
    RSTD_ENUM(PackageTargetManifest,
              (Library, (String name; String archive; TargetSourceManifest source;)),
              (Binary, (String name; TargetSourceManifest source; bool link_stdlib;)),
              (Test,
               (String name; TargetSourceManifest source; bool link_stdlib;
                Vec<TestAttachmentManifest>                    attachments;)),
              (Benchmark, (String name; TargetSourceManifest source; bool link_stdlib;)))
};

auto package_target_kind(const PackageTargetManifest& target) noexcept -> PackageTargetKind {
    if (target.is_Library()) return PackageTargetKind::Library;
    if (target.is_Binary()) return PackageTargetKind::Binary;
    if (target.is_Test()) return PackageTargetKind::Test;
    return PackageTargetKind::Benchmark;
}

auto package_target_name(const PackageTargetManifest& target) noexcept -> ref<str> {
    if (target.is_Library()) return target.as_Library().name.as_str();
    if (target.is_Binary()) return target.as_Binary().name.as_str();
    if (target.is_Test()) return target.as_Test().name.as_str();
    return target.as_Benchmark().name.as_str();
}

auto package_target_source(const PackageTargetManifest& target) noexcept
    -> const TargetSourceManifest& {
    if (target.is_Library()) return target.as_Library().source;
    if (target.is_Binary()) return target.as_Binary().source;
    if (target.is_Test()) return target.as_Test().source;
    return target.as_Benchmark().source;
}

auto package_target_source(PackageTargetManifest& target) noexcept -> TargetSourceManifest& {
    if (target.is_Library()) return target.as_Library().source;
    if (target.is_Binary()) return target.as_Binary().source;
    if (target.is_Test()) return target.as_Test().source;
    return target.as_Benchmark().source;
}

auto package_target_artifact_name(const PackageTargetManifest& target) noexcept -> ref<str> {
    if (target.is_Library()) return target.as_Library().archive.as_str();
    return package_target_name(target);
}

auto package_target_links_stdlib(const PackageTargetManifest& target) noexcept -> bool {
    if (target.is_Binary()) return target.as_Binary().link_stdlib;
    if (target.is_Test()) return target.as_Test().link_stdlib;
    if (target.is_Benchmark()) return target.as_Benchmark().link_stdlib;
    return true;
}

auto package_target_attachments(const PackageTargetManifest& target) noexcept
    -> Option<ref<Vec<TestAttachmentManifest>>> {
    if (! target.is_Test()) return None();
    return Some(ref<Vec<TestAttachmentManifest>>::from_raw_parts(
        rstd::addressof(target.as_Test().attachments)));
}

enum class CompileTestOutcome
{
    Success,
    Failure,
};

struct CompileTestCase {
    String             name;
    PathBuf            source;
    CompileTestOutcome outcome { CompileTestOutcome::Failure };
    Vec<String>        options;
    CppArgumentLayer   arguments;
    Vec<String>        diagnostic_contains;
    Vec<String>        diagnostic_contains_any;
};

struct PackageManifest {
    String                                             name;
    PackageVersion                                     version;
    PathBuf                                            root;
    PathBuf                                            source_root;
    PathBuf                                            manifest_path;
    Option<PathBuf>                                    install_script;
    Option<ProjectProfile>                             profile;
    Vec<PackageTargetManifest>                         targets;
    TargetPredicate                                    target;
    Vec<CompileTestCase>                               compile_tests;
    UsageRequirements                                  usage;
    Vec<DeclaredDependency>                            dependencies;
    Vec<DeclaredDependency>                            dev_dependencies;
    Vec<DeclaredRuntimeDependency>                     runtime_dependencies;
    Vec<WorkspaceDependencyReference>                  workspace_dependencies;
    Vec<WorkspaceDependencyReference>                  workspace_dev_dependencies;
    Vec<WorkspaceRuntimeDependencyReference>           workspace_runtime_dependencies;
    Vec<PkgConfigExternalDependency>                   pkg_config_external_dependencies;
    Vec<WorkspacePkgConfigExternalDependencyReference> workspace_pkg_config_external_dependencies;
    Vec<CMakeDependencyRequirement>                    cmake_external_dependencies;
    Vec<WorkspaceCMakeExternalDependencyReference>     workspace_cmake_external_dependencies;
};

struct WorkspacePackageDefaults {
    Option<String> version;
};

struct WorkspaceDependencyDefinition {
    String                   name;
    PackageSourceRequirement source;
};

struct WorkspacePkgConfigExternalDependencyDefinition {
    String                         alias;
    PkgConfigDependencyRequirement requirement;
};

struct WorkspaceCMakeExternalDependencyDefinition {
    String                alias;
    String                package;
    CMakeDependencySource source;
    CMakeIntegration      integration { CMakeIntegration::Install };
    Option<PathBuf>       adapter;
    Option<PathBuf>       config_directory;
    Vec<CMakeCacheEntry>  cache;
};

struct WorkspaceManifest {
    String                                              name;
    PathBuf                                             root;
    PathBuf                                             manifest_path;
    Option<ProjectProfile>                              profile;
    Vec<PathBuf>                                        members;
    Vec<PathBuf>                                        default_members;
    WorkspacePackageDefaults                            package;
    Vec<WorkspaceDependencyDefinition>                  dependencies;
    Vec<WorkspacePkgConfigExternalDependencyDefinition> pkg_config_external_dependencies;
    Vec<WorkspaceCMakeExternalDependencyDefinition>     cmake_external_dependencies;
};

enum class ManifestKind
{
    Package,
    Workspace,
};

struct ManifestDocument {
    ManifestKind              kind { ManifestKind::Package };
    Option<PackageManifest>   package;
    Option<WorkspaceManifest> workspace;
};

struct ManifestLocation {
    PathBuf directory;
    PathBuf manifest;
};

struct ManifestNodePath {
    String value;

    static auto make(ref<str> value) -> ManifestNodePath {
        return ManifestNodePath { String::make(value) };
    }
};

class ManifestLocatorError {
    RSTD_ENUM(ManifestLocatorError,
              (Io, (String operation; PathBuf path; rstd::io::error::Error source;)),
              (NotDirectory, (PathBuf path;)),
              (NotRegularFile, (PathBuf path;)),
              (NotFound, (PathBuf directory;)))
};

class ManifestSchemaError {
    RSTD_ENUM(ManifestSchemaError,
              (UnknownField, (ManifestNodePath node; String field;)),
              (MissingField, (ManifestNodePath node; String field;)),
              (WrongType, (ManifestNodePath node; String expected;)),
              (InvalidValue, (ManifestNodePath node; String reason;)),
              (Io,
               (ManifestNodePath node; String operation; PathBuf path; rstd::io::error::Error source;)),
              (Locate, (ManifestLocatorError source;)),
              (Profile, (BuildProfileError source;)))
};

class ManifestFileCause {
    RSTD_ENUM(ManifestFileCause,
              (Read, (rstd::io::error::Error source;)),
              (Parse, (rstd::toml::Error source;)),
              (Schema, (ManifestSchemaError source;)))
};

struct ManifestFileError {
    PathBuf           path;
    ManifestFileCause cause;
};

class ManifestError {
    RSTD_ENUM(ManifestError,
              (Locate, (ManifestLocatorError source;)),
              (File, (ManifestFileError source;)),
              (Kind,
               (PathBuf requested_directory; ManifestKind expected; ManifestKind actual;)))
};

template<typename T>
using ManifestLocatorResult = rstd::Result<T, ManifestLocatorError>;

template<typename T>
using ManifestSchemaResult = rstd::Result<T, ManifestSchemaError>;

template<typename T>
using ManifestResult = rstd::Result<T, ManifestError>;

} // namespace lito

export namespace rstd
{

template<>
struct Impl<convert::From<lito::ManifestLocatorError>, lito::ManifestSchemaError> {
    static auto from(lito::ManifestLocatorError error) -> lito::ManifestSchemaError {
        return lito::ManifestSchemaError::Locate(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::BuildProfileError>, lito::ManifestSchemaError> {
    static auto from(lito::BuildProfileError error) -> lito::ManifestSchemaError {
        return lito::ManifestSchemaError::Profile(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::ManifestLocatorError>, lito::ManifestError> {
    static auto from(lito::ManifestLocatorError error) -> lito::ManifestError {
        return lito::ManifestError::Locate(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::ManifestFileError>, lito::ManifestError> {
    static auto from(lito::ManifestFileError error) -> lito::ManifestError {
        return lito::ManifestError::File(rstd::move(error));
    }
};

template<>
struct Impl<fmt::Display, lito::ManifestNodePath> : ImplBase<lito::ManifestNodePath> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_str(this->self().value.as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::ManifestNodePath> : ImplBase<lito::ManifestNodePath> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_str(this->self().value.as_str());
    }
};

template<>
struct Impl<fmt::Display, lito::ManifestLocatorError> : ImplBase<lito::ManifestLocatorError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Io()) {
            const auto& value = error.as_Io();
            return formatter.write_fmt(
                fmt::Arguments::make("cannot {} '{}'", value.operation, value.path.as_path()));
        }
        if (error.is_NotDirectory()) {
            return formatter.write_fmt(fmt::Arguments::make(
                "manifest root '{}' is not a directory", error.as_NotDirectory().path.as_path()));
        }
        if (error.is_NotRegularFile()) {
            return formatter.write_fmt(fmt::Arguments::make(
                "manifest '{}' is not a regular file", error.as_NotRegularFile().path.as_path()));
        }
        return formatter.write_fmt(fmt::Arguments::make(
            "cannot find lito.toml or legacy tenon.toml in '{}'", error.as_NotFound().directory.as_path()));
    }
};

template<>
struct Impl<fmt::Debug, lito::ManifestLocatorError> : ImplBase<lito::ManifestLocatorError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::ManifestLocatorError> : ImplBase<lito::ManifestLocatorError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& value = this->self();
        if (! value.is_Io()) return None();
        return Some(dyn<error::Error>::from_ref(value.as_Io().source));
    }
};

template<>
struct Impl<fmt::Display, lito::ManifestSchemaError> : ImplBase<lito::ManifestSchemaError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_UnknownField()) {
            const auto& value = error.as_UnknownField();
            return formatter.write_fmt(fmt::Arguments::make(
                "{} contains unknown field '{}'", value.node, value.field));
        }
        if (error.is_MissingField()) {
            const auto& value = error.as_MissingField();
            return formatter.write_fmt(
                fmt::Arguments::make("{} is missing '{}'", value.node, value.field));
        }
        if (error.is_WrongType()) {
            const auto& value = error.as_WrongType();
            return formatter.write_fmt(
                fmt::Arguments::make("{} must be {}", value.node, value.expected));
        }
        if (error.is_InvalidValue()) {
            return formatter.write_str(error.as_InvalidValue().reason.as_str());
        }
        if (error.is_Io()) {
            const auto& value = error.as_Io();
            return formatter.write_fmt(fmt::Arguments::make(
                "cannot {} {} '{}'", value.operation, value.node, value.path.as_path()));
        }
        if (error.is_Profile()) {
            return formatter.write_raw("manifest build profile is invalid",
                                       sizeof("manifest build profile is invalid") - 1);
        }
        return formatter.write_raw("manifest discovery failed", sizeof("manifest discovery failed") - 1);
    }
};

template<>
struct Impl<fmt::Debug, lito::ManifestSchemaError> : ImplBase<lito::ManifestSchemaError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::ManifestSchemaError> : ImplBase<lito::ManifestSchemaError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& value = this->self();
        if (value.is_Io()) return Some(dyn<error::Error>::from_ref(value.as_Io().source));
        if (value.is_Locate()) return Some(dyn<error::Error>::from_ref(value.as_Locate().source));
        if (value.is_Profile()) {
            return Some(dyn<error::Error>::from_ref(value.as_Profile().source));
        }
        return None();
    }
};

template<>
struct Impl<fmt::Display, lito::ManifestFileCause> : ImplBase<lito::ManifestFileCause> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& value = this->self();
        if (value.is_Read()) {
            return formatter.write_raw("cannot read manifest", sizeof("cannot read manifest") - 1);
        }
        if (value.is_Parse()) {
            return formatter.write_raw("cannot parse manifest", sizeof("cannot parse manifest") - 1);
        }
        return formatter.write_raw("manifest is invalid", sizeof("manifest is invalid") - 1);
    }
};

template<>
struct Impl<fmt::Debug, lito::ManifestFileCause> : ImplBase<lito::ManifestFileCause> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::ManifestFileCause> : ImplBase<lito::ManifestFileCause> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& value = this->self();
        if (value.is_Read()) return Some(dyn<error::Error>::from_ref(value.as_Read().source));
        if (value.is_Parse()) return Some(dyn<error::Error>::from_ref(value.as_Parse().source));
        return Some(dyn<error::Error>::from_ref(value.as_Schema().source));
    }
};

template<>
struct Impl<fmt::Display, lito::ManifestFileError> : ImplBase<lito::ManifestFileError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_fmt(
            fmt::Arguments::make("cannot load manifest '{}'", this->self().path.as_path()));
    }
};

template<>
struct Impl<fmt::Debug, lito::ManifestFileError> : ImplBase<lito::ManifestFileError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::ManifestFileError> : ImplBase<lito::ManifestFileError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        return Some(dyn<error::Error>::from_ref(this->self().cause));
    }
};

template<>
struct Impl<fmt::Display, lito::ManifestError> : ImplBase<lito::ManifestError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& value = this->self();
        if (value.is_Locate()) {
            return formatter.write_raw("cannot locate manifest", sizeof("cannot locate manifest") - 1);
        }
        if (value.is_File()) {
            return formatter.write_raw("manifest file loading failed",
                                       sizeof("manifest file loading failed") - 1);
        }
        return formatter.write_fmt(fmt::Arguments::make(
            "directory '{}' contains a workspace manifest where a package is required",
            value.as_Kind().requested_directory.as_path()));
    }
};

template<>
struct Impl<fmt::Debug, lito::ManifestError> : ImplBase<lito::ManifestError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::ManifestError> : ImplBase<lito::ManifestError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& value = this->self();
        if (value.is_Locate()) return Some(dyn<error::Error>::from_ref(value.as_Locate().source));
        if (value.is_File()) return Some(dyn<error::Error>::from_ref(value.as_File().source));
        return None();
    }
};

} // namespace rstd
