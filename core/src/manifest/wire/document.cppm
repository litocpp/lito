module;
#include <rstd/macro.hpp>
export module lito.core:manifest.wire.document;
import rstd;
import rstd.serde;
import :manifest.wire;
import :manifest.wire.common;
import :manifest.wire.target;
import :manifest.wire.dependency;
import :manifest.wire.external;
import :manifest.wire.profile;
using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::manifest::wire
{
struct CompileTests {
    Vec<CompileTestCase> cases;
};
} // namespace lito::manifest::wire
export namespace rstd
{
template<>
struct Impl<serde::Deserialize, lito::manifest::wire::CompileTests> {
    template<typename D>
    static auto deserialize(D& d)
        -> Result<lito::manifest::wire::CompileTests, typename D::error_type> {
        using namespace lito::manifest::wire;
        auto cases = serde::RequiredField<Vec<CompileTestCase>>("cases"_str);
        rstd_try(serde::deserialize_record(d, serde::UnknownFieldPolicy::Reject, cases));
        auto result = CompileTests {
            .cases = rstd_try(cases.take(d)),
        };

        return Ok(rstd::move(result));
    }
};
} // namespace rstd

export namespace lito::manifest::wire
{
template<bool Workspace>
struct ExternalDependencies {
    Option<rstd::collections::BTreeMap<String, PkgConfig<Workspace>>>     pkg_config;
    Option<rstd::collections::BTreeMap<String, CMakeExternal<Workspace>>> cmake;
    Option<rstd::collections::BTreeMap<String, CargoExternal<Workspace>>> cargo;
};
} // namespace lito::manifest::wire
export namespace rstd
{
template<bool Workspace>
struct Impl<serde::Deserialize, lito::manifest::wire::ExternalDependencies<Workspace>> {
    template<typename D>
    static auto deserialize(D& d)
        -> Result<lito::manifest::wire::ExternalDependencies<Workspace>, typename D::error_type> {
        using namespace lito::manifest::wire;
        auto pkg_config =
            serde::OptionalField<rstd::collections::BTreeMap<String, PkgConfig<Workspace>>>(
                "pkg-config"_str);
        auto cmake =
            serde::OptionalField<rstd::collections::BTreeMap<String, CMakeExternal<Workspace>>>(
                "cmake"_str);
        auto cargo =
            serde::OptionalField<rstd::collections::BTreeMap<String, CargoExternal<Workspace>>>(
                "cargo"_str);
        rstd_try(serde::deserialize_record(
            d, serde::UnknownFieldPolicy::Reject, pkg_config, cmake, cargo));
        auto result = ExternalDependencies<Workspace> {
            .pkg_config = pkg_config.take(),
            .cmake      = cmake.take(),
            .cargo      = cargo.take(),
        };

        return Ok(rstd::move(result));
    }
};
} // namespace rstd

export namespace lito::manifest::wire
{
struct Workspace {
    String                   name;
    Vec<String>              members;
    Option<Vec<String>>      default_members;
    Option<WorkspacePackage> package;
    Option<rstd::collections::BTreeMap<String, Dependency<DependencyMode::Workspace>>> dependencies;
    Option<rstd::collections::BTreeMap<String, ExternalSource<true>>> external_sources;
    Option<ExternalDependencies<true>>                                external_dependencies;
};
} // namespace lito::manifest::wire
export namespace rstd
{
template<>
struct Impl<serde::Deserialize, lito::manifest::wire::Workspace> {
    template<typename D>
    static auto deserialize(D& d)
        -> Result<lito::manifest::wire::Workspace, typename D::error_type> {
        using namespace lito::manifest::wire;
        auto name            = serde::RequiredField<String>("name"_str);
        auto members         = serde::RequiredField<Vec<String>>("members"_str);
        auto default_members = serde::OptionalField<Vec<String>>("default-members"_str);
        auto package         = serde::OptionalField<WorkspacePackage>("package"_str);
        auto dependencies    = serde::OptionalField<
            rstd::collections::BTreeMap<String, Dependency<DependencyMode::Workspace>>>(
            "dependencies"_str);
        auto external_sources =
            serde::OptionalField<rstd::collections::BTreeMap<String, ExternalSource<true>>>(
                "external-sources"_str);
        auto external_dependencies =
            serde::OptionalField<ExternalDependencies<true>>("external-dependencies"_str);
        rstd_try(serde::deserialize_record(d,
                                           serde::UnknownFieldPolicy::Reject,
                                           name,
                                           members,
                                           default_members,
                                           package,
                                           dependencies,
                                           external_sources,
                                           external_dependencies));
        auto result = Workspace {
            .name                  = rstd_try(name.take(d)),
            .members               = rstd_try(members.take(d)),
            .default_members       = default_members.take(),
            .package               = package.take(),
            .dependencies          = dependencies.take(),
            .external_sources      = external_sources.take(),
            .external_dependencies = external_dependencies.take(),
        };

        return Ok(rstd::move(result));
    }
};
} // namespace rstd

