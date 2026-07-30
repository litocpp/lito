export module tenon.build_layout;

import rstd;
import tenon.model;

using namespace rstd::literals;

namespace tenon::build_layout_detail
{

template<typename T>
auto failure(String message) -> Result<T> {
    return rstd::Err(Error::make(ErrorKind::Artifact, rstd::move(message)));
}

auto join(rstd::ref<rstd::path::Path> base, rstd::ref<rstd::str> component) -> PathBuf {
    return PathBuf::from(base).join(PathBuf::from(component).as_path());
}

auto hex_digit(rstd::u8 value) noexcept -> char {
    return value < rstd::u8(10) ? static_cast<char>('0' + value.to_primitive())
                                : static_cast<char>('A' + (value - rstd::u8(10)).to_primitive());
}

auto source_filename(rstd::ref<rstd::str> relative,
                     rstd::ref<rstd::str> suffix) -> String {
    auto result = String::make();
    for (rstd::usize index {}; index < relative.size(); ++index) {
        const auto byte = relative[index];
        const bool plain = (byte >= rstd::u8('a') && byte <= rstd::u8('z')) ||
                           (byte >= rstd::u8('A') && byte <= rstd::u8('Z')) ||
                           (byte >= rstd::u8('0') && byte <= rstd::u8('9')) ||
                           byte == rstd::u8('_') || byte == rstd::u8('-') ||
                           byte == rstd::u8('.');
        if (plain) {
            result.push_ascii(static_cast<char>(byte.to_primitive()));
        } else {
            const auto raw = byte.to_primitive();
            result.push_ascii('%');
            result.push_ascii(hex_digit(rstd::u8(raw >> 4u)));
            result.push_ascii(hex_digit(rstd::u8(raw & 0x0fu)));
        }
    }
    result.push_str(suffix);
    return result;
}

auto module_filename(rstd::ref<rstd::str> logical_name) -> String {
    auto result = String::make();
    for (auto byte : logical_name) {
        const auto ascii = (byte >= rstd::u8('a') && byte <= rstd::u8('z')) ||
                           (byte >= rstd::u8('A') && byte <= rstd::u8('Z')) ||
                           (byte >= rstd::u8('0') && byte <= rstd::u8('9')) ||
                           byte == rstd::u8('_');
        result.push_ascii(ascii ? static_cast<char>(byte.to_primitive()) : '-');
    }
    result.push_str(".pcm"_str);
    return result;
}

} // namespace tenon::build_layout_detail

export namespace tenon
{

class BuildLayout {
    PathBuf output_;

    explicit BuildLayout(PathBuf output) : output_(rstd::move(output)) {}

    auto source_path(rstd::ref<rstd::str> category,
                     rstd::ref<rstd::str> target,
                     rstd::ref<rstd::path::Path> source,
                     rstd::ref<rstd::path::Path> package_root,
                     rstd::ref<rstd::str> suffix) const -> Result<PathBuf> {
        using namespace build_layout_detail;
        auto relative = source.strip_prefix(package_root);
        if (relative.is_none() || (*relative).is_empty()) {
            return failure<PathBuf>(
                rstd::format("source '{}' is outside package root", source));
        }
        auto text = (*relative).to_str();
        if (text.is_none()) {
            return failure<PathBuf>(
                rstd::format("source path '{}' is not valid UTF-8", source));
        }
        auto directory = join(join(output_.as_path(), category).as_path(), target);
        return rstd::Ok(directory.join(
            PathBuf::from(source_filename(*text, suffix)).as_path()));
    }

public:
    static auto create(rstd::ref<rstd::path::Path> owner_root,
                       rstd::ref<rstd::path::Path> requested_output,
                       rstd::ref<rstd::str> profile) -> Result<BuildLayout> {
        using namespace build_layout_detail;
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
            return failure<BuildLayout>(rstd::format(
                "cannot create output directory '{}': {}",
                output.as_path(),
                rstd::move(created).unwrap_err()));
        }
        auto canonical = rstd::fs::canonicalize(output.as_path());
        if (canonical.is_err()) {
            return failure<BuildLayout>(rstd::format(
                "cannot resolve output directory '{}': {}",
                output.as_path(),
                rstd::move(canonical).unwrap_err()));
        }
        return rstd::Ok(BuildLayout(rstd::move(canonical).unwrap()));
    }

    auto output() const -> rstd::ref<rstd::path::Path> { return output_.as_path(); }

    auto object(rstd::ref<rstd::str> target,
                rstd::ref<rstd::path::Path> source,
                rstd::ref<rstd::path::Path> package_root) const -> Result<PathBuf> {
        return source_path("obj"_str, target, source, package_root, ".o"_str);
    }

    auto depfile(rstd::ref<rstd::str> target,
                 rstd::ref<rstd::path::Path> source,
                 rstd::ref<rstd::path::Path> package_root) const -> Result<PathBuf> {
        return source_path("dep"_str, target, source, package_root, ".d"_str);
    }

    auto fingerprint(rstd::ref<rstd::str> target,
                     rstd::ref<rstd::path::Path> source,
                     rstd::ref<rstd::path::Path> package_root) const -> Result<PathBuf> {
        return source_path("fingerprint"_str, target, source, package_root, ".txt"_str);
    }

    auto bmi(rstd::ref<rstd::str> target,
             rstd::ref<rstd::str> logical_name) const -> PathBuf {
        using namespace build_layout_detail;
        auto directory = join(join(output_.as_path(), "bmi"_str).as_path(), target);
        return directory.join(PathBuf::from(module_filename(logical_name)).as_path());
    }

    auto archive(rstd::ref<rstd::str> target,
                 rstd::ref<rstd::str> artifact_name) const -> PathBuf {
        using namespace build_layout_detail;
        auto directory = join(join(output_.as_path(), "lib"_str).as_path(), target);
        return directory.join(PathBuf::from(artifact_name).as_path());
    }

    auto executable(rstd::ref<rstd::str> target,
                    rstd::ref<rstd::str> artifact_name) const -> PathBuf {
        using namespace build_layout_detail;
        auto directory = join(join(output_.as_path(), "bin"_str).as_path(), target);
        return directory.join(PathBuf::from(artifact_name).as_path());
    }
};

} // namespace tenon
