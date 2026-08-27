export module lito.driver:build.layout;

import rstd;
import licrypto;
import lito.core;
import :build.layout_error;
import lito.cpp;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

template<typename T>
auto layout_failure(String message) -> BuildLayoutResult<T> {
    return Err(BuildLayoutError::Message(rstd::move(message)));
}

template<typename T>
auto io_failure(ref<str> operation, ref<rstd::path::Path> path, rstd::io::error::Error error)
    -> BuildLayoutResult<T> {
    return Err(
        BuildLayoutError::Io(String::make(operation), PathBuf::from(path), rstd::move(error)));
}

auto join(ref<rstd::path::Path> base, ref<str> component) -> PathBuf {
    return PathBuf::from(base).join(PathBuf::from(component).as_path());
}

auto validated_relative_text(ref<rstd::path::Path> relative) -> BuildLayoutResult<String> {
    if (relative.is_empty() || relative.is_absolute() || relative.has_root()) {
        return layout_failure<String>(
            rstd::format("source artifact path '{}' is not relative", relative));
    }
    if (! relative.components().all([](auto component) {
            return component.is_normal();
        })) {
        return layout_failure<String>(
            rstd::format("source artifact path '{}' contains a non-normal component", relative));
    }
    auto text = relative.to_str();
    if (text.is_none()) {
        return layout_failure<String>(
            rstd::format("source artifact path '{}' is not valid UTF-8", relative));
    }
    return Ok(String::make(*text));
}

auto module_filename(ref<str> logical_name) -> String {
    auto result = String::make();
    for (auto byte : logical_name) {
        const auto ascii =
            (byte >= u8('a') && byte <= u8('z')) || (byte >= u8('A') && byte <= u8('Z')) ||
            (byte >= u8('0') && byte <= u8('9')) || byte == u8('_') || byte == u8('.');
        result.push_ascii(ascii ? static_cast<char>(byte.to_primitive()) : '-');
    }
    result.push_str(".pcm"_str);
    return result;
}

} // namespace lito

export namespace lito
{

class BuildDirectory {
    PathBuf base_;

    explicit BuildDirectory(PathBuf base): base_(rstd::move(base)) {}

public:
    static auto resolve_root(ref<rstd::path::Path> owner_root, ref<rstd::path::Path> requested)
        -> BuildDirectory {
        if (requested.is_empty()) return BuildDirectory(join(owner_root, "build"_str));
        if (requested.is_absolute()) return BuildDirectory(PathBuf::from(requested));
        return BuildDirectory(PathBuf::from(owner_root).join(requested));
    }

    static auto resolve(ref<rstd::path::Path> owner_root,
                        ref<rstd::path::Path> requested,
                        ref<str>              profile) -> BuildDirectory {
        if (requested.is_empty()) {
            auto root = resolve_root(owner_root, requested);
            return BuildDirectory(join(root.path(), profile));
        }
        return resolve_root(owner_root, requested);
    }

    auto path() const noexcept -> ref<rstd::path::Path> { return base_.as_path(); }
    auto clone() const -> BuildDirectory { return BuildDirectory(base_.clone()); }
};

class BuildLayout {
    PathBuf base_;
    PathBuf output_;

    explicit BuildLayout(PathBuf base, PathBuf output)
        : base_(rstd::move(base)), output_(rstd::move(output)) {}

    auto relative_source_path(ref<rstd::path::Path> directory,
                              ref<rstd::path::Path> relative_source,
                              ref<str>              suffix) const -> BuildLayoutResult<PathBuf> {
        auto relative = validated_relative_text(relative_source);
        if (relative.is_err()) return Err(rstd::move(relative).unwrap_err());
        relative->push_str(suffix);
        return Ok(
            PathBuf::from(directory).join(PathBuf::from(rstd::move(relative).unwrap()).as_path()));
    }

    auto source_path(ref<rstd::path::Path>                 root,
                     const lito::package::PackageTargetId& target,
                     ref<rstd::path::Path>                 relative_source,
                     ref<str> suffix) const -> BuildLayoutResult<PathBuf> {
        auto directory = target_directory(root, target);
        return relative_source_path(directory.as_path(), relative_source, suffix);
    }

    auto target_directory(ref<rstd::path::Path>                 root,
                          const lito::package::PackageTargetId& target) const -> PathBuf {
        auto package_directory = join(root, target.package.as_str());
        auto kind_directory =
            join(package_directory.as_path(), lito::package::package_target_kind_name(target.kind));
        return join(kind_directory.as_path(), target.name.as_str());
    }

