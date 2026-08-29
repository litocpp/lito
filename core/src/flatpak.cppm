module;
#include <rstd/enum.hpp>

export module lito.core:flatpak;

import rstd;
import rstd.json;
import licrypto;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;
using Json    = rstd::json::Value;
using Map     = rstd::json::Map;
using Array   = rstd::json::Array;

export namespace lito::flatpak
{

enum class ArchiveType
{
    TarGzip,
};

constexpr auto archive_type_name(ArchiveType type) noexcept -> ref<str> {
    switch (type) {
    case ArchiveType::TarGzip: return "tar-gzip"_str;
    }
    rstd::unreachable();
}

class Source {
    RSTD_ENUM(Source,
              (Git, (String url; String commit; PathBuf destination; Vec<String> only_arches;)),
              (File,
               (String url; licrypto::Sha256Digest sha256; PathBuf destination; String filename;
                Vec<String> only_arches;)),
              (Archive,
               (String url; licrypto::Sha256Digest sha256; ArchiveType archive_type;
                PathBuf                                                destination;
                Vec<String>                                            only_arches;)),
              (Inline, (String contents; PathBuf destination; String filename;)),
              (Shell, (Vec<String> commands;)))
};

struct Entry {
    String origin;
    Source source;
};

struct SourceSet {
    Vec<Entry> entries;

    auto push(String origin, Source source) -> void {
        entries.push(Entry { .origin = rstd::move(origin), .source = rstd::move(source) });
    }

    auto append(SourceSet other) -> void {
        entries.reserve(entries.len() + other.entries.len());
        for (auto& entry : other.entries) entries.push(rstd::move(entry));
    }
};

class Error {
    RSTD_ENUM(Error,
              (Io, (String operation; PathBuf path; rstd::io::error::Error source;)),
              (Message, (String message;)))
};

template<typename T>
using Result = rstd::Result<T, Error>;

auto sources_json(const SourceSet& sources) -> Result<String>;
auto write_sources(ref<rstd::path::Path> root,
                   ref<rstd::path::Path> output,
                   const SourceSet&      sources) -> Result<PathBuf>;

} // namespace lito::flatpak

template<typename T>
auto flatpak_failure(String message) -> lito::flatpak::Result<T> {
    return Err(lito::flatpak::Error::Message(rstd::move(message)));
}

template<typename T>
auto flatpak_failure(ref<str> message) -> lito::flatpak::Result<T> {
    return flatpak_failure<T>(String::make(message));
}

auto public_http_url(ref<str> value) -> bool {
    auto remainder = value.strip_prefix("https://"_str);
    if (remainder.is_none()) remainder = value.strip_prefix("http://"_str);
    if (remainder.is_none() || remainder->is_empty()) return false;
    auto has_authority = false;
    for (const auto character : *remainder) {
        const auto ascii = character.to_primitive();
        if (ascii == '/' || ascii == '?' || ascii == '#') break;
        if (ascii == '@' || ascii <= 0x20 || ascii == 0x7f) return false;
        has_authority = true;
    }
    return has_authority;
}

auto path_text(ref<rstd::path::Path> path, ref<str> origin, ref<str> field)
    -> lito::flatpak::Result<String> {
    if (path.is_absolute()) {
        return flatpak_failure<String>(
            rstd::format("Flatpak source from {} has absolute {} '{}'", origin, field, path));
    }
    auto components = path.components();
    auto count      = usize {};
    for (auto component : components) {
        if (! component.is_normal()) {
            return flatpak_failure<String>(
                rstd::format("Flatpak source from {} has non-normal {} '{}'", origin, field, path));
        }
        ++count;
    }
    if (count == usize {}) {
        return flatpak_failure<String>(
            rstd::format("Flatpak source from {} has empty {}", origin, field));
    }
    auto text = path.to_str();
    if (text.is_none()) {
        return flatpak_failure<String>(
            rstd::format("Flatpak source from {} has non-UTF-8 {} '{}'", origin, field, path));
    }
    return Ok(String::make(*text));
}

auto append_architectures(Map& object, const Vec<String>& architectures) -> void {
    if (architectures.is_empty()) return;
    auto values = Array::with_capacity(architectures.len());
    for (const auto& architecture : architectures) {
        values.push(rstd::into<Json>(architecture.as_str()));
    }
    object.insert(String::make("only-arches"_str), Json::Array(rstd::move(values)));
}

