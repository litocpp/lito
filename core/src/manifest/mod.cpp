module;
#include <rstd/macro.hpp>

module lito.core;

import rstd;
import rstd.toml;
import rstd.serde;
import :manifest.wire.common;
import :manifest.package_schema;
import :manifest;
import :package.identity;
import lito.system;
import :manifest.primitives;
import :manifest.profile_schema;
import :manifest.wire.document;
import :manifest.convention;
import :manifest.target_schema;
import :manifest.dependency_schema;
import :manifest.build_tool_schema;
import :manifest.build_script_schema;
import :manifest.initialize;
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
auto project_initialization_failure(ref<rstd::path::Path> path, String message)
    -> lito::manifest::ProjectInitializationResult<T> {
    return Err(lito::manifest::ProjectInitializationError {
        .path    = PathBuf::from(path),
        .message = rstd::move(message),
    });
}

template<typename T>
auto project_initialization_failure(ref<rstd::path::Path> path, ref<str> message)
    -> lito::manifest::ProjectInitializationResult<T> {
    return project_initialization_failure<T>(path, String::make(message));
}

auto inferred_project_name(ref<rstd::path::Path> directory)
    -> lito::manifest::ProjectInitializationResult<String> {
    auto named_path = PathBuf::from(directory);
    if (named_path.as_path().file_name().is_none()) {
        auto canonical = rstd::fs::canonicalize(directory);
        if (canonical.is_err()) {
            return project_initialization_failure<String>(
                directory,
                rstd::format("cannot infer package name: {}", rstd::move(canonical).unwrap_err()));
        }
        named_path = rstd::move(canonical).unwrap();
    }
    auto name = named_path.as_path().file_name();
    if (name.is_none() || name->to_str().is_none()) {
        return project_initialization_failure<String>(
            directory, "directory name must be valid UTF-8; provide --name explicitly"_str);
    }
    return Ok(String::make(*name->to_str()));
}

auto checked_project_name(ref<rstd::path::Path> directory, Option<String> requested)
    -> lito::manifest::ProjectInitializationResult<String> {
    auto name = requested.is_some() ? rstd::move(requested).unwrap()
                                    : rstd_try(inferred_project_name(directory));
    if (! package_name_is_valid(name.as_str())) {
        return project_initialization_failure<String>(
            directory,
            rstd::format("package name '{}' must contain only ASCII letters, digits, '-' or '_'",
                         name.as_str()));
    }
    return Ok(rstd::move(name));
}

auto ensure_initialization_destination(ref<rstd::path::Path> directory)
    -> lito::manifest::ProjectInitializationResult<bool> {
    auto exists = rstd::fs::exists(directory);
    if (exists.is_err()) {
        return project_initialization_failure<bool>(
            directory,
            rstd::format("cannot inspect destination: {}", rstd::move(exists).unwrap_err()));
    }
    if (! *exists) return Ok(false);
    auto metadata = rstd::fs::metadata(directory);
    if (metadata.is_err()) {
        return project_initialization_failure<bool>(
            directory,
            rstd::format("cannot inspect destination: {}", rstd::move(metadata).unwrap_err()));
    }
    if (! metadata->is_dir()) {
        return project_initialization_failure<bool>(directory,
                                                    "destination is not a directory"_str);
    }
    auto opened = rstd::fs::read_dir(directory);
    if (opened.is_err()) {
        return project_initialization_failure<bool>(
            directory,
            rstd::format("cannot enumerate destination: {}", rstd::move(opened).unwrap_err()));
    }
    auto entries = rstd::move(opened).unwrap();
    auto first   = entries.next();
    if (first.is_some()) {
        if (first->is_err()) {
            return project_initialization_failure<bool>(
                directory,
                rstd::format("cannot enumerate destination: {}", rstd::move(*first).unwrap_err()));
        }
        return project_initialization_failure<bool>(directory, "destination is not empty"_str);
    }
    return Ok(true);
}

