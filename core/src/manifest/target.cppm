module;
#include <rstd/enum.hpp>

export module lito.core:manifest.target;

import rstd;
import lito.system;
import :dependency.usage;
import :package.identity;
import :condition;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace lito::system;

export namespace lito::manifest
{

enum class SourceDiscoveryMode
{
    Explicit,
    Module,
};

struct TestAttachmentManifest {
    String       package;
    Vec<PathBuf> sources;
};

struct ConditionalTargetSources {
    String                      source;
    lito::condition::Expression condition;
    Vec<String>                 source_groups;
};

struct TargetSourceManifest {
    Option<String>                module;
    SourceDiscoveryMode           discovery { SourceDiscoveryMode::Explicit };
    Vec<PathBuf>                  declared_sources;
    Vec<String>                   source_groups;
    Vec<ConditionalTargetSources> conditions;
};

enum class SourceGroupRoot
{
    Package,
    Generated,
};

struct SourceGroupManifest {
    String          name;
    SourceGroupRoot root { SourceGroupRoot::Package };
    Option<String>  external_source;
    Vec<PathBuf>    sources;
};

enum class RuntimeResourceRoot
{
    Generated,
};

struct RuntimeResourceManifest {
    String              name;
    RuntimeResourceRoot root { RuntimeResourceRoot::Generated };
    PathBuf             path;
};

class LibraryOutput {
    RSTD_ENUM(LibraryOutput, (Static, (String artifact;)), (Shared, (String artifact;)))
};

class PackageTargetManifest {
    RSTD_ENUM(PackageTargetManifest,
              (Library,
               (String name; LibraryOutput output; TargetSourceManifest source;
                Vec<String>                                             linker_options;)),
              (Plugin, (String name; TargetSourceManifest source;)),
              (ProcMacro, (String name; TargetSourceManifest source;)),
              (Binary,
               (String name; TargetSourceManifest source; bool link_stdlib; bool host_tool;
                Vec<RuntimeResourceManifest>                                     resources;)),
              (Test,
               (String name; TargetSourceManifest source; bool link_stdlib;
                Vec<TestAttachmentManifest>                    attachments;)),
              (Benchmark, (String name; TargetSourceManifest source; bool link_stdlib;)))
};

auto package_target_kind(const PackageTargetManifest& target) noexcept
    -> lito::package::PackageTargetKind {
    if (target.is_Library()) return lito::package::PackageTargetKind::Library;
    if (target.is_Plugin()) return lito::package::PackageTargetKind::Plugin;
    if (target.is_ProcMacro()) return lito::package::PackageTargetKind::ProcMacro;
    if (target.is_Binary()) return lito::package::PackageTargetKind::Binary;
    if (target.is_Test()) return lito::package::PackageTargetKind::Test;
    return lito::package::PackageTargetKind::Benchmark;
}

auto package_target_name(const PackageTargetManifest& target) noexcept -> ref<str> {
    if (target.is_Library()) return target.as_Library().name.as_str();
    if (target.is_Plugin()) return target.as_Plugin().name.as_str();
    if (target.is_ProcMacro()) return target.as_ProcMacro().name.as_str();
    if (target.is_Binary()) return target.as_Binary().name.as_str();
    if (target.is_Test()) return target.as_Test().name.as_str();
    return target.as_Benchmark().name.as_str();
}

auto package_target_source(const PackageTargetManifest& target) noexcept
    -> const TargetSourceManifest& {
    if (target.is_Library()) return target.as_Library().source;
    if (target.is_Plugin()) return target.as_Plugin().source;
    if (target.is_ProcMacro()) return target.as_ProcMacro().source;
    if (target.is_Binary()) return target.as_Binary().source;
    if (target.is_Test()) return target.as_Test().source;
    return target.as_Benchmark().source;
}

auto package_target_source(PackageTargetManifest& target) noexcept -> TargetSourceManifest& {
    if (target.is_Library()) return target.as_Library().source;
    if (target.is_Plugin()) return target.as_Plugin().source;
    if (target.is_ProcMacro()) return target.as_ProcMacro().source;
    if (target.is_Binary()) return target.as_Binary().source;
    if (target.is_Test()) return target.as_Test().source;
    return target.as_Benchmark().source;
}

auto package_target_artifact_name(const PackageTargetManifest& target) noexcept -> ref<str> {
    if (target.is_Library()) {
        const auto& output = target.as_Library().output;
        return output.is_Static() ? output.as_Static().artifact.as_str()
                                  : output.as_Shared().artifact.as_str();
    }
    return package_target_name(target);
}

auto package_library_is_shared(const PackageTargetManifest& target) noexcept -> bool {
    return target.is_Library() && target.as_Library().output.is_Shared();
}

auto package_target_linker_options(const PackageTargetManifest& target) noexcept
    -> Option<ref<Vec<String>>> {
    if (! target.is_Library()) return None();
    return Some(
        ref<Vec<String>>::from_raw_parts(rstd::addressof(target.as_Library().linker_options)));
}

auto package_target_links_stdlib(const PackageTargetManifest& target) noexcept -> bool {
    if (target.is_Binary()) return target.as_Binary().link_stdlib;
    if (target.is_Test()) return target.as_Test().link_stdlib;
    if (target.is_Benchmark()) return target.as_Benchmark().link_stdlib;
    return true;
}

auto package_target_is_host_tool(const PackageTargetManifest& target) noexcept -> bool {
    return target.is_Binary() && target.as_Binary().host_tool;
}

auto package_target_attachments(const PackageTargetManifest& target) noexcept
    -> Option<ref<Vec<TestAttachmentManifest>>> {
    if (! target.is_Test()) return None();
    return Some(ref<Vec<TestAttachmentManifest>>::from_raw_parts(
        rstd::addressof(target.as_Test().attachments)));
}

auto package_target_resources(const PackageTargetManifest& target) noexcept
    -> Option<ref<Vec<RuntimeResourceManifest>>> {
    if (! target.is_Binary()) return None();
    return Some(ref<Vec<RuntimeResourceManifest>>::from_raw_parts(
        rstd::addressof(target.as_Binary().resources)));
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
    Vec<String>        diagnostic_contains;
    Vec<String>        diagnostic_contains_any;
};

} // namespace lito::manifest