auto claim_destination(rstd::collections::BTreeMap<String, String>& claims,
                       String                                       path,
                       ref<str>                                     origin,
                       ref<str> kind) -> lito::flatpak::Result<empty> {
    auto existing = claims.get(path.as_str());
    if (existing.is_some()) {
        return flatpak_failure<empty>(
            rstd::format("Flatpak {} destination '{}' from {} conflicts with {}",
                         kind,
                         path.as_str(),
                         origin,
                         (**existing).as_str()));
    }
    claims.insert(rstd::move(path), String::make(origin));
    return Ok(empty {});
}

auto validate_sources(const lito::flatpak::SourceSet& sources) -> lito::flatpak::Result<empty> {
    auto directories = rstd::collections::BTreeMap<String, String>::make();
    auto files       = rstd::collections::BTreeMap<String, String>::make();
    for (const auto& entry : sources.entries) {
        const auto  origin = entry.origin.as_str();
        const auto& source = entry.source;
        if (source.is_Git()) {
            const auto& value = source.as_Git();
            if (! public_http_url(value.url.as_str())) {
                return flatpak_failure<empty>(rstd::format(
                    "Flatpak Git source from {} requires a public HTTP(S) URL", origin));
            }
            auto destination = rstd_try(path_text(value.destination.as_path(), origin, "dest"_str));
            rstd_try(
                claim_destination(directories, rstd::move(destination), origin, "directory"_str));
            continue;
        }
        if (source.is_Archive()) {
            const auto& value = source.as_Archive();
            if (! public_http_url(value.url.as_str())) {
                return flatpak_failure<empty>(rstd::format(
                    "Flatpak archive source from {} requires a public HTTP(S) URL", origin));
            }
            auto destination = rstd_try(path_text(value.destination.as_path(), origin, "dest"_str));
            rstd_try(
                claim_destination(directories, rstd::move(destination), origin, "directory"_str));
            continue;
        }
        if (source.is_File()) {
            const auto& value = source.as_File();
            if (! public_http_url(value.url.as_str())) {
                return flatpak_failure<empty>(rstd::format(
                    "Flatpak file source from {} requires a public HTTP(S) URL", origin));
            }
            if (value.filename.is_empty() || value.filename.as_str().contains("/"_str)) {
                return flatpak_failure<empty>(
                    rstd::format("Flatpak file source from {} has invalid filename '{}'",
                                 origin,
                                 value.filename.as_str()));
            }
            auto destination =
                value.destination.join(PathBuf::from(value.filename.as_str()).as_path());
            auto text = rstd_try(path_text(destination.as_path(), origin, "file"_str));
            rstd_try(claim_destination(files, rstd::move(text), origin, "file"_str));
            continue;
        }
        if (source.is_Inline()) {
            const auto& value = source.as_Inline();
            if (value.filename.is_empty() || value.filename.as_str().contains("/"_str)) {
                return flatpak_failure<empty>(
                    rstd::format("Flatpak inline source from {} has invalid filename '{}'",
                                 origin,
                                 value.filename.as_str()));
            }
            auto destination =
                value.destination.join(PathBuf::from(value.filename.as_str()).as_path());
            auto text = rstd_try(path_text(destination.as_path(), origin, "file"_str));
            rstd_try(claim_destination(files, rstd::move(text), origin, "file"_str));
            continue;
        }
        if (source.as_Shell().commands.is_empty()) {
            return flatpak_failure<empty>(
                rstd::format("Flatpak shell source from {} has no commands", origin));
        }
    }
    return Ok(empty {});
}

