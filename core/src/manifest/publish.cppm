module;
#include <rstd/macro.hpp>

export module lito.core:manifest.publish;

import rstd;
import :manifest.package;
import :manifest.standalone;
import :manifest.target;
import :dependency.usage;
import :source.tree;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

export namespace lito::manifest
{

struct PackagePublishPolicy {
    Vec<PathBuf>    excluded_roots;
    Option<PathBuf> archive;
};

struct PackageFileSetError {
    String message;
};

template<typename T>
using PackageFileSetResult = Result<T, PackageFileSetError>;

class PackageFileSet {
    lito::source::SourceTree tree_;
    Vec<PathBuf>             directories_;

    PackageFileSet(lito::source::SourceTree tree, Vec<PathBuf> directories)
        : tree_(rstd::move(tree)), directories_(rstd::move(directories)) {}
    friend class PackageFileSetResolver;

public:
    auto tree() const noexcept -> const lito::source::SourceTree& { return tree_; }
    auto directories() const noexcept -> const Vec<PathBuf>& { return directories_; }

    auto paths() const -> Vec<String> {
        auto result = Vec<String>::make();
        for (const auto& entry : tree_.entries()) {
            if (entry.kind() == lito::source::SourceEntryKind::File) {
                result.push(String::make(entry.path().as_str()));
            }
        }
        return result;
    }
};

class PackageFileSetResolver {
public:
    static auto resolve(const PackageManifest& manifest, const PackagePublishPolicy& policy = {})
        -> PackageFileSetResult<PackageFileSet>;
};

} // namespace lito::manifest

