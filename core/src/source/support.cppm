module;
#include <rstd/macro.hpp>

export module lito.source:support;

import rstd;
import lito.source.contract;
import lito.error;
import lito.lock.contract;
import lito.package.graph_contract;
import lito.workspace.contract;
import lito.build.profile_contract;
import lito.system.storage;
import lito.manifest;
import lito.system.process;
import lito.system.environment;
import lito.workspace;

using namespace rstd::prelude;
using namespace rstd::literals;
using IndexMap = rstd::collections::BTreeMap<String, usize>;

namespace lito
{

template<typename T>
auto source_failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Dependency, rstd::move(message)));
}

template<typename T>
auto source_failure(ref<str> message) -> Result<T> {
    return Err(Error::make(ErrorKind::Dependency, message));
}

auto path_components(ref<rstd::path::Path> path) -> Result<Vec<String>> {
    auto result     = Vec<String>::make();
    auto components = path.components();
    for (auto component = components.next(); component.is_some(); component = components.next()) {
        if (component->is_root_dir() || component->is_cur_dir()) continue;
        if (component->is_parent_dir()) {
            return source_failure<Vec<String>>(
                rstd::format("canonical path '{}' contains a parent component", path));
        }
        auto text = component->as_os_str().to_str();
        if (text.is_none()) {
            return source_failure<Vec<String>>(
                rstd::format("canonical path '{}' contains a non-UTF-8 component", path));
        }
        result.push(String::make(*text));
    }
    return Ok(rstd::move(result));
}

auto relative_path(ref<rstd::path::Path> root, ref<rstd::path::Path> target) -> Result<PathBuf> {
    auto root_components   = path_components(root);
    auto target_components = path_components(target);
    if (root_components.is_err()) return Err(rstd::move(root_components).unwrap_err());
    if (target_components.is_err()) {
        return Err(rstd::move(target_components).unwrap_err());
    }
    auto  roots   = rstd::move(root_components).unwrap();
    auto  targets = rstd::move(target_components).unwrap();
    usize common {};
    while (common < roots.len() && common < targets.len() && roots[common] == targets[common]) {
        ++common;
    }
    auto relative = PathBuf::make();
    for (auto index = common; index < roots.len(); ++index) {
        relative.push(PathBuf::from(".."_str).as_path());
    }
    for (auto index = common; index < targets.len(); ++index) {
        relative.push(PathBuf::from(targets[index].as_str()).as_path());
    }
    return Ok(relative.is_empty() ? PathBuf::from("."_str) : rstd::move(relative));
}

inline constexpr uint64_t FNV_OFFSET = 14695981039346656037ull;
inline constexpr uint64_t FNV_PRIME  = 1099511628211ull;

auto source_hash(ref<str> value) -> String {
    auto hash = FNV_OFFSET;
    for (auto byte : value) {
        hash ^= byte.to_primitive();
        hash *= FNV_PRIME;
    }
    static constexpr char digits[] = "0123456789abcdef";
    char                  result[16];
    for (size_t index = 0; index < 16; ++index) {
        result[15 - index] = digits[hash & 0xfu];
        hash >>= 4u;
    }
    return String::make(
        ref<str>::from_raw_parts_unchecked(reinterpret_cast<const byte*>(result), usize(16)));
}

auto push_path(Vec<String>& arguments, ref<rstd::path::Path> path) -> Result<empty> {
    auto text = path.to_str();
    if (text.is_none()) {
        return source_failure<empty>(rstd::format("Git cache path '{}' is not valid UTF-8", path));
    }
    arguments.push(String::make(*text));
    return Ok(empty {});
}

auto git_output(Vec<String>                       arguments,
                ref<str>                          operation,
                const ResolvedProcessEnvironment& environment) -> Result<String> {
    auto output = run_command(arguments, environment);
    if (output.is_err()) {
        auto error = rstd::move(output).unwrap_err();
        return source_failure<String>(rstd::format("{}: {}", operation, error.message.as_str()));
    }
    auto value = rstd::move(output).unwrap();
    if (value.exit_code != i32 {}) {
        return source_failure<String>(rstd::format("{} failed with exit code {}:\n{}",
                                                   operation,
                                                   value.exit_code,
                                                   value.standard_error.as_str()));
    }
    return Ok(trim_ascii(rstd::move(value.standard_output)));
}

auto git_status(Vec<String>                       arguments,
                ref<str>                          operation,
                const ResolvedProcessEnvironment& environment) -> Result<empty> {
    auto output = git_output(rstd::move(arguments), operation, environment);
    if (output.is_err()) return Err(rstd::move(output).unwrap_err());
    return Ok(empty {});
}

auto same_reference(const GitReference& left, const GitReference& right) -> bool {
    return left.kind == right.kind && left.value == right.value;
}

auto load_git_catalog(ref<rstd::path::Path> root) -> Result<WorkspaceCatalog> {
    auto document = load_manifest_document(root);
    if (document.is_err()) return Err(rstd::move(document).unwrap_err());
    auto loaded = rstd::move(document).unwrap();
    if (loaded.kind == ManifestKind::Workspace && loaded.workspace.is_some()) {
        return load_workspace_catalog(rstd::move(loaded.workspace).unwrap());
    }
    if (loaded.kind == ManifestKind::Package && loaded.package.is_some()) {
        return WorkspaceCatalog::single(rstd::move(loaded.package).unwrap());
    }
    return source_failure<WorkspaceCatalog>(
        "Git source root manifest has no package or workspace"_str);
}

struct SourceEntry {
    ResolvedPackageSource    source;
    Option<WorkspaceCatalog> catalog;
};

} // namespace lito
