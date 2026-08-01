export module tenon.manifest:schema;

import rstd;
import rstd.toml;
import tenon.model;
import :locator;
import tenon.build_profile;

using namespace rstd::prelude;
using namespace rstd::literals;
using Toml         = rstd::toml::Value;
using Table        = rstd::toml::Table;
using KeyPredicate = bool (*)(ref<str>);

namespace tenon
{

template<typename T>
auto failure(ref<str> message) -> Result<T> {
    return Err(Error::make(ErrorKind::Manifest, message));
}

template<typename T>
auto failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Manifest, rstd::move(message)));
}

auto member(const Toml& value, ref<str> key) -> Option<ref<Toml>> {
    return value.get(key);
}

auto canonical_existing(ref<rstd::path::Path> path, ref<str> context) -> Result<PathBuf> {
    auto canonical = rstd::fs::canonicalize(path);
    if (canonical.is_err()) {
        return failure<PathBuf>(
            rstd::format("{} '{}': {}", context, path, rstd::move(canonical).unwrap_err()));
    }
    return Ok(rstd::move(canonical).unwrap());
}

auto table_value(const Toml& value, ref<str> context) -> Result<ref<Table>> {
    auto table = value.as_table();
    if (table.is_none()) {
        return failure<ref<Table>>(rstd::format("{} must be a table", context));
    }
    return Ok(*table);
}

auto required_table(const Toml& document, ref<str> key, ref<str> context) -> Result<ref<Table>> {
    auto value = member(document, key);
    if (value.is_none()) {
        return failure<ref<Table>>(rstd::format("{} is missing '{}'", context, key));
    }
    return table_value(**value, rstd::format("{}.{}", context, key).as_str());
}

auto string_value(const Toml& value, ref<str> context) -> Result<String> {
    auto text = value.as_str();
    if (text.is_none()) return failure<String>(rstd::format("{} must be a string", context));
    return Ok(String::make(*text));
}

auto required_string(const Toml& table, ref<str> key, ref<str> context) -> Result<String> {
    auto value = member(table, key);
    if (value.is_none()) {
        return failure<String>(rstd::format("{} is missing '{}'", context, key));
    }
    return string_value(**value, rstd::format("{}.{}", context, key).as_str());
}

auto optional_string(const Toml& table, ref<str> key, ref<str> context) -> Result<Option<String>> {
    auto value = member(table, key);
    if (value.is_none()) return Ok(Option<String> {});
    auto parsed = string_value(**value, rstd::format("{}.{}", context, key).as_str());
    if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
    return Ok(Some(rstd::move(parsed).unwrap()));
}

auto string_array(Option<ref<Toml>> value, ref<str> context) -> Result<Vec<String>> {
    auto result = Vec<String>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto array = (**value).as_array();
    if (array.is_none()) return failure<Vec<String>>(rstd::format("{} must be an array", context));
    for (const auto& item : **array) {
        auto text = string_value(item, rstd::format("{} item", context).as_str());
        if (text.is_err()) return Err(rstd::move(text).unwrap_err());
        result.push(rstd::move(text).unwrap());
    }
    return Ok(rstd::move(result));
}

auto reject_unknown(const Table& table, ref<str> context, KeyPredicate allowed) -> Result<empty> {
    auto keys = table.keys();
    for (auto key = keys.next(); key.is_some(); key = keys.next()) {
        if (! allowed((**key).as_str())) {
            return failure<empty>(
                rstd::format("{} contains unknown field '{}'", context, (**key).as_str()));
        }
    }
    return Ok(empty {});
}

auto package_root_key(ref<str> key) -> bool {
    return key == "package"_str || key == "library"_str || key == "executable"_str ||
           key == "usage"_str || key == "dependencies"_str;
}

auto workspace_root_key(ref<str> key) -> bool {
    return key == "workspace"_str;
}

auto package_key(ref<str> key) -> bool {
    return key == "name"_str || key == "version"_str || key == "module"_str;
}

auto library_key(ref<str> key) -> bool {
    return key == "archive"_str || key == "discovery"_str || key == "sources"_str;
}

auto executable_key(ref<str> key) -> bool {
    return key == "name"_str || key == "discovery"_str || key == "sources"_str;
}

