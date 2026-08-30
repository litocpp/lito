module;
#include <rstd/macro.hpp>

export module lito.core:manifest.standalone;

import rstd;
import rstd.toml;
import lito.system;
import :manifest.document;
import :manifest.error;
import :manifest.package;
import :dependency.cmake;
import :dependency.pkg_config;
import :dependency.source;
import :dependency.visibility;
import :registry.identity;
import :source.git;
import :source.requirement;
import :source.tree;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;
using Toml    = rstd::toml::Value;
using Table   = rstd::toml::Table;

export namespace lito::manifest
{

struct StandaloneRegistryAlias {
    String                     name;
    lito::registry::RegistryId identity;
};

struct StandaloneManifestOptions {
    lito::registry::RegistryId   owner_registry;
    Vec<StandaloneRegistryAlias> registry_aliases;
};

class StandalonePackageManifest {
    String contents_;

public:
    explicit StandalonePackageManifest(String contents): contents_(rstd::move(contents)) {}

    auto as_str() const noexcept -> ref<str> { return contents_.as_str(); }
    auto clone() const -> StandalonePackageManifest {
        return StandalonePackageManifest(contents_.clone());
    }
};

auto serialize_standalone_package_manifest(const PackageManifest&           manifest,
                                           const StandaloneManifestOptions& options)
    -> ManifestResult<StandalonePackageManifest>;

} // namespace lito::manifest

