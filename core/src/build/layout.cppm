export module lito.build.layout;

import rstd;
import lito.error;
import lito.build.identity;
import lito.package.identity;
import lito.package.target_contract;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

template<typename T>
auto failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Artifact, rstd::move(message)));
}

auto join(ref<rstd::path::Path> base, ref<str> component) -> PathBuf {
    return PathBuf::from(base).join(PathBuf::from(component).as_path());
}

auto validated_relative_text(ref<rstd::path::Path> relative) -> Result<String> {
    if (relative.is_empty() || relative.is_absolute() || relative.has_root()) {
        return failure<String>(rstd::format("source artifact path '{}' is not relative", relative));
    }
    auto components = relative.components();
    for (auto component = components.next(); component.is_some(); component = components.next()) {
        if (! component->is_normal()) {
            return failure<String>(rstd::format(
                "source artifact path '{}' contains a non-normal component", relative));
        }
    }
    auto text = relative.to_str();
    if (text.is_none()) {
        return failure<String>(
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

class BuildLayout {
    PathBuf output_;
    PathBuf scan_cache_;

    BuildLayout(PathBuf output, PathBuf scan_cache)
        : output_(rstd::move(output)), scan_cache_(rstd::move(scan_cache)) {}

    auto relative_source_path(ref<rstd::path::Path> directory,
                              ref<rstd::path::Path> relative_source,
                              ref<str>              suffix) const -> Result<PathBuf> {
        auto relative = validated_relative_text(relative_source);
        if (relative.is_err()) return Err(rstd::move(relative).unwrap_err());
        relative->push_str(suffix);
        return Ok(
            PathBuf::from(directory).join(PathBuf::from(rstd::move(relative).unwrap()).as_path()));
    }

    auto source_path(ref<rstd::path::Path>  root,
                     const PackageTargetId& target,
                     ref<rstd::path::Path>  relative_source,
                     ref<str>               suffix) const -> Result<PathBuf> {
        auto directory = target_directory(root, target);
        return relative_source_path(directory.as_path(), relative_source, suffix);
    }

    auto target_directory(ref<rstd::path::Path> root, const PackageTargetId& target) const
        -> PathBuf {
        auto package_directory = join(root, target.package.as_str());
        auto kind_directory =
            join(package_directory.as_path(), package_target_kind_name(target.kind));
        return join(kind_directory.as_path(), target.name.as_str());
    }

    auto test_attachment_directory(const TestAttachmentTarget& attachment) const -> PathBuf {
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
                        ref<str>              profile) -> BuildLayout {
        auto output     = PathBuf::make();
        auto scan_cache = PathBuf::make();
        if (requested_output.is_empty()) {
            auto build = join(owner_root, "build"_str);
            output     = join(build.as_path(), profile);
            scan_cache = join(build.as_path(), "lito-scan-cache"_str);
        } else if (requested_output.is_absolute()) {
            output     = PathBuf::from(requested_output);
            scan_cache = join(output.as_path(), "lito-scan-cache"_str);
        } else {
            output     = PathBuf::from(owner_root).join(requested_output);
            scan_cache = join(output.as_path(), "lito-scan-cache"_str);
        }
        return BuildLayout(rstd::move(output), rstd::move(scan_cache));
    }

    static auto create(ref<rstd::path::Path> owner_root,
                       ref<rstd::path::Path> requested_output,
                       ref<str>              profile) -> Result<BuildLayout> {
        auto layout  = resolve(owner_root, requested_output, profile);
        auto created = rstd::fs::create_dir_all(layout.output());
        if (created.is_err()) {
            return failure<BuildLayout>(rstd::format("cannot create output directory '{}': {}",
                                                     layout.output(),
                                                     rstd::move(created).unwrap_err()));
        }
        auto canonical = rstd::fs::canonicalize(layout.output());
        if (canonical.is_err()) {
            return failure<BuildLayout>(rstd::format("cannot resolve output directory '{}': {}",
                                                     layout.output(),
                                                     rstd::move(canonical).unwrap_err()));
        }
        auto scan_created = rstd::fs::create_dir_all(layout.scan_cache_.as_path());
        if (scan_created.is_err()) {
            return failure<BuildLayout>(rstd::format("cannot create scan cache directory '{}': {}",
                                                     layout.scan_cache_.as_path(),
                                                     rstd::move(scan_created).unwrap_err()));
        }
        auto canonical_scan = rstd::fs::canonicalize(layout.scan_cache_.as_path());
        if (canonical_scan.is_err()) {
            return failure<BuildLayout>(rstd::format("cannot resolve scan cache directory '{}': {}",
                                                     layout.scan_cache_.as_path(),
                                                     rstd::move(canonical_scan).unwrap_err()));
        }
        return Ok(BuildLayout(rstd::move(canonical).unwrap(), rstd::move(canonical_scan).unwrap()));
    }

    auto output() const -> ref<rstd::path::Path> { return output_.as_path(); }

    auto generated_root() const -> PathBuf { return join(output_.as_path(), "generated"_str); }

    auto generated_package_directory(ref<str> package) const -> Result<PathBuf> {
        auto component = PathBuf::from(package);
        auto parts     = component.as_path().components();
        auto first     = parts.next();
        if (package.is_empty() || first.is_none() || ! first->is_normal() ||
            parts.next().is_some()) {
            return failure<PathBuf>(
                rstd::format("generated package name '{}' is invalid", package));
        }
        auto root = generated_root();
        return Ok(join(root.as_path(), package));
    }

    auto create_generated_package_directory(ref<str> package) const -> Result<PathBuf> {
        auto requested = generated_package_directory(package);
        if (requested.is_err()) return Err(rstd::move(requested).unwrap_err());
        auto created = rstd::fs::create_dir_all(requested->as_path());
        if (created.is_err()) {
            return failure<PathBuf>(
                rstd::format("cannot create generated package directory '{}': {}",
                             requested->as_path(),
                             rstd::move(created).unwrap_err()));
        }
        auto canonical = rstd::fs::canonicalize(requested->as_path());
        if (canonical.is_err()) {
            return failure<PathBuf>(
                rstd::format("cannot resolve generated package directory '{}': {}",
                             requested->as_path(),
                             rstd::move(canonical).unwrap_err()));
        }
        return Ok(rstd::move(canonical).unwrap());
    }

    auto configure_receipt() const -> PathBuf {
        auto directory = join(output_.as_path(), "lito-configure"_str);
        return join(directory.as_path(), "receipt.json"_str);
    }

    auto object(const PackageTargetId& target, ref<rstd::path::Path> relative_source) const
        -> Result<PathBuf> {
        return source_path(
            join(output_.as_path(), "obj"_str).as_path(), target, relative_source, ".o"_str);
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

    auto cache_target_directory(const PackageTargetId& target) const -> PathBuf {
        auto targets = join(compile_cache_directory().as_path(), "targets"_str);
        return target_directory(targets.as_path(), target);
    }

    auto cache_scan(const PackageTargetId& target, ref<rstd::path::Path> relative_source) const
        -> Result<PathBuf> {
        return source_path(scan_cache_.as_path(), target, relative_source, ".json"_str);
    }

    auto cache_unit(const PackageTargetId& target, ref<rstd::path::Path> relative_source) const
        -> Result<PathBuf> {
        auto directory = cache_target_directory(target);
        return relative_source_path(directory.as_path(), relative_source, ".json"_str);
    }

    auto cache_compile_test(const PackageTargetId& target,
                            ref<rstd::path::Path>  relative_source) const -> Result<PathBuf> {
        auto directory = cache_target_directory(target);
        return relative_source_path(directory.as_path(), relative_source, ".test.json"_str);
    }

    auto bmi_directory() const -> PathBuf { return join(output_.as_path(), "bmi"_str); }

    auto bmi(ref<str> format_key, ref<str> artifact_key, ref<str> logical_name) const -> PathBuf {
        auto format_directory   = join(bmi_directory().as_path(), format_key);
        auto artifact_directory = join(format_directory.as_path(), artifact_key);
        return artifact_directory.join(PathBuf::from(module_filename(logical_name)).as_path());
    }

    auto test_attachment_archive(const TestAttachmentTarget& attachment,
                                 ref<str>                    archive_stem) const -> PathBuf {
        auto filename = String::make("lib"_str);
        filename.push_str(archive_stem);
        filename.push_str(".test.a"_str);
        return test_attachment_directory(attachment)
            .join(PathBuf::from(rstd::move(filename)).as_path());
    }

    auto archive(const PackageTargetId& target, ref<str> artifact_name) const -> PathBuf {
        auto directory =
            join(join(output_.as_path(), "lib"_str).as_path(), target.package.as_str());
        return directory.join(PathBuf::from(artifact_name).as_path());
    }

    auto executable(const PackageTargetId& target, ref<str> artifact_name) const -> PathBuf {
        auto directory =
            join(join(output_.as_path(), "bin"_str).as_path(), target.package.as_str());
        return directory.join(PathBuf::from(artifact_name).as_path());
    }

    auto test(const PackageTargetId& target, ref<str> artifact_name) const -> PathBuf {
        auto directory =
            join(join(output_.as_path(), "test"_str).as_path(), target.package.as_str());
        return directory.join(PathBuf::from(artifact_name).as_path());
    }

    auto benchmark(const PackageTargetId& target, ref<str> artifact_name) const -> PathBuf {
        auto directory =
            join(join(output_.as_path(), "bench"_str).as_path(), target.package.as_str());
        return directory.join(PathBuf::from(artifact_name).as_path());
    }
};

} // namespace lito
