module;
#include <rstd/macro.hpp>

module lito.core;

import rstd;
import rstd.toml;
import :manifest;
import :package.identity;
import lito.system;
import :manifest.primitives;
import :manifest.profile_schema;
import :manifest.key_schema;
import :manifest.convention;
import :manifest.target_schema;
import :manifest.dependency_schema;
import :manifest.build_tool_schema;
import :manifest.build_script_schema;
import :parse;
import :source.tree;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace lito::system;
using namespace rstd::literals;
using Toml  = rstd::toml::Value;
using Table = rstd::toml::Table;
using namespace lito::manifest;

auto lito::manifest::valid_package_name(ref<str> value) -> bool {
    return package_name_is_valid(value);
}

template<typename T>
auto manifest_edit_failure(ref<rstd::path::Path> path, String message)
    -> lito::manifest::ManifestEditResult<T> {
    return Err(lito::manifest::ManifestEditError {
        .path    = PathBuf::from(path),
        .message = rstd::move(message),
    });
}

template<typename T>
auto manifest_edit_failure(ref<rstd::path::Path> path, ref<str> message)
    -> lito::manifest::ManifestEditResult<T> {
    return manifest_edit_failure<T>(path, String::make(message));
}

auto assemble_manifest_document(PathBuf                               root,
                                PathBuf                               path,
                                Toml                                  document,
                                Option<ref<lito::source::SourceTree>> embedded_source = None())
    -> ManifestSchemaResult<ManifestDocument> {
    auto root_table = table_value(document, "manifest root"_str);
    if (root_table.is_err()) return Err(rstd::move(root_table).unwrap_err());

    auto workspace_value = member(document, "workspace"_str);
    if (workspace_value.is_some()) {
        auto root_known = reject_unknown(**root_table, "manifest root"_str, workspace_root_key);
        if (root_known.is_err()) return Err(rstd::move(root_known).unwrap_err());
        auto workspace_table = table_value(**workspace_value, "manifest.workspace"_str);
        if (workspace_table.is_err()) {
            return Err(rstd::move(workspace_table).unwrap_err());
        }
        auto workspace_known =
            reject_unknown(**workspace_table, "manifest.workspace"_str, workspace_key);
        if (workspace_known.is_err()) {
            return Err(rstd::move(workspace_known).unwrap_err());
        }
        auto workspace_name = required_string(**workspace_value, "name"_str, "workspace"_str);
        if (workspace_name.is_err()) return Err(rstd::move(workspace_name).unwrap_err());
        if (! package_name_is_valid(workspace_name->as_str())) {
            return manifest_schema_failure<ManifestDocument>(
                "workspace.name must contain only ASCII letters, digits, '-' or '_'"_str);
        }
        auto members =
            declared_paths(member(**workspace_value, "members"_str), "workspace.members"_str, true);
        auto default_member_value = member(**workspace_value, "default-members"_str);
        auto default_members      = declared_paths(
            default_member_value, "workspace.default-members"_str, default_member_value.is_some());
        if (members.is_err()) return Err(rstd::move(members).unwrap_err());
        if (default_members.is_err()) {
            return Err(rstd::move(default_members).unwrap_err());
        }
        auto package_defaults        = WorkspacePackageDefaults {};
        auto workspace_package_value = member(**workspace_value, "package"_str);
        if (workspace_package_value.is_some()) {
            auto workspace_package_table =
                table_value(**workspace_package_value, "manifest.workspace.package"_str);
            if (workspace_package_table.is_err()) {
                return Err(rstd::move(workspace_package_table).unwrap_err());
            }
            auto workspace_package_known = reject_unknown(
                **workspace_package_table, "manifest.workspace.package"_str, workspace_package_key);
            if (workspace_package_known.is_err()) {
                return Err(rstd::move(workspace_package_known).unwrap_err());
            }
            auto workspace_version =
                optional_string(**workspace_package_value, "version"_str, "workspace.package"_str);
            if (workspace_version.is_err()) {
                return Err(rstd::move(workspace_version).unwrap_err());
            }
            if (workspace_version->is_some() && (**workspace_version).is_empty()) {
                return manifest_schema_failure<ManifestDocument>(
                    "workspace.package.version must not be empty"_str);
            }
            auto workspace_license =
                optional_string(**workspace_package_value, "license"_str, "workspace.package"_str);
            if (workspace_license.is_err()) {
                return Err(rstd::move(workspace_license).unwrap_err());
            }
            if (workspace_license->is_some() && (**workspace_license).is_empty()) {
                return manifest_schema_failure<ManifestDocument>(
                    "workspace.package.license must not be empty"_str);
            }
            auto workspace_authors       = Option<Vec<String>> {};
            auto workspace_authors_value = member(**workspace_package_value, "authors"_str);
            if (workspace_authors_value.is_some()) {
                workspace_authors = Some(rstd_try(
                    parse_author_list(workspace_authors_value, "workspace.package.authors"_str)));
            }
            package_defaults.version = rstd::move(workspace_version).unwrap();
            package_defaults.license = rstd::move(workspace_license).unwrap();
            package_defaults.authors = rstd::move(workspace_authors);
        }
        auto workspace_dependencies =
            rstd_try(parse_workspace_dependencies(member(**workspace_value, "dependencies"_str)));
        auto workspace_external_sources = rstd_try(
            parse_workspace_external_sources(member(**workspace_value, "external-sources"_str)));
        auto external_dependencies = rstd_try(parse_workspace_external_dependencies(
            member(**workspace_value, "external-dependencies"_str)));
        auto profile = rstd_try(parse_project_profile(member(document, "profile"_str)));
        return Ok(ManifestDocument {
            .kind      = ManifestKind::Workspace,
            .workspace = Some(WorkspaceManifest {
                .name                             = rstd::move(workspace_name).unwrap(),
                .root                             = rstd::move(root),
                .manifest_path                    = rstd::move(path),
                .profile                          = rstd::move(profile),
                .members                          = rstd::move(members).unwrap(),
                .default_members                  = rstd::move(default_members).unwrap(),
                .package                          = rstd::move(package_defaults),
                .dependencies                     = rstd::move(workspace_dependencies),
                .external_sources                 = rstd::move(workspace_external_sources),
                .pkg_config_external_dependencies = rstd::move(external_dependencies.pkg_config),
                .cmake_external_dependencies      = rstd::move(external_dependencies.cmake),
                .cargo_external_dependencies      = rstd::move(external_dependencies.cargo),
            }),
        });
    }

    auto root_known = reject_unknown(**root_table, "manifest root"_str, package_root_key);
    if (root_known.is_err()) return Err(rstd::move(root_known).unwrap_err());
    auto package_table = required_table(document, "package"_str, "manifest"_str);
    if (package_table.is_err()) return Err(rstd::move(package_table).unwrap_err());
    auto package_known =
        reject_unknown(**package_table, "manifest.package"_str, manifest_package_key);
    if (package_known.is_err()) return Err(rstd::move(package_known).unwrap_err());

    const auto& package_value = **member(document, "package"_str);
    auto        name          = required_string(package_value, "name"_str, "package"_str);
    if (name.is_err()) return Err(rstd::move(name).unwrap_err());
    if (! package_name_is_valid(name->as_str())) {
        return manifest_schema_failure<ManifestDocument>(
            "package.name must contain only ASCII letters, digits, '-' or '_'"_str);
    }
    auto standard = rstd_try(parse_package_standard(package_value));
    auto target_language =
        standard.is_some() ? package_standard_language(*standard) : PackageLanguage::Cpp;
    auto library = rstd_try(parse_library_target(member(document, "lib"_str), target_language));
    auto bins    = rstd_try(parse_runnable_targets(member(document, "bin"_str),
                                                   lito::package::PackageTargetKind::Binary,
                                                   "bin"_str,
                                                   target_language));
    auto tests   = rstd_try(parse_runnable_targets(member(document, "test"_str),
                                                   lito::package::PackageTargetKind::Test,
                                                   "test"_str,
                                                   target_language));
    auto benches = rstd_try(parse_runnable_targets(member(document, "bench"_str),
                                                   lito::package::PackageTargetKind::Benchmark,
                                                   "bench"_str,
                                                   target_language));
    auto script  = rstd_try(
        parse_script_package(member(document, "script"_str), root.as_path(), embedded_source));

    auto compile_tests      = Vec<CompileTestCase>::make();
    auto compile_test_value = member(document, "compile-test"_str);
    if (compile_test_value.is_some()) {
        auto table = rstd_try(table_value(**compile_test_value, "manifest.compile-test"_str));
        rstd_try(reject_unknown(*table, "manifest.compile-test"_str, compile_test_key));
        compile_tests = rstd_try(parse_compile_tests(member(**compile_test_value, "cases"_str)));
    }

    auto source_root = embedded_source.is_some()
                           ? Ok(root.clone())
                           : resolve_package_source_root(package_value, root.as_path());
    if (source_root.is_err()) return Err(rstd::move(source_root).unwrap_err());
    auto install_script = embedded_source.is_some() ? Ok(Option<PathBuf> {})
                                                    : discover_install_script(root.as_path());
    if (install_script.is_err()) return Err(rstd::move(install_script).unwrap_err());

    const auto has_library = library.is_some();
    const auto has_bins    = ! bins.is_empty();
    auto       targets     = Vec<PackageTargetManifest>::with_capacity(
        (has_library ? usize(1) : usize {}) + bins.len() + tests.len() + benches.len());
    if (library.is_some()) targets.push(rstd::move(library).unwrap());
    for (auto& target : bins) targets.push(rstd::move(target));
    for (auto& target : tests) targets.push(rstd::move(target));
    for (auto& target : benches) targets.push(rstd::move(target));
    auto conventional =
        embedded_source.is_some()
            ? Ok(Vec<PackageTargetManifest>::make())
            : discover_conventional_benchmarks(root.as_path(), source_root->as_path(), targets);
    if (conventional.is_err()) return Err(rstd::move(conventional).unwrap_err());
    for (auto& target : *conventional) targets.push(rstd::move(target));
    const auto has_compile_contract = ! targets.is_empty() || ! compile_tests.is_empty();
    if (standard.is_none() && has_compile_contract) {
        standard = Some(PackageStandardRequirement::Cpp(CppStandard::Cpp20));
    }
    if (standard.is_some() && ! has_compile_contract) {
        return manifest_schema_failure<ManifestDocument>(
            "package.standard requires a compile target"_str);
    }
    if (standard.is_some() && package_standard_language(*standard) == PackageLanguage::C &&
        ! compile_tests.is_empty()) {
        return manifest_schema_failure<ManifestDocument>(
            "compile-test is currently only supported by C++ packages"_str);
    }
    if (script.is_some() && has_compile_contract) {
        return manifest_schema_failure<ManifestDocument>(
            "manifest.script cannot be combined with C or C++ targets"_str);
    }
    if (targets.is_empty() && compile_tests.is_empty() && install_script->is_none() &&
        script.is_none()) {
        return manifest_schema_failure<ManifestDocument>(
            "manifest must contain at least one of 'lib', 'bin', 'test', 'bench', or "
            "'compile-test', provide install.lua, or declare a script package"_str);
    }
    auto has_benches = false;
    for (const auto& target : targets) {
        if (target.is_Benchmark()) {
            has_benches = true;
            break;
        }
    }
    const auto version_optional = install_script->is_none() && ! has_library && ! has_bins &&
                                  ! has_benches && script.is_none();
    auto       version          = parse_package_version(package_value, version_optional);
    if (version.is_err()) return Err(rstd::move(version).unwrap_err());
    auto license = parse_package_license(package_value);
    if (license.is_err()) return Err(rstd::move(license).unwrap_err());
    auto authors = parse_package_authors(package_value);
    if (authors.is_err()) return Err(rstd::move(authors).unwrap_err());
    auto publish = parse_package_publish(package_value);
    if (publish.is_err()) return Err(rstd::move(publish).unwrap_err());

    auto usage = parse_usage(member(document, "usage"_str), source_root->as_path());
    auto conditions =
        parse_conditional_configurations(member(document, "when"_str), source_root->as_path());
    auto features         = parse_features(member(document, "features"_str));
    auto dependencies     = parse_dependencies(member(document, "dependencies"_str));
    auto dev_dependencies = parse_dependencies(member(document, "dev-dependencies"_str), true);
    auto runtime_dependencies =
        parse_runtime_dependencies(member(document, "runtime-dependencies"_str));
    auto build_tools   = parse_build_tools(member(document, "build-tools"_str));
    auto source_groups = parse_source_groups(member(document, "source-groups"_str));
    auto external_sources =
        parse_package_external_sources(member(document, "external-sources"_str), root.as_path());
    auto external = parse_external_dependencies(member(document, "external-dependencies"_str));
    auto target = parse_target_predicate(member(package_value, "target"_str), "package.target"_str);
    if (usage.is_err()) return Err(rstd::move(usage).unwrap_err());
    if (conditions.is_err()) return Err(rstd::move(conditions).unwrap_err());
    if (features.is_err()) return Err(rstd::move(features).unwrap_err());
    if (dependencies.is_err()) return Err(rstd::move(dependencies).unwrap_err());
    if (dev_dependencies.is_err()) return Err(rstd::move(dev_dependencies).unwrap_err());
    if (runtime_dependencies.is_err()) {
        return Err(rstd::move(runtime_dependencies).unwrap_err());
    }
    if (build_tools.is_err()) return Err(rstd::move(build_tools).unwrap_err());
    if (source_groups.is_err()) return Err(rstd::move(source_groups).unwrap_err());
    if (external_sources.is_err()) return Err(rstd::move(external_sources).unwrap_err());
    if (external.is_err()) return Err(rstd::move(external).unwrap_err());
    if (target.is_err()) return Err(rstd::move(target).unwrap_err());
    auto parsed_usage = rstd::move(usage).unwrap();
    if (! has_library && (! parsed_usage.public_include_directories.is_empty() ||
                          ! parsed_usage.public_include_directory_requirements.is_empty() ||
                          ! parsed_usage.public_definitions.is_empty())) {
        return manifest_schema_failure<ManifestDocument>(
            "usage.public-* requires a library target"_str);
    }
    auto parsed_dependencies         = rstd::move(dependencies).unwrap();
    auto parsed_dev_dependencies     = rstd::move(dev_dependencies).unwrap();
    auto parsed_runtime_dependencies = rstd::move(runtime_dependencies).unwrap();
    for (const auto& dependency : parsed_dev_dependencies.explicit_dependencies) {
        if (contains_dependency(parsed_dependencies, dependency.name.as_str())) {
            return manifest_schema_failure<ManifestDocument>(rstd::format(
                "dependency '{}' is declared in both dependencies and dev-dependencies",
                dependency.name.as_str()));
        }
    }
    for (const auto& dependency : parsed_dev_dependencies.workspace_dependencies) {
        if (contains_dependency(parsed_dependencies, dependency.name.as_str())) {
            return manifest_schema_failure<ManifestDocument>(rstd::format(
                "dependency '{}' is declared in both dependencies and dev-dependencies",
                dependency.name.as_str()));
        }
    }
    auto       external_dependencies   = rstd::move(external).unwrap();
    auto       parsed_external_sources = rstd::move(external_sources).unwrap();
    auto       parsed_source_groups    = rstd::move(source_groups).unwrap();
    const auto has_external_source     = [&](ref<str> name) {
        for (const auto& source : parsed_external_sources.explicit_sources) {
            if (source.name == name) return true;
        }
        for (const auto& source : parsed_external_sources.workspace_sources) {
            if (source.name == name) return true;
        }
        return false;
    };
    const auto validate_include_sources =
        [&](const Vec<lito::dependency::IncludeDirectoryRequirement>& requirements,
            ref<str> owner) -> ManifestSchemaResult<empty> {
        for (const auto& requirement : requirements) {
            if (requirement.root != lito::dependency::IncludeDirectoryRoot::ExternalSource)
                continue;
            if (requirement.external_source.is_none() ||
                ! has_external_source(requirement.external_source->as_str())) {
                return manifest_schema_failure<empty>(rstd::format(
                    "{} references unknown external source '{}'",
                    owner,
                    requirement.external_source.is_some() ? requirement.external_source->as_str()
                                                          : "<none>"_str));
            }
        }
        return Ok(empty {});
    };
    rstd_try(validate_include_sources(parsed_usage.public_include_directory_requirements,
                                      "usage.public-include-directories"_str));
    rstd_try(validate_include_sources(parsed_usage.private_include_directory_requirements,
                                      "usage.private-include-directories"_str));
    for (const auto& conditional : *conditions) {
        rstd_try(
            validate_include_sources(conditional.usage.values.public_include_directory_requirements,
                                     "conditional usage.public-include-directories"_str));
        rstd_try(validate_include_sources(
            conditional.usage.values.private_include_directory_requirements,
            "conditional usage.private-include-directories"_str));
    }
    for (const auto& group : parsed_source_groups) {
        if (group.external_source.is_some() &&
            ! has_external_source(group.external_source->as_str())) {
            return manifest_schema_failure<ManifestDocument>(
                rstd::format("source group '{}' references unknown external source '{}'",
                             group.name.as_str(),
                             group.external_source->as_str()));
        }
    }
    const auto has_source_group = [&](ref<str> name) {
        for (const auto& group : parsed_source_groups) {
            if (group.name == name) return true;
        }
        return false;
    };
    for (const auto& manifest_target : targets) {
        const auto& target_source = package_target_source(manifest_target);
        for (const auto& group : target_source.source_groups) {
            if (! has_source_group(group.as_str())) {
                return manifest_schema_failure<ManifestDocument>(
                    rstd::format("target '{}::{}' references unknown source group '{}'",
                                 name->as_str(),
                                 package_target_name(manifest_target),
                                 group.as_str()));
            }
        }
        for (const auto& conditional : target_source.conditions) {
            for (const auto& group : conditional.source_groups) {
                if (! has_source_group(group.as_str())) {
                    return manifest_schema_failure<ManifestDocument>(rstd::format(
                        "target '{}::{}' condition '{}' references unknown source group '{}'",
                        name->as_str(),
                        package_target_name(manifest_target),
                        conditional.source.as_str(),
                        group.as_str()));
                }
            }
        }
    }
    for (const auto& dependency : external_dependencies.cmake) {
        if (dependency.source.is_none()) continue;
        auto found = false;
        for (const auto& source : parsed_external_sources.explicit_sources) {
            if (source.name == dependency.source->as_str()) found = true;
        }
        for (const auto& source : parsed_external_sources.workspace_sources) {
            if (source.name == dependency.source->as_str()) found = true;
        }
        if (! found) {
            return manifest_schema_failure<ManifestDocument>(rstd::format(
                "CMake external dependency '{}' references unknown external source '{}'",
                dependency.alias.as_str(),
                dependency.source->as_str()));
        }
    }
    for (auto& dependency : external_dependencies.cargo) {
        if (! has_external_source(dependency.recipe.source.as_str())) {
            return manifest_schema_failure<ManifestDocument>(rstd::format(
                "Cargo external dependency '{}' references unknown external source '{}'",
                dependency.alias.as_str(),
                dependency.recipe.source.as_str()));
        }
        dependency.declaration_root = Some(root.clone());
    }
    auto profile = rstd_try(parse_project_profile(member(document, "profile"_str)));

    return Ok(ManifestDocument {
        .kind    = ManifestKind::Package,
        .package = Some(PackageManifest {
            .name                       = rstd::move(name).unwrap(),
            .version                    = rstd::move(version).unwrap(),
            .license                    = rstd::move(license).unwrap(),
            .authors                    = rstd::move(authors).unwrap(),
            .publish                    = rstd::move(publish).unwrap(),
            .standard                   = rstd::move(standard),
            .root                       = root.clone(),
            .source_root                = rstd::move(source_root).unwrap(),
            .manifest_path              = rstd::move(path),
            .install_script             = rstd::move(install_script).unwrap(),
            .profile                    = rstd::move(profile),
            .build_tools                = rstd::move(build_tools).unwrap(),
            .script                     = rstd::move(script),
            .external_sources           = rstd::move(parsed_external_sources.explicit_sources),
            .workspace_external_sources = rstd::move(parsed_external_sources.workspace_sources),
            .source_groups              = rstd::move(parsed_source_groups),
            .targets                    = rstd::move(targets),
            .target                     = rstd::move(target).unwrap(),
            .compile_tests              = rstd::move(compile_tests),
            .usage                      = rstd::move(parsed_usage),
            .conditions                 = rstd::move(conditions).unwrap(),
            .features                   = rstd::move(features).unwrap(),
            .dependencies               = rstd::move(parsed_dependencies.explicit_dependencies),
            .dev_dependencies           = rstd::move(parsed_dev_dependencies.explicit_dependencies),
            .runtime_dependencies   = rstd::move(parsed_runtime_dependencies.explicit_dependencies),
            .workspace_dependencies = rstd::move(parsed_dependencies.workspace_dependencies),
            .workspace_dev_dependencies =
                rstd::move(parsed_dev_dependencies.workspace_dependencies),
            .workspace_runtime_dependencies =
                rstd::move(parsed_runtime_dependencies.workspace_dependencies),
            .pkg_config_external_dependencies = rstd::move(external_dependencies.pkg_config),
            .workspace_pkg_config_external_dependencies =
                rstd::move(external_dependencies.workspace_pkg_config),
            .cmake_external_dependencies = rstd::move(external_dependencies.cmake),
            .workspace_cmake_external_dependencies =
                rstd::move(external_dependencies.workspace_cmake),
            .cargo_external_dependencies = rstd::move(external_dependencies.cargo),
            .workspace_cargo_external_dependencies =
                rstd::move(external_dependencies.workspace_cargo),
        }),
    });
}

