module;
#include <rstd/macro.hpp>

module lito.driver:build.generated.cache;

import rstd;
import lito.core;
import lito.cpp;
import lito.system;
import licrypto;
import :build.script.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::system;

namespace lito
{

auto action_file_digest(ref<rstd::path::Path> path) -> BuildScriptResult<String> {
    auto data = rstd::fs::read(path);
    if (data.is_err()) {
        return build_script_io_failure<String>(
            "read build-tool action file"_str, path, rstd::move(data).unwrap_err());
    }
    return Ok(licrypto::sha256_hex(data->as_slice()));
}

auto action_directory_digest(ref<rstd::path::Path> path) -> BuildScriptResult<String> {
    auto opened = rstd::fs::read_dir(path);
    if (opened.is_err()) {
        return build_script_io_failure<String>(
            "enumerate build-tool action directory"_str, path, rstd::move(opened).unwrap_err());
    }
    auto records = Vec<String>::make();
    auto entries = rstd::move(opened).unwrap();
    for (auto item : entries) {
        if (item.is_err()) {
            return build_script_io_failure<String>(
                "enumerate build-tool action directory"_str, path, rstd::move(item).unwrap_err());
        }
        auto entry = rstd::move(item).unwrap();
        auto type  = entry.file_type();
        if (type.is_err()) {
            auto entry_path = entry.path();
            return build_script_io_failure<String>("inspect build-tool action directory entry"_str,
                                                   entry_path.as_path(),
                                                   rstd::move(type).unwrap_err());
        }
        auto kind = type->is_file()      ? "file"_str
                    : type->is_dir()     ? "directory"_str
                    : type->is_symlink() ? "symlink"_str
                                         : "other"_str;
        auto name = entry.file_name().as_os_str().to_string_lossy();
        records.push(rstd::format("{}:{}:{}", kind.len(), kind, name.as_str()));
    }
    rstd::slice_::sort_unstable(records.as_mut_slice().as_mut_ref());
    auto identity = String::make("build-tool-directory-v1"_str);
    for (const auto& record : records) {
        identity.push_ascii('\n');
        identity.push_str(record.as_str());
    }
    return Ok(licrypto::sha256_hex(identity.as_str()));
}

enum class ActionDependencyKind
{
    File,
    Directory,
};

auto action_dependency_kind_name(ActionDependencyKind kind) noexcept -> ref<str> {
    return kind == ActionDependencyKind::File ? "file"_str : "directory"_str;
}

auto action_dependency_digest(ref<rstd::path::Path> path, ActionDependencyKind kind)
    -> BuildScriptResult<String> {
    return kind == ActionDependencyKind::File ? action_file_digest(path)
                                              : action_directory_digest(path);
}

struct ActionDependency {
    PathBuf              path;
    ActionDependencyKind kind { ActionDependencyKind::File };
    String               digest;
};

auto make_depfile_paths(ref<str> text, ref<rstd::path::Path> working_directory)
    -> BuildScriptResult<Vec<PathBuf>> {
    auto bytes     = text.as_bytes();
    auto separator = Option<usize> {};
    auto escaped   = false;
    for (usize index {}; index < bytes.len(); ++index) {
        const auto byte = bytes[index];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (byte == u8('\\')) {
            escaped = true;
            continue;
        }
        if (byte == u8(':')) {
            separator = Some(index);
            break;
        }
    }
    if (separator.is_none()) {
        return action_request_failure<Vec<PathBuf>>(
            "build-tool depfile does not contain a target separator"_str);
    }

    auto result  = Vec<PathBuf>::make();
    auto token   = Vec<u8>::make();
    auto publish = [&]() -> BuildScriptResult<empty> {
        if (token.is_empty()) return Ok(empty {});
        auto decoded = String::from_utf8(rstd::move(token));
        token        = Vec<u8>::make();
        if (decoded.is_err()) {
            return action_request_failure<empty>(
                "build-tool depfile contains a non-UTF-8 dependency path"_str);
        }
        auto path = PathBuf::from(rstd::move(decoded).unwrap());
        if (! path.as_path().is_absolute()) {
            path = PathBuf::from(working_directory).join(path.as_path());
        }
        result.push(rstd::move(path));
        return Ok(empty {});
    };

    for (usize index = *separator + usize(1); index < bytes.len(); ++index) {
        const auto byte = bytes[index];
        if (byte == u8('\\')) {
            if (index + usize(1) >= bytes.len()) {
                token.push(u8(byte.to_primitive()));
                continue;
            }
            const auto next = bytes[index + usize(1)];
            if (next == u8('\n')) {
                ++index;
                continue;
            }
            if (next == u8('\r') && index + usize(2) < bytes.len() &&
                bytes[index + usize(2)] == u8('\n')) {
                index += usize(2);
                continue;
            }
            token.push(u8(next.to_primitive()));
            ++index;
            continue;
        }
        if (byte == u8(' ') || byte == u8('\t') || byte == u8('\r') || byte == u8('\n')) {
            rstd_try(publish());
            continue;
        }
        token.push(u8(byte.to_primitive()));
    }
    rstd_try(publish());
    return Ok(rstd::move(result));
}

auto load_action_dependencies(ref<rstd::path::Path> depfile,
                              ref<rstd::path::Path> working_directory,
                              const Vec<PathBuf>&   allowed_roots,
                              const Vec<PathBuf>&   direct_inputs)
    -> BuildScriptResult<Vec<ActionDependency>> {
    auto contents = rstd::fs::read_to_string(depfile);
    if (contents.is_err()) {
        return build_script_io_failure<Vec<ActionDependency>>(
            "read build-tool depfile"_str, depfile, rstd::move(contents).unwrap_err());
    }
    auto paths  = rstd_try(make_depfile_paths(contents->as_str(), working_directory));
    auto result = Vec<ActionDependency>::make();
    for (const auto& path : paths) {
        auto canonical = rstd::fs::canonicalize(path.as_path());
        if (canonical.is_err()) {
            return build_script_io_failure<Vec<ActionDependency>>(
                "resolve build-tool depfile dependency"_str,
                path.as_path(),
                rstd::move(canonical).unwrap_err());
        }
        auto metadata = rstd::fs::symlink_metadata(canonical->as_path());
        if (metadata.is_err() || metadata->is_symlink() ||
            (! metadata->is_file() && ! metadata->is_dir())) {
            return action_failure<Vec<ActionDependency>>(BuildToolActionError::InvalidInput(
                canonical->clone(),
                String::make("depfile dependency is not a regular file or directory"_str)));
        }
        auto              allowed   = false;
        static const auto env_roots = [] {
            auto roots = Vec<PathBuf>::make();
            if (auto env = rstd::env::var("LITO_ALLOWED_ROOTS"_str); env.is_ok()) {
                auto remaining = env->as_str();
                while (! remaining.is_empty()) {
                    if (auto split = remaining.split_once(":"_str); split.is_some()) {
                        auto head = rstd::get<0>(*split);
                        if (! head.is_empty()) {
                            roots.push(PathBuf::from(head));
                        }
                        remaining = rstd::get<1>(*split);
                    } else {
                        roots.push(PathBuf::from(remaining));
                        break;
                    }
                }
            }
            return roots;
        }();
        for (const auto& root : env_roots) {
            if (canonical->as_path().strip_prefix(root.as_path()).is_some()) {
                allowed = true;
                break;
            }
        }
        if (! allowed) {
            for (const auto& root : allowed_roots) {
                if (canonical->as_path().strip_prefix(root.as_path()).is_some()) {
                    allowed = true;
                    break;
                }
            }
        }
        if (! allowed) {
            for (const auto& input : direct_inputs) {
                if (canonical->as_path() == input.as_path()) {
                    allowed = true;
                    break;
                }
            }
        }
        if (! allowed) {
            return action_failure<Vec<ActionDependency>>(BuildToolActionError::InvalidInput(
                canonical->clone(),
                String::make("depfile dependency is outside allowed roots"_str)));
        }
        auto duplicate = false;
        for (const auto& dependency : result) {
            if (dependency.path.as_path() == canonical->as_path()) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;
        auto kind =
            metadata->is_file() ? ActionDependencyKind::File : ActionDependencyKind::Directory;
        auto digest = rstd_try(action_dependency_digest(canonical->as_path(), kind));
        result.push(ActionDependency {
            .path   = rstd::move(canonical).unwrap(),
            .kind   = kind,
            .digest = rstd::move(digest),
        });
    }
    const auto order = [](const ActionDependency& left, const ActionDependency& right) {
        return left.path.as_path().to_string_lossy() < right.path.as_path().to_string_lossy();
    };
    rstd::slice_::sort_unstable_by(result.as_mut_slice().as_mut_ref(), order);
    return Ok(rstd::move(result));
}

auto action_receipt_matches(ref<rstd::path::Path> receipt,
                            ref<str>              identity,
                            const Vec<PathBuf>&   outputs,
                            bool                  expects_dependencies,
                            ref<rstd::path::Path> generated_root) -> BuildScriptResult<bool> {
    auto exists = rstd::fs::exists(receipt);
    if (exists.is_err()) {
        return action_receipt_failure<bool>(
            "inspect build-tool action receipt"_str, receipt, rstd::move(exists).unwrap_err());
    }
    if (! *exists) return Ok(false);
    auto contents = rstd::fs::read_to_string(receipt);
    if (contents.is_err()) return Ok(false);
    auto parsed = rstd::json::from_str(contents->as_str());
    if (parsed.is_err()) return Ok(false);
    auto version           = parsed->get("version"_str);
    auto recorded_identity = parsed->get("identity"_str);
    auto recorded_outputs  = parsed->get("outputs"_str);
    if (version.is_none() || (**version).as_i64() != Some(i64(3)) || recorded_identity.is_none() ||
        (**recorded_identity).as_str() != Some(identity) || recorded_outputs.is_none() ||
        (**recorded_outputs).as_array().is_none() ||
        (**(**recorded_outputs).as_array()).len() != outputs.len()) {
        return Ok(false);
    }
    const auto& entries = **(**recorded_outputs).as_array();
    for (usize index {}; index < outputs.len(); ++index) {
        auto path   = entries[index].get("path"_str);
        auto digest = entries[index].get("sha256"_str);
        auto text   = outputs[index].as_path().to_str();
        if (text.is_none() || path.is_none() || (**path).as_str() != Some(*text) ||
            digest.is_none() || (**digest).as_str().is_none()) {
            return Ok(false);
        }
        auto final  = PathBuf::from(generated_root).join(outputs[index].as_path());
        auto actual = action_file_digest(final.as_path());
        if (actual.is_err() || actual->as_str() != *(**digest).as_str()) return Ok(false);
    }
    auto recorded_dependencies = parsed->get("dependencies"_str);
    if (recorded_dependencies.is_none() || (**recorded_dependencies).as_array().is_none()) {
        return Ok(! expects_dependencies);
    }
    const auto& dependencies = **(**recorded_dependencies).as_array();
    if (expects_dependencies && dependencies.is_empty()) return Ok(false);
    for (const auto& entry : dependencies) {
        auto path   = entry.get("path"_str);
        auto kind   = entry.get("kind"_str);
        auto digest = entry.get("sha256"_str);
        if (path.is_none() || (**path).as_str().is_none() || kind.is_none() ||
            (**kind).as_str().is_none() || digest.is_none() || (**digest).as_str().is_none()) {
            return Ok(false);
        }
        auto dependency_kind = ActionDependencyKind::File;
        if (*(**kind).as_str() == "directory"_str)
            dependency_kind = ActionDependencyKind::Directory;
        else if (*(**kind).as_str() != "file"_str)
            return Ok(false);
        auto dependency_path = PathBuf::from(String::make(*(**path).as_str()));
        auto actual          = action_dependency_digest(dependency_path.as_path(), dependency_kind);
        if (actual.is_err() || actual->as_str() != *(**digest).as_str()) return Ok(false);
    }
    return Ok(true);
}

auto action_receipt_text(ref<str>                     identity,
                         const Vec<PathBuf>&          outputs,
                         const Vec<String>&           digests,
                         const Vec<ActionDependency>& dependencies) -> String {
    auto entries = rstd::json::Array::with_capacity(outputs.len());
    for (usize index {}; index < outputs.len(); ++index) {
        auto item = rstd::json::Map::make();
        item.insert(String::make("path"_str),
                    Json::String(outputs[index].as_path().to_string_lossy()));
        item.insert(String::make("sha256"_str), Json::String(digests[index].clone()));
        entries.push(Json::Object(rstd::move(item)));
    }
    auto document = rstd::json::Map::make();
    document.insert(String::make("version"_str),
                    Json::Number(rstd::json::Number::from_u64(u64(3))));
    document.insert(String::make("identity"_str), Json::String(String::make(identity)));
    document.insert(String::make("outputs"_str), Json::Array(rstd::move(entries)));
    auto dependency_entries = rstd::json::Array::with_capacity(dependencies.len());
    for (const auto& dependency : dependencies) {
        auto item = rstd::json::Map::make();
        item.insert(String::make("path"_str),
                    Json::String(dependency.path.as_path().to_string_lossy()));
        item.insert(String::make("kind"_str),
                    Json::String(String::make(action_dependency_kind_name(dependency.kind))));
        item.insert(String::make("sha256"_str), Json::String(dependency.digest.clone()));
        dependency_entries.push(Json::Object(rstd::move(item)));
    }
    document.insert(String::make("dependencies"_str), Json::Array(rstd::move(dependency_entries)));
    auto text =
        rstd::json::to_string(Json::Object(rstd::move(document)),
                              rstd::json::FormatOptions { .pretty = true, .indent = usize(2) });
    text.push_ascii('\n');
    return text;
}

auto collect_action_outputs(ref<rstd::path::Path> root,
                            ref<rstd::path::Path> directory,
                            Vec<PathBuf>&         files) -> BuildScriptResult<empty> {
    auto opened = rstd::fs::read_dir(directory);
    if (opened.is_err()) {
        return build_script_io_failure<empty>(
            "enumerate staged build-tool outputs"_str, directory, rstd::move(opened).unwrap_err());
    }
    auto entries = rstd::move(opened).unwrap();
    for (auto item : entries) {
        if (item.is_err()) {
            return build_script_io_failure<empty>("enumerate staged build-tool outputs"_str,
                                                  directory,
                                                  rstd::move(item).unwrap_err());
        }
        auto entry = rstd::move(item).unwrap();
        auto type  = entry.file_type();
        auto path  = entry.path();
        if (type.is_err()) {
            return build_script_io_failure<empty>("inspect staged build-tool output"_str,
                                                  path.as_path(),
                                                  rstd::move(type).unwrap_err());
        }
        if (type->is_symlink()) {
            return action_failure<empty>(BuildToolActionError::InvalidOutput(
                path.clone(), String::make("produced output is a symlink"_str)));
        }
        if (type->is_dir()) {
            rstd_try(collect_action_outputs(root, path.as_path(), files));
            continue;
        }
        if (! type->is_file()) {
            return action_failure<empty>(BuildToolActionError::InvalidOutput(
                path.clone(), String::make("produced output is not a regular file"_str)));
        }
        auto relative = path.as_path().strip_prefix(root);
        if (relative.is_none() || (*relative).is_empty()) {
            return action_failure<empty>(BuildToolActionError::InvalidOutput(
                path.clone(), String::make("produced output escapes staging"_str)));
        }
        files.push(PathBuf::from(*relative));
    }
    return Ok(empty {});
}

} // namespace lito