auto source_json(const lito::flatpak::Entry& entry) -> lito::flatpak::Result<Json> {
    auto        object = Map::make();
    const auto& source = entry.source;
    if (source.is_Git()) {
        const auto& value = source.as_Git();
        auto        destination =
            rstd_try(path_text(value.destination.as_path(), entry.origin.as_str(), "dest"_str));
        object.insert(String::make("commit"_str), rstd::into<Json>(value.commit.as_str()));
        object.insert(String::make("dest"_str), rstd::into<Json>(destination.as_str()));
        object.insert(String::make("type"_str), rstd::into<Json>("git"_str));
        object.insert(String::make("url"_str), rstd::into<Json>(value.url.as_str()));
        append_architectures(object, value.only_arches);
        return Ok(Json::Object(rstd::move(object)));
    }
    if (source.is_File()) {
        const auto& value = source.as_File();
        auto        destination =
            rstd_try(path_text(value.destination.as_path(), entry.origin.as_str(), "dest"_str));
        object.insert(String::make("dest"_str), rstd::into<Json>(destination.as_str()));
        object.insert(String::make("dest-filename"_str), rstd::into<Json>(value.filename.as_str()));
        auto sha256 = value.sha256.to_hex();
        object.insert(String::make("sha256"_str), rstd::into<Json>(sha256.as_str()));
        object.insert(String::make("type"_str), rstd::into<Json>("file"_str));
        object.insert(String::make("url"_str), rstd::into<Json>(value.url.as_str()));
        append_architectures(object, value.only_arches);
        return Ok(Json::Object(rstd::move(object)));
    }
    if (source.is_Archive()) {
        const auto& value = source.as_Archive();
        auto        destination =
            rstd_try(path_text(value.destination.as_path(), entry.origin.as_str(), "dest"_str));
        object.insert(String::make("archive-type"_str),
                      rstd::into<Json>(lito::flatpak::archive_type_name(value.archive_type)));
        object.insert(String::make("dest"_str), rstd::into<Json>(destination.as_str()));
        auto sha256 = value.sha256.to_hex();
        object.insert(String::make("sha256"_str), rstd::into<Json>(sha256.as_str()));
        object.insert(String::make("type"_str), rstd::into<Json>("archive"_str));
        object.insert(String::make("url"_str), rstd::into<Json>(value.url.as_str()));
        append_architectures(object, value.only_arches);
        return Ok(Json::Object(rstd::move(object)));
    }
    if (source.is_Inline()) {
        const auto& value = source.as_Inline();
        auto        destination =
            rstd_try(path_text(value.destination.as_path(), entry.origin.as_str(), "dest"_str));
        object.insert(String::make("contents"_str), rstd::into<Json>(value.contents.as_str()));
        object.insert(String::make("dest"_str), rstd::into<Json>(destination.as_str()));
        object.insert(String::make("dest-filename"_str), rstd::into<Json>(value.filename.as_str()));
        object.insert(String::make("type"_str), rstd::into<Json>("inline"_str));
        return Ok(Json::Object(rstd::move(object)));
    }
    auto commands = Array::with_capacity(source.as_Shell().commands.len());
    for (const auto& command : source.as_Shell().commands) {
        commands.push(rstd::into<Json>(command.as_str()));
    }
    object.insert(String::make("commands"_str), Json::Array(rstd::move(commands)));
    object.insert(String::make("type"_str), rstd::into<Json>("shell"_str));
    return Ok(Json::Object(rstd::move(object)));
}

auto lito::flatpak::sources_json(const SourceSet& sources) -> Result<String> {
    rstd_try(validate_sources(sources));
    auto document = Array::with_capacity(sources.entries.len());
    for (const auto& source : sources.entries) document.push(rstd_try(source_json(source)));
    return Ok(
        rstd::json::to_string(Json::Array(rstd::move(document)),
                              rstd::json::FormatOptions { .pretty = true, .indent = usize(2) }));
}

auto lito::flatpak::write_sources(ref<rstd::path::Path> root,
                                  ref<rstd::path::Path> output,
                                  const SourceSet&      sources) -> Result<PathBuf> {
    auto contents = rstd_try(sources_json(sources));
    auto destination =
        output.is_absolute() ? PathBuf::from(output) : PathBuf::from(root).join(output);
    auto written = rstd::fs::write_atomic(destination.as_path(), contents.as_str().as_bytes());
    if (written.is_err()) {
        return Err(Error::Io(String::make("write Flatpak sources"_str),
                             destination.clone(),
                             rstd::move(written).unwrap_err()));
    }
    return Ok(rstd::move(destination));
}

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::flatpak::Error> : ImplBase<lito::flatpak::Error> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Message()) return formatter.write_str(error.as_Message().message.as_str());
        const auto& value = error.as_Io();
        return formatter.write_fmt(
            fmt::Arguments::make("cannot {} '{}'", value.operation, value.path.as_path()));
    }
};

template<>
struct Impl<fmt::Debug, lito::flatpak::Error> : ImplBase<lito::flatpak::Error> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::flatpak::Error> : ImplBase<lito::flatpak::Error> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (! error.is_Io()) return None();
        return Some(dyn<error::Error>::from_ref(error.as_Io().source));
    }
};

} // namespace rstd