namespace
{

using namespace lito::manifest;

template<typename T>
auto publish_failure(String message) -> PackageFileSetResult<T> {
    return Err(PackageFileSetError { .message = rstd::move(message) });
}

template<typename T>
auto publish_failure(ref<str> message) -> PackageFileSetResult<T> {
    return publish_failure<T>(String::make(message));
}

struct PublishGlob {
    String      source;
    Vec<String> components;
    usize       matches {};
};

auto split_components(ref<str> value) -> Vec<String> {
    auto result    = Vec<String>::make();
    auto remaining = value;
    while (true) {
        auto separated = remaining.split_once("/"_str);
        if (separated.is_none()) {
            result.push(String::make(remaining));
            return result;
        }
        result.push(String::make(separated->template get<0>()));
        remaining = separated->template get<1>();
    }
}

auto parse_glob(ref<str> value) -> PackageFileSetResult<PublishGlob> {
    if (value.is_empty() || value.starts_with("/"_str) || value.starts_with("!"_str) ||
        value.contains("\\"_str) || value.contains("?"_str) || value.contains("["_str) ||
        value.contains("]"_str) || value.contains("{"_str) || value.contains("}"_str)) {
        return publish_failure<PublishGlob>(
            rstd::format("package publish pattern '{}' uses unsupported syntax", value));
    }
    auto components = split_components(value);
    for (const auto& component : components) {
        if (component.is_empty() || component == "."_str || component == ".."_str ||
            (component != "**"_str && component.as_str().contains("**"_str))) {
            return publish_failure<PublishGlob>(
                rstd::format("package publish pattern '{}' is not portable", value));
        }
    }
    return Ok(PublishGlob {
        .source     = String::make(value),
        .components = rstd::move(components),
    });
}

auto component_matches(ref<str> pattern, ref<str> value) noexcept -> bool {
    auto pattern_index = usize {};
    auto value_index   = usize {};
    auto star          = Option<usize> {};
    auto after_star    = usize {};
    while (value_index < value.len()) {
        if (pattern_index < pattern.len() && pattern.as_bytes()[pattern_index] != u8('*') &&
            pattern.as_bytes()[pattern_index] == value.as_bytes()[value_index]) {
            ++pattern_index;
            ++value_index;
            continue;
        }
        if (pattern_index < pattern.len() && pattern.as_bytes()[pattern_index] == u8('*')) {
            star       = Some(pattern_index);
            after_star = value_index;
            ++pattern_index;
            continue;
        }
        if (star.is_none()) return false;
        pattern_index = *star + usize(1);
        value_index   = ++after_star;
    }
    while (pattern_index < pattern.len() && pattern.as_bytes()[pattern_index] == u8('*')) {
        ++pattern_index;
    }
    return pattern_index == pattern.len();
}

auto glob_matches(const Vec<String>& pattern,
                  const Vec<String>& path,
                  usize              pattern_index = {},
                  usize              path_index    = {}) noexcept -> bool {
    if (pattern_index == pattern.len()) return path_index == path.len();
    if (pattern[pattern_index] == "**"_str) {
        return glob_matches(pattern, path, pattern_index + usize(1), path_index) ||
               (path_index < path.len() &&
                glob_matches(pattern, path, pattern_index, path_index + usize(1)));
    }
    return path_index < path.len() &&
           component_matches(pattern[pattern_index].as_str(), path[path_index].as_str()) &&
           glob_matches(pattern, path, pattern_index + usize(1), path_index + usize(1));
}

auto glob_matches(PublishGlob& pattern, ref<str> path) -> bool {
    auto components = split_components(path);
    if (! glob_matches(pattern.components, components)) return false;
    ++pattern.matches;
    return true;
}

auto glob_can_target_descendant(const Vec<String>& pattern,
                                const Vec<String>& root,
                                usize              pattern_index = {},
                                usize              root_index    = {}) noexcept -> bool {
    if (root_index == root.len()) return pattern_index < pattern.len();
    if (pattern_index == pattern.len()) return false;
    if (pattern[pattern_index] == "**"_str) {
        return glob_can_target_descendant(pattern, root, pattern_index + usize(1), root_index) ||
               glob_can_target_descendant(pattern, root, pattern_index, root_index + usize(1));
    }
    return component_matches(pattern[pattern_index].as_str(), root[root_index].as_str()) &&
           glob_can_target_descendant(
               pattern, root, pattern_index + usize(1), root_index + usize(1));
}

auto fixed_directory(ref<str> name, ref<str> portable) noexcept -> bool {
    return name == ".git"_str || name == ".hg"_str || name == ".svn"_str || name == ".lito"_str ||
           portable == "build"_str;
}

auto under_policy_root(ref<rstd::path::Path> path, const PackagePublishPolicy& policy) noexcept
    -> bool {
    for (const auto& root : policy.excluded_roots) {
        if (path.starts_with(root.as_path())) return true;
    }
    return false;
}

auto is_archive_path(ref<rstd::path::Path> path, const PackagePublishPolicy& policy) noexcept
    -> bool {
    return policy.archive.is_some() && path == policy.archive->as_path();
}

struct FileSetState {
    const PackageManifest&                     manifest;
    const PackagePublishPolicy&                policy;
    Vec<PublishGlob>                           includes;
    Vec<PublishGlob>                           excludes;
    lito::source::SourceTree                   tree { lito::source::SourceTree::make() };
    rstd::collections::BTreeMap<String, empty> candidates;
    rstd::collections::BTreeMap<String, empty> selected;
    Vec<String>                                pruned_roots;
    Vec<PathBuf>                               directories;
};

auto nested_manifest(ref<rstd::path::Path> directory) -> PackageFileSetResult<bool> {
    auto path   = PathBuf::from(directory).join(PathBuf::from("lito.toml"_str).as_path());
    auto exists = rstd::fs::exists(path.as_path());
    if (exists.is_err()) {
        return publish_failure<bool>(rstd::format("cannot inspect nested package manifest '{}': {}",
                                                  path.as_path(),
                                                  exists.unwrap_err()));
    }
    if (! *exists) return Ok(false);
    auto metadata = rstd::fs::symlink_metadata(path.as_path());
    if (metadata.is_err()) {
        return publish_failure<bool>(rstd::format("cannot inspect nested package manifest '{}': {}",
                                                  path.as_path(),
                                                  metadata.unwrap_err()));
    }
    return Ok(metadata->is_file() && ! metadata->is_symlink());
}

auto explicit_pattern_crosses_nested(const FileSetState& state, ref<str> portable) noexcept
    -> bool {
    auto root = split_components(portable);
    for (const auto& pattern : state.includes) {
        if (glob_can_target_descendant(pattern.components, root)) return true;
    }
    for (const auto& pattern : state.excludes) {
        if (glob_can_target_descendant(pattern.components, root)) return true;
    }
    return false;
}

auto selected_by_patterns(FileSetState& state, ref<str> portable) -> bool {
    auto included = state.includes.is_empty();
    for (auto& pattern : state.includes) {
        if (glob_matches(pattern, portable)) included = true;
    }
    auto excluded = false;
    for (auto& pattern : state.excludes) {
        if (glob_matches(pattern, portable)) excluded = true;
    }
    return portable == "lito.toml"_str || (included && ! excluded);
}

auto append_file(FileSetState&             state,
                 ref<rstd::path::Path>     physical,
                 ref<str>                  portable,
                 const rstd::fs::Metadata& metadata) -> PackageFileSetResult<empty> {
    if (metadata.nlink() > u64(1)) {
        return publish_failure<empty>(
            rstd::format("published file '{}' is a hardlink alias", physical));
    }
    auto validated = lito::source::SourcePath::parse(portable);
    if (validated.is_err()) {
        return publish_failure<empty>(
            rstd::format("published path '{}': {}", portable, rstd::move(validated).unwrap_err()));
    }
    state.candidates.insert(String::make(portable), empty {});
    if (! selected_by_patterns(state, portable)) return Ok(empty {});
    auto contents = rstd::fs::read(physical);
    if (contents.is_err()) {
        return publish_failure<empty>(
            rstd::format("cannot read published file '{}': {}", physical, contents.unwrap_err()));
    }
    auto mode  = (metadata.permissions().mode() & u32(0111)) == u32 {}
                     ? lito::source::SourceFileMode::Regular
                     : lito::source::SourceFileMode::Executable;
    auto added = state.tree.add_bytes(portable, contents->as_slice(), mode);
    if (added.is_err()) {
        return publish_failure<empty>(rstd::format(
            "cannot add published file '{}': {}", portable, rstd::move(added).unwrap_err()));
    }
    state.selected.insert(String::make(portable), empty {});
    return Ok(empty {});
}

auto collect_directory(FileSetState& state, ref<rstd::path::Path> physical, ref<str> prefix)
    -> PackageFileSetResult<empty> {
    state.directories.push(PathBuf::from(physical));
    auto opened = rstd::fs::read_dir(physical);
    if (opened.is_err()) {
        return publish_failure<empty>(rstd::format(
            "cannot enumerate package directory '{}': {}", physical, opened.unwrap_err()));
    }
    auto entries = rstd::move(opened).unwrap();
    for (auto item : entries) {
        if (item.is_err()) {
            return publish_failure<empty>(rstd::format(
                "cannot enumerate package directory '{}': {}", physical, item.unwrap_err()));
        }
        auto entry = rstd::move(item).unwrap();
        auto name  = entry.file_name();
        auto text  = name.as_os_str().to_str();
        if (text.is_none()) {
            return publish_failure<empty>(
                rstd::format("package directory '{}' contains a non-UTF-8 name", physical));
        }
        auto portable =
            prefix.is_empty() ? String::make(*text) : rstd::format("{}/{}", prefix, *text);
        auto path = entry.path();
        auto type = entry.file_type();
        if (type.is_err()) {
            return publish_failure<empty>(rstd::format(
                "cannot inspect package entry '{}': {}", path.as_path(), type.unwrap_err()));
        }
        if (type->is_dir()) {
            if (fixed_directory(*text, portable.as_str()) ||
                under_policy_root(path.as_path(), state.policy)) {
                state.pruned_roots.push(rstd::move(portable));
                continue;
            }
            if (rstd_try(nested_manifest(path.as_path()))) {
                if (explicit_pattern_crosses_nested(state, portable.as_str())) {
                    return publish_failure<empty>(rstd::format(
                        "package publish pattern crosses nested package root '{}'", portable));
                }
                state.pruned_roots.push(rstd::move(portable));
                continue;
            }
            rstd_try(collect_directory(state, path.as_path(), portable.as_str()));
            continue;
        }
        if (type->is_symlink() || ! type->is_file()) {
            return publish_failure<empty>(
                rstd::format("published entry '{}' must be a regular file", path.as_path()));
        }
        if (under_policy_root(path.as_path(), state.policy) ||
            is_archive_path(path.as_path(), state.policy)) {
            state.pruned_roots.push(rstd::move(portable));
            continue;
        }
        auto metadata = rstd::fs::symlink_metadata(path.as_path());
        if (metadata.is_err()) {
            return publish_failure<empty>(rstd::format(
                "cannot inspect package file '{}': {}", path.as_path(), metadata.unwrap_err()));
        }
        rstd_try(append_file(state, path.as_path(), portable.as_str(), *metadata));
    }
    return Ok(empty {});
}

auto portable_path(const PackageManifest& manifest, ref<rstd::path::Path> path, ref<str> owner)
    -> PackageFileSetResult<String> {
    auto canonical = rstd::fs::canonicalize(path);
    if (canonical.is_err()) {
        return publish_failure<String>(rstd::format(
            "cannot resolve published {} '{}': {}", owner, path, canonical.unwrap_err()));
    }
    auto relative = canonical->as_path().strip_prefix(manifest.root.as_path());
    if (relative.is_none() || relative->is_empty()) {
        return publish_failure<String>(
            rstd::format("published {} '{}' must be inside package root", owner, path));
    }
    auto parsed = lito::source::SourcePath::from_relative_path(*relative);
    if (parsed.is_err()) {
        return publish_failure<String>(rstd::format(
            "published {} path '{}': {}", owner, *relative, rstd::move(parsed).unwrap_err()));
    }
    return Ok(String::make(parsed->as_str()));
}

auto path_is_under(ref<str> path, ref<str> root) noexcept -> bool {
    if (path == root) return true;
    auto prefix = rstd::format("{}/", root);
    return path.starts_with(prefix.as_str());
}

auto require_published_path(const FileSetState&   state,
                            ref<rstd::path::Path> physical,
                            ref<str>              owner,
                            bool complete_directory = false) -> PackageFileSetResult<empty> {
    auto portable = rstd_try(portable_path(state.manifest, physical, owner));
    auto metadata = rstd::fs::symlink_metadata(physical);
    if (metadata.is_err()) {
        return publish_failure<empty>(rstd::format(
            "cannot inspect published {} '{}': {}", owner, physical, metadata.unwrap_err()));
    }
    if (metadata->is_symlink() || (! metadata->is_file() && ! metadata->is_dir())) {
        return publish_failure<empty>(
            rstd::format("published {} '{}' must be a regular file or directory", owner, physical));
    }
    for (const auto& root : state.pruned_roots) {
        if (path_is_under(portable.as_str(), root.as_str()) ||
            (complete_directory && path_is_under(root.as_str(), portable.as_str()))) {
            return publish_failure<empty>(rstd::format(
                "published {} '{}' is excluded from the package file set", owner, portable));
        }
    }
    if (metadata->is_file()) {
        if (! state.selected.contains_key(portable.as_str())) {
            return publish_failure<empty>(rstd::format(
                "published {} '{}' is not selected by package.publish", owner, portable));
        }
        return Ok(empty {});
    }
    auto selected_any = false;
    for (auto candidate : state.candidates.keys()) {
        if (! path_is_under((*candidate).as_str(), portable.as_str())) continue;
        selected_any = true;
        if (complete_directory && ! state.selected.contains_key((*candidate).as_str())) {
            return publish_failure<empty>(rstd::format(
                "published {} directory '{}' is only partially selected", owner, portable));
        }
    }
    if (! selected_any) {
        return publish_failure<empty>(rstd::format(
            "published {} directory '{}' contains no selected files", owner, portable));
    }
    return Ok(empty {});
}

auto require_relative(const FileSetState&   state,
                      ref<rstd::path::Path> root,
                      ref<rstd::path::Path> relative,
                      ref<str>              owner,
                      bool complete_directory = false) -> PackageFileSetResult<empty> {
    auto physical = PathBuf::from(root).join(relative);
    return require_published_path(state, physical.as_path(), owner, complete_directory);
}

auto require_include_directories(
    const FileSetState&                                       state,
    const Vec<lito::dependency::IncludeDirectoryRequirement>& requirements)
    -> PackageFileSetResult<empty> {
    for (const auto& requirement : requirements) {
        if (requirement.root != lito::dependency::IncludeDirectoryRoot::Package) continue;
        rstd_try(require_relative(state,
                                  state.manifest.source_root.as_path(),
                                  requirement.path.as_path(),
                                  "include directory"_str,
                                  true));
    }
    return Ok(empty {});
}

auto validate_references(const FileSetState& state) -> PackageFileSetResult<empty> {
    for (const auto& target : state.manifest.targets) {
        const auto& source = package_target_source(target);
        for (const auto& path : source.declared_sources) {
            rstd_try(require_relative(
                state, state.manifest.source_root.as_path(), path.as_path(), "target source"_str));
        }
        auto attachments = package_target_attachments(target);
        if (attachments.is_some()) {
            for (const auto& attachment : **attachments) {
                for (const auto& path : attachment.sources) {
                    rstd_try(require_relative(state,
                                              state.manifest.source_root.as_path(),
                                              path.as_path(),
                                              "test attachment"_str));
                }
            }
        }
    }
    for (const auto& group : state.manifest.source_groups) {
        if (group.root != SourceGroupRoot::Package || group.external_source.is_some()) continue;
        for (const auto& path : group.sources) {
            rstd_try(require_relative(state,
                                      state.manifest.source_root.as_path(),
                                      path.as_path(),
                                      "source group file"_str));
        }
    }
    for (const auto& test : state.manifest.compile_tests) {
        rstd_try(require_relative(state,
                                  state.manifest.source_root.as_path(),
                                  test.source.as_path(),
                                  "compile-test source"_str));
    }
    if (state.manifest.install_script.is_some()) {
        rstd_try(require_published_path(
            state, state.manifest.install_script->as_path(), "install script"_str));
    }
    auto build_script     = state.manifest.root.join(PathBuf::from("build.lua"_str).as_path());
    auto has_build_script = rstd::fs::exists(build_script.as_path());
    if (has_build_script.is_err()) {
        return publish_failure<empty>(rstd::format("cannot inspect build script '{}': {}",
                                                   build_script.as_path(),
                                                   has_build_script.unwrap_err()));
    }
    if (*has_build_script) {
        rstd_try(require_published_path(state, build_script.as_path(), "build script"_str));
    }
    rstd_try(require_include_directories(
        state, state.manifest.usage.public_include_directory_requirements));
    rstd_try(require_include_directories(
        state, state.manifest.usage.private_include_directory_requirements));
    for (const auto& condition : state.manifest.conditions) {
        rstd_try(require_include_directories(
            state, condition.usage.values.public_include_directory_requirements));
        rstd_try(require_include_directories(
            state, condition.usage.values.private_include_directory_requirements));
    }
    for (const auto& source : state.manifest.external_sources) {
        if (! source.source.is_Path()) continue;
        auto root = source.declaration_root.is_some() ? source.declaration_root->as_path()
                                                      : state.manifest.root.as_path();
        rstd_try(require_relative(
            state, root, source.source.as_Path().path.as_path(), "external source"_str, true));
    }
    for (const auto& dependency : state.manifest.cmake_external_dependencies) {
        if (dependency.adapter.is_none()) continue;
        auto root = dependency.adapter_root.is_some() ? dependency.adapter_root->as_path()
                                                      : state.manifest.root.as_path();
        rstd_try(require_relative(state, root, dependency.adapter->as_path(), "CMake adapter"_str));
    }
    return Ok(empty {});
}

auto include_package_readme(FileSetState& state) -> PackageFileSetResult<empty> {
    if (state.manifest.readme.path.is_none() || state.manifest.readme.archive_path.is_none()) {
        return Ok(empty {});
    }
    const auto& physical = *state.manifest.readme.path;
    const auto& portable = *state.manifest.readme.archive_path;
    auto        metadata = rstd::fs::symlink_metadata(physical.as_path());
    if (metadata.is_err()) {
        return publish_failure<empty>(rstd::format(
            "cannot inspect package.readme '{}': {}", physical.as_path(), metadata.unwrap_err()));
    }
    if (metadata->is_symlink() || ! metadata->is_file()) {
        return publish_failure<empty>(
            rstd::format("package.readme '{}' must be a regular file", physical.as_path()));
    }
    if (metadata->nlink() > u64(1)) {
        return publish_failure<empty>(
            rstd::format("package.readme '{}' is a hardlink alias", physical.as_path()));
    }
    auto validated = lito::source::SourcePath::parse(portable.as_str());
    if (validated.is_err()) {
        return publish_failure<empty>(
            rstd::format("package.readme archive path '{}': {}", portable, validated.unwrap_err()));
    }

    auto canonical = rstd::fs::canonicalize(physical.as_path());
    if (canonical.is_err()) {
        return publish_failure<empty>(rstd::format(
            "cannot resolve package.readme '{}': {}", physical.as_path(), canonical.unwrap_err()));
    }
    auto package_relative  = canonical->as_path().strip_prefix(state.manifest.root.as_path());
    auto same_package_file = false;
    if (package_relative.is_some()) {
        auto package_path = lito::source::SourcePath::from_relative_path(*package_relative);
        same_package_file = package_path.is_ok() && package_path->as_str() == portable.as_str();
    }
    if (state.candidates.contains_key(portable.as_str()) && ! same_package_file) {
        return publish_failure<empty>(
            rstd::format("package.readme '{}' conflicts with package file '{}'",
                         physical.as_path(),
                         portable.as_str()));
    }
    if (state.selected.contains_key(portable.as_str())) return Ok(empty {});

    auto contents = rstd::fs::read(physical.as_path());
    if (contents.is_err()) {
        return publish_failure<empty>(rstd::format(
            "cannot read package.readme '{}': {}", physical.as_path(), contents.unwrap_err()));
    }
    auto mode  = (metadata->permissions().mode() & u32(0111)) == u32 {}
                     ? lito::source::SourceFileMode::Regular
                     : lito::source::SourceFileMode::Executable;
    auto added = state.tree.add_bytes(portable.as_str(), contents->as_slice(), mode);
    if (added.is_err()) {
        return publish_failure<empty>(rstd::format(
            "cannot add package.readme '{}': {}", portable, rstd::move(added).unwrap_err()));
    }
    state.candidates.insert(portable.clone(), empty {});
    state.selected.insert(portable.clone(), empty {});
    return Ok(empty {});
}

} // namespace

