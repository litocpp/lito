export module tenon.build_layout;

import rstd;
import tenon.model;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace tenon
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

} // namespace tenon

export namespace tenon
{

class BuildLayout {
    PathBuf output_;

    explicit BuildLayout(PathBuf output): output_(rstd::move(output)) {}

    auto source_path(ref<rstd::path::Path> root,
                     ref<str>              target,
                     ref<rstd::path::Path> relative_source,
                     ref<str>              suffix) const -> Result<PathBuf> {
        auto relative = validated_relative_text(relative_source);
        if (relative.is_err()) return Err(rstd::move(relative).unwrap_err());
        relative->push_str(suffix);
        auto directory = target.is_empty() ? PathBuf::from(root) : join(root, target);
        return Ok(directory.join(PathBuf::from(rstd::move(relative).unwrap()).as_path()));
    }

public:
    static auto create(ref<rstd::path::Path> owner_root,
                       ref<rstd::path::Path> requested_output,
                       ref<str>              profile) -> Result<BuildLayout> {
        auto output = PathBuf::make();
        if (requested_output.is_empty()) {
            output = join(join(owner_root, "build"_str).as_path(), profile);
        } else if (requested_output.is_absolute()) {
            output = PathBuf::from(requested_output);
        } else {
            output = PathBuf::from(owner_root).join(requested_output);
        }
        auto created = rstd::fs::create_dir_all(output.as_path());
        if (created.is_err()) {
            return failure<BuildLayout>(rstd::format("cannot create output directory '{}': {}",
                                                     output.as_path(),
                                                     rstd::move(created).unwrap_err()));
        }
        auto canonical = rstd::fs::canonicalize(output.as_path());
        if (canonical.is_err()) {
            return failure<BuildLayout>(rstd::format("cannot resolve output directory '{}': {}",
                                                     output.as_path(),
                                                     rstd::move(canonical).unwrap_err()));
        }
        return Ok(BuildLayout(rstd::move(canonical).unwrap()));
    }

    auto output() const -> ref<rstd::path::Path> { return output_.as_path(); }

    auto object(ref<str> target, ref<rstd::path::Path> relative_source) const -> Result<PathBuf> {
        return source_path(
            join(output_.as_path(), "obj"_str).as_path(), target, relative_source, ".o"_str);
    }

    auto compile_cache_directory() const -> PathBuf {
        return join(output_.as_path(), "tenon-cache"_str);
    }

    auto cache_environment(ref<str> key) const -> PathBuf {
        auto directory = join(compile_cache_directory().as_path(), "environments"_str);
        auto filename  = String::make(key);
        filename.push_str(".json"_str);
        return directory.join(PathBuf::from(rstd::move(filename)).as_path());
    }

    auto cache_target_directory(ref<str> target) const -> PathBuf {
        auto units = join(compile_cache_directory().as_path(), "units"_str);
        return join(units.as_path(), target);
    }

    auto cache_unit(ref<str> target, ref<rstd::path::Path> relative_source) const
        -> Result<PathBuf> {
        return source_path(
            cache_target_directory(target).as_path(), ""_str, relative_source, ".json"_str);
    }

    auto bmi_directory() const -> PathBuf { return join(output_.as_path(), "bmi"_str); }

    auto bmi(ref<str> logical_name) const -> PathBuf {
        return bmi_directory().join(PathBuf::from(module_filename(logical_name)).as_path());
    }

    auto archive(ref<str> target, ref<str> artifact_name) const -> PathBuf {
        auto directory = join(join(output_.as_path(), "lib"_str).as_path(), target);
        return directory.join(PathBuf::from(artifact_name).as_path());
    }

    auto executable(ref<str> target, ref<str> artifact_name) const -> PathBuf {
        auto directory = join(join(output_.as_path(), "bin"_str).as_path(), target);
        return directory.join(PathBuf::from(artifact_name).as_path());
    }
};

} // namespace tenon