export namespace lito::manifest::wire
{
struct Document {
    Option<Workspace>                                                               workspace;
    Option<Package>                                                                 package;
    Option<LibraryTarget>                                                           lib;
    Option<ModuleTarget>                                                            plugin;
    Option<ModuleTarget>                                                            pmacro;
    Option<Vec<BinaryTarget>>                                                       bin;
    Option<Vec<TestTarget>>                                                         test;
    Option<Vec<BenchmarkTarget>>                                                    bench;
    Option<Vec<ExampleTarget>>                                                      example;
    Option<CompileTests>                                                            compile_test;
    Option<Usage>                                                                   usage;
    Option<rstd::collections::BTreeMap<String, Dependency<DependencyMode::Normal>>> dependencies;
    Option<rstd::collections::BTreeMap<String, Dependency<DependencyMode::Development>>>
        dev_dependencies;
    Option<rstd::collections::BTreeMap<String, Dependency<DependencyMode::Runtime>>>
                                                                       runtime_dependencies;
    Option<ExternalDependencies<false>>                                external_dependencies;
    Option<rstd::collections::BTreeMap<String, ExternalSource<false>>> external_sources;
    Option<SourceGroups>                                               source_groups;
    Option<BuildTools>                                                 build_tools;
    Option<Script>                                                     script;
    Option<Profiles>                                                   profile;
    Option<Vec<Condition>>                                             when;
    Option<Features>                                                   features;
};
template<bool Embedded>
struct DocumentInput {
    Document value;
};
} // namespace lito::manifest::wire
export namespace rstd
{
template<bool Embedded>
struct Impl<serde::Deserialize, lito::manifest::wire::DocumentInput<Embedded>> {
    template<typename D>
    static auto deserialize(D& d)
        -> Result<lito::manifest::wire::DocumentInput<Embedded>, typename D::error_type> {
        using namespace lito::manifest::wire;
        auto workspace    = serde::OptionalField<Workspace>("workspace"_str);
        auto package      = serde::OptionalField<PackageInput<Embedded>>("package"_str);
        auto lib          = serde::OptionalField<LibraryTarget>("lib"_str);
        auto plugin       = serde::OptionalField<ModuleTarget>("plugin"_str);
        auto pmacro       = serde::OptionalField<ModuleTarget>("pmacro"_str);
        auto bin          = serde::OptionalField<Vec<BinaryTarget>>("bin"_str);
        auto test         = serde::OptionalField<Vec<TestTarget>>("test"_str);
        auto bench        = serde::OptionalField<Vec<BenchmarkTarget>>("bench"_str);
        auto example      = serde::OptionalField<Vec<ExampleTarget>>("example"_str);
        auto compile_test = serde::OptionalField<CompileTests>("compile-test"_str);
        auto usage        = serde::OptionalField<Usage>("usage"_str);
        auto dependencies = serde::OptionalField<
            rstd::collections::BTreeMap<String, Dependency<DependencyMode::Normal>>>(
            "dependencies"_str);
        auto dev_dependencies = serde::OptionalField<
            rstd::collections::BTreeMap<String, Dependency<DependencyMode::Development>>>(
            "dev-dependencies"_str);
        auto runtime_dependencies = serde::OptionalField<
            rstd::collections::BTreeMap<String, Dependency<DependencyMode::Runtime>>>(
            "runtime-dependencies"_str);
        auto external_dependencies =
            serde::OptionalField<ExternalDependencies<false>>("external-dependencies"_str);
        auto external_sources =
            serde::OptionalField<rstd::collections::BTreeMap<String, ExternalSource<false>>>(
                "external-sources"_str);
        auto source_groups = serde::OptionalField<SourceGroups>("source-groups"_str);
        auto build_tools   = serde::OptionalField<BuildTools>("build-tools"_str);
        auto script        = serde::OptionalField<Script>("script"_str);
        auto profile       = serde::OptionalField<Profiles>("profile"_str);
        auto when          = serde::OptionalField<Vec<Condition>>("when"_str);
        auto features      = serde::OptionalField<Features>("features"_str);
        rstd_try(serde::deserialize_record(d,
                                           serde::UnknownFieldPolicy::Reject,
                                           workspace,
                                           package,
                                           lib,
                                           plugin,
                                           pmacro,
                                           bin,
                                           test,
                                           bench,
                                           example,
                                           compile_test,
                                           usage,
                                           dependencies,
                                           dev_dependencies,
                                           runtime_dependencies,
                                           external_dependencies,
                                           external_sources,
                                           source_groups,
                                           build_tools,
                                           script,
                                           profile,
                                           when,
                                           features));
        auto result = Document {
            .workspace             = workspace.take(),
            .lib                   = lib.take(),
            .plugin                = plugin.take(),
            .pmacro                = pmacro.take(),
            .bin                   = bin.take(),
            .test                  = test.take(),
            .bench                 = bench.take(),
            .example               = example.take(),
            .compile_test          = compile_test.take(),
            .usage                 = usage.take(),
            .dependencies          = dependencies.take(),
            .dev_dependencies      = dev_dependencies.take(),
            .runtime_dependencies  = runtime_dependencies.take(),
            .external_dependencies = external_dependencies.take(),
            .external_sources      = external_sources.take(),
            .source_groups         = source_groups.take(),
            .build_tools           = build_tools.take(),
            .script                = script.take(),
            .profile               = profile.take(),
            .when                  = when.take(),
            .features              = features.take(),
        };
        auto declared_package = package.take();
        if (declared_package.is_some()) result.package = Some(rstd::move(declared_package->value));
        if (result.workspace.is_some()) {
            if (result.package.is_some()) return Err(d.unknown_field(package.names().canonical()));
            if (result.lib.is_some()) return Err(d.unknown_field(lib.names().canonical()));
            if (result.plugin.is_some()) return Err(d.unknown_field(plugin.names().canonical()));
            if (result.pmacro.is_some()) return Err(d.unknown_field(pmacro.names().canonical()));
            if (result.bin.is_some()) return Err(d.unknown_field(bin.names().canonical()));
            if (result.test.is_some()) return Err(d.unknown_field(test.names().canonical()));
            if (result.bench.is_some()) return Err(d.unknown_field(bench.names().canonical()));
            if (result.example.is_some()) return Err(d.unknown_field(example.names().canonical()));
            if (result.compile_test.is_some())
                return Err(d.unknown_field(compile_test.names().canonical()));
            if (result.usage.is_some()) return Err(d.unknown_field(usage.names().canonical()));
            if (result.dependencies.is_some())
                return Err(d.unknown_field(dependencies.names().canonical()));
            if (result.dev_dependencies.is_some())
                return Err(d.unknown_field(dev_dependencies.names().canonical()));
            if (result.runtime_dependencies.is_some())
                return Err(d.unknown_field(runtime_dependencies.names().canonical()));
            if (result.external_dependencies.is_some())
                return Err(d.unknown_field(external_dependencies.names().canonical()));
            if (result.external_sources.is_some())
                return Err(d.unknown_field(external_sources.names().canonical()));
            if (result.source_groups.is_some())
                return Err(d.unknown_field(source_groups.names().canonical()));
            if (result.build_tools.is_some())
                return Err(d.unknown_field(build_tools.names().canonical()));
            if (result.script.is_some()) return Err(d.unknown_field(script.names().canonical()));
            if (result.when.is_some()) return Err(d.unknown_field(when.names().canonical()));
            if (result.features.is_some())
                return Err(d.unknown_field(features.names().canonical()));
        } else if (result.package.is_none())
            return Err(d.missing_field(package.names().canonical()));
        return Ok(DocumentInput<Embedded> { .value = rstd::move(result) });
    }
};
} // namespace rstd