auto lito::manifest::PackageFileSetResolver::resolve(const PackageManifest&      manifest,
                                                     const PackagePublishPolicy& policy)
    -> PackageFileSetResult<PackageFileSet> {
    if (manifest.manifest_path.as_path().file_name().is_none() ||
        manifest.manifest_path.as_path().file_name()->to_str().is_none() ||
        *manifest.manifest_path.as_path().file_name()->to_str() != "lito.toml"_str) {
        return publish_failure<PackageFileSet>(
            "Registry packages require a root lito.toml manifest"_str);
    }
    auto state = FileSetState {
        .manifest = manifest,
        .policy   = policy,
    };
    if (manifest.publish.include.is_some()) {
        for (const auto& pattern : *manifest.publish.include) {
            state.includes.push(rstd_try(parse_glob(pattern.as_str())));
        }
    }
    for (const auto& pattern : manifest.publish.exclude) {
        state.excludes.push(rstd_try(parse_glob(pattern.as_str())));
    }
    rstd_try(collect_directory(state, manifest.root.as_path(), ""_str));
    for (const auto& pattern : state.includes) {
        if (pattern.matches == usize {}) {
            return publish_failure<PackageFileSet>(rstd::format(
                "package.publish.include pattern '{}' matches no files", pattern.source));
        }
    }
    for (const auto& pattern : state.excludes) {
        if (pattern.matches == usize {}) {
            return publish_failure<PackageFileSet>(rstd::format(
                "package.publish.exclude pattern '{}' matches no files", pattern.source));
        }
    }
    if (! state.selected.contains_key("lito.toml"_str)) {
        return publish_failure<PackageFileSet>(
            "package root lito.toml is missing from the publish file set"_str);
    }
    rstd_try(include_package_readme(state));
    rstd_try(validate_references(state));
    return Ok(PackageFileSet(rstd::move(state.tree), rstd::move(state.directories)));
}
