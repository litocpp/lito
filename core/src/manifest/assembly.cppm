module;
#include <rstd/macro.hpp>

export module lito.manifest:assembly;

import rstd;
import rstd.toml;
import lito.error;
import lito.manifest.contract;
import lito.package.identity;
import lito.dependency.contract;
import lito.source.contract;
import lito.platform;
import :locator;
import :primitives;
import :profile_schema;
import :key_schema;
import :convention;
import :target_schema;
import :dependency_schema;

using namespace rstd::prelude;
using namespace rstd::literals;
using Toml = rstd::toml::Value;

export namespace lito
{

auto valid_package_name(ref<str> value) -> bool {
    return package_name_is_valid(value);
}

} // namespace lito

namespace lito
{

auto assemble_manifest_document(PathBuf root, PathBuf path, Toml document)
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
            return failure<ManifestDocument>(
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
                return failure<ManifestDocument>("workspace.package.version must not be empty"_str);
            }
            package_defaults.version = rstd::move(workspace_version).unwrap();
        }
        auto workspace_dependencies =
            rstd_try(parse_workspace_dependencies(member(**workspace_value, "dependencies"_str)));
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
                .pkg_config_external_dependencies = rstd::move(external_dependencies.pkg_config),
                .cmake_external_dependencies      = rstd::move(external_dependencies.cmake),
            }),
        });
    }

    auto root_known = reject_unknown(**root_table, "manifest root"_str, package_root_key);
    if (root_known.is_err()) return Err(rstd::move(root_known).unwrap_err());
    auto package_table = required_table(document, "package"_str, "manifest"_str);
    if (package_table.is_err()) return Err(rstd::move(package_table).unwrap_err());
    auto package_known = reject_unknown(**package_table, "manifest.package"_str, package_key);
    if (package_known.is_err()) return Err(rstd::move(package_known).unwrap_err());

    const auto& package_value = **member(document, "package"_str);
    auto        name          = required_string(package_value, "name"_str, "package"_str);
    if (name.is_err()) return Err(rstd::move(name).unwrap_err());
    if (! package_name_is_valid(name->as_str())) {
        return failure<ManifestDocument>(
            "package.name must contain only ASCII letters, digits, '-' or '_'"_str);
    }
    auto library = rstd_try(parse_library_target(member(document, "lib"_str)));
    auto bins    = rstd_try(
        parse_runnable_targets(member(document, "bin"_str), PackageTargetKind::Binary, "bin"_str));
    auto tests = rstd_try(
        parse_runnable_targets(member(document, "test"_str), PackageTargetKind::Test, "test"_str));
    auto benches = rstd_try(parse_runnable_targets(
        member(document, "bench"_str), PackageTargetKind::Benchmark, "bench"_str));

    auto compile_tests      = Vec<CompileTestCase>::make();
    auto compile_test_value = member(document, "compile-test"_str);
    if (compile_test_value.is_some()) {
        auto table = rstd_try(table_value(**compile_test_value, "manifest.compile-test"_str));
        rstd_try(reject_unknown(*table, "manifest.compile-test"_str, compile_test_key));
        compile_tests = rstd_try(parse_compile_tests(member(**compile_test_value, "cases"_str)));
    }

    auto source_root = resolve_package_source_root(package_value, root.as_path());
    if (source_root.is_err()) return Err(rstd::move(source_root).unwrap_err());
    auto install_script = discover_install_script(root.as_path());
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
        discover_conventional_benchmarks(root.as_path(), source_root->as_path(), targets);
    if (conventional.is_err()) return Err(rstd::move(conventional).unwrap_err());
    for (auto& target : *conventional) targets.push(rstd::move(target));
    if (targets.is_empty() && compile_tests.is_empty() && install_script->is_none()) {
        return failure<ManifestDocument>(
            "manifest must contain at least one of 'lib', 'bin', 'test', 'bench', or "
            "'compile-test', or provide install.lua"_str);
    }
    auto has_benches = false;
    for (const auto& target : targets) {
        if (target.is_Benchmark()) {
            has_benches = true;
            break;
        }
    }
    const auto version_optional = install_script->is_none() &&
                                  ! has_library && ! has_bins && ! has_benches;
    auto       version          = parse_package_version(package_value, version_optional);
    if (version.is_err()) return Err(rstd::move(version).unwrap_err());

    auto usage            = parse_usage(member(document, "usage"_str), source_root->as_path());
    auto dependencies     = parse_dependencies(member(document, "dependencies"_str));
    auto dev_dependencies = parse_dependencies(member(document, "dev-dependencies"_str), true);
    auto external = parse_external_dependencies(member(document, "external-dependencies"_str));
    auto target = parse_target_predicate(member(package_value, "target"_str), "package.target"_str);
    if (usage.is_err()) return Err(rstd::move(usage).unwrap_err());
    if (dependencies.is_err()) return Err(rstd::move(dependencies).unwrap_err());
    if (dev_dependencies.is_err()) return Err(rstd::move(dev_dependencies).unwrap_err());
    if (external.is_err()) return Err(rstd::move(external).unwrap_err());
    if (target.is_err()) return Err(rstd::move(target).unwrap_err());
    auto parsed_usage = rstd::move(usage).unwrap();
    if (! has_library && (! parsed_usage.public_include_directories.is_empty() ||
                          ! parsed_usage.public_definitions.is_empty() ||
                          ! parsed_usage.public_options.is_empty())) {
        return failure<ManifestDocument>("usage.public-* requires a library target"_str);
    }
    auto parsed_dependencies     = rstd::move(dependencies).unwrap();
    auto parsed_dev_dependencies = rstd::move(dev_dependencies).unwrap();
    for (const auto& dependency : parsed_dev_dependencies.explicit_dependencies) {
        if (contains_dependency(parsed_dependencies, dependency.name.as_str())) {
            return failure<ManifestDocument>(rstd::format(
                "dependency '{}' is declared in both dependencies and dev-dependencies",
                dependency.name.as_str()));
        }
    }
    for (const auto& dependency : parsed_dev_dependencies.workspace_dependencies) {
        if (contains_dependency(parsed_dependencies, dependency.name.as_str())) {
            return failure<ManifestDocument>(rstd::format(
                "dependency '{}' is declared in both dependencies and dev-dependencies",
                dependency.name.as_str()));
        }
    }
    auto external_dependencies = rstd::move(external).unwrap();
    auto profile               = rstd_try(parse_project_profile(member(document, "profile"_str)));

    return Ok(ManifestDocument {
        .kind    = ManifestKind::Package,
        .package = Some(PackageManifest {
            .name                   = rstd::move(name).unwrap(),
            .version                = rstd::move(version).unwrap(),
            .root                   = root.clone(),
            .source_root            = rstd::move(source_root).unwrap(),
            .manifest_path          = rstd::move(path),
            .install_script         = rstd::move(install_script).unwrap(),
            .profile                = rstd::move(profile),
            .targets                = rstd::move(targets),
            .target                 = rstd::move(target).unwrap(),
            .compile_tests          = rstd::move(compile_tests),
            .usage                  = rstd::move(parsed_usage),
            .dependencies           = rstd::move(parsed_dependencies.explicit_dependencies),
            .dev_dependencies       = rstd::move(parsed_dev_dependencies.explicit_dependencies),
            .workspace_dependencies = rstd::move(parsed_dependencies.workspace_dependencies),
            .workspace_dev_dependencies =
                rstd::move(parsed_dev_dependencies.workspace_dependencies),
            .pkg_config_external_dependencies = rstd::move(external_dependencies.pkg_config),
            .workspace_pkg_config_external_dependencies =
                rstd::move(external_dependencies.workspace_pkg_config),
            .cmake_external_dependencies = rstd::move(external_dependencies.cmake),
            .workspace_cmake_external_dependencies =
                rstd::move(external_dependencies.workspace_cmake),
        }),
    });
}

} // namespace lito

export namespace lito
{

auto load_manifest_document(ref<rstd::path::Path> requested_directory)
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
    auto assembled = assemble_manifest_document(
        rstd::move(root), path.clone(), rstd::move(parsed).unwrap());
    if (assembled.is_err()) {
        return Err(ManifestError::File(ManifestFileError {
            .path  = rstd::move(path),
            .cause = ManifestFileCause::Schema(rstd::move(assembled).unwrap_err()),
        }));
    }
    return Ok(rstd::move(assembled).unwrap());
}

auto load_package_manifest(ref<rstd::path::Path> requested_directory)
    -> ManifestResult<PackageManifest> {
    auto loaded = load_manifest_document(requested_directory);
    if (loaded.is_err()) return Err(rstd::move(loaded).unwrap_err());
    auto document = rstd::move(loaded).unwrap();
    if (document.kind != ManifestKind::Package || document.package.is_none()) {
        return Err(ManifestError::Kind(PathBuf::from(requested_directory),
                                       ManifestKind::Package,
                                       document.kind));
    }
    return Ok(rstd::move(document.package).unwrap());
}

} // namespace lito
