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
           key == "test"_str || key == "compile-test"_str || key == "usage"_str ||
           key == "dependencies"_str;
}

auto workspace_root_key(ref<str> key) -> bool {
    return key == "workspace"_str;
}

auto package_key(ref<str> key) -> bool {
    return key == "name"_str || key == "version"_str || key == "module"_str || key == "target"_str;
}

auto library_key(ref<str> key) -> bool {
    return key == "archive"_str || key == "discovery"_str || key == "sources"_str ||
           key == "source-groups"_str;
}

auto executable_key(ref<str> key) -> bool {
    return key == "name"_str || key == "discovery"_str || key == "sources"_str ||
           key == "source-groups"_str;
}

auto test_key(ref<str> key) -> bool {
    return executable_key(key) || key == "attach"_str;
}

auto compile_test_key(ref<str> key) -> bool {
    return key == "cases"_str;
}

auto target_predicate_key(ref<str> key) -> bool {
    return key == "family"_str || key == "os"_str || key == "not-family"_str || key == "not-os"_str;
}

auto source_group_key(ref<str> key) -> bool {
    return key == "sources"_str || key == "target"_str;
}

auto test_attachment_key(ref<str> key) -> bool {
    return key == "package"_str || key == "sources"_str || key == "source-groups"_str;
}

auto compile_test_case_key(ref<str> key) -> bool {
    return key == "name"_str || key == "source"_str || key == "outcome"_str ||
           key == "options"_str || key == "diagnostic-contains"_str ||
           key == "diagnostic-contains-any"_str;
}

auto usage_key(ref<str> key) -> bool {
    return key == "public-include-directories"_str || key == "private-include-directories"_str ||
           key == "public-definitions"_str || key == "private-definitions"_str ||
           key == "public-options"_str || key == "private-options"_str ||
           key == "private-linker-options"_str;
}

auto dependency_key(ref<str> key) -> bool {
    return key == "path"_str || key == "git"_str || key == "branch"_str || key == "tag"_str ||
           key == "rev"_str || key == "pkg-config"_str || key == "version"_str ||
           key == "static"_str || key == "visibility"_str;
}