auto usage_key(ref<str> key) -> bool {
    return key == "public-include-directories"_str || key == "private-include-directories"_str ||
           key == "public-definitions"_str || key == "private-definitions"_str ||
           key == "public-options"_str || key == "private-options"_str ||
           key == "private-linker-options"_str;
}

auto dependency_key(ref<str> key) -> bool {
    return key == "path"_str || key == "git"_str || key == "branch"_str || key == "tag"_str ||
           key == "rev"_str || key == "visibility"_str;
}

auto workspace_key(ref<str> key) -> bool {
    return key == "members"_str || key == "default-members"_str || key == "package"_str;
}

auto package_version_key(ref<str> key) -> bool {
    return key == "workspace"_str;
}

auto workspace_package_key(ref<str> key) -> bool {
    return key == "version"_str;
}

auto parse_package_version(const Toml& package) -> Result<PackageVersion> {
    auto declared = member(package, "version"_str);
    if (declared.is_none()) {
        return failure<PackageVersion>("package is missing 'version'"_str);
    }

    auto explicit_value = (**declared).as_str();
    if (explicit_value.is_some()) {
        if (explicit_value->is_empty()) {
            return failure<PackageVersion>("package.version must not be empty"_str);
        }
        return Ok(PackageVersion {
            .source = PackageVersionSource::Explicit,
            .value  = Some(String::make(*explicit_value)),
        });
    }

    auto inherited = table_value(**declared, "package.version"_str);
    if (inherited.is_err()) return Err(rstd::move(inherited).unwrap_err());
    auto known = reject_unknown(**inherited, "package.version"_str, package_version_key);
    if (known.is_err()) return Err(rstd::move(known).unwrap_err());
    auto workspace = member(**declared, "workspace"_str);
    if (workspace.is_none()) {
        return failure<PackageVersion>("package.version is missing 'workspace'"_str);
    }
    auto enabled = (**workspace).as_bool();
    if (enabled.is_none() || ! *enabled) {
        return failure<PackageVersion>("package.version.workspace must be true"_str);
    }
    return Ok(PackageVersion {
        .source = PackageVersionSource::Workspace,
    });
}

auto valid_module_name(ref<str> value) -> bool {
    if (value.size() == usize {}) return false;
    bool segment_start = true;
    for (usize index {}; index < value.size(); ++index) {
        const auto byte = value[index];
        if (byte == u8('.')) {
            if (segment_start) return false;
            segment_start = true;
            continue;
        }
        const bool alpha = (byte >= u8('a') && byte <= u8('z')) ||
                           (byte >= u8('A') && byte <= u8('Z')) || byte == u8('_');
        const bool digit = byte >= u8('0') && byte <= u8('9');
        if ((! alpha && ! digit) || (segment_start && digit)) return false;
        segment_start = false;
    }
    return ! segment_start;
}

auto package_name_is_valid(ref<str> value) -> bool {
    if (value.is_empty()) return false;
    for (auto byte : value) {
        const bool alpha =
            (byte >= u8('a') && byte <= u8('z')) || (byte >= u8('A') && byte <= u8('Z'));
        const bool digit = byte >= u8('0') && byte <= u8('9');
        if (! alpha && ! digit && byte != u8('-') && byte != u8('_')) return false;
    }
    return true;
}

auto valid_artifact_name(ref<str> value) -> bool {
    if (value.size() == usize {} || value == "."_str || value == ".."_str) return false;
    for (usize index {}; index < value.size(); ++index) {
        const auto byte     = value[index];
        const bool accepted = (byte >= u8('a') && byte <= u8('z')) ||
                              (byte >= u8('A') && byte <= u8('Z')) ||
                              (byte >= u8('0') && byte <= u8('9')) || byte == u8('_') ||
                              byte == u8('-') || byte == u8('.');
        if (! accepted) return false;
    }
    return true;
}

auto relative_path(String text, ref<str> context) -> Result<PathBuf> {
    if (text.is_empty()) return failure<PathBuf>(rstd::format("{} must not be empty", context));
    auto path = PathBuf::from(rstd::move(text));
    if (! path.as_path().is_relative()) {
        return failure<PathBuf>(rstd::format("{} must be a relative path", context));
    }
    return Ok(rstd::move(path));
}