auto lito::manifest::add_registry_dependency(ref<rstd::path::Path> requested_directory,
                                             const lito::registry::RegistryPackageName& package,
                                             const lito::registry::VersionRequirement&  requirement,
                                             Option<String>                             registry)
    -> ManifestEditResult<ManifestDependencyEdit> {
    auto located = locate_manifest(requested_directory);
    if (located.is_err()) {
        return manifest_edit_failure<ManifestDependencyEdit>(
            requested_directory, rstd::format("{}", rstd::move(located).unwrap_err()));
    }
    auto location = rstd::move(located).unwrap();
    auto root     = rstd::move(location.directory);
    auto path     = rstd::move(location.manifest);
    auto contents = rstd::fs::read_to_string(path.as_path());
    if (contents.is_err()) {
        return manifest_edit_failure<ManifestDependencyEdit>(
            path.as_path(),
            rstd::format("cannot read file: {}", rstd::move(contents).unwrap_err()));
    }
    auto parsed = rstd::toml::from_str(contents->as_str());
    if (parsed.is_err()) {
        return manifest_edit_failure<ManifestDependencyEdit>(
            path.as_path(), rstd::format("cannot parse TOML: {}", rstd::move(parsed).unwrap_err()));
    }
    auto document = rstd::move(parsed).unwrap();
    auto loaded   = assemble_manifest_document(root.clone(), path.clone(), document.clone());
    if (loaded.is_err()) {
        return manifest_edit_failure<ManifestDependencyEdit>(
            path.as_path(), rstd::format("{}", rstd::move(loaded).unwrap_err()));
    }
    auto kind       = loaded->kind;
    auto root_table = document.as_table_mut();
    if (root_table.is_none()) {
        return manifest_edit_failure<ManifestDependencyEdit>(path.as_path(),
                                                             "manifest root must be a table"_str);
    }
    auto owner = *root_table;
    if (kind == ManifestKind::Workspace) {
        auto workspace = owner->get_mut("workspace"_str);
        if (workspace.is_none() || (**workspace).as_table_mut().is_none()) {
            return manifest_edit_failure<ManifestDependencyEdit>(
                path.as_path(), "workspace manifest has no workspace table"_str);
        }
        owner = (**workspace).as_table_mut().unwrap();
    }
    auto dependencies = owner->get_mut("dependencies"_str);
    if (dependencies.is_none()) {
        owner->insert(String::make("dependencies"_str), Toml::Table(Table::make()));
        dependencies = owner->get_mut("dependencies"_str);
    }
    auto dependency_table = (**dependencies).as_table_mut();
    if (dependency_table.is_none()) {
        return manifest_edit_failure<ManifestDependencyEdit>(path.as_path(),
                                                             "dependencies must be a table"_str);
    }
    auto dependency = (**dependency_table).get_mut(package.as_str());
    if (dependency.is_none()) {
        (**dependency_table).insert(String::make(package.as_str()), Toml::Table(Table::make()));
        dependency = (**dependency_table).get_mut(package.as_str());
    }
    auto fields = (**dependency).as_table_mut();
    if (fields.is_none()) {
        return manifest_edit_failure<ManifestDependencyEdit>(path.as_path(),
                                                             "dependency must be a table"_str);
    }
    constexpr ref<str> source_keys[] = {
        "path"_str,   "git"_str,     "branch"_str,    "tag"_str,     "rev"_str,
        "commit"_str, "builtin"_str, "workspace"_str, "version"_str, "registry"_str,
    };
    for (auto key : source_keys) (void)(**fields).remove(key);
    (**fields).insert(String::make("version"_str), Toml::String(String::make(requirement.text())));
    if (registry.is_some()) {
        if (registry->is_empty()) {
            return manifest_edit_failure<ManifestDependencyEdit>(
                path.as_path(), "Registry name must not be empty"_str);
        }
        (**fields).insert(String::make("registry"_str),
                          Toml::String(rstd::move(registry).unwrap()));
    }
    auto validated = assemble_manifest_document(rstd::move(root), path.clone(), document.clone());
    if (validated.is_err()) {
        return manifest_edit_failure<ManifestDependencyEdit>(
            path.as_path(),
            rstd::format("edited manifest is invalid: {}", rstd::move(validated).unwrap_err()));
    }
    auto serialized = rstd::toml::to_string(document);
    if (serialized.is_err()) {
        return manifest_edit_failure<ManifestDependencyEdit>(
            path.as_path(),
            rstd::format("cannot serialize TOML: {}", rstd::move(serialized).unwrap_err()));
    }
    auto output = rstd::move(serialized).unwrap();
    if (! output.as_str().ends_with("\n"_str)) output.push_ascii(u8('\n'));
    auto written = rstd::fs::write_atomic(path.as_path(), output.as_str().as_bytes());
    if (written.is_err()) {
        return manifest_edit_failure<ManifestDependencyEdit>(
            path.as_path(),
            rstd::format("cannot write file: {}", rstd::move(written).unwrap_err()));
    }
    return Ok(ManifestDependencyEdit {
        .path    = rstd::move(path),
        .package = String::make(package.as_str()),
    });
}