auto workspace_key(ref<str> key) -> bool {
    return key == "name"_str || key == "members"_str || key == "default-members"_str ||
           key == "package"_str;
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

auto predicate_values(Option<ref<Toml>> value, ref<str> context) -> Result<Vec<String>> {
    auto result = Vec<String>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto single = (**value).as_str();
    if (single.is_some()) {
        if (single->is_empty()) {
            return failure<Vec<String>>(rstd::format("{} must not be empty", context));
        }
        result.push(String::make(*single));
        return Ok(rstd::move(result));
    }
    auto values = string_array(value, context);
    if (values.is_err()) return Err(rstd::move(values).unwrap_err());
    result = rstd::move(values).unwrap();
    if (result.is_empty()) {
        return failure<Vec<String>>(rstd::format("{} must not be empty", context));
    }
    for (const auto& item : result) {
        if (item.is_empty()) {
            return failure<Vec<String>>(rstd::format("{} item must not be empty", context));
        }
    }
    return Ok(rstd::move(result));
}

auto parse_target_predicate(Option<ref<Toml>> value, ref<str> context) -> Result<TargetPredicate> {
    if (value.is_none()) return Ok(TargetPredicate {});
    auto table = table_value(**value, context);
    if (table.is_err()) return Err(rstd::move(table).unwrap_err());
    auto known = reject_unknown(**table, context, target_predicate_key);
    if (known.is_err()) return Err(rstd::move(known).unwrap_err());
    auto families = predicate_values(member(**value, "family"_str),
                                     rstd::format("{}.family", context).as_str());
    auto operating_systems =
        predicate_values(member(**value, "os"_str), rstd::format("{}.os", context).as_str());
    auto excluded_families = predicate_values(member(**value, "not-family"_str),
                                              rstd::format("{}.not-family", context).as_str());
    auto excluded_operating_systems = predicate_values(member(**value, "not-os"_str),
                                                       rstd::format("{}.not-os", context).as_str());
    if (families.is_err()) return Err(rstd::move(families).unwrap_err());
    if (operating_systems.is_err()) {
        return Err(rstd::move(operating_systems).unwrap_err());
    }
    if (excluded_families.is_err()) {
        return Err(rstd::move(excluded_families).unwrap_err());
    }
    if (excluded_operating_systems.is_err()) {
        return Err(rstd::move(excluded_operating_systems).unwrap_err());
    }
    const auto valid_family = [](ref<str> item) {
        return item == "unix"_str || item == "windows"_str || item == "unknown"_str;
    };
    const auto valid_os = [](ref<str> item) {
        return item == "linux"_str || item == "windows"_str || item == "macos"_str ||
               item == "android"_str || item == "freebsd"_str || item == "netbsd"_str ||
               item == "openbsd"_str || item == "unknown"_str;
    };
    for (const auto& item : *families) {
        if (! valid_family(item.as_str())) {
            return failure<TargetPredicate>(
                rstd::format("{}.family contains unsupported value '{}'", context, item.as_str()));
        }
    }
    for (const auto& item : *excluded_families) {
        if (! valid_family(item.as_str())) {
            return failure<TargetPredicate>(rstd::format(
                "{}.not-family contains unsupported value '{}'", context, item.as_str()));
        }
    }
    for (const auto& item : *operating_systems) {
        if (! valid_os(item.as_str())) {
            return failure<TargetPredicate>(
                rstd::format("{}.os contains unsupported value '{}'", context, item.as_str()));
        }
    }
    for (const auto& item : *excluded_operating_systems) {
        if (! valid_os(item.as_str())) {
            return failure<TargetPredicate>(
                rstd::format("{}.not-os contains unsupported value '{}'", context, item.as_str()));
        }
    }
    return Ok(TargetPredicate {
        .families                   = rstd::move(families).unwrap(),
        .operating_systems          = rstd::move(operating_systems).unwrap(),
        .excluded_families          = rstd::move(excluded_families).unwrap(),
        .excluded_operating_systems = rstd::move(excluded_operating_systems).unwrap(),
    });
}

auto parse_source_groups(Option<ref<Toml>> value, ref<str> context)
    -> Result<Vec<ConditionalSourceGroup>> {
    auto result = Vec<ConditionalSourceGroup>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto groups = (**value).as_array();
    if (groups.is_none()) {
        return failure<Vec<ConditionalSourceGroup>>(rstd::format("{} must be an array", context));
    }
    for (usize index {}; index < (**groups).len(); ++index) {
        const auto item_context = rstd::format("{}[{}]", context, index);
        auto       table        = table_value((**groups)[index], item_context.as_str());
        if (table.is_err()) return Err(rstd::move(table).unwrap_err());
        auto known = reject_unknown(**table, item_context.as_str(), source_group_key);
        if (known.is_err()) return Err(rstd::move(known).unwrap_err());
        auto sources = declared_paths(member((**groups)[index], "sources"_str),
                                      rstd::format("{}.sources", item_context.as_str()).as_str(),
                                      true);
        auto predicate =
            parse_target_predicate(member((**groups)[index], "target"_str),
                                   rstd::format("{}.target", item_context.as_str()).as_str());
        if (sources.is_err()) return Err(rstd::move(sources).unwrap_err());
        if (predicate.is_err()) return Err(rstd::move(predicate).unwrap_err());
        result.push(ConditionalSourceGroup {
            .predicate = rstd::move(predicate).unwrap(),
            .sources   = rstd::move(sources).unwrap(),
        });
    }
    return Ok(rstd::move(result));
}

auto path_repeated(const Vec<PathBuf>& paths, ref<rstd::path::Path> candidate) -> bool {
    for (const auto& path : paths) {
        if (path.as_path() == candidate) return true;
    }
    return false;
}

auto append_attachment_source(TestAttachmentManifest& attachment, PathBuf source, ref<str> context)
    -> Result<empty> {
    if (path_repeated(attachment.sources, source.as_path())) {
        return failure<empty>(rstd::format("{} repeats source '{}'", context, source.as_path()));
    }
    attachment.sources.push(rstd::move(source));
    return Ok(empty {});
}

auto append_attachment_group(TestAttachmentManifest& attachment,
                             ConditionalSourceGroup  group,
                             ref<str>                context) -> Result<empty> {
    for (usize index {}; index < group.sources.len(); ++index) {
        for (usize prior {}; prior < index; ++prior) {
            if (group.sources[prior].as_path() == group.sources[index].as_path()) {
                return failure<empty>(rstd::format(
                    "{} repeats source '{}'", context, group.sources[index].as_path()));
            }
        }
    }
    attachment.conditional_source_groups.push(rstd::move(group));
    return Ok(empty {});
}

auto parse_test_attachments(Option<ref<Toml>> value) -> Result<Vec<TestAttachmentManifest>> {
    auto result = Vec<TestAttachmentManifest>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto entries = (**value).as_array();
    if (entries.is_none()) {
        return failure<Vec<TestAttachmentManifest>>("test.attach must be an array"_str);
    }
    for (usize index {}; index < (**entries).len(); ++index) {
        const auto context = rstd::format("test.attach[{}]", index);
        auto       table   = table_value((**entries)[index], context.as_str());
        if (table.is_err()) return Err(rstd::move(table).unwrap_err());
        auto known = reject_unknown(**table, context.as_str(), test_attachment_key);
        if (known.is_err()) return Err(rstd::move(known).unwrap_err());
        auto package = required_string((**entries)[index], "package"_str, context.as_str());
        auto sources = declared_paths(member((**entries)[index], "sources"_str),
                                      rstd::format("{}.sources", context.as_str()).as_str(),
                                      false);
        auto groups =
            parse_source_groups(member((**entries)[index], "source-groups"_str),
                                rstd::format("{}.source-groups", context.as_str()).as_str());
        if (package.is_err()) return Err(rstd::move(package).unwrap_err());
        if (sources.is_err()) return Err(rstd::move(sources).unwrap_err());
        if (groups.is_err()) return Err(rstd::move(groups).unwrap_err());
        if (! package_name_is_valid(package->as_str())) {
            return failure<Vec<TestAttachmentManifest>>(
                rstd::format("{}.package must name a valid package", context.as_str()));
        }
        if (sources->is_empty() && groups->is_empty()) {
            return failure<Vec<TestAttachmentManifest>>(
                rstd::format("{} must contain sources or source-groups", context.as_str()));
        }

        auto position = Option<usize> {};
        for (usize candidate {}; candidate < result.len(); ++candidate) {
            if (result[candidate].package == package->as_str()) {
                position = Some(candidate);
                break;
            }
        }
        if (position.is_none()) {
            result.push(TestAttachmentManifest { .package = rstd::move(package).unwrap() });
            position = Some(result.len() - usize(1));
        }
        auto& attachment = result[*position];
        for (auto& source : *sources) {
            auto appended =
                append_attachment_source(attachment, rstd::move(source), context.as_str());
            if (appended.is_err()) return Err(rstd::move(appended).unwrap_err());
        }
        for (auto& group : *groups) {
            auto appended =
                append_attachment_group(attachment, rstd::move(group), context.as_str());
            if (appended.is_err()) return Err(rstd::move(appended).unwrap_err());
        }
    }
    if (result.is_empty()) {
        return failure<Vec<TestAttachmentManifest>>("test.attach must not be empty"_str);
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

auto parse_compile_tests(Option<ref<Toml>> value) -> Result<Vec<CompileTestCase>> {
    auto result = Vec<CompileTestCase>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto cases = (**value).as_array();
    if (cases.is_none()) {
        return failure<Vec<CompileTestCase>>("compile-test.cases must be an array"_str);
    }
    auto names   = rstd::collections::BTreeMap<String, empty>::make();
    auto sources = rstd::collections::BTreeMap<String, empty>::make();
    for (usize index {}; index < (**cases).len(); ++index) {
        const auto  context = rstd::format("compile-test.cases[{}]", index);
        const auto& value   = (**cases)[index];
        auto        table   = table_value(value, context.as_str());
        if (table.is_err()) return Err(rstd::move(table).unwrap_err());
        auto known = reject_unknown(**table, context.as_str(), compile_test_case_key);
        if (known.is_err()) return Err(rstd::move(known).unwrap_err());
        auto name    = required_string(value, "name"_str, context.as_str());
        auto source  = required_string(value, "source"_str, context.as_str());
        auto outcome = required_string(value, "outcome"_str, context.as_str());
        auto options = string_array(member(value, "options"_str),
                                    rstd::format("{}.options", context.as_str()).as_str());
        auto contains =
            string_array(member(value, "diagnostic-contains"_str),
                         rstd::format("{}.diagnostic-contains", context.as_str()).as_str());
        auto contains_any =
            string_array(member(value, "diagnostic-contains-any"_str),
                         rstd::format("{}.diagnostic-contains-any", context.as_str()).as_str());
        if (name.is_err()) return Err(rstd::move(name).unwrap_err());
        if (source.is_err()) return Err(rstd::move(source).unwrap_err());
        if (outcome.is_err()) return Err(rstd::move(outcome).unwrap_err());
        if (options.is_err()) return Err(rstd::move(options).unwrap_err());
        if (contains.is_err()) return Err(rstd::move(contains).unwrap_err());
        if (contains_any.is_err()) return Err(rstd::move(contains_any).unwrap_err());
        if (name->is_empty()) {
            return failure<Vec<CompileTestCase>>(
                rstd::format("{}.name must not be empty", context.as_str()));
        }
        if (names.contains_key(name->as_str())) {
            return failure<Vec<CompileTestCase>>(
                rstd::format("compile-test repeats case name '{}'", name->as_str()));
        }
        auto relative = relative_path(rstd::move(source).unwrap(),
                                      rstd::format("{}.source", context.as_str()).as_str());
        if (relative.is_err()) return Err(rstd::move(relative).unwrap_err());
        auto source_text = relative->as_path().to_str();
        if (source_text.is_none()) {
            return failure<Vec<CompileTestCase>>(
                rstd::format("{}.source is not valid UTF-8", context.as_str()));
        }
        if (sources.contains_key(*source_text)) {
            return failure<Vec<CompileTestCase>>(rstd::format(
                "compile-test source '{}' is used by more than one case", relative->as_path()));
        }
        auto expected = CompileTestOutcome::Failure;
        if (outcome->as_str() == "success"_str) {
            expected = CompileTestOutcome::Success;
        } else if (outcome->as_str() != "failure"_str) {
            return failure<Vec<CompileTestCase>>(
                rstd::format("{}.outcome must be success or failure", context.as_str()));
        }
        auto option_values       = rstd::move(options).unwrap();
        auto contains_values     = rstd::move(contains).unwrap();
        auto contains_any_values = rstd::move(contains_any).unwrap();
        if (expected == CompileTestOutcome::Success &&
            (! contains_values.is_empty() || ! contains_any_values.is_empty())) {
            return failure<Vec<CompileTestCase>>(rstd::format(
                "{} cannot require diagnostics for a successful outcome", context.as_str()));
        }
        names.insert(name->clone(), empty {});
        sources.insert(String::make(*source_text), empty {});
        result.push(CompileTestCase {
            .name                    = rstd::move(name).unwrap(),
            .source                  = rstd::move(relative).unwrap(),
            .outcome                 = expected,
            .options                 = rstd::move(option_values),
            .diagnostic_contains     = rstd::move(contains_values),
            .diagnostic_contains_any = rstd::move(contains_any_values),
        });
    }
    if (result.is_empty()) {
        return failure<Vec<CompileTestCase>>("compile-test.cases must not be empty"_str);
    }
    return Ok(rstd::move(result));
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
    auto private_linker_valid =
        validate_linker_options(private_linker_option_values, "usage.private-linker-options"_str);
    if (public_definitions_valid.is_err()) {
        return Err(rstd::move(public_definitions_valid).unwrap_err());
    }
    if (private_definitions_valid.is_err()) {
        return Err(rstd::move(private_definitions_valid).unwrap_err());
    }
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

auto parse_pkg_config_version(ref<str> value, ref<str> context)
    -> Result<PkgConfigVersionRequirement> {
    auto text       = value.trim_ascii();
    auto comparison = PkgConfigVersionOperator::Equal;
    auto prefix     = usize {};
    if (text.starts_with(">="_str)) {
        comparison = PkgConfigVersionOperator::GreaterEqual;
        prefix     = usize(2);
    } else if (text.starts_with("<="_str)) {
        comparison = PkgConfigVersionOperator::LessEqual;
        prefix     = usize(2);
    } else if (text.starts_with("="_str)) {
        comparison = PkgConfigVersionOperator::Equal;
        prefix     = usize(1);
    } else if (text.starts_with(">"_str)) {
        comparison = PkgConfigVersionOperator::Greater;
        prefix     = usize(1);
    } else if (text.starts_with("<"_str)) {
        comparison = PkgConfigVersionOperator::Less;
        prefix     = usize(1);
    } else {
        return failure<PkgConfigVersionRequirement>(
            rstd::format("{} must begin with one of '=', '<', '>', '<=', or '>='", context));
    }
    auto version = text.get(prefix, text.len());
    if (version.is_none()) {
        return failure<PkgConfigVersionRequirement>(
            rstd::format("{} must contain a version value", context));
    }
    auto normalized = version->trim_ascii();
    if (normalized.is_empty() || normalized.contains(" "_str) || normalized.contains("\t"_str) ||
        normalized.contains("<"_str) || normalized.contains(">"_str) ||
        normalized.contains("="_str)) {
        return failure<PkgConfigVersionRequirement>(
            rstd::format("{} contains an invalid version value", context));
    }
    return Ok(PkgConfigVersionRequirement {
        .comparison = comparison,
        .value      = String::make(normalized),
    });
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
        auto pkg_config      = optional_string(**specification, "pkg-config"_str, "dependency"_str);
        auto version         = optional_string(**specification, "version"_str, "dependency"_str);
        auto branch          = optional_string(**specification, "branch"_str, "dependency"_str);
        auto tag             = optional_string(**specification, "tag"_str, "dependency"_str);
        auto rev             = optional_string(**specification, "rev"_str, "dependency"_str);
        auto visibility_text = required_string(**specification, "visibility"_str, "dependency"_str);
        if (path_text.is_err()) return Err(rstd::move(path_text).unwrap_err());
        if (git.is_err()) return Err(rstd::move(git).unwrap_err());
        if (pkg_config.is_err()) return Err(rstd::move(pkg_config).unwrap_err());
        if (version.is_err()) return Err(rstd::move(version).unwrap_err());
        if (branch.is_err()) return Err(rstd::move(branch).unwrap_err());
        if (tag.is_err()) return Err(rstd::move(tag).unwrap_err());
        if (rev.is_err()) return Err(rstd::move(rev).unwrap_err());
        if (visibility_text.is_err()) {
            return Err(rstd::move(visibility_text).unwrap_err());
        }
        auto path_value       = rstd::move(path_text).unwrap();
        auto git_value        = rstd::move(git).unwrap();
        auto pkg_config_value = rstd::move(pkg_config).unwrap();
        auto source_count     = usize {};
        if (path_value.is_some()) ++source_count;
        if (git_value.is_some()) ++source_count;
        if (pkg_config_value.is_some()) ++source_count;
        if (source_count != usize(1)) {
            return failure<Vec<DeclaredDependency>>(rstd::format(
                "dependency '{}' must contain exactly one of 'path', 'git', or 'pkg-config'",
                name.as_str()));
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
        if (! git_value.is_some() && selector_count != usize {}) {
            return failure<Vec<DeclaredDependency>>(
                rstd::format("dependency '{}' Git selector requires 'git'", name.as_str()));
        }
        auto version_value = rstd::move(version).unwrap();
        auto static_value  = false;
        auto static_member = member(**specification, "static"_str);
        if (static_member.is_some()) {
            auto parsed_static = (**static_member).as_bool();
            if (parsed_static.is_none()) {
                return failure<Vec<DeclaredDependency>>(
                    rstd::format("dependency '{}'.static must be a boolean", name.as_str()));
            }
            static_value = *parsed_static;
        }
        if (pkg_config_value.is_none() && (version_value.is_some() || static_member.is_some())) {
            return failure<Vec<DeclaredDependency>>(rstd::format(
                "dependency '{}' version and static require 'pkg-config'", name.as_str()));
        }
        auto source = Option<DeclaredDependencySource> {};
        if (path_value.is_some()) {
            auto path = relative_path(rstd::move(path_value).unwrap(), "dependency.path"_str);
            if (path.is_err()) return Err(rstd::move(path).unwrap_err());
            source = Some(DeclaredDependencySource::Path(rstd::move(path).unwrap()));
        } else if (git_value.is_some()) {
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
            source = Some(DeclaredDependencySource::Git(rstd::move(url), rstd::move(reference)));
        } else {
            auto module = rstd::move(pkg_config_value).unwrap();
            if (module.is_empty() || module.as_str().starts_with("-"_str)) {
                return failure<Vec<DeclaredDependency>>(rstd::format(
                    "dependency '{}'.pkg-config must be non-empty and must not start with '-'",
                    name.as_str()));
            }
            auto requirement = Option<PkgConfigVersionRequirement> {};
            if (version_value.is_some()) {
                auto parsed_version =
                    parse_pkg_config_version(version_value->as_str(), "dependency.version"_str);
                if (parsed_version.is_err()) {
                    return Err(rstd::move(parsed_version).unwrap_err());
                }
                requirement = Some(rstd::move(parsed_version).unwrap());
            }
            source = Some(DeclaredDependencySource::PkgConfig(PkgConfigDependencyRequirement {
                .module  = rstd::move(module),
                .version = rstd::move(requirement),
                .mode    = static_value ? PkgConfigQueryMode::Static : PkgConfigQueryMode::Shared,
            }));
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
        return Ok(ManifestDocument {
            .kind      = ManifestKind::Workspace,
            .workspace = Some(WorkspaceManifest {
                .name            = rstd::move(workspace_name).unwrap(),
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

    auto library_value_option      = member(document, "library"_str);
    auto executable_value_option   = member(document, "executable"_str);
    auto test_value_option         = member(document, "test"_str);
    auto compile_test_value_option = member(document, "compile-test"_str);
    auto artifact_count            = usize {};
    if (library_value_option.is_some()) ++artifact_count;
    if (executable_value_option.is_some()) ++artifact_count;
    if (test_value_option.is_some()) ++artifact_count;
    if (compile_test_value_option.is_some()) ++artifact_count;
    if (artifact_count != usize(1)) {
        return failure<ManifestDocument>(
            "manifest must contain exactly one of 'library', 'executable', 'test', or "
            "'compile-test'"_str);
    }
    auto artifact_kind = ArtifactKind::TestExecutable;
    if (library_value_option.is_some()) artifact_kind = ArtifactKind::StaticLibrary;
    if (executable_value_option.is_some()) artifact_kind = ArtifactKind::Executable;
    if (compile_test_value_option.is_some()) artifact_kind = ArtifactKind::CompileTest;
    const auto& artifact_value = library_value_option.is_some()      ? **library_value_option
                                 : executable_value_option.is_some() ? **executable_value_option
                                 : test_value_option.is_some()       ? **test_value_option
                                                                     : **compile_test_value_option;
    const auto  artifact_context =
        artifact_kind == ArtifactKind::StaticLibrary    ? "manifest.library"_str
        : artifact_kind == ArtifactKind::Executable     ? "manifest.executable"_str
        : artifact_kind == ArtifactKind::TestExecutable ? "manifest.test"_str
                                                        : "manifest.compile-test"_str;
    auto artifact_table = table_value(artifact_value, artifact_context);
    if (artifact_table.is_err()) return Err(rstd::move(artifact_table).unwrap_err());
    auto artifact_known =
        reject_unknown(**artifact_table,
                       artifact_context,
                       artifact_kind == ArtifactKind::StaticLibrary    ? library_key
                       : artifact_kind == ArtifactKind::CompileTest    ? compile_test_key
                       : artifact_kind == ArtifactKind::TestExecutable ? test_key
                                                                       : executable_key);
    if (artifact_known.is_err()) return Err(rstd::move(artifact_known).unwrap_err());

    const auto& package_value = **member(document, "package"_str);
    auto        name          = required_string(package_value, "name"_str, "package"_str);
    auto        version       = parse_package_version(package_value);
    auto        root_module   = optional_string(package_value, "module"_str, "package"_str);
    if (name.is_err()) return Err(rstd::move(name).unwrap_err());
    if (version.is_err()) return Err(rstd::move(version).unwrap_err());
    if (root_module.is_err()) return Err(rstd::move(root_module).unwrap_err());
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
    Result<String> artifact_name = Ok(name->clone());
    if (artifact_kind != ArtifactKind::CompileTest) {
        artifact_name = required_string(artifact_value,
                                        artifact_kind == ArtifactKind::StaticLibrary ? "archive"_str
                                                                                     : "name"_str,
                                        artifact_context);
    }
    if (artifact_name.is_err()) return Err(rstd::move(artifact_name).unwrap_err());
    if (! valid_artifact_name(artifact_name->as_str())) {
        return failure<ManifestDocument>(rstd::format(
            "{}.{} must be a safe artifact basename",
            artifact_context,
            artifact_kind == ArtifactKind::StaticLibrary ? "archive"_str : "name"_str));
    }
    Result<String> discovery_text = Ok(String::make("explicit"_str));
    if (artifact_kind != ArtifactKind::CompileTest) {
        discovery_text = required_string(artifact_value, "discovery"_str, artifact_context);
    }
    if (discovery_text.is_err()) return Err(rstd::move(discovery_text).unwrap_err());
    const auto explicit_discovery = discovery_text->as_str() == "explicit"_str;
    const auto module_discovery   = discovery_text->as_str() == "module"_str;
    if (! explicit_discovery && ! module_discovery) {
        return failure<ManifestDocument>(
            rstd::format("{}.discovery must be explicit or module", artifact_context));
    }
    Result<Vec<ConditionalSourceGroup>> source_groups = Ok(Vec<ConditionalSourceGroup>::make());
    if (artifact_kind != ArtifactKind::CompileTest) {
        source_groups =
            parse_source_groups(member(artifact_value, "source-groups"_str),
                                rstd::format("{}.source-groups", artifact_context).as_str());
    }
    if (source_groups.is_err()) return Err(rstd::move(source_groups).unwrap_err());
    Result<Vec<TestAttachmentManifest>> test_attachments = Ok(Vec<TestAttachmentManifest>::make());
    if (artifact_kind == ArtifactKind::TestExecutable) {
        test_attachments = parse_test_attachments(member(artifact_value, "attach"_str));
    }
    if (test_attachments.is_err()) {
        return Err(rstd::move(test_attachments).unwrap_err());
    }
    Result<Vec<CompileTestCase>> compile_tests = Ok(Vec<CompileTestCase>::make());
    if (artifact_kind == ArtifactKind::CompileTest) {
        compile_tests = parse_compile_tests(member(artifact_value, "cases"_str));
    }
    if (compile_tests.is_err()) return Err(rstd::move(compile_tests).unwrap_err());
    auto sources = Vec<PathBuf>::make();
    if (artifact_kind == ArtifactKind::CompileTest) {
        for (const auto& item : *compile_tests) sources.push(item.source.clone());
    } else {
        auto parsed_sources =
            declared_paths(member(artifact_value, "sources"_str),
                           artifact_kind == ArtifactKind::StaticLibrary ? "library.sources"_str
                           : artifact_kind == ArtifactKind::Executable  ? "executable.sources"_str
                                                                        : "test.sources"_str,
                           explicit_discovery && source_groups->is_empty());
        if (parsed_sources.is_err()) return Err(rstd::move(parsed_sources).unwrap_err());
        sources = rstd::move(parsed_sources).unwrap();
    }

    auto usage        = parse_usage(member(document, "usage"_str), root.as_path());
    auto dependencies = parse_dependencies(member(document, "dependencies"_str));
    auto target = parse_target_predicate(member(package_value, "target"_str), "package.target"_str);
    if (usage.is_err()) return Err(rstd::move(usage).unwrap_err());
    if (dependencies.is_err()) return Err(rstd::move(dependencies).unwrap_err());
    if (target.is_err()) return Err(rstd::move(target).unwrap_err());

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
            .declared_sources          = rstd::move(sources),
            .conditional_source_groups = rstd::move(source_groups).unwrap(),
            .test_attachments          = rstd::move(test_attachments).unwrap(),
            .target                    = rstd::move(target).unwrap(),
            .compile_tests             = rstd::move(compile_tests).unwrap(),
            .usage                     = rstd::move(usage).unwrap(),
            .dependencies              = rstd::move(dependencies).unwrap(),
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