auto declared_paths(Option<ref<Toml>> value, ref<str> context, bool required)
    -> Result<Vec<PathBuf>> {
    if (required && value.is_none()) {
        return failure<Vec<PathBuf>>(rstd::format("{} is required", context));
    }
    auto strings = string_array(value, context);
    if (strings.is_err()) return Err(rstd::move(strings).unwrap_err());
    auto result = Vec<PathBuf>::make();
    auto items  = rstd::move(strings).unwrap();
    for (auto& item : items) {
        auto path = relative_path(rstd::move(item), context);
        if (path.is_err()) return Err(rstd::move(path).unwrap_err());
        result.push(rstd::move(path).unwrap());
    }
    if (required && result.is_empty()) {
        return failure<Vec<PathBuf>>(rstd::format("{} must not be empty", context));
    }
    return Ok(rstd::move(result));
}

auto resolve_directories(Option<ref<Toml>> value, ref<rstd::path::Path> root, ref<str> context)
    -> Result<Vec<PathBuf>> {
    auto declared = declared_paths(value, context, false);
    if (declared.is_err()) return Err(rstd::move(declared).unwrap_err());
    auto result = Vec<PathBuf>::make();
    auto paths  = rstd::move(declared).unwrap();
    for (const auto& path : paths) {
        auto requested = PathBuf::from(root).join(path.as_path());
        auto canonical =
            canonical_existing(requested.as_path(), "cannot resolve include directory"_str);
        if (canonical.is_err()) return Err(rstd::move(canonical).unwrap_err());
        auto resolved = rstd::move(canonical).unwrap();
        if (resolved.as_path().strip_prefix(root).is_none()) {
            return failure<Vec<PathBuf>>(
                rstd::format("{} entry '{}' is outside package root", context, path.as_path()));
        }
        auto metadata = rstd::fs::metadata(resolved.as_path());
        if (metadata.is_err()) {
            return failure<Vec<PathBuf>>(rstd::format("cannot inspect include directory '{}': {}",
                                                      resolved.as_path(),
                                                      rstd::move(metadata).unwrap_err()));
        }
        if (! metadata->is_dir()) {
            return failure<Vec<PathBuf>>(
                rstd::format("{} entry '{}' is not a directory", context, path.as_path()));
        }
        result.push(rstd::move(resolved));
    }
    return Ok(rstd::move(result));
}

auto validate_options(const Vec<String>& options, ref<str> context) -> Result<empty> {
    for (const auto& option : options) {
        auto value = option.as_str();
        if (value == "-frtti"_str || value == "-fexceptions"_str ||
            value.starts_with("-stdlib="_str) || value.starts_with("-std="_str) ||
            value == "-fmodules-reduced-bmi"_str || value == "-fno-modules-reduced-bmi"_str ||
            is_profile_owned_option(value)) {
            return failure<empty>(
                rstd::format("{} option '{}' overrides a Tenon-owned setting", context, value));
        }
    }
    return Ok(empty {});
}

auto validate_definitions(const Vec<String>& definitions, ref<str> context) -> Result<empty> {
    for (const auto& definition : definitions) {
        if (is_profile_owned_definition(definition.as_str())) {
            return failure<empty>(rstd::format("{} definition '{}' overrides a Tenon-owned setting",
                                               context,
                                               definition.as_str()));
        }
    }
    return Ok(empty {});
}

auto validate_linker_options(const Vec<String>& options, ref<str> context) -> Result<empty> {
    for (const auto& option : options) {
        if (option.as_str().starts_with("-stdlib="_str)) {
            return failure<empty>(rstd::format(
                "{} option '{}' overrides a Tenon-owned setting", context, option.as_str()));
        }
    }
    return Ok(empty {});
}

