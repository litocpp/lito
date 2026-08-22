module;
#include <rstd/macro.hpp>

module lito.driver:build.resource;

import rstd;
import lito.crypto;
import lito.core;
import :build.event;
import :build.artifact;
import :build.layout;
import :build.script_error;
import lito.cpp;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

template<typename T>
auto resource_io_failure(ref<str>               operation,
                         ref<rstd::path::Path>  path,
                         rstd::io::error::Error error) -> BuildScriptResult<T> {
    return Err(
        BuildScriptError::Io(String::make(operation), PathBuf::from(path), rstd::move(error)));
}

auto collect_resource_files(ref<rstd::path::Path> root,
                            ref<rstd::path::Path> directory,
                            Vec<PathBuf>&         files) -> BuildScriptResult<empty> {
    auto opened = rstd::fs::read_dir(directory);
    if (opened.is_err()) {
        return resource_io_failure<empty>(
            "enumerate runtime resource"_str, directory, rstd::move(opened).unwrap_err());
    }
    auto entries = rstd::move(opened).unwrap();
    for (auto item : entries) {
        if (item.is_err()) {
            return resource_io_failure<empty>(
                "enumerate runtime resource"_str, directory, rstd::move(item).unwrap_err());
        }
        auto entry = rstd::move(item).unwrap();
        auto type  = entry.file_type();
        auto path  = entry.path();
        if (type.is_err()) {
            return resource_io_failure<empty>(
                "inspect runtime resource"_str, path.as_path(), rstd::move(type).unwrap_err());
        }
        if (type->is_symlink()) {
            return Err(BuildScriptError::Message(
                rstd::format("runtime resource '{}' contains symlink '{}'", root, path.as_path())));
        }
        if (type->is_dir()) {
            rstd_try(collect_resource_files(root, path.as_path(), files));
            continue;
        }
        if (! type->is_file()) {
            return Err(BuildScriptError::Message(rstd::format(
                "runtime resource '{}' contains unsupported entry '{}'", root, path.as_path())));
        }
        auto relative = path.as_path().strip_prefix(root);
        if (relative.is_none() || (*relative).is_empty()) {
            return Err(BuildScriptError::Message(
                rstd::format("runtime resource file '{}' escapes '{}'", path.as_path(), root)));
        }
        files.push(PathBuf::from(*relative));
    }
    return Ok(empty {});
}

auto resource_identity(ref<rstd::path::Path> root, const Vec<PathBuf>& files)
    -> BuildScriptResult<String> {
    auto state = lito::crypto::Sha256::make();
    for (const auto& relative : files) {
        auto text = relative.as_path().to_str();
        if (text.is_none()) {
            return Err(BuildScriptError::Message(
                rstd::format("runtime resource path '{}' is not valid UTF-8", relative.as_path())));
        }
        state.update(text->as_bytes());
        const auto separator = array<u8, 1> { u8 {} };
        state.update(separator.as_slice());
        auto path = PathBuf::from(root).join(relative.as_path());
        auto data = rstd::fs::read(path.as_path());
        if (data.is_err()) {
            return resource_io_failure<String>(
                "read runtime resource"_str, path.as_path(), rstd::move(data).unwrap_err());
        }
        state.update(data->as_slice());
        state.update(separator.as_slice());
    }
    return Ok(lito::crypto::sha256_hex(rstd::move(state).finalize()));
}

auto selected_resource_target(const Vec<lito::package::PackageTargetId>& selected,
                              const lito::package::PackageTargetId&      target) noexcept -> bool {
    for (const auto& candidate : selected) {
        if (candidate == target) return true;
    }
    return false;
}

} // namespace lito