auto write_new_project_file(ref<rstd::path::Path> path, ref<str> contents)
    -> lito::manifest::ProjectInitializationResult<empty> {
    auto file = rstd::fs::File::create_new(path);
    if (file.is_err()) {
        return project_initialization_failure<empty>(
            path, rstd::format("cannot create file: {}", rstd::move(file).unwrap_err()));
    }
    auto written = rstd::move(file).unwrap().write_all(contents.as_bytes());
    if (written.is_err()) {
        (void)rstd::fs::remove_file(path);
        return project_initialization_failure<empty>(
            path, rstd::format("cannot write file: {}", rstd::move(written).unwrap_err()));
    }
    return Ok(empty {});
}

auto cleanup_failed_initialization(ref<rstd::path::Path> root,
                                   ref<rstd::path::Path> manifest,
                                   ref<rstd::path::Path> source_directory,
                                   bool                  created_root,
                                   bool                  created_manifest) noexcept -> void {
    if (created_manifest) (void)rstd::fs::remove_file(manifest);
    (void)rstd::fs::remove_dir(source_directory);
    if (created_root) (void)rstd::fs::remove_dir(root);
}

auto lito::manifest::initialize_project(ref<rstd::path::Path> directory, Option<String> package)
    -> ProjectInitializationResult<ProjectInitialization> {
    auto           name          = rstd_try(checked_project_name(directory, rstd::move(package)));
    auto           root_existed  = rstd_try(ensure_initialization_destination(directory));
    auto           root          = PathBuf::from(directory);
    auto           manifest      = root.join(PathBuf::from("lito.toml"_str).as_path());
    auto           source_folder = root.join(PathBuf::from("src"_str).as_path());
    auto           source        = source_folder.join(PathBuf::from("main.cpp"_str).as_path());
    auto           manifest_text = rstd::format("[package]\n"
                                                "name = \"{}\"\n"
                                                "version = \"0.1.0\"\n"
                                                "\n"
                                                "[[bin]]\n"
                                                "name = \"{}\"\n"
                                                "sources = [\"src/main.cpp\"]\n",
                                                name.as_str(),
                                                name.as_str());
    constexpr auto source_text   = "#include <cstdio>\n"
                                   "\n"
                                   "auto main() -> int {\n"
                                   "    std::puts(\"Hello, world!\");\n"
                                   "    return 0;\n"
                                   "}\n"_str;

    if (! root_existed) {
        auto created = rstd::fs::create_dir_all(root.as_path());
        if (created.is_err()) {
            return project_initialization_failure<ProjectInitialization>(
                root.as_path(),
                rstd::format("cannot create destination: {}", rstd::move(created).unwrap_err()));
        }
    }
    auto created_source_directory = rstd::fs::create_dir(source_folder.as_path());
    if (created_source_directory.is_err()) {
        if (! root_existed) (void)rstd::fs::remove_dir(root.as_path());
        return project_initialization_failure<ProjectInitialization>(
            source_folder.as_path(),
            rstd::format("cannot create source directory: {}",
                         rstd::move(created_source_directory).unwrap_err()));
    }
    auto manifest_written = write_new_project_file(manifest.as_path(), manifest_text.as_str());
    if (manifest_written.is_err()) {
        auto error = rstd::move(manifest_written).unwrap_err();
        cleanup_failed_initialization(
            root.as_path(), manifest.as_path(), source_folder.as_path(), ! root_existed, false);
        return Err(rstd::move(error));
    }
    auto source_written = write_new_project_file(source.as_path(), source_text);
    if (source_written.is_err()) {
        auto error = rstd::move(source_written).unwrap_err();
        cleanup_failed_initialization(
            root.as_path(), manifest.as_path(), source_folder.as_path(), ! root_existed, true);
        return Err(rstd::move(error));
    }
    return Ok(ProjectInitialization {
        .root    = rstd::move(root),
        .package = rstd::move(name),
    });
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
    auto decode = [&]<bool Embedded>() -> ManifestSchemaResult<wire::Document> {
        auto input = rstd_try(decode_manifest_value<wire::DocumentInput<Embedded>>(
            document, rstd::serde::DataPath()));
        return Ok(rstd::move(input.value));
    };
    auto input = rstd_try(embedded_source.is_some() ? decode.template operator()<true>()
                                                    : decode.template operator()<false>());
    if (input.workspace.is_some()) {
        auto workspace_value = rstd::move(input.workspace).unwrap();
        auto workspace_name  = rstd::move(workspace_value.name);
        if (! package_name_is_valid(workspace_name.as_str())) {
            return manifest_schema_failure<ManifestDocument>(
                "workspace.name must contain only ASCII letters, digits, '-' or '_'"_str);
        }
        auto members = declared_paths(
            Some(rstd::move(workspace_value.members)), "workspace.members"_str, true);
        const auto has_default_members  = workspace_value.default_members.is_some();
        auto       default_member_value = rstd::move(workspace_value.default_members);
        auto       default_members      = declared_paths(
            rstd::move(default_member_value), "workspace.default-members"_str, has_default_members);
        if (members.is_err()) return Err(rstd::move(members).unwrap_err());
        if (default_members.is_err()) {
            return Err(rstd::move(default_members).unwrap_err());
        }
        auto package_defaults        = WorkspacePackageDefaults {};
        auto workspace_package_value = rstd::move(workspace_value.package);
        if (workspace_package_value.is_some()) {
            auto       defaults          = rstd::move(workspace_package_value).unwrap();
            const auto require_non_empty = [](const Option<String>& value,
                                              ref<str> context) -> ManifestSchemaResult<empty> {
                if (value.is_some() && value->is_empty())
                    return manifest_schema_failure<empty>(
                        rstd::format("{} must not be empty", context));
                return Ok(empty {});
            };
            rstd_try(require_non_empty(defaults.version, "workspace.package.version"_str));
            rstd_try(require_non_empty(defaults.license, "workspace.package.license"_str));
            rstd_try(require_non_empty(defaults.description, "workspace.package.description"_str));
            rstd_try(require_non_empty(defaults.repository, "workspace.package.repository"_str));
            rstd_try(
                require_non_empty(defaults.documentation, "workspace.package.documentation"_str));
            if (defaults.authors.is_some())
                package_defaults.authors = Some(rstd_try(parse_author_list(
                    rstd::move(defaults.authors).unwrap(), "workspace.package.authors"_str)));
            package_defaults.readme = rstd_try(
                parse_workspace_package_readme(rstd::move(defaults.readme), root.as_path()));
            package_defaults.version       = rstd::move(defaults.version);
            package_defaults.license       = rstd::move(defaults.license);
            package_defaults.description   = rstd::move(defaults.description);
            package_defaults.repository    = rstd::move(defaults.repository);
            package_defaults.documentation = rstd::move(defaults.documentation);
        }
        auto workspace_dependencies =
            rstd_try(parse_workspace_dependencies(rstd::move(workspace_value.dependencies)));
        auto workspace_external_sources = rstd_try(
            parse_workspace_external_sources(rstd::move(workspace_value.external_sources)));
        auto external_dependencies = rstd_try(parse_workspace_external_dependencies(
            rstd::move(workspace_value.external_dependencies)));
        auto profile               = rstd_try(parse_project_profile(rstd::move(input.profile)));
        return Ok(ManifestDocument {
            .kind      = ManifestKind::Workspace,
            .workspace = Some(WorkspaceManifest {
                .name                             = rstd::move(workspace_name),
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

    auto package_value = rstd::move(input.package).unwrap();
    auto name          = rstd::move(package_value.name);
    if (! package_name_is_valid(name.as_str())) {
        return manifest_schema_failure<ManifestDocument>(
            "package.name must contain only ASCII letters, digits, '-' or '_'"_str);
    }
    auto standard = rstd_try(parse_package_standard(rstd::move(package_value.standard)));
    auto target_language =
        standard.is_some() ? package_standard_language(*standard) : PackageLanguage::Cpp;
    auto library = rstd_try(parse_library_target(rstd::move(input.lib), target_language));
    auto plugin =
        rstd_try(parse_plugin_target(rstd::move(input.plugin), name.as_str(), target_language));
    auto proc_macro =
        rstd_try(parse_pmacro_target(rstd::move(input.pmacro), name.as_str(), target_language));
    auto bins    = rstd_try(parse_runnable_targets(rstd::move(input.bin),
                                                   lito::package::PackageTargetKind::Binary,
                                                   "bin"_str,
                                                   target_language));
    auto tests   = rstd_try(parse_runnable_targets(rstd::move(input.test),
                                                   lito::package::PackageTargetKind::Test,
                                                   "test"_str,
                                                   target_language));
    auto benches = rstd_try(parse_runnable_targets(rstd::move(input.bench),
                                                   lito::package::PackageTargetKind::Benchmark,
                                                   "bench"_str,
                                                   target_language));
    auto script =
        rstd_try(parse_script_package(rstd::move(input.script), root.as_path(), embedded_source));

    auto compile_tests = Vec<CompileTestCase>::make();
    if (input.compile_test.is_some())
        compile_tests = rstd_try(parse_compile_tests(rstd::move(input.compile_test->cases)));

    auto source_root =
        embedded_source.is_some()
            ? Ok(root.clone())
            : resolve_package_source_root(rstd::move(package_value.source_root), root.as_path());
    if (source_root.is_err()) return Err(rstd::move(source_root).unwrap_err());
    auto install_script = embedded_source.is_some() ? Ok(Option<PathBuf> {})
                                                    : discover_install_script(root.as_path());
    if (install_script.is_err()) return Err(rstd::move(install_script).unwrap_err());

    const auto compile_contracts = (library.is_some() ? usize(1) : usize {}) +
                                   (plugin.is_some() ? usize(1) : usize {}) +
                                   (proc_macro.is_some() ? usize(1) : usize {});
    if (compile_contracts > usize(1)) {
        return manifest_schema_failure<ManifestDocument>(
            "manifest.lib, manifest.plugin and manifest.pmacro are mutually exclusive"_str);
    }
    const auto has_library    = library.is_some();
    const auto has_plugin     = plugin.is_some();
    const auto has_proc_macro = proc_macro.is_some();
    const auto has_bins       = ! bins.is_empty();
    auto       targets        = Vec<PackageTargetManifest>::with_capacity(
        (has_library ? usize(1) : usize {}) + (has_plugin ? usize(1) : usize {}) +
        (has_proc_macro ? usize(1) : usize {}) + bins.len() + tests.len() + benches.len());
    if (library.is_some()) targets.push(rstd::move(library).unwrap());
    if (plugin.is_some()) targets.push(rstd::move(plugin).unwrap());
    if (proc_macro.is_some()) targets.push(rstd::move(proc_macro).unwrap());
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
            "manifest must contain at least one of 'lib', 'plugin', 'pmacro', 'bin', 'test', "
            "'bench' or 'compile-test', provide install.lua, or declare a script package"_str);
    }
    auto has_benches = false;
    for (const auto& target : targets) {
        if (target.is_Benchmark()) {
            has_benches = true;
            break;
        }
    }
    const auto version_optional = install_script->is_none() && ! has_library && ! has_plugin &&
                                  ! has_proc_macro && ! has_bins && ! has_benches &&
                                  script.is_none();
    auto       version = parse_package_version(rstd::move(package_value.version), version_optional);
    if (version.is_err()) return Err(rstd::move(version).unwrap_err());
    auto license = parse_package_license(rstd::move(package_value.license));
    if (license.is_err()) return Err(rstd::move(license).unwrap_err());
    auto authors = parse_package_authors(rstd::move(package_value.authors));
    if (authors.is_err()) return Err(rstd::move(authors).unwrap_err());
    auto description =
        parse_package_metadata(rstd::move(package_value.description), "description"_str);
    if (description.is_err()) return Err(rstd::move(description).unwrap_err());
    auto repository =
        parse_package_metadata(rstd::move(package_value.repository), "repository"_str);
    if (repository.is_err()) return Err(rstd::move(repository).unwrap_err());
    auto documentation =
        parse_package_metadata(rstd::move(package_value.documentation), "documentation"_str);
    if (documentation.is_err()) return Err(rstd::move(documentation).unwrap_err());
    auto readme =
        parse_package_readme(rstd::move(package_value.readme), root.as_path(), embedded_source);
    if (readme.is_err()) return Err(rstd::move(readme).unwrap_err());
    auto publish = parse_package_publish(rstd::move(package_value.publish));
    if (publish.is_err()) return Err(rstd::move(publish).unwrap_err());

    auto usage = parse_usage(
        rstd::move(input.usage), source_root->as_path(), "manifest.usage"_str, embedded_source);
    auto conditions =
        parse_conditional_configurations(rstd::move(input.when), source_root->as_path());
    auto features             = parse_features(rstd::move(input.features));
    auto dependencies         = parse_dependencies(rstd::move(input.dependencies));
    auto dev_dependencies     = parse_dependencies(rstd::move(input.dev_dependencies), true);
    auto runtime_dependencies = parse_runtime_dependencies(rstd::move(input.runtime_dependencies));
    auto build_tools          = parse_build_tools(rstd::move(input.build_tools));
    auto source_groups        = parse_source_groups(rstd::move(input.source_groups));
    auto external_sources =
        parse_package_external_sources(rstd::move(input.external_sources), root.as_path());
    auto external = parse_external_dependencies(rstd::move(input.external_dependencies));
    auto target   = parse_target_predicate(rstd::move(package_value.target), "package.target"_str);
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
    auto external_dependencies   = rstd::move(external).unwrap();
    auto parsed_external_sources = rstd::move(external_sources).unwrap();
    auto parsed_source_groups    = rstd::move(source_groups).unwrap();
    if (! has_library) {
        const auto reject_public = [](bool     is_public,
                                      ref<str> owner) -> ManifestSchemaResult<empty> {
            if (! is_public) return Ok(empty {});
            return manifest_schema_failure<empty>(rstd::format(
                "{}.pub requires the consuming package to have a library target", owner));
        };
        for (const auto& dependency : parsed_dependencies.explicit_dependencies) {
            rstd_try(reject_public(dependency.is_public.is_some() && *dependency.is_public,
                                   rstd::format("dependency '{}'", dependency.name).as_str()));
        }
        for (const auto& dependency : parsed_dependencies.workspace_dependencies) {
            rstd_try(reject_public(dependency.is_public.is_some() && *dependency.is_public,
                                   rstd::format("dependency '{}'", dependency.name).as_str()));
        }
        for (const auto& dependency : external_dependencies.pkg_config) {
            rstd_try(reject_public(
                dependency.consumption.is_public,
                rstd::format("pkg-config external dependency '{}'", dependency.alias).as_str()));
        }
        for (const auto& dependency : external_dependencies.workspace_pkg_config) {
            rstd_try(reject_public(
                dependency.consumption.is_public,
                rstd::format("pkg-config external dependency '{}'", dependency.alias).as_str()));
        }
        const auto reject_public_cmake =
            [&](const auto& dependency) -> ManifestSchemaResult<empty> {
            for (const auto& target : dependency.targets) {
                rstd_try(reject_public(target.consumption.is_public,
                                       rstd::format("CMake external dependency '{}' target '{}'",
                                                    dependency.alias,
                                                    target.name)
                                           .as_str()));
            }
            return Ok(empty {});
        };
        for (const auto& dependency : external_dependencies.cmake) {
            rstd_try(reject_public_cmake(dependency));
        }
        for (const auto& dependency : external_dependencies.workspace_cmake) {
            rstd_try(reject_public_cmake(dependency));
        }
        for (const auto& dependency : external_dependencies.cargo) {
            rstd_try(reject_public(
                dependency.consumption.dependency.is_public,
                rstd::format("Cargo external dependency '{}'", dependency.alias).as_str()));
        }
        for (const auto& dependency : external_dependencies.workspace_cargo) {
            rstd_try(reject_public(
                dependency.consumption.dependency.is_public,
                rstd::format("Cargo external dependency '{}'", dependency.alias).as_str()));
        }
    }
    const auto has_external_source = [&](ref<str> name) {
        return parsed_external_sources.explicit_sources.iter().any([&](auto source) {
            return source->name == name;
        }) || parsed_external_sources.workspace_sources.iter().any([&](auto source) {
            return source->name == name;
        });
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
        return parsed_source_groups.iter().any([&](auto group) {
            return group->name == name;
        });
    };
    for (const auto& manifest_target : targets) {
        const auto& target_source = package_target_source(manifest_target);
        for (const auto& group : target_source.source_groups) {
            if (! has_source_group(group.as_str())) {
                return manifest_schema_failure<ManifestDocument>(
                    rstd::format("target '{}::{}' references unknown source group '{}'",
                                 name.as_str(),
                                 package_target_name(manifest_target),
                                 group.as_str()));
            }
        }
        for (const auto& conditional : target_source.conditions) {
            for (const auto& group : conditional.source_groups) {
                if (! has_source_group(group.as_str())) {
                    return manifest_schema_failure<ManifestDocument>(rstd::format(
                        "target '{}::{}' condition '{}' references unknown source group '{}'",
                        name.as_str(),
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
    auto profile = rstd_try(parse_project_profile(rstd::move(input.profile)));

    return Ok(ManifestDocument {
        .kind    = ManifestKind::Package,
        .package = Some(PackageManifest {
            .name                       = rstd::move(name),
            .version                    = rstd::move(version).unwrap(),
            .license                    = rstd::move(license).unwrap(),
            .authors                    = rstd::move(authors).unwrap(),
            .description                = rstd::move(description).unwrap(),
            .repository                 = rstd::move(repository).unwrap(),
            .documentation              = rstd::move(documentation).unwrap(),
            .readme                     = rstd::move(readme).unwrap(),
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
        owner->insert("dependencies"_Str, Toml::Table(Table::make()));
        dependencies = owner->get_mut("dependencies"_str);
    }
    auto dependency_table = (**dependencies).as_table_mut();
    if (dependency_table.is_none()) {
        return manifest_edit_failure<ManifestDependencyEdit>(path.as_path(),
                                                             "dependencies must be a table"_str);
    }
    auto       dependency = (**dependency_table).get_mut(package.as_str());
    const auto use_shorthand =
        registry.is_none() && (dependency.is_none() || (**dependency).as_str().is_some());
    if (use_shorthand) {
        auto version = Toml::String(String::make(requirement.text()));
        if (dependency.is_some())
            **dependency = rstd::move(version);
        else
            (**dependency_table).insert(String::make(package.as_str()), rstd::move(version));
    } else {
        if (dependency.is_none()) {
            (**dependency_table).insert(String::make(package.as_str()), Toml::Table(Table::make()));
            dependency = (**dependency_table).get_mut(package.as_str());
        } else if ((**dependency).as_str().is_some()) {
            **dependency = Toml::Table(Table::make());
        }
        auto fields = (**dependency).as_table_mut();
        if (fields.is_none()) {
            return manifest_edit_failure<ManifestDependencyEdit>(
                path.as_path(), "dependency must be a Registry version string or table"_str);
        }
        constexpr ref<str> source_keys[] = {
            "path"_str,   "git"_str,     "branch"_str,    "tag"_str,     "rev"_str,
            "commit"_str, "builtin"_str, "workspace"_str, "version"_str, "registry"_str,
        };
        for (auto key : source_keys) (void)(**fields).remove(key);
        (**fields).insert("version"_Str, Toml::String(String::make(requirement.text())));
        if (registry.is_some()) {
            if (registry->is_empty()) {
                return manifest_edit_failure<ManifestDependencyEdit>(
                    path.as_path(), "Registry name must not be empty"_str);
            }
            (**fields).insert("registry"_Str, Toml::String(rstd::move(registry).unwrap()));
        }
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
            .cause = ManifestFileCause::Schema(
                ManifestSchemaError::Domain("builtin source identity must not be empty"_Str)),
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
                    lito::parse::NodePath::root("builtin package source"_str), "lito.toml"_Str))),
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