auto lito::manifest::load_manifest_document(ref<rstd::path::Path> requested_directory)
    -> ManifestResult<ManifestDocument> {
    auto located = locate_manifest(requested_directory);
    if (located.is_err()) return Err(rstd::into<ManifestError>(rstd::move(located).unwrap_err()));
    auto location = rstd::move(located).unwrap();
    auto path     = rstd::move(location.manifest);
    auto root     = rstd::move(location.directory);
    auto contents = rstd::fs::read_to_string(path.as_path());
    if (contents.is_err()) {
        return Err(ManifestError::File(ManifestFileError {
            .path  = path.clone(),
            .cause = ManifestFileCause::Read(rstd::move(contents).unwrap_err()),
        }));
    }
    auto parsed = rstd::toml::from_str(contents->as_str());
    if (parsed.is_err()) {
        return Err(ManifestError::File(ManifestFileError {
            .path  = path.clone(),
            .cause = ManifestFileCause::Parse(rstd::move(parsed).unwrap_err()),
        }));
    }
    auto assembled =
        assemble_manifest_document(rstd::move(root), path.clone(), rstd::move(parsed).unwrap());
    if (assembled.is_err()) {
        return Err(ManifestError::File(ManifestFileError {
            .path  = rstd::move(path),
            .cause = ManifestFileCause::Schema(rstd::move(assembled).unwrap_err()),
        }));
    }
    return Ok(rstd::move(assembled).unwrap());
}