    auto test_attachment_directory(const cpp::TestAttachmentTarget& attachment) const -> PathBuf {
        auto test_root      = join(output_.as_path(), "test-attachments"_str);
        auto test_package   = join(test_root.as_path(), attachment.test_target.package.as_str());
        auto test_directory = join(test_package.as_path(), attachment.test_target.name.as_str());
        auto library_package =
            join(test_directory.as_path(), attachment.library_target.package.as_str());
        return join(library_package.as_path(), attachment.library_target.name.as_str());
    }

public:
    static auto resolve(ref<rstd::path::Path> owner_root,
                        ref<rstd::path::Path> requested_output,
                        ref<str>              profile,
                        ref<str>              target_output_key = ""_str) -> BuildLayout {
        auto directory = BuildDirectory::resolve(owner_root, requested_output, profile);
        auto output    = PathBuf::from(directory.path());
        if (! target_output_key.is_empty()) {
            output = join(join(output.as_path(), "targets"_str).as_path(), target_output_key);
        }
        return BuildLayout(PathBuf::from(directory.path()), rstd::move(output));
    }

    static auto create(ref<rstd::path::Path> owner_root,
                       ref<rstd::path::Path> requested_output,
                       ref<str>              profile,
                       ref<str> target_output_key = ""_str) -> BuildLayoutResult<BuildLayout> {
        if (! target_output_key.is_empty()) {
            auto component = PathBuf::from(target_output_key);
            auto parts     = component.as_path().components();
            auto first     = parts.next();
            if (first.is_none() || ! first->is_normal() || parts.next().is_some()) {
                return layout_failure<BuildLayout>(rstd::format(
                    "target output key '{}' is not a normal path component", target_output_key));
            }
        }
        auto layout  = resolve(owner_root, requested_output, profile, target_output_key);
        auto created = rstd::fs::create_dir_all(layout.output());
        if (created.is_err()) {
            return io_failure<BuildLayout>(
                "create output directory"_str, layout.output(), rstd::move(created).unwrap_err());
        }
        auto canonical = rstd::fs::canonicalize(layout.output());
        if (canonical.is_err()) {
            return io_failure<BuildLayout>("resolve output directory"_str,
                                           layout.output(),
                                           rstd::move(canonical).unwrap_err());
        }
        auto scan_cache   = layout.scan_cache_directory();
        auto scan_created = rstd::fs::create_dir_all(scan_cache.as_path());
        if (scan_created.is_err()) {
            return io_failure<BuildLayout>("create scan cache directory"_str,
                                           scan_cache.as_path(),
                                           rstd::move(scan_created).unwrap_err());
        }
        auto canonical_scan = rstd::fs::canonicalize(scan_cache.as_path());
        if (canonical_scan.is_err()) {
            return io_failure<BuildLayout>("resolve scan cache directory"_str,
                                           scan_cache.as_path(),
                                           rstd::move(canonical_scan).unwrap_err());
        }
        auto base = rstd::fs::canonicalize(layout.base_.as_path());
        if (base.is_err()) {
            return io_failure<BuildLayout>("resolve build directory"_str,
                                           layout.base_.as_path(),
                                           rstd::move(base).unwrap_err());
        }
        return Ok(BuildLayout(rstd::move(base).unwrap(), rstd::move(canonical).unwrap()));
    }

    auto base_directory() const -> ref<rstd::path::Path> { return base_.as_path(); }
    auto directory() const -> ref<rstd::path::Path> { return output_.as_path(); }
    auto output() const -> ref<rstd::path::Path> { return output_.as_path(); }

    auto clone() const -> BuildLayout { return BuildLayout { base_.clone(), output_.clone() }; }

    auto generated_root() const -> PathBuf { return join(output_.as_path(), "generated"_str); }

    auto source_materialization_root() const -> PathBuf {
        return join(output_.as_path(), "sources"_str);
    }

    auto archive_materialization(ref<str> identity) const -> PathBuf {
        auto archives = join(source_materialization_root().as_path(), "archives"_str);
        return join(archives.as_path(), licrypto::sha256_hex(identity).as_str());
    }

    auto dependency_work_root() const -> PathBuf {
        return join(output_.as_path(), "dependencies"_str);
    }

    auto cmake_work_root() const -> PathBuf {
        return join(dependency_work_root().as_path(), "cmake"_str);
    }

    auto cmake_work_root(ref<str> recipe_identity) const -> PathBuf {
        return join(cmake_work_root().as_path(), recipe_identity);
    }