auto parse_usage(Option<ref<Toml>> value, ref<rstd::path::Path> root) -> Result<UsageRequirements> {
    if (value.is_none()) return Ok(UsageRequirements {});
    auto table = table_value(**value, "manifest.usage"_str);
    if (table.is_err()) return Err(rstd::move(table).unwrap_err());
    auto known = reject_unknown(**table, "manifest.usage"_str, usage_key);
    if (known.is_err()) return Err(rstd::move(known).unwrap_err());

    auto public_includes  = resolve_directories(member(**value, "public-include-directories"_str),
                                                root,
                                                "usage.public-include-directories"_str);
    auto private_includes = resolve_directories(member(**value, "private-include-directories"_str),
                                                root,
                                                "usage.private-include-directories"_str);
    auto public_definitions =
        string_array(member(**value, "public-definitions"_str), "usage.public-definitions"_str);
    auto private_definitions =
        string_array(member(**value, "private-definitions"_str), "usage.private-definitions"_str);
    auto public_options =
        string_array(member(**value, "public-options"_str), "usage.public-options"_str);
    auto private_options =
        string_array(member(**value, "private-options"_str), "usage.private-options"_str);
    auto private_linker_options = string_array(member(**value, "private-linker-options"_str),
                                               "usage.private-linker-options"_str);
    if (public_includes.is_err()) return Err(rstd::move(public_includes).unwrap_err());
    if (private_includes.is_err()) return Err(rstd::move(private_includes).unwrap_err());
    if (public_definitions.is_err()) return Err(rstd::move(public_definitions).unwrap_err());
    if (private_definitions.is_err()) return Err(rstd::move(private_definitions).unwrap_err());
    if (public_options.is_err()) return Err(rstd::move(public_options).unwrap_err());
    if (private_options.is_err()) return Err(rstd::move(private_options).unwrap_err());
    if (private_linker_options.is_err()) {
        return Err(rstd::move(private_linker_options).unwrap_err());
    }
    auto public_definition_values     = rstd::move(public_definitions).unwrap();
    auto private_definition_values    = rstd::move(private_definitions).unwrap();
    auto public_option_values         = rstd::move(public_options).unwrap();
    auto private_option_values        = rstd::move(private_options).unwrap();
    auto private_linker_option_values = rstd::move(private_linker_options).unwrap();
    auto public_definitions_valid =
        validate_definitions(public_definition_values, "usage.public-definitions"_str);
    auto private_definitions_valid =
        validate_definitions(private_definition_values, "usage.private-definitions"_str);
    auto public_valid  = validate_options(public_option_values, "usage.public-options"_str);
    auto private_valid = validate_options(private_option_values, "usage.private-options"_str);
    auto private_linker_valid =
        validate_linker_options(private_linker_option_values, "usage.private-linker-options"_str);
    if (public_definitions_valid.is_err()) {
        return Err(rstd::move(public_definitions_valid).unwrap_err());
    }
    if (private_definitions_valid.is_err()) {
        return Err(rstd::move(private_definitions_valid).unwrap_err());
    }
    if (public_valid.is_err()) return Err(rstd::move(public_valid).unwrap_err());
    if (private_valid.is_err()) return Err(rstd::move(private_valid).unwrap_err());
    if (private_linker_valid.is_err()) {
        return Err(rstd::move(private_linker_valid).unwrap_err());
    }
    return Ok(UsageRequirements {
        .public_include_directories  = rstd::move(public_includes).unwrap(),
        .private_include_directories = rstd::move(private_includes).unwrap(),
        .public_definitions          = rstd::move(public_definition_values),
        .private_definitions         = rstd::move(private_definition_values),
        .public_options              = rstd::move(public_option_values),
        .private_options             = rstd::move(private_option_values),
        .private_linker_options      = rstd::move(private_linker_option_values),
    });
}

auto parse_visibility(ref<str> value, ref<str> context) -> Result<DependencyVisibility> {
    if (value == "public"_str) return Ok(DependencyVisibility::Public);
    if (value == "private"_str) return Ok(DependencyVisibility::Private);
    if (value == "runtime"_str) return Ok(DependencyVisibility::Runtime);
    return failure<DependencyVisibility>(
        rstd::format("{} must be public, private, or runtime", context));
}