namespace lito
{

auto runtime_resource_directory_identity(ref<rstd::path::Path> root) -> BuildScriptResult<String> {
    auto inspected = rstd::fs::symlink_metadata(root);
    if (inspected.is_err()) {
        return resource_io_failure<String>(
            "inspect runtime resource"_str, root, rstd::move(inspected).unwrap_err());
    }
    if (inspected->is_symlink() || ! inspected->is_dir()) {
        return Err(BuildScriptError::Message(
            rstd::format("runtime resource '{}' must be a directory", root)));
    }
    auto files = Vec<PathBuf>::make();
    rstd_try(collect_resource_files(root, root, files));
    rstd::slice_::sort_unstable_by(
        files.as_mut_slice().as_mut_ref(), [](const PathBuf& left, const PathBuf& right) {
            return left.as_path().to_string_lossy() < right.as_path().to_string_lossy();
        });
    if (files.is_empty()) {
        return Err(BuildScriptError::Message(rstd::format("runtime resource '{}' is empty", root)));
    }
    return resource_identity(root, files);
}

auto resolve_runtime_resources(const cpp::PackageMetadata&                metadata,
                               const BuildLayout&                         layout,
                               const Vec<lito::package::PackageTargetId>& selected,
                               const Option<BuildEventSink>&              observer)
    -> BuildScriptResult<Vec<BuiltRuntimeResource>> {
    auto result = Vec<BuiltRuntimeResource>::make();
    for (const auto& target : metadata.targets) {
        if (! selected_resource_target(selected, target.id)) continue;
        for (const auto& declaration : target.runtime_resources) {
            auto package_root =
                rstd_try(layout.generated_package_directory(target.id.package.as_str()));
            auto requested = package_root.join(declaration.path.as_path());
            auto inspected = rstd::fs::symlink_metadata(requested.as_path());
            if (inspected.is_err()) {
                return resource_io_failure<Vec<BuiltRuntimeResource>>(
                    "inspect runtime resource"_str,
                    requested.as_path(),
                    rstd::move(inspected).unwrap_err());
            }
            if (inspected->is_symlink() || ! inspected->is_dir()) {
                return Err(BuildScriptError::Message(
                    rstd::format("runtime resource '{}' for target '{}::{}' must be a directory",
                                 requested.as_path(),
                                 target.id.package.as_str(),
                                 target.id.name.as_str())));
            }
            auto canonical = rstd::fs::canonicalize(requested.as_path());
            if (canonical.is_err()) {
                return resource_io_failure<Vec<BuiltRuntimeResource>>(
                    "resolve runtime resource"_str,
                    requested.as_path(),
                    rstd::move(canonical).unwrap_err());
            }
            if (canonical->as_path().strip_prefix(package_root.as_path()).is_none()) {
                return Err(BuildScriptError::Message(rstd::format(
                    "runtime resource '{}' escapes generated package root", requested.as_path())));
            }
            auto files = Vec<PathBuf>::make();
            rstd_try(collect_resource_files(canonical->as_path(), canonical->as_path(), files));
            rstd::slice_::sort_unstable_by(
                files.as_mut_slice().as_mut_ref(), [](const PathBuf& left, const PathBuf& right) {
                    return left.as_path().to_string_lossy() < right.as_path().to_string_lossy();
                });
            if (files.is_empty()) {
                return Err(BuildScriptError::Message(
                    rstd::format("runtime resource '{}' is empty", requested.as_path())));
            }
            auto identity = rstd_try(resource_identity(canonical->as_path(), files));
            auto receipt  = layout.runtime_resource_receipt(target.id, declaration.name.as_str());
            auto previous = rstd::fs::read_to_string(receipt.as_path());
            auto reused = previous.is_ok() && previous->as_str().trim_ascii() == identity.as_str();
            if (! reused) {
                auto parent  = receipt.as_path().parent().unwrap();
                auto created = rstd::fs::create_dir_all(parent);
                if (created.is_err()) {
                    return resource_io_failure<Vec<BuiltRuntimeResource>>(
                        "create runtime resource receipt directory"_str,
                        parent,
                        rstd::move(created).unwrap_err());
                }
                auto text = identity.clone();
                text.push_ascii('\n');
                auto written = rstd::fs::write_atomic(receipt.as_path(), text.as_str().as_bytes());
                if (written.is_err()) {
                    return resource_io_failure<Vec<BuiltRuntimeResource>>(
                        "write runtime resource receipt"_str,
                        receipt.as_path(),
                        rstd::move(written).unwrap_err());
                }
            }
            if (observer.is_some() && observer->notify != nullptr) {
                observer->notify(observer->context,
                                 BuildEvent { reused ? BuildEventKind::GeneratedResourceReuse
                                                     : BuildEventKind::GeneratedResource,
                                              declaration.name.as_str(),
                                              canonical->as_path() });
            }
            result.push(BuiltRuntimeResource {
                .target   = target.id.clone(),
                .name     = declaration.name.clone(),
                .root     = rstd::move(canonical).unwrap(),
                .identity = rstd::move(identity),
                .files    = rstd::move(files),
            });
        }
    }
    return Ok(rstd::move(result));
}

} // namespace lito