    auto cargo_work_root() const -> PathBuf {
        return join(dependency_work_root().as_path(), "cargo"_str);
    }

    auto cargo_work_root(ref<str> request_identity) const -> PathBuf {
        return join(cargo_work_root().as_path(), request_identity);
    }

    auto scan_cache_directory() const -> PathBuf {
        return join(output_.as_path(), "lito-scan-cache"_str);
    }

    auto generated_package_directory(ref<str> package) const -> BuildLayoutResult<PathBuf> {
        auto component = PathBuf::from(package);
        auto parts     = component.as_path().components();
        auto first     = parts.next();
        if (package.is_empty() || first.is_none() || ! first->is_normal() ||
            parts.next().is_some()) {
            return layout_failure<PathBuf>(
                rstd::format("generated package name '{}' is invalid", package));
        }
        auto root = generated_root();
        return Ok(join(root.as_path(), package));
    }

    auto create_generated_package_directory(ref<str> package) const -> BuildLayoutResult<PathBuf> {
        auto requested = generated_package_directory(package);
        if (requested.is_err()) return Err(rstd::move(requested).unwrap_err());
        auto created = rstd::fs::create_dir_all(requested->as_path());
        if (created.is_err()) {
            return io_failure<PathBuf>("create generated package directory"_str,
                                       requested->as_path(),
                                       rstd::move(created).unwrap_err());
        }
        auto canonical = rstd::fs::canonicalize(requested->as_path());
        if (canonical.is_err()) {
            return io_failure<PathBuf>("resolve generated package directory"_str,
                                       requested->as_path(),
                                       rstd::move(canonical).unwrap_err());
        }
        return Ok(rstd::move(canonical).unwrap());
    }

    auto configure_receipt(ref<str> owner) const -> PathBuf {
        auto directory = join(output_.as_path(), "lito-configure"_str);
        auto name      = String::make(owner);
        name.push_str(".json"_str);
        return join(directory.as_path(), name.as_str());
    }

    auto build_tool_action_root() const -> PathBuf {
        return join(output_.as_path(), "lito-build-tools"_str);
    }

    auto host_build_tool_root() const -> PathBuf {
        return join(output_.as_path(), "lito-host-tools"_str);
    }

    auto runtime_resource_receipt(const lito::package::PackageTargetId& target, ref<str> name) const
        -> PathBuf {
        auto root      = join(output_.as_path(), "lito-resources"_str);
        auto directory = target_directory(root.as_path(), target);
        auto filename  = String::make(name);
        filename.push_str(".receipt"_str);
        return directory.join(PathBuf::from(rstd::move(filename)).as_path());
    }

    auto object(const lito::package::PackageTargetId& target,
                ref<rstd::path::Path> relative_source) const -> BuildLayoutResult<PathBuf> {
        return source_path(
            join(output_.as_path(), "obj"_str).as_path(), target, relative_source, ".o"_str);
    }

    auto standard_module_object(ref<str> context_identity, ref<str> logical_name) const -> PathBuf {
        auto root     = join(join(output_.as_path(), "obj"_str).as_path(), "standard-library"_str);
        auto context  = join(root.as_path(), licrypto::sha256_hex(context_identity).as_str());
        auto filename = module_filename(logical_name);
        filename.push_str(".o"_str);
        return context.join(PathBuf::from(rstd::move(filename)).as_path());
    }

    auto compile_cache_directory() const -> PathBuf {
        return join(output_.as_path(), "lito-cache"_str);
    }

    auto cache_environment(ref<str> key) const -> PathBuf {
        auto directory = join(compile_cache_directory().as_path(), "environments"_str);
        auto filename  = String::make(key);
        filename.push_str(".json"_str);
        return directory.join(PathBuf::from(rstd::move(filename)).as_path());
    }

    auto cache_target_directory(const lito::package::PackageTargetId& target) const -> PathBuf {
        auto targets = join(compile_cache_directory().as_path(), "targets"_str);
        return target_directory(targets.as_path(), target);
    }

    auto cache_scan(const lito::package::PackageTargetId& target,
                    ref<rstd::path::Path> relative_source) const -> BuildLayoutResult<PathBuf> {
        auto scan_cache = scan_cache_directory();
        return source_path(scan_cache.as_path(), target, relative_source, ".json"_str);
    }

    auto cache_unit(const lito::package::PackageTargetId& target,
                    ref<rstd::path::Path> relative_source) const -> BuildLayoutResult<PathBuf> {
        auto directory = cache_target_directory(target);
        return relative_source_path(directory.as_path(), relative_source, ".json"_str);
    }