auto parse_dependencies(Option<ref<Toml>> value) -> Result<Vec<DeclaredDependency>> {
    auto result = Vec<DeclaredDependency>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto table = table_value(**value, "manifest.dependencies"_str);
    if (table.is_err()) return Err(rstd::move(table).unwrap_err());

    auto keys = (**table).keys();
    for (auto key = keys.next(); key.is_some(); key = keys.next()) {
        const auto& name = **key;
        if (! package_name_is_valid(name.as_str())) {
            return failure<Vec<DeclaredDependency>>(
                rstd::format("dependency name '{}' must contain only ASCII letters, "
                             "digits, '-' or '_'",
                             name.as_str()));
        }
        auto specification = (**table).get(name.as_str());
        auto dependency_table =
            table_value(**specification, rstd::format("dependency '{}'", name.as_str()).as_str());
        if (dependency_table.is_err()) {
            return Err(rstd::move(dependency_table).unwrap_err());
        }
        auto known = reject_unknown(**dependency_table,
                                    rstd::format("dependency '{}'", name.as_str()).as_str(),
                                    dependency_key);
        if (known.is_err()) return Err(rstd::move(known).unwrap_err());
        auto path_text       = optional_string(**specification, "path"_str, "dependency"_str);
        auto git             = optional_string(**specification, "git"_str, "dependency"_str);
        auto branch          = optional_string(**specification, "branch"_str, "dependency"_str);
        auto tag             = optional_string(**specification, "tag"_str, "dependency"_str);
        auto rev             = optional_string(**specification, "rev"_str, "dependency"_str);
        auto visibility_text = required_string(**specification, "visibility"_str, "dependency"_str);
        if (path_text.is_err()) return Err(rstd::move(path_text).unwrap_err());
        if (git.is_err()) return Err(rstd::move(git).unwrap_err());
        if (branch.is_err()) return Err(rstd::move(branch).unwrap_err());
        if (tag.is_err()) return Err(rstd::move(tag).unwrap_err());
        if (rev.is_err()) return Err(rstd::move(rev).unwrap_err());
        if (visibility_text.is_err()) {
            return Err(rstd::move(visibility_text).unwrap_err());
        }
        auto path_value = rstd::move(path_text).unwrap();
        auto git_value  = rstd::move(git).unwrap();
        if (path_value.is_some() == git_value.is_some()) {
            return failure<Vec<DeclaredDependency>>(rstd::format(
                "dependency '{}' must contain exactly one of 'path' or 'git'", name.as_str()));
        }
        auto  branch_value = rstd::move(branch).unwrap();
        auto  tag_value    = rstd::move(tag).unwrap();
        auto  rev_value    = rstd::move(rev).unwrap();
        usize selector_count {};
        if (branch_value.is_some()) ++selector_count;
        if (tag_value.is_some()) ++selector_count;
        if (rev_value.is_some()) ++selector_count;
        if (selector_count > usize(1)) {
            return failure<Vec<DeclaredDependency>>(
                rstd::format("dependency '{}' may contain only one of 'branch', 'tag', or 'rev'",
                             name.as_str()));
        }
        if (path_value.is_some() && selector_count != usize {}) {
            return failure<Vec<DeclaredDependency>>(
                rstd::format("dependency '{}' Git selector requires 'git'", name.as_str()));
        }
        auto source = Option<PackageSourceRequirement> {};
        if (path_value.is_some()) {
            auto path = relative_path(rstd::move(path_value).unwrap(), "dependency.path"_str);
            if (path.is_err()) return Err(rstd::move(path).unwrap_err());
            source = Some(PackageSourceRequirement::Path(rstd::move(path).unwrap()));
        } else {
            auto url = rstd::move(git_value).unwrap();
            if (url.is_empty()) {
                return failure<Vec<DeclaredDependency>>(
                    rstd::format("dependency '{}'.git must not be empty", name.as_str()));
            }
            if (url.as_str().starts_with("-"_str)) {
                return failure<Vec<DeclaredDependency>>(
                    rstd::format("dependency '{}'.git must not start with '-'", name.as_str()));
            }
            if (url.as_str().contains("#"_str)) {
                return failure<Vec<DeclaredDependency>>(rstd::format(
                    "dependency '{}'.git must not contain a URL fragment", name.as_str()));
            }
            auto reference = GitReference {};
            if (branch_value.is_some()) {
                reference.kind  = GitReferenceKind::Branch;
                reference.value = rstd::move(branch_value).unwrap();
            } else if (tag_value.is_some()) {
                reference.kind  = GitReferenceKind::Tag;
                reference.value = rstd::move(tag_value).unwrap();
            } else if (rev_value.is_some()) {
                reference.kind  = GitReferenceKind::Rev;
                reference.value = rstd::move(rev_value).unwrap();
            }
            if (reference.kind != GitReferenceKind::DefaultBranch && reference.value.is_empty()) {
                return failure<Vec<DeclaredDependency>>(
                    rstd::format("dependency '{}' Git selector must not be empty", name.as_str()));
            }
            if (reference.value.as_str().starts_with("-"_str)) {
                return failure<Vec<DeclaredDependency>>(rstd::format(
                    "dependency '{}' Git selector must not start with '-'", name.as_str()));
            }
            source = Some(PackageSourceRequirement::Git(rstd::move(url), rstd::move(reference)));
        }
        auto visibility = parse_visibility(visibility_text->as_str(), "dependency.visibility"_str);
        if (visibility.is_err()) return Err(rstd::move(visibility).unwrap_err());
        result.push(DeclaredDependency {
            .name       = name.clone(),
            .source     = rstd::move(source).unwrap(),
            .visibility = rstd::move(visibility).unwrap(),
        });
    }
    return Ok(rstd::move(result));
}

} // namespace tenon