namespace
{

using namespace lito::manifest;

template<typename T>
auto standalone_failure(const PackageManifest& manifest, String message) -> ManifestResult<T> {
    return Err(ManifestError::File(ManifestFileError {
        .path  = manifest.manifest_path.clone(),
        .cause = ManifestFileCause::Schema(ManifestSchemaError::Domain(rstd::move(message))),
    }));
}

template<typename T>
auto standalone_failure(const PackageManifest& manifest, ref<str> message) -> ManifestResult<T> {
    return standalone_failure<T>(manifest, String::make(message));
}

auto string_value(ref<str> value) -> Toml {
    return Toml::String(String::make(value));
}

auto string_array(const Vec<String>& values) -> Toml {
    auto result = rstd::toml::Array::with_capacity(values.len());
    for (const auto& value : values) result.push(string_value(value.as_str()));
    return Toml::Array(rstd::move(result));
}

auto visibility_text(lito::dependency::DependencyVisibility visibility) noexcept -> ref<str> {
    switch (visibility) {
    case lito::dependency::DependencyVisibility::Private: return "private"_str;
    case lito::dependency::DependencyVisibility::Public: return "public"_str;
    case lito::dependency::DependencyVisibility::LinkOnly: return "link"_str;
    }
    rstd::unreachable();
}

auto registry_identity(const StandaloneManifestOptions& options, ref<str> value)
    -> Option<ref<lito::registry::RegistryId>> {
    auto parsed = lito::registry::RegistryId::parse(value);
    if (parsed.is_ok()) {
        for (const auto& alias : options.registry_aliases) {
            if (alias.identity == *parsed) {
                return Some(ref<lito::registry::RegistryId>::from_raw_parts(
                    rstd::addressof(alias.identity)));
            }
        }
        if (options.owner_registry == *parsed) {
            return Some(ref<lito::registry::RegistryId>::from_raw_parts(
                rstd::addressof(options.owner_registry)));
        }
    }
    for (const auto& alias : options.registry_aliases) {
        if (alias.name.as_str() == value) {
            return Some(
                ref<lito::registry::RegistryId>::from_raw_parts(rstd::addressof(alias.identity)));
        }
    }
    return None();
}

auto package_source_table(const PackageManifest&                          manifest,
                          const lito::source::PackageRegistryRequirement& source,
                          ref<str>                                        dependency_name,
                          const StandaloneManifestOptions& options) -> ManifestResult<Table> {
    if (source.package.as_str() != dependency_name) {
        return standalone_failure<Table>(
            manifest,
            rstd::format("published dependency '{}' must not rename package '{}'",
                         dependency_name,
                         source.package.as_str()));
    }
    auto table = Table::make();
    table.insert(String::make("version"_str), string_value(source.requirement.text()));
    if (source.registry.is_some()) {
        auto identity = registry_identity(options, source.registry->as_str());
        if (identity.is_none()) {
            return standalone_failure<Table>(
                manifest,
                rstd::format("published dependency '{}' uses unknown Registry '{}'",
                             dependency_name,
                             source.registry->as_str()));
        }
        if (! (**identity == options.owner_registry)) {
            table.insert(String::make("registry"_str), string_value((*identity)->as_str()));
        }
    }
    return Ok(rstd::move(table));
}

auto dependency_table(const PackageManifest&           manifest,
                      const DeclaredDependency&        dependency,
                      bool                             development,
                      const StandaloneManifestOptions& options) -> ManifestResult<Option<Toml>> {
    if (dependency.source.publication.is_none()) {
        if (development) return Ok(Option<Toml> {});
        return standalone_failure<Option<Toml>>(
            manifest,
            rstd::format("published normal dependency '{}' must declare a Registry version",
                         dependency.name.as_str()));
    }
    auto table = rstd_try(package_source_table(
        manifest, *dependency.source.publication, dependency.name.as_str(), options));
    if (! development && dependency.visibility.is_some()) {
        table.insert(String::make("visibility"_str),
                     string_value(visibility_text(*dependency.visibility)));
    }
    if (dependency.features.is_some()) {
        table.insert(String::make("features"_str), string_array(*dependency.features));
    }
    if (dependency.default_features.is_some()) {
        table.insert(String::make("default-features"_str),
                     Toml::Boolean(*dependency.default_features));
    }
    return Ok(Some(Toml::Table(rstd::move(table))));
}

auto package_dependencies(const PackageManifest&           manifest,
                          const Vec<DeclaredDependency>&   dependencies,
                          bool                             development,
                          const StandaloneManifestOptions& options)
    -> ManifestResult<Option<Toml>> {
    if (dependencies.is_empty()) return Ok(Option<Toml> {});
    auto table = Table::make();
    for (const auto& dependency : dependencies) {
        auto serialized = rstd_try(dependency_table(manifest, dependency, development, options));
        if (serialized.is_some()) {
            table.insert(dependency.name.clone(), rstd::move(serialized).unwrap());
        }
    }
    if (table.is_empty()) return Ok(Option<Toml> {});
    return Ok(Some(Toml::Table(rstd::move(table))));
}

auto runtime_dependencies(const PackageManifest& manifest, const StandaloneManifestOptions& options)
    -> ManifestResult<Option<Toml>> {
    if (manifest.runtime_dependencies.is_empty()) return Ok(Option<Toml> {});
    auto table = Table::make();
    for (const auto& dependency : manifest.runtime_dependencies) {
        if (dependency.source.publication.is_none()) {
            return standalone_failure<Option<Toml>>(
                manifest,
                rstd::format("published runtime dependency '{}' must declare a Registry version",
                             dependency.name.as_str()));
        }
        table.insert(
            dependency.name.clone(),
            Toml::Table(rstd_try(package_source_table(
                manifest, *dependency.source.publication, dependency.name.as_str(), options))));
    }
    return Ok(Some(Toml::Table(rstd::move(table))));
}

auto portable_relative_path(const PackageManifest& manifest,
                            ref<rstd::path::Path>  declaration_root,
                            ref<rstd::path::Path>  declared,
                            ref<str>               context) -> ManifestResult<String> {
    auto requested = PathBuf::from(declaration_root).join(declared);
    auto canonical = rstd::fs::canonicalize(requested.as_path());
    if (canonical.is_err()) {
        return standalone_failure<String>(manifest,
                                          rstd::format("cannot resolve published {} '{}': {}",
                                                       context,
                                                       requested.as_path(),
                                                       rstd::move(canonical).unwrap_err()));
    }
    auto relative = canonical->as_path().strip_prefix(manifest.root.as_path());
    if (relative.is_none() || relative->as_os_str().is_empty()) {
        return standalone_failure<String>(
            manifest,
            rstd::format("published {} '{}' must be inside package root '{}'",
                         context,
                         requested.as_path(),
                         manifest.root.as_path()));
    }
    auto portable = lito::source::SourcePath::from_relative_path(*relative);
    if (portable.is_err()) {
        return standalone_failure<String>(manifest,
                                          rstd::format("published {} path '{}': {}",
                                                       context,
                                                       *relative,
                                                       rstd::move(portable).unwrap_err()));
    }
    return Ok(String::make(portable->as_str()));
}

auto sha256_text(const licrypto::Sha256Digest& digest) -> String {
    return digest.to_hex();
}

auto external_source_table(const PackageManifest&                  manifest,
                           const PackageExternalSourceDeclaration& source) -> ManifestResult<Toml> {
    auto        table       = Table::make();
    const auto& requirement = source.source;
    if (requirement.is_Path()) {
        auto root = source.declaration_root.is_some() ? source.declaration_root->as_path()
                                                      : manifest.root.as_path();
        table.insert(
            String::make("path"_str),
            string_value(rstd_try(portable_relative_path(manifest,
                                                         root,
                                                         requirement.as_Path().path.as_path(),
                                                         "external source"_str))
                             .as_str()));
    } else if (requirement.is_Git()) {
        const auto& git = requirement.as_Git();
        if (git.reference.kind != lito::source::GitReferenceKind::Commit ||
            ! lito::source::git_commit_is_valid(git.reference.value.as_str())) {
            return standalone_failure<Toml>(
                manifest,
                rstd::format("published external source '{}' Git reference must be a full commit",
                             source.name.as_str()));
        }
        table.insert(String::make("git"_str), string_value(git.url.as_str()));
        table.insert(String::make("commit"_str), string_value(git.reference.value.as_str()));
    } else if (requirement.is_Archive()) {
        const auto& archive = requirement.as_Archive();
        table.insert(String::make("archive"_str), string_value(archive.url.as_str()));
        table.insert(String::make("sha256"_str),
                     string_value(sha256_text(archive.sha256).as_str()));
    } else {
        auto archives = Table::make();
        for (const auto& variant : requirement.as_ArchitectureArchives().variants) {
            auto entry = Table::make();
            entry.insert(String::make("archive"_str), string_value(variant.url.as_str()));
            entry.insert(String::make("sha256"_str),
                         string_value(sha256_text(variant.sha256).as_str()));
            archives.insert(String::make(architecture_name(variant.architecture)),
                            Toml::Table(rstd::move(entry)));
        }
        table.insert(String::make("archives"_str), Toml::Table(rstd::move(archives)));
    }
    return Ok(Toml::Table(rstd::move(table)));
}

auto external_sources(const PackageManifest& manifest) -> ManifestResult<Option<Toml>> {
    if (manifest.external_sources.is_empty()) return Ok(Option<Toml> {});
    auto table = Table::make();
    for (const auto& source : manifest.external_sources) {
        table.insert(source.name.clone(), rstd_try(external_source_table(manifest, source)));
    }
    return Ok(Some(Toml::Table(rstd::move(table))));
}

auto pkg_config_version(const lito::dependency::PkgConfigVersionRequirement& version) -> String {
    auto prefix = "="_str;
    switch (version.comparison) {
    case lito::dependency::PkgConfigVersionOperator::Equal: prefix = "="_str; break;
    case lito::dependency::PkgConfigVersionOperator::Less: prefix = "<"_str; break;
    case lito::dependency::PkgConfigVersionOperator::Greater: prefix = ">"_str; break;
    case lito::dependency::PkgConfigVersionOperator::LessEqual: prefix = "<="_str; break;
    case lito::dependency::PkgConfigVersionOperator::GreaterEqual: prefix = ">="_str; break;
    }
    return rstd::format("{}{}", prefix, version.value.as_str());
}

auto pkg_config_usage(lito::dependency::PkgConfigDependencyUsage usage) noexcept -> ref<str> {
    switch (usage) {
    case lito::dependency::PkgConfigDependencyUsage::Link: return "link"_str;
    case lito::dependency::PkgConfigDependencyUsage::Compile: return "compile"_str;
    }
    return "link"_str;
}

auto pkg_config_dependencies(const PackageManifest& manifest) -> Option<Toml> {
    if (manifest.pkg_config_external_dependencies.is_empty()) return Option<Toml> {};
    auto table = Table::make();
    for (const auto& dependency : manifest.pkg_config_external_dependencies) {
        auto value = Table::make();
        value.insert(String::make("module"_str),
                     string_value(dependency.requirement.module.as_str()));
        if (dependency.requirement.version.is_some()) {
            value.insert(
                String::make("version"_str),
                string_value(pkg_config_version(*dependency.requirement.version).as_str()));
        }
        if (dependency.requirement.mode == lito::dependency::PkgConfigQueryMode::Static) {
            value.insert(String::make("static"_str), Toml::Boolean(true));
        }
        value.insert(String::make("usage"_str), string_value(pkg_config_usage(dependency.usage)));
        value.insert(String::make("visibility"_str),
                     string_value(visibility_text(dependency.visibility)));
        if (dependency.condition.is_some()) {
            value.insert(String::make("condition"_str),
                         string_value(dependency.condition->source.as_str()));
        }
        table.insert(dependency.alias.clone(), Toml::Table(rstd::move(value)));
    }
    return Some(Toml::Table(rstd::move(table)));
}

auto cmake_dependencies(const PackageManifest& manifest) -> ManifestResult<Option<Toml>> {
    if (manifest.cmake_external_dependencies.is_empty()) return Ok(Option<Toml> {});
    auto table = Table::make();
    for (const auto& dependency : manifest.cmake_external_dependencies) {
        auto value = Table::make();
        value.insert(String::make("package"_str), string_value(dependency.package.as_str()));
        if (! dependency.components.is_empty()) {
            value.insert(String::make("components"_str), string_array(dependency.components));
        }
        if (dependency.source.is_some()) {
            value.insert(String::make("source"_str), string_value(dependency.source->as_str()));
        }
        if (dependency.adapter.is_some()) {
            auto root = dependency.adapter_root.is_some() ? dependency.adapter_root->as_path()
                                                          : manifest.root.as_path();
            value.insert(String::make("adapter"_str),
                         string_value(rstd_try(portable_relative_path(manifest,
                                                                      root,
                                                                      dependency.adapter->as_path(),
                                                                      "CMake adapter"_str))
                                          .as_str()));
        }
        if (dependency.config_directory.is_some()) {
            auto text = dependency.config_directory->as_path().to_str();
            if (text.is_none()) {
                return standalone_failure<Option<Toml>>(
                    manifest, "published CMake config-directory is not valid UTF-8"_str);
            }
            value.insert(String::make("config-directory"_str), string_value(*text));
        }
        if (! dependency.cache.is_empty()) {
            auto cache = Table::make();
            for (const auto& entry : dependency.cache) {
                cache.insert(entry.name.clone(), string_value(entry.value.as_str()));
            }
            value.insert(String::make("cache"_str), Toml::Table(rstd::move(cache)));
        }
        if (! dependency.targets.is_empty()) {
            auto targets = rstd::toml::Array::with_capacity(dependency.targets.len());
            for (const auto& target : dependency.targets) {
                auto item = Table::make();
                item.insert(String::make("name"_str), string_value(target.name.as_str()));
                item.insert(String::make("visibility"_str),
                            string_value(visibility_text(target.visibility)));
                targets.push(Toml::Table(rstd::move(item)));
            }
            value.insert(String::make("targets"_str), Toml::Array(rstd::move(targets)));
        }
        if (! dependency.host_tools.is_empty()) {
            auto tools = rstd::toml::Array::with_capacity(dependency.host_tools.len());
            for (const auto& tool : dependency.host_tools) {
                auto item = Table::make();
                item.insert(String::make("name"_str), string_value(tool.name.as_str()));
                item.insert(String::make("target"_str), string_value(tool.target.as_str()));
                tools.push(Toml::Table(rstd::move(item)));
            }
            value.insert(String::make("host-tools"_str), Toml::Array(rstd::move(tools)));
        }
        if (dependency.condition.is_some()) {
            value.insert(String::make("condition"_str),
                         string_value(dependency.condition->source.as_str()));
        }
        table.insert(dependency.alias.clone(), Toml::Table(rstd::move(value)));
    }
    return Ok(Some(Toml::Table(rstd::move(table))));
}

auto cargo_dependencies(const PackageManifest& manifest) -> ManifestResult<Option<Toml>> {
    if (manifest.cargo_external_dependencies.is_empty()) return Ok(Option<Toml> {});
    auto table = Table::make();
    for (const auto& dependency : manifest.cargo_external_dependencies) {
        auto manifest_path = dependency.recipe.manifest_path.as_path().to_str();
        if (manifest_path.is_none()) {
            return standalone_failure<Option<Toml>>(
                manifest, "Cargo external manifest-path is not valid UTF-8"_str);
        }
        auto value = Table::make();
        value.insert(String::make("source"_str), string_value(dependency.recipe.source.as_str()));
        value.insert(String::make("package"_str), string_value(dependency.recipe.package.as_str()));
        value.insert(String::make("manifest-path"_str), string_value(*manifest_path));
        value.insert(String::make("usage"_str),
                     string_value(dependency.consumption.usage ==
                                          lito::dependency::CargoDependencyUsage::Link
                                      ? "link"_str
                                      : "runtime"_str));
        if (! dependency.consumption.features.is_empty()) {
            value.insert(String::make("features"_str),
                         string_array(dependency.consumption.features));
        }
        if (! dependency.consumption.default_features) {
            value.insert(String::make("default-features"_str), Toml::Boolean(false));
        }
        if (dependency.consumption.profile.is_some()) {
            value.insert(String::make("profile"_str),
                         string_value(dependency.consumption.profile->as_str()));
        }
        if (dependency.consumption.visibility.is_some()) {
            value.insert(String::make("visibility"_str),
                         string_value(visibility_text(*dependency.consumption.visibility)));
        }
        if (dependency.consumption.condition.is_some()) {
            value.insert(String::make("condition"_str),
                         string_value(dependency.consumption.condition->source.as_str()));
        }
        table.insert(dependency.alias.clone(), Toml::Table(rstd::move(value)));
    }
    return Ok(Some(Toml::Table(rstd::move(table))));
}

auto replace_optional(Table& root, ref<str> key, Option<Toml> value) -> void {
    if (value.is_some())
        root.insert(String::make(key), rstd::move(value).unwrap());
    else
        root.remove(key);
}

} // namespace