    auto cache_archive(const lito::package::PackageTargetId& target) const -> PathBuf {
        auto directory = join(cache_target_directory(target).as_path(), "artifacts"_str);
        return join(directory.as_path(), "archive.json"_str);
    }

    auto standard_module_cache_directory(ref<str> context_identity) const -> PathBuf {
        auto root = join(compile_cache_directory().as_path(), "standard-library"_str);
        return join(root.as_path(), licrypto::sha256_hex(context_identity).as_str());
    }

    auto cache_standard_module_unit(ref<str> context_identity, ref<str> logical_name) const
        -> PathBuf {
        auto directory = standard_module_cache_directory(context_identity);
        auto filename  = module_filename(logical_name);
        filename.push_str(".json"_str);
        return directory.join(PathBuf::from(rstd::move(filename)).as_path());
    }

    auto cache_standard_module_scan(ref<str> context_identity, ref<str> logical_name) const
        -> PathBuf {
        auto root      = join(scan_cache_directory().as_path(), "standard-library"_str);
        auto directory = join(root.as_path(), licrypto::sha256_hex(context_identity).as_str());
        auto filename  = module_filename(logical_name);
        filename.push_str(".json"_str);
        return directory.join(PathBuf::from(rstd::move(filename)).as_path());
    }

    auto cache_compile_test(const lito::package::PackageTargetId& target,
                            ref<rstd::path::Path>                 relative_source) const
        -> BuildLayoutResult<PathBuf> {
        auto directory = cache_target_directory(target);
        return relative_source_path(directory.as_path(), relative_source, ".test.json"_str);
    }

    auto bmi_directory() const -> PathBuf { return join(output_.as_path(), "bmi"_str); }

    auto bmi(ref<str> format_key, ref<str> artifact_key, ref<str> logical_name) const -> PathBuf {
        auto format_directory   = join(bmi_directory().as_path(), format_key);
        auto artifact_directory = join(format_directory.as_path(), artifact_key);
        return artifact_directory.join(PathBuf::from(module_filename(logical_name)).as_path());
    }

    auto test_attachment_archive(const cpp::TestAttachmentTarget& attachment,
                                 ref<str>                         archive_stem) const -> PathBuf {
        auto filename = String::make("lib"_str);
        filename.push_str(archive_stem);
        filename.push_str(".test.a"_str);
        return test_attachment_directory(attachment)
            .join(PathBuf::from(rstd::move(filename)).as_path());
    }

    auto archive(const lito::package::PackageTargetId& target, ref<str> artifact_name) const
        -> PathBuf {
        auto directory =
            join(join(output_.as_path(), "lib"_str).as_path(), target.package.as_str());
        return directory.join(PathBuf::from(artifact_name).as_path());
    }

    auto shared_library(const lito::package::PackageTargetId& target, ref<str> artifact_name) const
        -> PathBuf {
        auto directory =
            join(join(output_.as_path(), "lib"_str).as_path(), target.package.as_str());
        return directory.join(PathBuf::from(artifact_name).as_path());
    }

    auto executable(const lito::package::PackageTargetId& target, ref<str> artifact_name) const
        -> PathBuf {
        auto directory =
            join(join(output_.as_path(), "bin"_str).as_path(), target.package.as_str());
        return directory.join(PathBuf::from(artifact_name).as_path());
    }

    auto install_executable(const lito::package::PackageTargetId& target,
                            ref<str>                              artifact_name,
                            ref<str> variant_identity) const -> PathBuf {
        auto install_root = join(output_.as_path(), "install-artifacts"_str);
        auto package      = join(install_root.as_path(), target.package.as_str());
        auto variant = join(package.as_path(), licrypto::sha256_hex(variant_identity).as_str());
        return variant.join(PathBuf::from(artifact_name).as_path());
    }

    auto test(const lito::package::PackageTargetId& target, ref<str> artifact_name) const
        -> PathBuf {
        auto directory =
            join(join(output_.as_path(), "test"_str).as_path(), target.package.as_str());
        return directory.join(PathBuf::from(artifact_name).as_path());
    }

    auto benchmark(const lito::package::PackageTargetId& target, ref<str> artifact_name) const
        -> PathBuf {
        auto directory =
            join(join(output_.as_path(), "bench"_str).as_path(), target.package.as_str());
        return directory.join(PathBuf::from(artifact_name).as_path());
    }
};

} // namespace lito