export namespace tenon
{

auto valid_package_name(ref<str> value) -> bool {
    return package_name_is_valid(value);
}

auto load_manifest_document(ref<rstd::path::Path> requested_directory) -> Result<ManifestDocument> {
    auto located = locate_manifest(requested_directory);
    if (located.is_err()) return Err(rstd::move(located).unwrap_err());
    auto location = rstd::move(located).unwrap();
    auto path     = rstd::move(location.manifest);
    auto root     = rstd::move(location.directory);
    auto contents = rstd::fs::read_to_string(path.as_path());
    if (contents.is_err()) {
        return failure<ManifestDocument>(rstd::format(
            "cannot read manifest '{}': {}", path.as_path(), rstd::move(contents).unwrap_err()));
    }
    auto parsed = rstd::toml::from_str(contents->as_str());
    if (parsed.is_err()) {
        return failure<ManifestDocument>(rstd::format(
            "cannot parse manifest '{}': {}", path.as_path(), rstd::move(parsed).unwrap_err()));
    }
    auto document   = rstd::move(parsed).unwrap();
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
        return Ok(ManifestDocument {
            .kind      = ManifestKind::Workspace,
            .workspace = Some(WorkspaceManifest {
                .root            = rstd::move(root),
                .manifest_path   = rstd::move(path),
                .members         = rstd::move(members).unwrap(),
                .default_members = rstd::move(default_members).unwrap(),
                .package         = rstd::move(package_defaults),
            }),
        });
    }

    auto root_known = reject_unknown(**root_table, "manifest root"_str, package_root_key);
    if (root_known.is_err()) return Err(rstd::move(root_known).unwrap_err());
    auto package_table = required_table(document, "package"_str, "manifest"_str);
    if (package_table.is_err()) return Err(rstd::move(package_table).unwrap_err());
    auto package_known = reject_unknown(**package_table, "manifest.package"_str, package_key);
    if (package_known.is_err()) return Err(rstd::move(package_known).unwrap_err());

    auto library_value_option    = member(document, "library"_str);
    auto executable_value_option = member(document, "executable"_str);
    if (library_value_option.is_some() == executable_value_option.is_some()) {
        return failure<ManifestDocument>(
            "manifest must contain exactly one of 'library' or 'executable'"_str);
    }
    const auto artifact_kind =
        library_value_option.is_some() ? ArtifactKind::StaticLibrary : ArtifactKind::Executable;
    const auto& artifact_value =
        library_value_option.is_some() ? **library_value_option : **executable_value_option;
    const auto artifact_context = artifact_kind == ArtifactKind::StaticLibrary
                                      ? "manifest.library"_str
                                      : "manifest.executable"_str;
    auto       artifact_table   = table_value(artifact_value, artifact_context);
    if (artifact_table.is_err()) return Err(rstd::move(artifact_table).unwrap_err());
    auto artifact_known =
        reject_unknown(**artifact_table,
                       artifact_context,
                       artifact_kind == ArtifactKind::StaticLibrary ? library_key : executable_key);
    if (artifact_known.is_err()) return Err(rstd::move(artifact_known).unwrap_err());