auto lito::manifest::load_package_manifest_from_source_tree_at(ref<str> source_identity,
                                                               ref<rstd::path::Path> requested_root,
                                                               const lito::source::SourceTree& tree)
    -> ManifestResult<PackageManifest> {
    if (source_identity.is_empty()) {
        auto path = PathBuf::from("builtin/lito.toml"_str);
        return Err(ManifestError::File(ManifestFileError {
            .path  = rstd::move(path),
            .cause = ManifestFileCause::Schema(ManifestSchemaError::Domain(
                String::make("builtin source identity must not be empty"_str))),
        }));
    }
    const lito::source::SourceTreeEntry* manifest_entry = nullptr;
    for (const auto& entry : tree.entries()) {
        if (entry.path().as_str() == "lito.toml"_str &&
            entry.kind() == lito::source::SourceEntryKind::File) {
            manifest_entry = rstd::addressof(entry);
            break;
        }
    }
    auto root = PathBuf::from(requested_root);
    auto path = root.join(PathBuf::from("lito.toml"_str).as_path());
    if (manifest_entry == nullptr) {
        return Err(ManifestError::File(ManifestFileError {
            .path  = rstd::move(path),
            .cause = ManifestFileCause::Schema(
                ManifestSchemaError::Parse(lito::parse::Error::MissingField(
                    lito::parse::NodePath::root("builtin package source"_str),
                    String::make("lito.toml"_str)))),
        }));
    }
    auto decoded = String::from_utf8(Vec<u8>::from(manifest_entry->contents()));
    if (decoded.is_err()) {
        return Err(ManifestError::File(ManifestFileError {
            .path  = rstd::move(path),
            .cause = ManifestFileCause::Utf8(rstd::move(decoded).unwrap_err()),
        }));
    }
    auto parsed = rstd::toml::from_str(decoded->as_str());
    if (parsed.is_err()) {
        return Err(ManifestError::File(ManifestFileError {
            .path  = rstd::move(path),
            .cause = ManifestFileCause::Parse(rstd::move(parsed).unwrap_err()),
        }));
    }
    auto tree_ref  = ref<lito::source::SourceTree>::from_raw_parts(rstd::addressof(tree));
    auto assembled = assemble_manifest_document(
        rstd::move(root), path.clone(), rstd::move(parsed).unwrap(), Some(tree_ref));
    if (assembled.is_err()) {
        return Err(ManifestError::File(ManifestFileError {
            .path  = rstd::move(path),
            .cause = ManifestFileCause::Schema(rstd::move(assembled).unwrap_err()),
        }));
    }
    auto document = rstd::move(assembled).unwrap();
    if (document.kind != ManifestKind::Package || document.package.is_none()) {
        return Err(ManifestError::Kind(
            PathBuf::from("builtin"_str), ManifestKind::Package, document.kind));
    }
    return Ok(rstd::move(document.package).unwrap());
}

auto lito::manifest::load_package_manifest_from_source_tree(ref<str> source_identity,
                                                            const lito::source::SourceTree& tree)
    -> ManifestResult<PackageManifest> {
    auto root = PathBuf::from(rstd::format("builtin/{}", source_identity));
    return load_package_manifest_from_source_tree_at(source_identity, root.as_path(), tree);
}

auto lito::manifest::load_package_manifest(ref<rstd::path::Path> requested_directory)
    -> ManifestResult<PackageManifest> {
    auto loaded = load_manifest_document(requested_directory);
    if (loaded.is_err()) return Err(rstd::move(loaded).unwrap_err());
    auto document = rstd::move(loaded).unwrap();
    if (document.kind != ManifestKind::Package || document.package.is_none()) {
        return Err(ManifestError::Kind(
            PathBuf::from(requested_directory), ManifestKind::Package, document.kind));
    }
    return Ok(rstd::move(document.package).unwrap());
}