auto lito::manifest::serialize_standalone_package_manifest(const PackageManifest& manifest,
                                                           const StandaloneManifestOptions& options)
    -> ManifestResult<StandalonePackageManifest> {
    if (manifest.version.value.is_none()) {
        return standalone_failure<StandalonePackageManifest>(
            manifest, "published package must have an exact version"_str);
    }
    auto contents = rstd::fs::read_to_string(manifest.manifest_path.as_path());
    if (contents.is_err()) {
        return Err(ManifestError::File(ManifestFileError {
            .path  = manifest.manifest_path.clone(),
            .cause = ManifestFileCause::Read(rstd::move(contents).unwrap_err()),
        }));
    }
    auto parsed = rstd::toml::from_str(contents->as_str());
    if (parsed.is_err()) {
        return Err(ManifestError::File(ManifestFileError {
            .path  = manifest.manifest_path.clone(),
            .cause = ManifestFileCause::Parse(rstd::move(parsed).unwrap_err()),
        }));
    }
    auto document = rstd::move(parsed).unwrap();
    auto package  = document.get_mut("package"_str);
    if (package.is_none() || (**package).as_table_mut().is_none()) {
        return standalone_failure<StandalonePackageManifest>(
            manifest, "package manifest has no package table"_str);
    }
    auto package_table = (**package).as_table_mut().unwrap();
    package_table->insert(String::make("version"_str),
                          string_value(manifest.version.value->as_str()));
    if (manifest.license.value.is_some()) {
        package_table->insert(String::make("license"_str),
                              string_value(manifest.license.value->as_str()));
    } else {
        package_table->remove("license"_str);
    }
    if (! manifest.authors.values.is_empty()) {
        package_table->insert(String::make("authors"_str), string_array(manifest.authors.values));
    } else {
        package_table->remove("authors"_str);
    }

    auto root = document.as_table_mut().unwrap();
    root->remove("workspace"_str);
    replace_optional(
        *root,
        "dependencies"_str,
        rstd_try(package_dependencies(manifest, manifest.dependencies, false, options)));
    replace_optional(
        *root,
        "dev-dependencies"_str,
        rstd_try(package_dependencies(manifest, manifest.dev_dependencies, true, options)));
    replace_optional(
        *root, "runtime-dependencies"_str, rstd_try(runtime_dependencies(manifest, options)));
    replace_optional(*root, "external-sources"_str, rstd_try(external_sources(manifest)));

    auto external   = Table::make();
    auto pkg_config = pkg_config_dependencies(manifest);
    if (pkg_config.is_some()) {
        external.insert(String::make("pkg-config"_str), rstd::move(pkg_config).unwrap());
    }
    auto cmake = rstd_try(cmake_dependencies(manifest));
    if (cmake.is_some()) {
        external.insert(String::make("cmake"_str), rstd::move(cmake).unwrap());
    }
    auto cargo = rstd_try(cargo_dependencies(manifest));
    if (cargo.is_some()) {
        external.insert(String::make("cargo"_str), rstd::move(cargo).unwrap());
    }
    replace_optional(*root,
                     "external-dependencies"_str,
                     external.is_empty() ? Option<Toml> {}
                                         : Some(Toml::Table(rstd::move(external))));

    auto serialized = rstd::toml::to_string(document);
    if (serialized.is_err()) {
        return standalone_failure<StandalonePackageManifest>(
            manifest,
            rstd::format("cannot serialize standalone package manifest: {}",
                         rstd::move(serialized).unwrap_err()));
    }
    auto result = rstd::move(serialized).unwrap();
    if (! result.as_str().ends_with("\n"_str)) result.push_ascii(u8('\n'));
    return Ok(StandalonePackageManifest(rstd::move(result)));
}