    const auto& package_value = **member(document, "package"_str);
    auto        name          = required_string(package_value, "name"_str, "package"_str);
    auto        version       = parse_package_version(package_value);
    auto        root_module   = optional_string(package_value, "module"_str, "package"_str);
    auto        artifact_name = required_string(
        artifact_value,
        artifact_kind == ArtifactKind::StaticLibrary ? "archive"_str : "name"_str,
        artifact_kind == ArtifactKind::StaticLibrary ? "library"_str : "executable"_str);
    auto discovery_text = required_string(
        artifact_value,
        "discovery"_str,
        artifact_kind == ArtifactKind::StaticLibrary ? "library"_str : "executable"_str);
    if (name.is_err()) return Err(rstd::move(name).unwrap_err());
    if (version.is_err()) return Err(rstd::move(version).unwrap_err());
    if (root_module.is_err()) return Err(rstd::move(root_module).unwrap_err());
    if (artifact_name.is_err()) return Err(rstd::move(artifact_name).unwrap_err());
    if (discovery_text.is_err()) return Err(rstd::move(discovery_text).unwrap_err());
    if (! package_name_is_valid(name->as_str())) {
        return failure<ManifestDocument>(
            "package.name must contain only ASCII letters, digits, '-' or '_'"_str);
    }
    if (root_module->is_some() && ! valid_module_name((**root_module).as_str())) {
        return failure<ManifestDocument>("package.module must be a valid module name"_str);
    }
    if (artifact_kind == ArtifactKind::StaticLibrary && root_module->is_none()) {
        return failure<ManifestDocument>("package.module is required for a library"_str);
    }
    if (! valid_artifact_name(artifact_name->as_str())) {
        return failure<ManifestDocument>(rstd::format(
            "{}.{} must be a safe artifact basename",
            artifact_context,
            artifact_kind == ArtifactKind::StaticLibrary ? "archive"_str : "name"_str));
    }
    const auto explicit_discovery = discovery_text->as_str() == "explicit"_str;
    const auto module_discovery   = discovery_text->as_str() == "module"_str;
    if (! explicit_discovery && ! module_discovery) {
        return failure<ManifestDocument>(
            rstd::format("{}.discovery must be explicit or module", artifact_context));
    }
    if (module_discovery && member(artifact_value, "sources"_str).is_some()) {
        return failure<ManifestDocument>(
            rstd::format("{}.sources is not allowed when discovery is module", artifact_context));
    }
    auto sources =
        declared_paths(member(artifact_value, "sources"_str),
                       artifact_kind == ArtifactKind::StaticLibrary ? "library.sources"_str
                                                                    : "executable.sources"_str,
                       explicit_discovery);
    if (sources.is_err()) return Err(rstd::move(sources).unwrap_err());

    auto usage        = parse_usage(member(document, "usage"_str), root.as_path());
    auto dependencies = parse_dependencies(member(document, "dependencies"_str));
    if (usage.is_err()) return Err(rstd::move(usage).unwrap_err());
    if (dependencies.is_err()) return Err(rstd::move(dependencies).unwrap_err());

    return Ok(ManifestDocument {
        .kind    = ManifestKind::Package,
        .package = Some(PackageManifest {
            .name          = rstd::move(name).unwrap(),
            .version       = rstd::move(version).unwrap(),
            .root_module   = rstd::move(root_module).unwrap(),
            .root          = rstd::move(root),
            .manifest_path = rstd::move(path),
            .artifact_kind = artifact_kind,
            .artifact_name = rstd::move(artifact_name).unwrap(),
            .discovery =
                explicit_discovery ? SourceDiscoveryMode::Explicit : SourceDiscoveryMode::Module,
            .declared_sources = rstd::move(sources).unwrap(),
            .usage            = rstd::move(usage).unwrap(),
            .dependencies     = rstd::move(dependencies).unwrap(),
        }),
    });
}

auto load_package_manifest(ref<rstd::path::Path> requested_directory) -> Result<PackageManifest> {
    auto loaded = load_manifest_document(requested_directory);
    if (loaded.is_err()) return Err(rstd::move(loaded).unwrap_err());
    auto document = rstd::move(loaded).unwrap();
    if (document.kind != ManifestKind::Package || document.package.is_none()) {
        return failure<PackageManifest>(
            rstd::format("directory '{}' contains a workspace manifest where a "
                         "package is required",
                         requested_directory));
    }
    return Ok(rstd::move(document.package).unwrap());
}

} // namespace tenon
