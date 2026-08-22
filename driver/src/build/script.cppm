module;
#include <rstd/macro.hpp>

module lito.driver:build.script;

import rstd;
import lito.crypto;
import lito.tools;
import rstd.json;
import luato;
import lito.core;
import :build.event;
import :build.artifact;
import lito.cpp;
import :build.layout;
import :build.host_tool;
import :package.module_catalog;
import :build.host_tool_error;
import :build.tool_action_error;
import :build.script_error;
import lito.system;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;

namespace lito
{

using Json = rstd::json::Value;

constexpr auto build_host_api_identity = "lito:build-host-api:v1"_str;

template<typename T>
auto script_failure(String message) -> BuildScriptResult<T> {
    return Err(BuildScriptError::Message(rstd::move(message)));
}

template<typename T>
auto script_failure(ref<str> message) -> BuildScriptResult<T> {
    return Err(BuildScriptError::Message(String::make(message)));
}

template<typename T>
auto script_io_failure(ref<str> operation, ref<rstd::path::Path> path, rstd::io::error::Error error)
    -> BuildScriptResult<T> {
    return Err(
        BuildScriptError::Io(String::make(operation), PathBuf::from(path), rstd::move(error)));
}

template<typename T>
auto action_failure(BuildToolActionError error) -> BuildScriptResult<T> {
    return Err(BuildScriptError::BuildToolAction(rstd::move(error)));
}

template<typename T>
auto action_request_failure(String message) -> BuildScriptResult<T> {
    return action_failure<T>(BuildToolActionError::InvalidRequest(rstd::move(message)));
}

template<typename T>
auto action_request_failure(ref<str> message) -> BuildScriptResult<T> {
    return action_request_failure<T>(String::make(message));
}

template<typename T>
auto action_receipt_failure(ref<str>               operation,
                            ref<rstd::path::Path>  path,
                            rstd::io::error::Error error) -> BuildScriptResult<T> {
    return action_failure<T>(BuildToolActionError::Receipt(
        String::make(operation), PathBuf::from(path), rstd::move(error)));
}

template<typename T>
auto action_publication_failure(ref<str>               operation,
                                ref<rstd::path::Path>  path,
                                rstd::io::error::Error error) -> BuildScriptResult<T> {
    return action_failure<T>(BuildToolActionError::Publication(
        String::make(operation), PathBuf::from(path), rstd::move(error)));
}

auto normal_relative_path(String text, ref<str> context) -> BuildScriptResult<PathBuf> {
    auto path = PathBuf::from(rstd::move(text));
    if (path.is_empty() || path.as_path().is_absolute() || path.as_path().has_root()) {
        return script_failure<PathBuf>(
            rstd::format("{} must be a non-empty relative path", context));
    }
    auto components = path.as_path().components();
    for (auto component : components) {
        if (component.is_normal()) continue;
        return script_failure<PathBuf>(
            rstd::format("{} contains a non-normal path component", context));
    }
    return Ok(rstd::move(path));
}

class BuildOutputRegistry {
public:
    auto claim(ref<str> package, ref<rstd::path::Path> relative, ref<str> owner)
        -> BuildScriptResult<empty> {
        auto key = String::make(package);
        key.push_ascii('\n');
        key.push_str(relative.to_string_lossy().as_str());
        auto existing = owners_.get(key.as_str());
        if (existing.is_some()) {
            return script_failure<empty>(
                rstd::format("generated output '{}:{}' is claimed by build scripts '{}' and '{}'",
                             package,
                             relative,
                             **existing,
                             owner));
        }
        owners_.insert(rstd::move(key), String::make(owner));
        return Ok(empty {});
    }

private:
    rstd::collections::BTreeMap<String, String> owners_ =
        rstd::collections::BTreeMap<String, String>::make();
};

struct ConfigurePackage {
    String  name;
    PathBuf source_root;
    PathBuf generated_root;
};

struct OwnedOutput {
    String  package;
    PathBuf relative;

    auto clone() const -> OwnedOutput {
        return OwnedOutput { .package = package.clone(), .relative = relative.clone() };
    }
};

auto same_output(const OwnedOutput& left, const OwnedOutput& right) noexcept -> bool {
    return left.package == right.package.as_str() && left.relative == right.relative.as_path();
}

auto contains_output(const Vec<OwnedOutput>& values, const OwnedOutput& candidate) noexcept
    -> bool {
    for (const auto& value : values) {
        if (same_output(value, candidate)) return true;
    }
    return false;
}

auto package_is_selected(const Vec<ConfigurePackage>& packages, ref<str> name) noexcept -> bool {
    for (const auto& package : packages) {
        if (package.name == name) return true;
    }
    return false;
}

auto package_is_selected(const Vec<String>& packages, ref<str> name) noexcept -> bool {
    for (const auto& package : packages) {
        if (package == name) return true;
    }
    return false;
}

auto package_component_is_valid(ref<str> package) noexcept -> bool {
    auto path  = PathBuf::from(package);
    auto parts = path.as_path().components();
    auto first = parts.next();
    return ! package.is_empty() && first.is_some() && first->is_normal() && parts.next().is_none();
}

auto load_receipt(ref<rstd::path::Path> path) -> BuildScriptResult<Vec<OwnedOutput>> {
    auto exists = rstd::fs::exists(path);
    if (exists.is_err()) {
        return script_io_failure<Vec<OwnedOutput>>(
            "inspect configure receipt"_str, path, rstd::move(exists).unwrap_err());
    }
    if (! *exists) return Ok(Vec<OwnedOutput>::make());
    auto contents = rstd::fs::read_to_string(path);
    if (contents.is_err()) {
        return script_io_failure<Vec<OwnedOutput>>(
            "read configure receipt"_str, path, rstd::move(contents).unwrap_err());
    }
    auto parsed = rstd::json::from_str(contents->as_str());
    if (parsed.is_err()) {
        return Err(BuildScriptError::Json(PathBuf::from(path), rstd::move(parsed).unwrap_err()));
    }
    auto document = rstd::move(parsed).unwrap();
    auto version  = document.get("version"_str);
    auto outputs  = document.get("outputs"_str);
    if (version.is_none() || (**version).as_i64() != Some(i64(1)) || outputs.is_none() ||
        (**outputs).as_array().is_none()) {
        return script_failure<Vec<OwnedOutput>>(
            rstd::format("configure receipt '{}' has an unsupported schema", path));
    }

    auto result = Vec<OwnedOutput>::make();
    auto array  = (**outputs).as_array();
    for (usize index {}; index < (**array).len(); ++index) {
        const auto& item     = (**array)[index];
        auto        package  = item.get("package"_str);
        auto        relative = item.get("path"_str);
        if (package.is_none() || relative.is_none()) {
            return script_failure<Vec<OwnedOutput>>(
                rstd::format("configure receipt '{}' has an invalid output entry", path));
        }
        auto package_text  = (**package).as_str();
        auto relative_text = (**relative).as_str();
        if (package_text.is_none() || relative_text.is_none() ||
            ! package_component_is_valid(*package_text)) {
            return script_failure<Vec<OwnedOutput>>(
                rstd::format("configure receipt '{}' has an invalid output entry", path));
        }
        auto path_value =
            normal_relative_path(String::make(*relative_text), "configure receipt output path"_str);
        if (path_value.is_err()) return Err(rstd::move(path_value).unwrap_err());
        result.push(OwnedOutput {
            .package  = String::make(*package_text),
            .relative = rstd::move(path_value).unwrap(),
        });
    }
    return Ok(rstd::move(result));
}

auto encode_receipt(const Vec<OwnedOutput>& outputs) -> String {
    auto array = rstd::json::Array::with_capacity(outputs.len());
    for (const auto& output : outputs) {
        auto object = rstd::json::Map::make();
        object.insert(String::make("package"_str), Json::String(output.package.clone()));
        object.insert(String::make("path"_str),
                      Json::String(output.relative.as_path().to_string_lossy()));
        array.push(Json::Object(rstd::move(object)));
    }
    auto document = rstd::json::Map::make();
    document.insert(String::make("version"_str),
                    Json::Number(rstd::json::Number::from_i64(i64(1))));
    document.insert(String::make("outputs"_str), Json::Array(rstd::move(array)));
    auto text =
        rstd::json::to_string(Json::Object(rstd::move(document)),
                              rstd::json::FormatOptions { .pretty = true, .indent = usize(2) });
    text.push_ascii('\n');
    return text;
}

auto find_package_root(const cpp::PackageMetadata& metadata, ref<str> name)
    -> Option<ref<rstd::path::Path>> {
    for (const auto& target : metadata.targets) {
        if (target.id.package == name) return Some(target.root.as_path());
    }
    return None();
}

struct ConfigureOutcome {
    PathBuf                output;
    rstd::fs::WriteOutcome write { rstd::fs::WriteOutcome::Unchanged };
};

class ConfigureSession {
public:
    static auto create(const cpp::PackageMetadata& metadata,
                       const BuildLayout&          layout,
                       const Vec<String>&          selected_packages,
                       ref<str>                    receipt_owner,
                       Option<String>              default_package,
                       ref<str>                    script_owner,
                       BuildOutputRegistry&        outputs) -> BuildScriptResult<ConfigureSession> {
        auto packages = Vec<ConfigurePackage>::make();
        for (const auto& name : selected_packages) {
            auto source_root = find_package_root(metadata, name.as_str());
            if (source_root.is_none()) {
                return script_failure<ConfigureSession>(rstd::format(
                    "selected build-script package '{}' has no package root", name.as_str()));
            }
            auto generated = rstd_try(layout.create_generated_package_directory(name.as_str()));
            packages.push(ConfigurePackage {
                .name           = name.clone(),
                .source_root    = PathBuf::from(*source_root),
                .generated_root = rstd::move(generated),
            });
        }
        auto receipt  = layout.configure_receipt(receipt_owner);
        auto previous = load_receipt(receipt.as_path());
        if (previous.is_err()) return Err(rstd::move(previous).unwrap_err());
        auto current = Vec<OwnedOutput>::make();
        for (const auto& output : *previous) {
            if (! package_is_selected(packages, output.package.as_str()))
                current.push(output.clone());
        }
        return Ok(ConfigureSession(rstd::move(packages),
                                   rstd::move(receipt),
                                   rstd::move(previous).unwrap(),
                                   rstd::move(current),
                                   rstd::move(default_package),
                                   String::make(script_owner),
                                   outputs));
    }

    auto configure(ref<str>               package_name,
                   String                 input_text,
                   String                 output_text,
                   const ConfigureValues& values) -> BuildScriptResult<ConfigureOutcome> {
        const ConfigurePackage* owner = nullptr;
        for (const auto& package : packages_) {
            if (package.name == package_name) {
                owner = rstd::addressof(package);
                break;
            }
        }
        if (owner == nullptr) {
            return script_failure<ConfigureOutcome>(
                rstd::format("package '{}' is not a selected root package", package_name));
        }

        auto input_relative =
            normal_relative_path(rstd::move(input_text), "configure_file.input"_str);
        if (input_relative.is_err()) return Err(rstd::move(input_relative).unwrap_err());
        auto input_requested = owner->source_root.join(input_relative->as_path());
        auto input           = rstd::fs::canonicalize(input_requested.as_path());
        if (input.is_err()) {
            return script_io_failure<ConfigureOutcome>("resolve configure_file input"_str,
                                                       input_requested.as_path(),
                                                       rstd::move(input).unwrap_err());
        }
        if (input->as_path().strip_prefix(owner->source_root.as_path()).is_none()) {
            return script_failure<ConfigureOutcome>(
                rstd::format("configure_file input '{}' escapes package '{}'",
                             input_requested.as_path(),
                             package_name));
        }
        auto input_metadata = rstd::fs::metadata(input->as_path());
        if (input_metadata.is_err()) {
            return script_io_failure<ConfigureOutcome>("inspect configure_file input"_str,
                                                       input->as_path(),
                                                       rstd::move(input_metadata).unwrap_err());
        }
        if (! input_metadata->is_file()) {
            return script_failure<ConfigureOutcome>(
                rstd::format("configure_file input '{}' is not a regular file", input->as_path()));
        }
        auto template_text = rstd::fs::read_to_string(input->as_path());
        if (template_text.is_err()) {
            return script_io_failure<ConfigureOutcome>("read configure_file input"_str,
                                                       input->as_path(),
                                                       rstd::move(template_text).unwrap_err());
        }
        auto rendered =
            render_configure_template(template_text->as_str(), values, input->as_path());
        if (rendered.is_err())
            return Err(rstd::into<BuildScriptError>(rstd::move(rendered).unwrap_err()));

        auto output_relative =
            normal_relative_path(rstd::move(output_text), "configure_file.output"_str);
        if (output_relative.is_err()) return Err(rstd::move(output_relative).unwrap_err());
        auto requested = owner->generated_root.join(output_relative->as_path());
        auto parent    = requested.as_path().parent();
        if (parent.is_none()) {
            return script_failure<ConfigureOutcome>("configure_file output has no parent"_str);
        }
        auto parent_created = rstd::fs::create_dir_all(*parent);
        if (parent_created.is_err()) {
            return script_io_failure<ConfigureOutcome>("create configure_file output parent"_str,
                                                       *parent,
                                                       rstd::move(parent_created).unwrap_err());
        }
        auto canonical_parent = rstd::fs::canonicalize(*parent);
        if (canonical_parent.is_err()) {
            return script_io_failure<ConfigureOutcome>("resolve configure_file output parent"_str,
                                                       *parent,
                                                       rstd::move(canonical_parent).unwrap_err());
        }
        if (canonical_parent->as_path().strip_prefix(owner->generated_root.as_path()).is_none()) {
            return script_failure<ConfigureOutcome>(rstd::format(
                "configure_file output '{}' escapes generated package root", requested.as_path()));
        }
        auto file_name = requested.as_path().file_name();
        if (file_name.is_none()) {
            return script_failure<ConfigureOutcome>("configure_file output has no file name"_str);
        }
        auto output   = canonical_parent->join(PathBuf::from(*file_name).as_path());
        auto existing = rstd::fs::symlink_metadata(output.as_path());
        if (existing.is_ok() && (! existing->is_file() || existing->is_symlink())) {
            return script_failure<ConfigureOutcome>(rstd::format(
                "configure_file output '{}' is not a regular non-symlink file", output.as_path()));
        }
        if (existing.is_err()) {
            auto error = rstd::move(existing).unwrap_err();
            if (error.kind() !=
                rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound }) {
                return script_io_failure<ConfigureOutcome>(
                    "inspect configure_file output"_str, output.as_path(), rstd::move(error));
            }
        }
        for (const auto& claimed : claimed_) {
            if (claimed.as_path() == output.as_path()) {
                return script_failure<ConfigureOutcome>(rstd::format(
                    "configure_file output '{}' is claimed more than once", output.as_path()));
            }
        }
        rstd_try(output_registry_->claim(
            owner->name.as_str(), output_relative->as_path(), script_owner_.as_str()));

        auto written =
            rstd::fs::write_atomic_if_changed(output.as_path(), rendered->as_str().as_bytes());
        if (written.is_err()) {
            return script_io_failure<ConfigureOutcome>("write configure_file output"_str,
                                                       output.as_path(),
                                                       rstd::move(written).unwrap_err());
        }
        claimed_.push(output.clone());
        current_.push(OwnedOutput {
            .package  = owner->name.clone(),
            .relative = rstd::move(output_relative).unwrap(),
        });
        report_.files.push(ConfiguredFile {
            .input  = input->clone(),
            .output = output.clone(),
            .write  = *written,
        });
        switch (*written) {
        case rstd::fs::WriteOutcome::Created: ++report_.created; break;
        case rstd::fs::WriteOutcome::Replaced: ++report_.replaced; break;
        case rstd::fs::WriteOutcome::Unchanged: ++report_.unchanged; break;
        }
        return Ok(ConfigureOutcome { rstd::move(output), *written });
    }

    auto default_package() const noexcept -> Option<ref<str>> {
        if (default_package_.is_none()) return None();
        return Some(default_package_->as_str());
    }

    auto finish() -> BuildScriptResult<BuildScriptReport> {
        for (const auto& stale : previous_) {
            if (! package_is_selected(packages_, stale.package.as_str()) ||
                contains_output(current_, stale)) {
                continue;
            }
            const ConfigurePackage* owner = nullptr;
            for (const auto& package : packages_) {
                if (package.name == stale.package.as_str()) owner = rstd::addressof(package);
            }
            if (owner == nullptr) continue;
            auto requested = owner->generated_root.join(stale.relative.as_path());
            auto metadata  = rstd::fs::symlink_metadata(requested.as_path());
            if (metadata.is_err()) {
                auto error = rstd::move(metadata).unwrap_err();
                if (error.kind() ==
                    rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound }) {
                    continue;
                }
                return script_io_failure<BuildScriptReport>(
                    "inspect stale configure output"_str, requested.as_path(), rstd::move(error));
            }
            auto parent = requested.as_path().parent();
            if (parent.is_none()) {
                return script_failure<BuildScriptReport>(
                    "stale configure output has no parent"_str);
            }
            auto canonical_parent = rstd::fs::canonicalize(*parent);
            if (canonical_parent.is_err()) {
                return script_io_failure<BuildScriptReport>(
                    "resolve stale configure output parent"_str,
                    *parent,
                    rstd::move(canonical_parent).unwrap_err());
            }
            if (canonical_parent->as_path()
                    .strip_prefix(owner->generated_root.as_path())
                    .is_none()) {
                return script_failure<BuildScriptReport>(
                    rstd::format("stale configure output '{}' escapes generated package root",
                                 requested.as_path()));
            }
            if (! metadata->is_file() || metadata->is_symlink()) {
                return script_failure<BuildScriptReport>(
                    rstd::format("stale configure output '{}' is not an owned regular file",
                                 requested.as_path()));
            }
            auto removed = rstd::fs::remove_file(requested.as_path());
            if (removed.is_err()) {
                return script_io_failure<BuildScriptReport>("remove stale configure output"_str,
                                                            requested.as_path(),
                                                            rstd::move(removed).unwrap_err());
            }
            auto directory = rstd::move(canonical_parent).unwrap();
            while (directory.as_path() != owner->generated_root.as_path()) {
                auto enclosing = directory.as_path().parent();
                if (enclosing.is_none()) break;
                auto next = PathBuf::from(*enclosing);
                if (rstd::fs::remove_dir(directory.as_path()).is_err()) break;
                directory = rstd::move(next);
            }
            ++report_.stale_removed;
        }

        for (auto index = usize(1); index < current_.len(); ++index) {
            auto cursor = index;
            while (cursor > usize()) {
                auto prior = cursor - usize(1);
                auto ordered =
                    current_[prior].package < current_[cursor].package.as_str() ||
                    (current_[prior].package == current_[cursor].package.as_str() &&
                     current_[prior].relative.as_path() < current_[cursor].relative.as_path());
                if (ordered) break;
                auto moved       = rstd::move(current_[cursor]);
                current_[cursor] = rstd::move(current_[prior]);
                current_[prior]  = rstd::move(moved);
                --cursor;
            }
        }
        auto parent = receipt_.as_path().parent();
        if (parent.is_none()) return script_failure<BuildScriptReport>("receipt has no parent"_str);
        auto created = rstd::fs::create_dir_all(*parent);
        if (created.is_err()) {
            return script_io_failure<BuildScriptReport>("create configure receipt directory"_str,
                                                        *parent,
                                                        rstd::move(created).unwrap_err());
        }
        auto text = encode_receipt(current_);
        auto written =
            rstd::fs::write_atomic_if_changed(receipt_.as_path(), text.as_str().as_bytes());
        if (written.is_err()) {
            return script_io_failure<BuildScriptReport>("write configure receipt"_str,
                                                        receipt_.as_path(),
                                                        rstd::move(written).unwrap_err());
        }
        return Ok(rstd::move(report_));
    }

    auto report() noexcept -> BuildScriptReport& { return report_; }

private:
    ConfigureSession(Vec<ConfigurePackage> packages,
                     PathBuf               receipt,
                     Vec<OwnedOutput>      previous,
                     Vec<OwnedOutput>      current,
                     Option<String>        default_package,
                     String                script_owner,
                     BuildOutputRegistry&  outputs)
        : packages_(rstd::move(packages)),
          receipt_(rstd::move(receipt)),
          previous_(rstd::move(previous)),
          current_(rstd::move(current)),
          default_package_(rstd::move(default_package)),
          script_owner_(rstd::move(script_owner)),
          output_registry_(rstd::addressof(outputs)) {}

    Vec<ConfigurePackage> packages_;
    PathBuf               receipt_;
    Vec<OwnedOutput>      previous_;
    Vec<OwnedOutput>      current_;
    Vec<PathBuf>          claimed_;
    Option<String>        default_package_;
    String                script_owner_;
    BuildOutputRegistry*  output_registry_ {};
    BuildScriptReport     report_;
};

auto action_string_array(const luato::Array& values, ref<str> context)
    -> BuildScriptResult<Vec<String>> {
    auto result = Vec<String>::with_capacity(values.len());
    for (usize index {}; index < values.len(); ++index) {
        const auto& value = values.values()[index];
        if (! value.is_String()) {
            return action_request_failure<Vec<String>>(
                rstd::format("{}[{}] must be a string", context, index + usize(1)));
        }
        result.push(value.as_String().value.clone());
    }
    return Ok(rstd::move(result));
}

auto lua_string_array(const Vec<String>& values) -> luato::Array {
    auto result = Vec<luato::Value>::with_capacity(values.len());
    for (const auto& value : values) result.push(luato::Value::String(value.clone()));
    return luato::Array::from(rstd::move(result));
}

auto action_file_digest(ref<rstd::path::Path> path) -> BuildScriptResult<String> {
    auto data = rstd::fs::read(path);
    if (data.is_err()) {
        return script_io_failure<String>(
            "read build-tool action file"_str, path, rstd::move(data).unwrap_err());
    }
    return Ok(lito::crypto::sha256_hex(data->as_slice()));
}

struct ActionDependency {
    PathBuf path;
    String  digest;
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
        return script_io_failure<Vec<ActionDependency>>(
            "read build-tool depfile"_str, depfile, rstd::move(contents).unwrap_err());
    }
    auto paths  = rstd_try(make_depfile_paths(contents->as_str(), working_directory));
    auto result = Vec<ActionDependency>::make();
    for (const auto& path : paths) {
        auto canonical = rstd::fs::canonicalize(path.as_path());
        if (canonical.is_err()) {
            return script_io_failure<Vec<ActionDependency>>(
                "resolve build-tool depfile dependency"_str,
                path.as_path(),
                rstd::move(canonical).unwrap_err());
        }
        auto metadata = rstd::fs::symlink_metadata(canonical->as_path());
        if (metadata.is_err() || metadata->is_symlink() || ! metadata->is_file()) {
            return action_failure<Vec<ActionDependency>>(BuildToolActionError::InvalidInput(
                canonical->clone(), String::make("depfile dependency is not a regular file"_str)));
        }
        auto allowed = false;
        for (const auto& root : allowed_roots) {
            if (canonical->as_path().strip_prefix(root.as_path()).is_some()) {
                allowed = true;
                break;
            }
        }
        for (const auto& input : direct_inputs) {
            if (canonical->as_path() == input.as_path()) {
                allowed = true;
                break;
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
        auto digest = rstd_try(action_file_digest(canonical->as_path()));
        result.push(ActionDependency {
            .path   = rstd::move(canonical).unwrap(),
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
    if (version.is_none() || (**version).as_i64() != Some(i64(2)) || recorded_identity.is_none() ||
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
        auto digest = entry.get("sha256"_str);
        if (path.is_none() || (**path).as_str().is_none() || digest.is_none() ||
            (**digest).as_str().is_none()) {
            return Ok(false);
        }
        auto dependency_path = PathBuf::from(String::make(*(**path).as_str()));
        auto actual          = action_file_digest(dependency_path.as_path());
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
                    Json::Number(rstd::json::Number::from_u64(u64(2))));
    document.insert(String::make("identity"_str), Json::String(String::make(identity)));
    document.insert(String::make("outputs"_str), Json::Array(rstd::move(entries)));
    auto dependency_entries = rstd::json::Array::with_capacity(dependencies.len());
    for (const auto& dependency : dependencies) {
        auto item = rstd::json::Map::make();
        item.insert(String::make("path"_str),
                    Json::String(dependency.path.as_path().to_string_lossy()));
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
        return script_io_failure<empty>(
            "enumerate staged build-tool outputs"_str, directory, rstd::move(opened).unwrap_err());
    }
    auto entries = rstd::move(opened).unwrap();
    for (auto item : entries) {
        if (item.is_err()) {
            return script_io_failure<empty>("enumerate staged build-tool outputs"_str,
                                            directory,
                                            rstd::move(item).unwrap_err());
        }
        auto entry = rstd::move(item).unwrap();
        auto type  = entry.file_type();
        auto path  = entry.path();
        if (type.is_err()) {
            return script_io_failure<empty>("inspect staged build-tool output"_str,
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

struct GeneratedActionOutput {
    String  package;
    PathBuf relative;
    String  action_identity;
    usize   producer {};
};

struct ResolvedActionInput {
    PathBuf       path;
    String        digest;
    Option<usize> producer;
};

enum class RegisteredActionKind
{
    Process,
    Write,
    Copy,
    CppLeadingPreamble,
};

struct RegisteredAction {
    RegisteredActionKind     kind { RegisteredActionKind::Process };
    String                   package;
    String                   identity;
    String                   label;
    PathBuf                  working_directory;
    PathBuf                  executable;
    Vec<String>              arguments;
    Vec<ResolvedActionInput> inputs;
    Vec<PathBuf>             outputs;
    Option<usize>            output_working_directory;
    Option<usize>            depfile_output;
    Vec<PathBuf>             depfile_roots;
    String                   content;
};

auto replace_all(String& value, ref<str> marker, ref<str> replacement) -> usize {
    auto count = usize {};
    while (value.as_str().contains(marker)) {
        auto found = Option<usize> {};
        for (usize index {}; index + marker.len() <= value.len(); ++index) {
            if (value.as_str().get(index, index + marker.len()).unwrap() == marker) {
                found = Some(index);
                break;
            }
        }
        if (found.is_none()) break;
        value.replace_range(*found, *found + marker.len(), replacement);
        ++count;
    }
    return count;
}

auto xml_text(ref<str> value) -> String {
    auto result = String::make();
    auto start  = usize {};
    for (usize index {}; index < value.len(); ++index) {
        auto byte        = value.as_bytes()[index];
        auto replacement = ""_str;
        if (byte == u8('&'))
            replacement = "&amp;"_str;
        else if (byte == u8('<'))
            replacement = "&lt;"_str;
        else if (byte == u8('>'))
            replacement = "&gt;"_str;
        else if (byte == u8('"'))
            replacement = "&quot;"_str;
        else if (byte == u8('\''))
            replacement = "&apos;"_str;
        else
            continue;
        result.push_str(value.get(start, index).unwrap());
        result.push_str(replacement);
        start = index + usize(1);
    }
    result.push_str(value.get(start, value.len()).unwrap());
    return result;
}

struct ToolActionOutcome {
    bool                     changed { false };
    Vec<luato::OpaqueHandle> outputs;
};

struct GeneratedActionWorkerResult {
    usize                    action {};
    BuildScriptResult<empty> outcome;
};

class ToolActionSession {
public:
    ToolActionSession(cpp::PackageMetadata&             metadata,
                      cpp::ResolvedNativeTargetPlan&    target_plan,
                      const BuildLayout&                layout,
                      ref<str>                          profile,
                      ref<rstd::path::Path>             script,
                      Option<String>                    default_package,
                      String                            script_owner,
                      Vec<String>                       packages,
                      BuildOutputRegistry&              outputs,
                      const ResolvedHostBuildTools&     tools,
                      const TargetInfo&                 target_info,
                      const ResolvedProcessEnvironment& environment,
                      const Option<BuildEventSink>&     observer)
        : metadata_(rstd::addressof(metadata)),
          target_plan_(rstd::addressof(target_plan)),
          layout_(rstd::addressof(layout)),
          profile_(String::make(profile)),
          script_(PathBuf::from(script)),
          default_package_(rstd::move(default_package)),
          script_owner_(rstd::move(script_owner)),
          packages_(rstd::move(packages)),
          output_registry_(rstd::addressof(outputs)),
          tools_(rstd::addressof(tools)),
          target_info_(rstd::addressof(target_info)),
          environment_(rstd::addressof(environment)),
          observer_(rstd::addressof(observer)) {}

    auto tool(ref<str> alias) const -> BuildScriptResult<luato::OpaqueHandle> {
        auto identity = default_package_.is_some()
                            ? tools_->identity(default_package_->as_str(), alias)
                            : tools_->identity(alias);
        if (identity == nullptr) {
            return action_failure<luato::OpaqueHandle>(
                BuildToolActionError::UnknownTool(String::make(alias)));
        }
        return Ok(luato::OpaqueHandle { .identity = identity });
    }

    auto target(const luato::Table& request) const -> BuildScriptResult<luato::OpaqueHandle> {
        auto known = Vec<String>::make();
        known.push(String::make("package"_str));
        known.push(String::make("kind"_str));
        known.push(String::make("name"_str));
        auto checked = request.reject_unknown_fields(known.as_slice());
        if (checked.is_err()) {
            return action_request_failure<luato::OpaqueHandle>(
                rstd::format("{}", checked.unwrap_err()));
        }
        auto package = Result<String, luato::Error>(
            Err(luato::Error::binding(String::make("lito.target.package is required"_str))));
        if (request.contains("package"_str)) {
            package = request.required<String>("package"_str);
        } else if (default_package_.is_some()) {
            package = Ok(default_package_->clone());
        }
        auto kind = request.required<String>("kind"_str);
        auto name = request.required<String>("name"_str);
        if (package.is_err()) {
            return action_request_failure<luato::OpaqueHandle>(
                rstd::format("{}", package.unwrap_err()));
        }
        if (kind.is_err()) {
            return action_request_failure<luato::OpaqueHandle>(
                rstd::format("{}", kind.unwrap_err()));
        }
        if (name.is_err()) {
            return action_request_failure<luato::OpaqueHandle>(
                rstd::format("{}", name.unwrap_err()));
        }
        auto allowed = false;
        for (const auto& candidate : packages_) {
            if (candidate == package->as_str()) allowed = true;
        }
        if (! allowed) {
            return action_request_failure<luato::OpaqueHandle>(rstd::format(
                "package '{}' is not available to this build script", package->as_str()));
        }
        for (const auto& candidate : metadata_->targets) {
            if (candidate.id.package == package->as_str() &&
                lito::package::package_target_kind_name(candidate.id.kind) == kind->as_str() &&
                candidate.id.name == name->as_str()) {
                return Ok(luato::OpaqueHandle { .identity = rstd::addressof(candidate) });
            }
        }
        return action_request_failure<luato::OpaqueHandle>(
            rstd::format("target '{}::{}::{}' is not selected",
                         package->as_str(),
                         kind->as_str(),
                         name->as_str()));
    }

    auto external_dependency(luato::OpaqueHandle target_handle, ref<str> alias) const
        -> BuildScriptResult<luato::OpaqueHandle> {
        const cpp::ResolvedTarget* target = nullptr;
        for (const auto& candidate : metadata_->targets) {
            if (rstd::addressof(candidate) == target_handle.identity) {
                target = rstd::addressof(candidate);
                break;
            }
        }
        if (target == nullptr) {
            return action_request_failure<luato::OpaqueHandle>(
                "target handle does not belong to this build script"_str);
        }
        for (const auto& dependency : target->external_dependencies) {
            if (dependency.alias == alias) {
                return Ok(luato::OpaqueHandle { .identity = rstd::addressof(dependency) });
            }
        }
        return action_request_failure<luato::OpaqueHandle>(
            rstd::format("target '{}::{}' has no active external dependency '{}'",
                         target->id.package.as_str(),
                         target->id.name.as_str(),
                         alias));
    }

    auto external_tool(luato::OpaqueHandle dependency_handle, ref<str> name) const
        -> BuildScriptResult<luato::OpaqueHandle> {
        const cpp::ResolvedExternalDependency* dependency = nullptr;
        for (const auto& target : metadata_->targets) {
            for (const auto& candidate : target.external_dependencies) {
                if (rstd::addressof(candidate) == dependency_handle.identity) {
                    dependency = rstd::addressof(candidate);
                    break;
                }
            }
        }
        if (dependency == nullptr) {
            return action_request_failure<luato::OpaqueHandle>(
                "external dependency handle does not belong to this build script"_str);
        }
        for (const auto& tool : dependency->host_tools) {
            if (tool.name == name) {
                return Ok(luato::OpaqueHandle { .identity = rstd::addressof(tool) });
            }
        }
        return action_request_failure<luato::OpaqueHandle>(rstd::format(
            "external dependency '{}' has no host tool '{}'", dependency->alias.as_str(), name));
    }

    auto external_dependency_info(luato::OpaqueHandle dependency_handle) const
        -> BuildScriptResult<luato::Table> {
        for (const auto& target : metadata_->targets) {
            for (const auto& dependency : target.external_dependencies) {
                if (rstd::addressof(dependency) != dependency_handle.identity) continue;
                auto result   = luato::Table::make();
                auto inserted = result.set(String::make("alias"_str), dependency.alias.clone());
                if (inserted.is_err())
                    return action_request_failure<luato::Table>(
                        rstd::format("{}", inserted.unwrap_err()));
                inserted = result.set(String::make("provider"_str), dependency.provider.clone());
                if (inserted.is_err())
                    return action_request_failure<luato::Table>(
                        rstd::format("{}", inserted.unwrap_err()));
                inserted = result.set(String::make("version"_str), dependency.version.clone());
                if (inserted.is_err())
                    return action_request_failure<luato::Table>(
                        rstd::format("{}", inserted.unwrap_err()));
                inserted = result.set(String::make("identity"_str), dependency.identity.clone());
                if (inserted.is_err())
                    return action_request_failure<luato::Table>(
                        rstd::format("{}", inserted.unwrap_err()));
                auto targets = Vec<String>::with_capacity(dependency.targets.len());
                for (const auto& target : dependency.targets) targets.push(target.name.clone());
                inserted = result.set(String::make("targets"_str), lua_string_array(targets));
                if (inserted.is_err())
                    return action_request_failure<luato::Table>(
                        rstd::format("{}", inserted.unwrap_err()));
                return Ok(rstd::move(result));
            }
        }
        return action_request_failure<luato::Table>(
            "external dependency handle does not belong to this build script"_str);
    }

    auto preprocessor_environment(luato::OpaqueHandle target_handle) const
        -> BuildScriptResult<luato::Table> {
        auto target = target_index(target_handle);
        if (target.is_none()) {
            return action_request_failure<luato::Table>(
                "target handle does not belong to this build script"_str);
        }
        if (*target >= target_plan_->contexts.len()) {
            return action_request_failure<luato::Table>(
                "target handle has no resolved compile environment"_str);
        }
        auto       projection = cpp::preprocessor_projection(target_plan_->contexts[*target]);
        auto       result     = luato::Table::make();
        const auto set        = [&](String key, auto value) -> BuildScriptResult<empty> {
            auto inserted = result.set(rstd::move(key), rstd::move(value));
            if (inserted.is_err()) {
                return action_request_failure<empty>(rstd::format("{}", inserted.unwrap_err()));
            }
            return Ok(empty {});
        };
        rstd_try(set(String::make("include_directories"_str),
                     lua_string_array(projection.user_include_directories)));
        rstd_try(set(String::make("system_include_directories"_str),
                     lua_string_array(projection.system_include_directories)));
        rstd_try(set(String::make("definitions"_str), lua_string_array(projection.definitions)));
        rstd_try(
            set(String::make("undefinitions"_str), lua_string_array(projection.undefinitions)));
        rstd_try(
            set(String::make("compiler_flavor"_str),
                String::make(target_info_->environment == TargetEnvironment::Msvc ? "msvc"_str
                                                                                  : "unix"_str)));
        rstd_try(set(String::make("target"_str), target_info_->triple.clone()));
        rstd_try(set(String::make("identity"_str), rstd::move(projection.identity)));
        return Ok(rstd::move(result));
    }

    auto add_generated_source(luato::OpaqueHandle target_handle, luato::OpaqueHandle output_handle)
        -> BuildScriptResult<bool> {
        auto target = target_index(target_handle);
        if (target.is_none()) {
            return action_request_failure<bool>(
                "target handle does not belong to this build script"_str);
        }
        auto output = generated_output(output_handle);
        if (output == nullptr) {
            return action_request_failure<bool>(
                "generated output handle does not belong to this build script"_str);
        }
        if (metadata_->targets[*target].id.package != output->package.as_str()) {
            return action_request_failure<bool>(
                "generated output and target belong to different packages"_str);
        }
        return Ok(cpp::add_generated_source(metadata_->targets[*target], output->relative.clone()));
    }

    auto add_generated_include(luato::OpaqueHandle target_handle, String relative)
        -> BuildScriptResult<bool> {
        auto target = target_index(target_handle);
        if (target.is_none()) {
            return action_request_failure<bool>(
                "target handle does not belong to this build script"_str);
        }
        auto path = PathBuf::make();
        if (relative != "."_str) {
            path = rstd_try(
                normal_relative_path(rstd::move(relative), "generated include directory"_str));
        }
        auto generated = rstd_try(
            layout_->generated_package_directory(metadata_->targets[*target].id.package.as_str()));
        auto requested = generated.join(path.as_path());
        auto metadata_changed =
            cpp::add_generated_include_directory(metadata_->targets[*target], path.clone());
        auto context_changed = cpp::add_private_include_directory(target_plan_->contexts[*target],
                                                                  rstd::move(requested));
        return Ok(metadata_changed || context_changed);
    }

    auto add_generated_artifact(luato::OpaqueHandle        target_handle,
                                luato::OpaqueHandle        output_handle,
                                cpp::GeneratedArtifactRole role) -> BuildScriptResult<bool> {
        auto target = target_index(target_handle);
        if (target.is_none()) {
            return action_request_failure<bool>(
                "target handle does not belong to this build script"_str);
        }
        auto output = generated_output(output_handle);
        if (output == nullptr) {
            return action_request_failure<bool>(
                "generated output handle does not belong to this build script"_str);
        }
        if (metadata_->targets[*target].id.package != output->package.as_str()) {
            return action_request_failure<bool>(
                "generated output and target belong to different packages"_str);
        }
        auto contribution = cpp::add_generated_artifact(metadata_->targets[*target],
                                                        role,
                                                        output->relative.clone(),
                                                        output->action_identity.clone());
        auto identity     = cpp::add_generated_artifact_identity(target_plan_->contexts[*target],
                                                                 output->action_identity.as_str());
        return Ok(contribution || identity);
    }

    auto write(const luato::Table& request) -> BuildScriptResult<ToolActionOutcome> {
        auto known = Vec<String>::make();
        known.push(String::make("package"_str));
        known.push(String::make("output"_str));
        known.push(String::make("content"_str));
        known.push(String::make("inputs"_str));
        auto checked = request.reject_unknown_fields(known.as_slice());
        if (checked.is_err()) {
            return action_request_failure<ToolActionOutcome>(
                rstd::format("{}", checked.unwrap_err()));
        }
        auto package = Result<String, luato::Error>(
            Err(luato::Error::binding(String::make("lito.write.package is required"_str))));
        if (request.contains("package"_str))
            package = request.required<String>("package"_str);
        else if (default_package_.is_some())
            package = Ok(default_package_->clone());
        auto output  = request.required<String>("output"_str);
        auto content = request.required<String>("content"_str);
        auto inputs  = luato::Array::make();
        if (request.contains("inputs"_str)) {
            auto parsed = request.required<luato::Array>("inputs"_str);
            if (parsed.is_err()) {
                return action_request_failure<ToolActionOutcome>(
                    rstd::format("{}", parsed.unwrap_err()));
            }
            inputs = rstd::move(parsed).unwrap();
        }
        if (package.is_err()) {
            return action_request_failure<ToolActionOutcome>(
                rstd::format("{}", package.unwrap_err()));
        }
        if (output.is_err()) {
            return action_request_failure<ToolActionOutcome>(
                rstd::format("{}", output.unwrap_err()));
        }
        if (content.is_err()) {
            return action_request_failure<ToolActionOutcome>(
                rstd::format("{}", content.unwrap_err()));
        }
        if (! package_is_selected(packages_, package->as_str())) {
            return action_request_failure<ToolActionOutcome>(
                rstd::format("generated output package '{}' is not available to this build script",
                             package->as_str()));
        }
        auto package_root = find_package_root(*metadata_, package->as_str());
        if (package_root.is_none()) {
            return action_request_failure<ToolActionOutcome>(rstd::format(
                "generated output package '{}' has no source root", package->as_str()));
        }
        auto rendered      = rstd::move(content).unwrap();
        auto input_records = Vec<ResolvedActionInput>::with_capacity(inputs.len());
        for (usize index {}; index < inputs.len(); ++index) {
            auto resolved = rstd_try(resolve_action_input(inputs.values()[index],
                                                          package->as_str(),
                                                          *package_root,
                                                          index,
                                                          "lito.write.inputs"_str));
            auto marker   = rstd::format("@INPUT:{}@", index + usize(1));
            replace_all(
                rendered, marker.as_str(), resolved.path.as_path().to_string_lossy().as_str());
            auto xml_marker = rstd::format("@INPUT_XML:{}@", index + usize(1));
            auto escaped    = xml_text(resolved.path.as_path().to_string_lossy().as_str());
            replace_all(rendered, xml_marker.as_str(), escaped.as_str());
            input_records.push(rstd::move(resolved));
        }
        if (rendered.as_str().contains("@INPUT:"_str) ||
            rendered.as_str().contains("@INPUT_XML:"_str)) {
            return action_request_failure<ToolActionOutcome>(
                "lito.write.content contains an unresolved input marker"_str);
        }
        auto relative =
            rstd_try(normal_relative_path(rstd::move(output).unwrap(), "lito.write.output"_str));
        rstd_try(
            output_registry_->claim(package->as_str(), relative.as_path(), script_owner_.as_str()));
        auto identity_text =
            rstd::format("lito-write-action-v1\n{}\n{}\n{}\n{}",
                         package->as_str(),
                         profile_.as_str(),
                         relative.as_path(),
                         lito::crypto::sha256_hex(rendered.as_str().as_bytes()).as_str());
        for (const auto& input : input_records) {
            identity_text.push_str("\ninput:"_str);
            identity_text.push_str(input.path.as_path().to_string_lossy().as_str());
            identity_text.push_ascii(':');
            identity_text.push_str(input.digest.as_str());
        }
        if (lua_state_ != nullptr) {
            auto loaded_identities = Vec<String>::make();
            for (const auto& module : lua_state_->loaded_modules())
                loaded_identities.push(module.identity.clone());
            rstd::slice_::sort_unstable(loaded_identities.as_mut_slice().as_mut_ref());
            for (const auto& module : loaded_identities) {
                identity_text.push_str("\nmodule:"_str);
                identity_text.push_str(module.as_str());
            }
        }
        auto outputs = Vec<PathBuf>::make();
        outputs.push(rstd::move(relative));
        auto identity = lito::crypto::sha256_hex(identity_text.as_str());
        auto producer = actions_.len();
        actions_.push(RegisteredAction {
            .kind              = RegisteredActionKind::Write,
            .package           = package->clone(),
            .identity          = identity.clone(),
            .label             = String::make("lito.write"_str),
            .working_directory = PathBuf::from(*package_root),
            .inputs            = rstd::move(input_records),
            .outputs           = outputs.clone(),
            .content           = rstd::move(rendered),
        });
        return Ok(make_outcome(false, package->as_str(), outputs, identity.as_str(), producer));
    }

    auto copy(const luato::Table& request) -> BuildScriptResult<ToolActionOutcome> {
        auto known = Vec<String>::make();
        known.push(String::make("package"_str));
        known.push(String::make("input"_str));
        known.push(String::make("output"_str));
        auto checked = request.reject_unknown_fields(known.as_slice());
        if (checked.is_err()) {
            return action_request_failure<ToolActionOutcome>(
                rstd::format("{}", checked.unwrap_err()));
        }
        auto package = Result<String, luato::Error>(
            Err(luato::Error::binding(String::make("lito.copy.package is required"_str))));
        if (request.contains("package"_str))
            package = request.required<String>("package"_str);
        else if (default_package_.is_some())
            package = Ok(default_package_->clone());
        auto                output = request.required<String>("output"_str);
        const luato::Value* input {};
        for (const auto& entry : request.entries()) {
            if (entry.key == "input"_str) input = rstd::addressof(entry.value);
        }
        if (package.is_err()) {
            return action_request_failure<ToolActionOutcome>(
                rstd::format("{}", package.unwrap_err()));
        }
        if (output.is_err()) {
            return action_request_failure<ToolActionOutcome>(
                rstd::format("{}", output.unwrap_err()));
        }
        if (input == nullptr) {
            return action_request_failure<ToolActionOutcome>("lito.copy.input is required"_str);
        }
        if (! package_is_selected(packages_, package->as_str())) {
            return action_request_failure<ToolActionOutcome>(
                rstd::format("generated output package '{}' is not available to this build script",
                             package->as_str()));
        }
        auto package_root = find_package_root(*metadata_, package->as_str());
        if (package_root.is_none()) {
            return action_request_failure<ToolActionOutcome>(rstd::format(
                "generated output package '{}' has no source root", package->as_str()));
        }
        auto resolved = rstd_try(resolve_action_input(
            *input, package->as_str(), *package_root, usize {}, "lito.copy.input"_str));
        auto relative =
            rstd_try(normal_relative_path(rstd::move(output).unwrap(), "lito.copy.output"_str));
        rstd_try(
            output_registry_->claim(package->as_str(), relative.as_path(), script_owner_.as_str()));
        auto identity =
            lito::crypto::sha256_hex(rstd::format("lito-copy-action-v1\n{}\n{}\n{}:{}\n{}",
                                                  package->as_str(),
                                                  profile_.as_str(),
                                                  resolved.path.as_path(),
                                                  resolved.digest.as_str(),
                                                  relative.as_path())
                                         .as_str());
        auto inputs = Vec<ResolvedActionInput>::make();
        inputs.push(rstd::move(resolved));
        auto outputs = Vec<PathBuf>::make();
        outputs.push(rstd::move(relative));
        auto producer = actions_.len();
        actions_.push(RegisteredAction {
            .kind              = RegisteredActionKind::Copy,
            .package           = package->clone(),
            .identity          = identity.clone(),
            .label             = String::make("lito.copy"_str),
            .working_directory = PathBuf::from(*package_root),
            .inputs            = rstd::move(inputs),
            .outputs           = outputs.clone(),
        });
        return Ok(make_outcome(false, package->as_str(), outputs, identity.as_str(), producer));
    }

    auto transform(const luato::Table& request) -> BuildScriptResult<ToolActionOutcome> {
        auto known = Vec<String>::make();
        known.push(String::make("package"_str));
        known.push(String::make("kind"_str));
        known.push(String::make("input"_str));
        known.push(String::make("outputs"_str));
        auto checked = request.reject_unknown_fields(known.as_slice());
        if (checked.is_err()) {
            return action_request_failure<ToolActionOutcome>(
                rstd::format("{}", checked.unwrap_err()));
        }
        auto package = Result<String, luato::Error>(
            Err(luato::Error::binding(String::make("lito.transform.package is required"_str))));
        if (request.contains("package"_str))
            package = request.required<String>("package"_str);
        else if (default_package_.is_some())
            package = Ok(default_package_->clone());
        auto kind    = request.required<String>("kind"_str);
        auto input   = request.required<luato::OpaqueHandle>("input"_str);
        auto outputs = request.required<luato::Array>("outputs"_str);
        if (package.is_err()) {
            return action_request_failure<ToolActionOutcome>(
                rstd::format("{}", package.unwrap_err()));
        }
        if (kind.is_err()) {
            return action_request_failure<ToolActionOutcome>(rstd::format("{}", kind.unwrap_err()));
        }
        if (input.is_err()) {
            return action_request_failure<ToolActionOutcome>(
                rstd::format("{}", input.unwrap_err()));
        }
        if (outputs.is_err()) {
            return action_request_failure<ToolActionOutcome>(
                rstd::format("{}", outputs.unwrap_err()));
        }
        if (kind->as_str() != "cpp-leading-preamble"_str) {
            return action_request_failure<ToolActionOutcome>(
                rstd::format("unknown lito.transform kind '{}'", kind->as_str()));
        }
        auto source = generated_output(*input);
        if (source == nullptr || source->package != package->as_str()) {
            return action_request_failure<ToolActionOutcome>(
                "lito.transform.input is not an output owned by the selected package"_str);
        }
        auto declared_outputs =
            rstd_try(action_string_array(*outputs, "lito.transform.outputs"_str));
        if (declared_outputs.len() != usize(2)) {
            return action_request_failure<ToolActionOutcome>(
                "cpp-leading-preamble transform requires exactly two outputs"_str);
        }
        auto output_paths = Vec<PathBuf>::make();
        for (auto& output : declared_outputs) {
            auto relative =
                rstd_try(normal_relative_path(rstd::move(output), "transform output"_str));
            rstd_try(output_registry_->claim(
                package->as_str(), relative.as_path(), script_owner_.as_str()));
            output_paths.push(rstd::move(relative));
        }
        auto generated   = rstd_try(layout_->create_generated_package_directory(package->as_str()));
        auto source_path = generated.join(source->relative.as_path());
        auto identity_text = rstd::format("lito-transform-v2\n{}\n{}\n{}",
                                          kind->as_str(),
                                          source->action_identity.as_str(),
                                          profile_.as_str());
        if (lua_state_ != nullptr) {
            auto loaded_identities = Vec<String>::make();
            for (const auto& module : lua_state_->loaded_modules())
                loaded_identities.push(module.identity.clone());
            rstd::slice_::sort_unstable(loaded_identities.as_mut_slice().as_mut_ref());
            for (const auto& module : loaded_identities) {
                identity_text.push_str("\nmodule:"_str);
                identity_text.push_str(module.as_str());
            }
        }
        auto inputs = Vec<ResolvedActionInput>::make();
        inputs.push(ResolvedActionInput {
            .path     = rstd::move(source_path),
            .digest   = source->action_identity.clone(),
            .producer = Some(usize(source->producer.to_primitive())),
        });
        auto identity = lito::crypto::sha256_hex(identity_text.as_str());
        auto producer = actions_.len();
        actions_.push(RegisteredAction {
            .kind              = RegisteredActionKind::CppLeadingPreamble,
            .package           = package->clone(),
            .identity          = identity.clone(),
            .label             = String::make("lito.transform"_str),
            .working_directory = PathBuf::from(*find_package_root(*metadata_, package->as_str())),
            .inputs            = rstd::move(inputs),
            .outputs           = output_paths.clone(),
        });
        return Ok(
            make_outcome(false, package->as_str(), output_paths, identity.as_str(), producer));
    }

    auto set_lua_state(const luato::State& state) noexcept -> void {
        lua_state_ = rstd::addressof(state);
    }

    auto finalize_module_identity() -> BuildScriptResult<empty> {
        auto closure = String::make("lito-module-closure-v2"_str);
        closure.push_str("\nentry:"_str);
        closure.push_str(rstd_try(action_file_digest(script_.as_path())).as_str());
        closure.push_str("\nhost:"_str);
        closure.push_str(build_host_api_identity);
        auto loaded_identities = Vec<String>::make();
        if (lua_state_ != nullptr) {
            for (const auto& module : lua_state_->loaded_modules())
                loaded_identities.push(module.identity.clone());
        }
        rstd::slice_::sort_unstable(loaded_identities.as_mut_slice().as_mut_ref());
        for (const auto& module : loaded_identities) {
            closure.push_str("\nmodule:"_str);
            closure.push_str(module.as_str());
        }
        for (auto& action : actions_) {
            action.identity = lito::crypto::sha256_hex(
                rstd::format("{}\n{}", action.identity.as_str(), closure.as_str()).as_str());
        }
        for (auto& output : generated_outputs_) {
            if (output->producer >= actions_.len()) {
                return action_request_failure<empty>(
                    "generated output refers to an unknown producer"_str);
            }
            output->action_identity = actions_[output->producer].identity.clone();
        }
        return Ok(empty {});
    }

    auto run(const luato::Table& request) -> BuildScriptResult<ToolActionOutcome> {
        auto known = Vec<String>::make();
        known.push(String::make("tool"_str));
        known.push(String::make("package"_str));
        known.push(String::make("cwd"_str));
        known.push(String::make("args"_str));
        known.push(String::make("inputs"_str));
        known.push(String::make("outputs"_str));
        known.push(String::make("depfile"_str));
        known.push(String::make("output_cwd"_str));
        auto checked = request.reject_unknown_fields(known.as_slice());
        if (checked.is_err()) {
            return action_request_failure<ToolActionOutcome>(
                rstd::format("{}", checked.unwrap_err()));
        }
        auto handle  = request.required<luato::OpaqueHandle>("tool"_str);
        auto package = Result<String, luato::Error>(
            Err(luato::Error::binding(String::make("lito.run.package is required"_str))));
        if (request.contains("package"_str)) {
            package = request.required<String>("package"_str);
        } else if (default_package_.is_some()) {
            package = Ok(default_package_->clone());
        }
        auto cwd     = request.required<String>("cwd"_str);
        auto args    = request.required<luato::Array>("args"_str);
        auto inputs  = request.required<luato::Array>("inputs"_str);
        auto outputs = request.required<luato::Array>("outputs"_str);
        if (handle.is_err())
            return action_request_failure<ToolActionOutcome>(
                rstd::format("{}", handle.unwrap_err()));
        if (package.is_err())
            return action_request_failure<ToolActionOutcome>(
                rstd::format("{}", package.unwrap_err()));
        if (cwd.is_err())
            return action_request_failure<ToolActionOutcome>(rstd::format("{}", cwd.unwrap_err()));
        if (args.is_err())
            return action_request_failure<ToolActionOutcome>(rstd::format("{}", args.unwrap_err()));
        if (inputs.is_err())
            return action_request_failure<ToolActionOutcome>(
                rstd::format("{}", inputs.unwrap_err()));
        if (outputs.is_err())
            return action_request_failure<ToolActionOutcome>(
                rstd::format("{}", outputs.unwrap_err()));
        auto                              resolved_tool   = tools_->from_identity(handle->identity);
        const cpp::ExternalHostToolUsage* external_tool   = nullptr;
        auto                              tool_package    = String::make();
        auto                              tool_alias      = String::make();
        auto                              tool_identity   = String::make();
        auto                              tool_executable = PathBuf::make();
        if (resolved_tool.is_some()) {
            tool_package    = (**resolved_tool).package.clone();
            tool_alias      = (**resolved_tool).alias.clone();
            tool_identity   = (**resolved_tool).receipt_identity.clone();
            tool_executable = (**resolved_tool).executable.clone();
        } else {
            for (const auto& target : metadata_->targets) {
                for (const auto& dependency : target.external_dependencies) {
                    for (const auto& candidate : dependency.host_tools) {
                        if (rstd::addressof(candidate) != handle->identity) continue;
                        external_tool = rstd::addressof(candidate);
                        tool_package  = target.id.package.clone();
                        tool_alias    = rstd::format(
                            "{}:{}", dependency.alias.as_str(), candidate.name.as_str());
                        tool_identity   = candidate.identity.clone();
                        tool_executable = candidate.executable.clone();
                    }
                }
            }
        }
        if (resolved_tool.is_none() && external_tool == nullptr) {
            return action_request_failure<ToolActionOutcome>(
                "tool handle does not belong to this build script"_str);
        }
        if (tool_package != package->as_str()) {
            return action_request_failure<ToolActionOutcome>(
                rstd::format("build-tool '{}' belongs to package '{}', not '{}'",
                             tool_alias.as_str(),
                             tool_package.as_str(),
                             package->as_str()));
        }
        auto package_root = find_package_root(*metadata_, package->as_str());
        if (package_root.is_none()) {
            return action_request_failure<ToolActionOutcome>(
                rstd::format("build-tool action package '{}' is not selected", package->as_str()));
        }
        auto cwd_text     = rstd::move(cwd).unwrap();
        auto cwd_relative = PathBuf::make();
        if (cwd_text != "."_str) {
            cwd_relative =
                rstd_try(normal_relative_path(rstd::move(cwd_text), "build-tool action cwd"_str));
        }
        auto cwd_requested     = PathBuf::from(*package_root).join(cwd_relative.as_path());
        auto working_directory = rstd::fs::canonicalize(cwd_requested.as_path());
        if (working_directory.is_err()) {
            return script_io_failure<ToolActionOutcome>("resolve build-tool action cwd"_str,
                                                        cwd_requested.as_path(),
                                                        rstd::move(working_directory).unwrap_err());
        }
        if (working_directory->as_path().strip_prefix(*package_root).is_none()) {
            return action_request_failure<ToolActionOutcome>(
                "build-tool action cwd escapes package root"_str);
        }
        auto arguments        = rstd_try(action_string_array(*args, "lito.run.args"_str));
        auto declared_outputs = rstd_try(action_string_array(*outputs, "lito.run.outputs"_str));
        if (arguments.is_empty() || inputs->is_empty() || declared_outputs.is_empty()) {
            return action_request_failure<ToolActionOutcome>(
                "build-tool action requires args, inputs, and outputs"_str);
        }
        auto input_records = Vec<ResolvedActionInput>::make();
        for (usize input_index {}; input_index < inputs->len(); ++input_index) {
            auto resolved = rstd_try(resolve_action_input(inputs->values()[input_index],
                                                          package->as_str(),
                                                          working_directory->as_path(),
                                                          input_index,
                                                          "lito.run.inputs"_str));
            input_records.push(rstd::move(resolved));
        }
        auto output_paths = Vec<PathBuf>::make();
        for (auto& output : declared_outputs) {
            auto relative =
                rstd_try(normal_relative_path(rstd::move(output), "build-tool action output"_str));
            for (const auto& existing : output_paths) {
                if (existing.as_path() == relative.as_path()) {
                    return action_failure<ToolActionOutcome>(BuildToolActionError::InvalidOutput(
                        relative.clone(), String::make("path is declared more than once"_str)));
                }
            }
            rstd_try(output_registry_->claim(
                package->as_str(), relative.as_path(), script_owner_.as_str()));
            output_paths.push(rstd::move(relative));
        }
        auto output_working_directory = Option<usize> {};
        if (request.contains("output_cwd"_str)) {
            auto selected = request.required<i64>("output_cwd"_str);
            if (selected.is_err()) {
                return action_request_failure<ToolActionOutcome>(
                    rstd::format("{}", selected.unwrap_err()));
            }
            auto index = usize(static_cast<size_t>(selected->to_primitive()));
            if (*selected < i64(1) || index > output_paths.len()) {
                return action_request_failure<ToolActionOutcome>(
                    "lito.run.output_cwd must identify a declared output"_str);
            }
            output_working_directory = Some(index - usize(1));
            auto selected_parent     = output_paths[index - usize(1)].as_path().parent().unwrap();
            for (const auto& candidate : output_paths) {
                if (candidate.as_path().parent().unwrap() != selected_parent) {
                    return action_request_failure<ToolActionOutcome>(
                        "lito.run.output_cwd requires all outputs to share one directory"_str);
                }
            }
        }
        auto depfile_output_index = Option<usize> {};
        auto depfile_roots        = Vec<PathBuf>::make();
        auto generated_root =
            rstd_try(layout_->create_generated_package_directory(package->as_str()));
        if (request.contains("depfile"_str)) {
            auto depfile = request.required<luato::Table>("depfile"_str);
            if (depfile.is_err()) {
                return action_request_failure<ToolActionOutcome>(
                    rstd::format("{}", depfile.unwrap_err()));
            }
            auto depfile_fields = Vec<String>::make();
            depfile_fields.push(String::make("output"_str));
            depfile_fields.push(String::make("roots"_str));
            auto depfile_checked = depfile->reject_unknown_fields(depfile_fields.as_slice());
            if (depfile_checked.is_err()) {
                return action_request_failure<ToolActionOutcome>(
                    rstd::format("{}", depfile_checked.unwrap_err()));
            }
            auto output = depfile->required<i64>("output"_str);
            auto roots  = depfile->required<luato::Array>("roots"_str);
            if (output.is_err()) {
                return action_request_failure<ToolActionOutcome>(
                    rstd::format("{}", output.unwrap_err()));
            }
            if (roots.is_err()) {
                return action_request_failure<ToolActionOutcome>(
                    rstd::format("{}", roots.unwrap_err()));
            }
            auto output_index = usize(static_cast<size_t>(output->to_primitive()));
            if (*output < i64(1) || output_index > output_paths.len()) {
                return action_request_failure<ToolActionOutcome>(
                    "lito.run.depfile.output must identify a declared output"_str);
            }
            depfile_output_index = Some(output_index - usize(1));
            for (usize index {}; index < roots->len(); ++index) {
                const auto& value = roots->values()[index];
                if (! value.is_String()) {
                    return action_request_failure<ToolActionOutcome>(rstd::format(
                        "lito.run.depfile.roots[{}] must be a string", index + usize(1)));
                }
                auto path = PathBuf::from(value.as_String().value.clone());
                if (! path.as_path().is_absolute()) {
                    path = PathBuf::from(working_directory->as_path()).join(path.as_path());
                }
                auto canonical = rstd::fs::canonicalize(path.as_path());
                if (canonical.is_err()) {
                    if (path.as_path().strip_prefix(generated_root.as_path()).is_some()) continue;
                    return script_io_failure<ToolActionOutcome>(
                        "resolve build-tool depfile root"_str,
                        path.as_path(),
                        rstd::move(canonical).unwrap_err());
                }
                auto metadata = rstd::fs::symlink_metadata(canonical->as_path());
                if (metadata.is_err() || ! metadata->is_dir()) {
                    return action_failure<ToolActionOutcome>(BuildToolActionError::InvalidInput(
                        canonical->clone(), String::make("depfile root is not a directory"_str)));
                }
                depfile_roots.push(rstd::move(canonical).unwrap());
            }
        }
        depfile_roots.push(PathBuf::from(*package_root));
        depfile_roots.push(generated_root.clone());
        auto script_digest = rstd_try(action_file_digest(script_.as_path()));
        auto identity_text = rstd::format("build-tool-action-v1\n{}\n{}\n{}\n{}\n{}",
                                          package->as_str(),
                                          profile_.as_str(),
                                          tool_identity.as_str(),
                                          cwd_relative.as_path(),
                                          script_digest.as_str());
        identity_text.push_str("\nmodule-resolver=lito-restricted-v1"_str);
        if (lua_state_ != nullptr) {
            auto loaded_identities = Vec<String>::make();
            for (const auto& module : lua_state_->loaded_modules()) {
                loaded_identities.push(module.identity.clone());
            }
            rstd::slice_::sort_unstable(loaded_identities.as_mut_slice().as_mut_ref());
            for (const auto& module : loaded_identities) {
                identity_text.push_str("\nmodule:"_str);
                identity_text.push_str(module.as_str());
            }
        }
        for (const auto& argument : arguments) {
            identity_text.push_ascii('\n');
            identity_text.push_str(argument.as_str());
        }
        for (usize index {}; index < input_records.len(); ++index) {
            identity_text.push_ascii('\n');
            identity_text.push_str(input_records[index].path.as_path().to_string_lossy().as_str());
            identity_text.push_ascii(':');
            identity_text.push_str(input_records[index].digest.as_str());
        }
        for (const auto& output : output_paths) {
            identity_text.push_str("\noutput:"_str);
            identity_text.push_str(output.as_path().to_string_lossy().as_str());
        }
        if (depfile_output_index.is_some()) {
            identity_text.push_str("\ndepfile:"_str);
            identity_text.push_str(rstd::format("{}", *depfile_output_index + usize(1)).as_str());
            for (const auto& root : depfile_roots) {
                identity_text.push_str("\ndepfile-root:"_str);
                identity_text.push_str(root.as_path().to_string_lossy().as_str());
            }
        }
        if (output_working_directory.is_some()) {
            identity_text.push_str("\noutput-cwd:"_str);
            identity_text.push_str(
                rstd::format("{}", *output_working_directory + usize(1)).as_str());
        }
        identity_text.push_ascii('\n');
        identity_text.push_str(environment_->child_path().to_string_lossy().as_str());
        auto identity = lito::crypto::sha256_hex(identity_text.as_str());
        auto producer = actions_.len();
        actions_.push(RegisteredAction {
            .kind                     = RegisteredActionKind::Process,
            .package                  = package->clone(),
            .identity                 = identity.clone(),
            .label                    = rstd::move(tool_alias),
            .working_directory        = rstd::move(working_directory).unwrap(),
            .executable               = rstd::move(tool_executable),
            .arguments                = rstd::move(arguments),
            .inputs                   = rstd::move(input_records),
            .outputs                  = output_paths.clone(),
            .output_working_directory = output_working_directory,
            .depfile_output           = depfile_output_index,
            .depfile_roots            = rstd::move(depfile_roots),
        });
        return Ok(
            make_outcome(false, package->as_str(), output_paths, identity.as_str(), producer));
    }

    auto execute(usize jobs) -> BuildScriptResult<empty> {
        if (jobs == usize {}) {
            return action_request_failure<empty>(
                "generated action jobs must be greater than zero"_str);
        }
        if (actions_.is_empty()) return Ok(empty {});
        enum class Status
        {
            Pending,
            Ready,
            Running,
            Complete,
        };
        auto prerequisites = Vec<Vec<usize>>::with_capacity(actions_.len());
        auto dependents    = Vec<Vec<usize>>::with_capacity(actions_.len());
        for (usize index {}; index < actions_.len(); ++index) {
            prerequisites.emplace_back();
            dependents.emplace_back();
        }
        for (usize index {}; index < actions_.len(); ++index) {
            for (const auto& input : actions_[index].inputs) {
                if (input.producer.is_none()) continue;
                if (*input.producer >= index) {
                    return action_request_failure<empty>(
                        "generated action graph contains a forward or cyclic dependency"_str);
                }
                auto repeated = false;
                for (auto dependency : prerequisites[index]) {
                    if (dependency == *input.producer) repeated = true;
                }
                if (repeated) continue;
                prerequisites[index].emplace_back(*input.producer);
                dependents[*input.producer].emplace_back(index);
            }
        }

        auto worker_count = jobs < actions_.len() ? jobs : actions_.len();
        auto pool         = rstd::thread::ThreadPoolBuilder::make()
                                .worker_count(worker_count)
                                .thread_name(String::make("lito-generate"_str))
                                .build();
        if (pool.is_err()) {
            return script_io_failure<empty>("create generated action worker pool"_str,
                                            script_.as_path(),
                                            rstd::move(pool).unwrap_err_unchecked());
        }
        auto workers  = rstd::move(pool).unwrap_unchecked();
        auto task_set = rstd::thread::BlockingTaskSet<GeneratedActionWorkerResult>::make(
            workers.handle(), worker_count);
        if (task_set.is_err()) {
            rstd::move(workers).join();
            return script_io_failure<empty>("create generated action task set"_str,
                                            script_.as_path(),
                                            rstd::move(task_set).unwrap_err_unchecked());
        }
        auto tasks     = rstd::move(task_set).unwrap_unchecked();
        auto status    = Vec<Status>::with_capacity(actions_.len());
        auto remaining = Vec<usize>::with_capacity(actions_.len());
        for (const auto& values : prerequisites) {
            remaining.push(values.len());
            status.push(values.is_empty() ? Status::Ready : Status::Pending);
        }

        auto completed = usize {};
        auto in_flight = usize {};
        while (completed < actions_.len()) {
            while (in_flight < worker_count) {
                auto selected = Option<usize> {};
                for (usize index {}; index < status.len(); ++index) {
                    if (status[index] == Status::Ready) {
                        selected = Some(index);
                        break;
                    }
                }
                if (selected.is_none()) break;
                auto index    = *selected;
                status[index] = Status::Running;
                auto session  = this;
                auto submitted =
                    tasks.try_submit([session, index]() -> GeneratedActionWorkerResult {
                        return GeneratedActionWorkerResult {
                            .action  = index,
                            .outcome = session->execute_action(session->actions_[index]),
                        };
                    });
                if (submitted.is_err()) {
                    tasks.cancel_pending();
                    tasks.close();
                    rstd::move(workers).join();
                    return action_request_failure<empty>(
                        "cannot submit generated action to worker pool"_str);
                }
                ++in_flight;
            }
            if (in_flight == usize {}) {
                tasks.cancel_pending();
                tasks.close();
                rstd::move(workers).join();
                return action_request_failure<empty>(
                    "generated action graph has no ready action"_str);
            }
            auto received = tasks.recv();
            if (received.is_none()) {
                tasks.cancel_pending();
                tasks.close();
                rstd::move(workers).join();
                return action_request_failure<empty>(
                    "generated action worker pool closed before completion"_str);
            }
            auto task = rstd::move(received).unwrap_unchecked();
            if (task.is_cancelled()) {
                tasks.cancel_pending();
                tasks.close();
                rstd::move(workers).join();
                return action_request_failure<empty>("generated action was cancelled"_str);
            }
            auto value = rstd::move(task).into_value();
            if (value.is_none()) {
                tasks.cancel_pending();
                tasks.close();
                rstd::move(workers).join();
                return action_request_failure<empty>(
                    "generated action completed without a result"_str);
            }
            auto result = rstd::move(value).unwrap_unchecked();
            --in_flight;
            if (result.outcome.is_err()) {
                tasks.cancel_pending();
                tasks.close();
                rstd::move(workers).join();
                return Err(rstd::move(result.outcome).unwrap_err());
            }
            if (result.action >= status.len() || status[result.action] != Status::Running) {
                tasks.cancel_pending();
                tasks.close();
                rstd::move(workers).join();
                return action_request_failure<empty>(
                    "generated action completion does not match a running action"_str);
            }
            status[result.action] = Status::Complete;
            ++completed;
            for (auto dependent : dependents[result.action]) {
                if (remaining[dependent] == usize {}) {
                    tasks.cancel_pending();
                    tasks.close();
                    rstd::move(workers).join();
                    return action_request_failure<empty>(
                        "generated action prerequisite count underflow"_str);
                }
                --remaining[dependent];
                if (remaining[dependent] == usize {}) status[dependent] = Status::Ready;
            }
        }
        tasks.close();
        rstd::move(workers).join();
        return Ok(empty {});
    }

private:
    auto current_action_dependencies(const RegisteredAction& action) const
        -> BuildScriptResult<Vec<ActionDependency>> {
        auto result = Vec<ActionDependency>::with_capacity(action.inputs.len());
        for (const auto& input : action.inputs) {
            auto canonical = rstd::fs::canonicalize(input.path.as_path());
            if (canonical.is_err()) {
                return script_io_failure<Vec<ActionDependency>>(
                    "resolve generated action input"_str,
                    input.path.as_path(),
                    rstd::move(canonical).unwrap_err());
            }
            auto metadata = rstd::fs::symlink_metadata(canonical->as_path());
            if (metadata.is_err() || metadata->is_symlink() || ! metadata->is_file()) {
                return action_failure<Vec<ActionDependency>>(BuildToolActionError::InvalidInput(
                    canonical->clone(), String::make("path is not a regular file"_str)));
            }
            result.push(ActionDependency {
                .path   = rstd::move(canonical).unwrap(),
                .digest = rstd_try(action_file_digest(input.path.as_path())),
            });
        }
        return Ok(rstd::move(result));
    }

    static void merge_action_dependencies(Vec<ActionDependency>& destination,
                                          Vec<ActionDependency>  source) {
        for (auto& candidate : source) {
            auto duplicate = false;
            for (const auto& dependency : destination) {
                if (dependency.path.as_path() == candidate.path.as_path()) {
                    duplicate = true;
                    break;
                }
            }
            if (! duplicate) destination.push(rstd::move(candidate));
        }
        const auto order = [](const ActionDependency& left, const ActionDependency& right) {
            return left.path.as_path().to_string_lossy() < right.path.as_path().to_string_lossy();
        };
        rstd::slice_::sort_unstable_by(destination.as_mut_slice().as_mut_ref(), order);
    }

    auto render_process_invocation(const RegisteredAction& action,
                                   ref<rstd::path::Path>   staging) const
        -> BuildScriptResult<Vec<String>> {
        auto invocation = Vec<String>::make();
        invocation.push(action.executable.as_path().to_string_lossy());
        auto replaced_output = false;
        for (const auto& original : action.arguments) {
            auto argument = original.clone();
            for (usize output_index {}; output_index < action.outputs.len(); ++output_index) {
                auto name_marker = rstd::format("@OUTPUT_NAME:{}@", output_index + usize(1));
                auto name        = action.outputs[output_index].as_path().file_name();
                if (name.is_none()) {
                    return action_request_failure<Vec<String>>(
                        "build-tool action output has no file name"_str);
                }
                if (replace_all(argument, name_marker.as_str(), name->to_string_lossy().as_str()) !=
                    usize {}) {
                    replaced_output = true;
                }
                auto marker = rstd::format("@OUTPUT:{}@", output_index + usize(1));
                auto output = PathBuf::from(staging).join(action.outputs[output_index].as_path());
                if (replace_all(argument,
                                marker.as_str(),
                                output.as_path().to_string_lossy().as_str()) != usize {}) {
                    replaced_output = true;
                }
            }
            if (argument.as_str().contains("@OUTPUT@"_str)) {
                if (action.outputs.len() != usize(1)) {
                    return action_request_failure<Vec<String>>(
                        "build-tool action with multiple outputs must use numbered output markers"_str);
                }
                auto output = PathBuf::from(staging).join(action.outputs[usize {}].as_path());
                auto count  = replace_all(
                    argument, "@OUTPUT@"_str, output.as_path().to_string_lossy().as_str());
                if (count != usize(1)) {
                    return action_request_failure<Vec<String>>(
                        "build-tool action may use '@OUTPUT@' only once"_str);
                }
                replaced_output = true;
            }
            if (argument.as_str().contains("@OUTPUT_NAME@"_str)) {
                if (action.outputs.len() != usize(1)) {
                    return action_request_failure<Vec<String>>(
                        "build-tool action with multiple outputs must use numbered output name markers"_str);
                }
                auto name = action.outputs[usize {}].as_path().file_name();
                if (name.is_none()) {
                    return action_request_failure<Vec<String>>(
                        "build-tool action output has no file name"_str);
                }
                replace_all(argument, "@OUTPUT_NAME@"_str, name->to_string_lossy().as_str());
                replaced_output = true;
            }
            for (usize input_index {}; input_index < action.inputs.len(); ++input_index) {
                auto marker = rstd::format("@INPUT:{}@", input_index + usize(1));
                replace_all(argument,
                            marker.as_str(),
                            action.inputs[input_index].path.as_path().to_string_lossy().as_str());
            }
            if (argument.as_str().contains("@INPUT:"_str) ||
                argument.as_str().contains("@OUTPUT:"_str) ||
                argument.as_str().contains("@OUTPUT_NAME:"_str)) {
                return action_request_failure<Vec<String>>(
                    "build-tool action contains an unresolved input or output marker"_str);
            }
            invocation.push(rstd::move(argument));
        }
        if (! replaced_output) {
            return action_request_failure<Vec<String>>(
                "build-tool action args must contain '@OUTPUT@'"_str);
        }
        return Ok(rstd::move(invocation));
    }

    auto write_action_staging(const RegisteredAction& action, ref<rstd::path::Path> staging) const
        -> BuildScriptResult<empty> {
        auto output  = PathBuf::from(staging).join(action.outputs[usize {}].as_path());
        auto written = rstd::fs::write_atomic(output.as_path(), action.content.as_str().as_bytes());
        if (written.is_err()) {
            return action_publication_failure<empty>(
                "stage generated file"_str, output.as_path(), rstd::move(written).unwrap_err());
        }
        return Ok(empty {});
    }

    auto copy_action_staging(const RegisteredAction& action, ref<rstd::path::Path> staging) const
        -> BuildScriptResult<empty> {
        auto contents = rstd::fs::read(action.inputs[usize {}].path.as_path());
        if (contents.is_err()) {
            return script_io_failure<empty>("read copy input"_str,
                                            action.inputs[usize {}].path.as_path(),
                                            rstd::move(contents).unwrap_err());
        }
        auto output  = PathBuf::from(staging).join(action.outputs[usize {}].as_path());
        auto written = rstd::fs::write_atomic(output.as_path(), contents->as_slice());
        if (written.is_err()) {
            return action_publication_failure<empty>(
                "stage copied file"_str, output.as_path(), rstd::move(written).unwrap_err());
        }
        return Ok(empty {});
    }

    auto transform_action_staging(const RegisteredAction& action,
                                  ref<rstd::path::Path> staging) const -> BuildScriptResult<empty> {
        auto contents = rstd::fs::read_to_string(action.inputs[usize {}].path.as_path());
        if (contents.is_err()) {
            return script_io_failure<empty>("read transform input"_str,
                                            action.inputs[usize {}].path.as_path(),
                                            rstd::move(contents).unwrap_err());
        }
        auto preamble       = String::make();
        auto implementation = String::make();
        auto in_preamble    = true;
        auto has_code       = false;
        auto text           = contents->as_str();
        auto cursor         = usize {};
        while (cursor < text.len()) {
            auto end = cursor;
            while (end < text.len() && text.as_bytes()[end] != u8('\n')) ++end;
            auto line  = text.get(cursor, end).unwrap();
            auto blank = true;
            for (auto byte : line.as_bytes()) {
                if (byte == u8(' ') || byte == u8('\t') || byte == u8('\r')) continue;
                blank = false;
                break;
            }
            auto preamble_line = blank || line.starts_with("#include"_str) ||
                                 line.starts_with("/*"_str) || line.starts_with("*"_str) ||
                                 line.starts_with("//"_str);
            if (in_preamble && ! preamble_line) in_preamble = false;
            auto& destination = in_preamble ? preamble : implementation;
            destination.push_str(line);
            destination.push_ascii('\n');
            if (! in_preamble && ! blank) has_code = true;
            cursor = end < text.len() ? end + usize(1) : end;
        }
        if (preamble.is_empty() || ! has_code) {
            return action_failure<empty>(BuildToolActionError::InvalidOutput(
                action.inputs[usize {}].path.clone(),
                String::make("generated C++ file has no separable leading preamble"_str)));
        }
        ref<str> values[] = { preamble.as_str(), implementation.as_str() };
        for (usize index {}; index < action.outputs.len(); ++index) {
            auto output = PathBuf::from(staging).join(action.outputs[index].as_path());
            auto written =
                rstd::fs::write_atomic(output.as_path(), values[index.to_primitive()].as_bytes());
            if (written.is_err()) {
                return action_publication_failure<empty>("stage transform output"_str,
                                                         output.as_path(),
                                                         rstd::move(written).unwrap_err());
            }
        }
        return Ok(empty {});
    }

    auto execute_action(const RegisteredAction& action) const -> BuildScriptResult<empty> {
        auto generated_root =
            rstd_try(layout_->create_generated_package_directory(action.package.as_str()));
        auto action_root = layout_->build_tool_action_root().join(
            PathBuf::from(action.identity.clone()).as_path());
        auto receipt = action_root.join(PathBuf::from("receipt.json"_str).as_path());
        auto created = rstd::fs::create_dir_all(action_root.as_path());
        if (created.is_err()) {
            return script_io_failure<empty>("create build-tool action directory"_str,
                                            action_root.as_path(),
                                            rstd::move(created).unwrap_err());
        }
        auto lock_path = action_root.join(PathBuf::from("lock"_str).as_path());
        auto opened    = rstd::fs::OpenOptions::make().read(true).write(true).create(true).open(
            lock_path.as_path());
        if (opened.is_err()) {
            return script_io_failure<empty>("open build-tool action lock"_str,
                                            lock_path.as_path(),
                                            rstd::move(opened).unwrap_err());
        }
        auto locked = rstd::fs::FileLock::acquire(rstd::move(opened).unwrap(),
                                                  rstd::fs::FileLockMode::Exclusive);
        if (locked.is_err()) {
            return script_io_failure<empty>(
                "lock build-tool action"_str, lock_path.as_path(), rstd::move(locked).unwrap_err());
        }
        auto reusable = rstd_try(
            action_receipt_matches(receipt.as_path(),
                                   action.identity.as_str(),
                                   action.outputs,
                                   ! action.inputs.is_empty() || action.depfile_output.is_some(),
                                   generated_root.as_path()));
        if (reusable) {
            emit(BuildEventKind::BuildToolRunReuse,
                 action.label.as_str(),
                 action.outputs.len() == usize(1)
                     ? generated_root.join(action.outputs[usize {}].as_path()).as_path()
                     : generated_root.as_path());
            return Ok(empty {});
        }

        auto staging        = action_root.join(PathBuf::from("staging"_str).as_path());
        auto staging_exists = rstd::fs::exists(staging.as_path());
        if (staging_exists.is_err()) {
            return script_io_failure<empty>("inspect build-tool action staging"_str,
                                            staging.as_path(),
                                            rstd::move(staging_exists).unwrap_err());
        }
        if (*staging_exists) {
            auto removed = rstd::fs::remove_dir_all(staging.as_path());
            if (removed.is_err()) {
                return script_io_failure<empty>("clear build-tool action staging"_str,
                                                staging.as_path(),
                                                rstd::move(removed).unwrap_err());
            }
        }
        created = rstd::fs::create_dir_all(staging.as_path());
        if (created.is_err()) {
            return script_io_failure<empty>("create build-tool output staging"_str,
                                            staging.as_path(),
                                            rstd::move(created).unwrap_err());
        }
        for (const auto& output : action.outputs) {
            auto staged_output = staging.join(output.as_path());
            auto parent        = staged_output.as_path().parent().unwrap();
            created            = rstd::fs::create_dir_all(parent);
            if (created.is_err()) {
                return script_io_failure<empty>("create build-tool output parent"_str,
                                                parent,
                                                rstd::move(created).unwrap_err());
            }
        }

        if (action.kind == RegisteredActionKind::Process) {
            auto invocation = rstd_try(render_process_invocation(action, staging.as_path()));
            auto process_working_directory = action.working_directory.clone();
            if (action.output_working_directory.is_some()) {
                auto output =
                    staging.join(action.outputs[*action.output_working_directory].as_path());
                process_working_directory = PathBuf::from(output.as_path().parent().unwrap());
            }
            auto executed =
                run_command(invocation, *environment_, Some(process_working_directory.as_path()));
            if (executed.is_err()) {
                return action_failure<empty>(BuildToolActionError::Process(
                    action.label.clone(), rstd::move(executed).unwrap_err()));
            }
            if (executed->exit_code != i32 {}) {
                return action_failure<empty>(
                    BuildToolActionError::Execution(action.label.clone(),
                                                    executed->exit_code,
                                                    rstd::move(executed->standard_output),
                                                    rstd::move(executed->standard_error)));
            }
        } else if (action.kind == RegisteredActionKind::Write) {
            rstd_try(write_action_staging(action, staging.as_path()));
        } else if (action.kind == RegisteredActionKind::Copy) {
            rstd_try(copy_action_staging(action, staging.as_path()));
        } else {
            rstd_try(transform_action_staging(action, staging.as_path()));
        }

        auto actual_outputs = Vec<PathBuf>::make();
        rstd_try(collect_action_outputs(staging.as_path(), staging.as_path(), actual_outputs));
        auto       expected_outputs = action.outputs.clone();
        const auto order            = [](const PathBuf& left, const PathBuf& right) {
            return left.as_path().to_string_lossy() < right.as_path().to_string_lossy();
        };
        rstd::slice_::sort_unstable_by(actual_outputs.as_mut_slice().as_mut_ref(), order);
        rstd::slice_::sort_unstable_by(expected_outputs.as_mut_slice().as_mut_ref(), order);
        auto outputs_match = actual_outputs.len() == expected_outputs.len();
        for (usize index {}; outputs_match && index < actual_outputs.len(); ++index) {
            outputs_match = actual_outputs[index].as_path() == expected_outputs[index].as_path();
        }
        if (! outputs_match) {
            return action_failure<empty>(BuildToolActionError::InvalidOutput(
                staging.clone(), String::make("produced files do not match the declared set"_str)));
        }

        auto dependencies = rstd_try(current_action_dependencies(action));
        if (action.depfile_output.is_some()) {
            auto staged_depfile = staging.join(action.outputs[*action.depfile_output].as_path());
            auto direct_inputs  = Vec<PathBuf>::with_capacity(action.inputs.len());
            for (const auto& input : action.inputs) direct_inputs.push(input.path.clone());
            auto depfile = rstd_try(load_action_dependencies(staged_depfile.as_path(),
                                                             action.working_directory.as_path(),
                                                             action.depfile_roots,
                                                             direct_inputs));
            merge_action_dependencies(dependencies, rstd::move(depfile));
        }

        auto digests = Vec<String>::make();
        for (const auto& output : action.outputs) {
            auto staged_output = staging.join(output.as_path());
            auto bytes         = rstd::fs::read(staged_output.as_path());
            if (bytes.is_err()) {
                return script_io_failure<empty>("read staged build-tool output"_str,
                                                staged_output.as_path(),
                                                rstd::move(bytes).unwrap_err());
            }
            auto final  = generated_root.join(output.as_path());
            auto parent = final.as_path().parent().unwrap();
            created     = rstd::fs::create_dir_all(parent);
            if (created.is_err()) {
                return script_io_failure<empty>("create build-tool output parent"_str,
                                                parent,
                                                rstd::move(created).unwrap_err());
            }
            auto written = rstd::fs::write_atomic_if_changed(final.as_path(), bytes->as_slice());
            if (written.is_err()) {
                return action_publication_failure<empty>("publish build-tool output"_str,
                                                         final.as_path(),
                                                         rstd::move(written).unwrap_err());
            }
            digests.push(lito::crypto::sha256_hex(bytes->as_slice()));
        }
        auto receipt_text =
            action_receipt_text(action.identity.as_str(), action.outputs, digests, dependencies);
        auto receipt_written =
            rstd::fs::write_atomic(receipt.as_path(), receipt_text.as_str().as_bytes());
        if (receipt_written.is_err()) {
            return action_receipt_failure<empty>("write build-tool action receipt"_str,
                                                 receipt.as_path(),
                                                 rstd::move(receipt_written).unwrap_err());
        }
        emit(BuildEventKind::BuildToolRun,
             action.label.as_str(),
             action.outputs.len() == usize(1)
                 ? generated_root.join(action.outputs[usize {}].as_path()).as_path()
                 : generated_root.as_path());
        return Ok(empty {});
    }

    auto resolve_action_input(const luato::Value&   input,
                              ref<str>              package,
                              ref<rstd::path::Path> working_directory,
                              usize                 index,
                              ref<str> context) const -> BuildScriptResult<ResolvedActionInput> {
        auto canonical = PathBuf::make();
        auto producer  = Option<usize> {};
        auto digest    = String::make();
        if (input.is_String()) {
            auto relative  = rstd_try(normal_relative_path(input.as_String().value.clone(),
                                                           "generated action input"_str));
            auto requested = PathBuf::from(working_directory).join(relative.as_path());
            auto resolved  = rstd::fs::canonicalize(requested.as_path());
            if (resolved.is_err()) {
                return script_io_failure<ResolvedActionInput>("resolve generated action input"_str,
                                                              requested.as_path(),
                                                              rstd::move(resolved).unwrap_err());
            }
            auto package_root = find_package_root(*metadata_, package);
            if (package_root.is_none() ||
                resolved->as_path().strip_prefix(*package_root).is_none()) {
                return action_failure<ResolvedActionInput>(BuildToolActionError::InvalidInput(
                    requested.clone(), String::make("path escapes package root"_str)));
            }
            canonical = rstd::move(resolved).unwrap();
        } else if (input.is_Opaque()) {
            auto output =
                generated_output(luato::OpaqueHandle { .identity = input.as_Opaque().value });
            if (output == nullptr || output->package != package) {
                return action_request_failure<ResolvedActionInput>(
                    rstd::format("{}[{}] is not a generated output owned by package '{}'",
                                 context,
                                 index + usize(1),
                                 package));
            }
            auto generated = rstd_try(layout_->generated_package_directory(package));
            canonical      = generated.join(output->relative.as_path());
            producer       = Some(usize(output->producer.to_primitive()));
            digest =
                rstd::format("{}:{}", output->action_identity.as_str(), output->relative.as_path());
        } else {
            return action_request_failure<ResolvedActionInput>(
                rstd::format("{}[{}] must be a source path or generated output handle",
                             context,
                             index + usize(1)));
        }
        if (producer.is_some()) {
            return Ok(ResolvedActionInput {
                .path     = rstd::move(canonical),
                .digest   = rstd::move(digest),
                .producer = producer,
            });
        }
        auto metadata = rstd::fs::symlink_metadata(canonical.as_path());
        if (metadata.is_err() || metadata->is_symlink() || ! metadata->is_file()) {
            return action_failure<ResolvedActionInput>(BuildToolActionError::InvalidInput(
                canonical.clone(), String::make("path is not a regular file"_str)));
        }
        digest = rstd_try(action_file_digest(canonical.as_path()));
        return Ok(ResolvedActionInput {
            .path     = rstd::move(canonical),
            .digest   = rstd::move(digest),
            .producer = None(),
        });
    }

    auto target_index(luato::OpaqueHandle handle) const noexcept -> Option<cpp::TargetId> {
        for (auto index = cpp::TargetId {}; index < metadata_->targets.len(); ++index) {
            if (rstd::addressof(metadata_->targets[index]) == handle.identity) return Some(index);
        }
        return None();
    }

    auto generated_output(luato::OpaqueHandle handle) const noexcept
        -> const GeneratedActionOutput* {
        for (const auto& output : generated_outputs_) {
            if (rstd::addressof(*output) == handle.identity) return rstd::addressof(*output);
        }
        return nullptr;
    }

    auto make_outcome(bool                changed,
                      ref<str>            package,
                      const Vec<PathBuf>& outputs,
                      ref<str>            identity,
                      usize               producer) -> ToolActionOutcome {
        auto handles = Vec<luato::OpaqueHandle>::with_capacity(outputs.len());
        for (const auto& output : outputs) {
            auto owned = Box<GeneratedActionOutput>::make(GeneratedActionOutput {
                .package         = String::make(package),
                .relative        = output.clone(),
                .action_identity = String::make(identity),
                .producer        = producer,
            });
            handles.push(luato::OpaqueHandle { .identity = rstd::addressof(*owned) });
            generated_outputs_.push(rstd::move(owned));
        }
        return ToolActionOutcome {
            .changed = changed,
            .outputs = rstd::move(handles),
        };
    }

    void emit(BuildEventKind kind, ref<str> target, ref<rstd::path::Path> path) const noexcept {
        if (observer_->is_some() && (*observer_)->notify != nullptr)
            (*observer_)->notify((*observer_)->context, BuildEvent { kind, target, path });
    }

    cpp::PackageMetadata*             metadata_ {};
    cpp::ResolvedNativeTargetPlan*    target_plan_ {};
    const BuildLayout*                layout_ {};
    String                            profile_;
    PathBuf                           script_;
    Option<String>                    default_package_;
    String                            script_owner_;
    Vec<String>                       packages_;
    BuildOutputRegistry*              output_registry_ {};
    const ResolvedHostBuildTools*     tools_ {};
    const TargetInfo*                 target_info_ {};
    const ResolvedProcessEnvironment* environment_ {};
    const Option<BuildEventSink>*     observer_ {};
    const luato::State*               lua_state_ {};
    Vec<RegisteredAction>             actions_           = Vec<RegisteredAction>::make();
    Vec<Box<GeneratedActionOutput>>   generated_outputs_ = Vec<Box<GeneratedActionOutput>>::make();
};

auto binding_error(BuildScriptError error) -> luato::Error {
    return luato::Error::binding(rstd::format("{}", error));
}

auto configure_callback(ConfigureSession& session, luato::CallFrame& frame)
    -> luato::BindingResult {
    auto request = frame.required<luato::Table>(usize());
    if (request.is_err()) return Err(rstd::move(request).unwrap_err_unchecked());
    auto table = rstd::move(request).unwrap_unchecked();
    auto known = Vec<String>::make();
    known.push(String::make("package"_str));
    known.push(String::make("input"_str));
    known.push(String::make("output"_str));
    known.push(String::make("values"_str));
    auto checked = table.reject_unknown_fields(known.as_slice());
    if (checked.is_err()) return Err(rstd::move(checked).unwrap_err_unchecked());
    auto package = Result<String, luato::Error>(
        Err(luato::Error::binding(String::make("configure_file.package is required"_str))));
    if (table.contains("package"_str)) {
        package = table.required<String>("package"_str);
    } else {
        auto implicit = session.default_package();
        if (implicit.is_some()) package = Ok(String::make(*implicit));
    }
    auto input      = table.required<String>("input"_str);
    auto output     = table.required<String>("output"_str);
    auto raw_values = table.required<luato::Table>("values"_str);
    if (package.is_err()) return Err(rstd::move(package).unwrap_err_unchecked());
    if (input.is_err()) return Err(rstd::move(input).unwrap_err_unchecked());
    if (output.is_err()) return Err(rstd::move(output).unwrap_err_unchecked());
    if (raw_values.is_err()) return Err(rstd::move(raw_values).unwrap_err_unchecked());
    auto scalars = raw_values->scalar_entries();
    if (scalars.is_err()) return Err(rstd::move(scalars).unwrap_err_unchecked());

    auto values = ConfigureValues::make();
    for (auto& entry : *scalars) {
        if (! configure_placeholder_name_is_valid(entry.key.as_str())) {
            return Err(luato::Error::binding(
                rstd::format("{} is not a valid placeholder name", entry.path.as_str())));
        }
        if (entry.value.is_String()) {
            values.insert(rstd::move(entry.key),
                          ConfigureValue::from_string(entry.value.as_String().value.clone()));
        } else if (entry.value.is_Integer()) {
            values.insert(rstd::move(entry.key),
                          ConfigureValue::from_integer(entry.value.as_Integer().value));
        } else {
            values.insert(rstd::move(entry.key),
                          ConfigureValue::from_boolean(entry.value.as_Boolean().value));
        }
    }
    auto configured = session.configure(package->as_str(),
                                        rstd::move(input).unwrap_unchecked(),
                                        rstd::move(output).unwrap_unchecked(),
                                        values);
    if (configured.is_err()) return Err(binding_error(rstd::move(configured).unwrap_err()));
    auto result = luato::Table::make();
    auto path   = configured->output.as_path().to_str();
    if (path.is_none()) {
        return Err(luato::Error::binding(
            String::make("configure_file output path is not valid UTF-8"_str)));
    }
    auto inserted = result.set(String::make("output"_str), String::make(*path));
    if (inserted.is_err()) return Err(rstd::move(inserted).unwrap_err_unchecked());
    inserted = result.set(String::make("changed"_str),
                          configured->write != rstd::fs::WriteOutcome::Unchanged);
    if (inserted.is_err()) return Err(rstd::move(inserted).unwrap_err_unchecked());
    frame.push(rstd::move(result));
    return Ok(usize(1));
}

auto tool_callback(ToolActionSession& session, luato::CallFrame& frame) -> luato::BindingResult {
    auto alias = frame.required<String>(usize {});
    if (alias.is_err()) return Err(rstd::move(alias).unwrap_err_unchecked());
    auto tool = session.tool(alias->as_str());
    if (tool.is_err()) return Err(binding_error(rstd::move(tool).unwrap_err()));
    frame.push(*tool);
    return Ok(usize(1));
}

auto target_callback(ToolActionSession& session, luato::CallFrame& frame) -> luato::BindingResult {
    auto request = frame.required<luato::Table>(usize {});
    if (request.is_err()) return Err(rstd::move(request).unwrap_err_unchecked());
    auto target = session.target(*request);
    if (target.is_err()) return Err(binding_error(rstd::move(target).unwrap_err()));
    frame.push(*target);
    return Ok(usize(1));
}

auto external_dependency_callback(ToolActionSession& session, luato::CallFrame& frame)
    -> luato::BindingResult {
    auto target = frame.required<luato::OpaqueHandle>(usize {});
    auto alias  = frame.required<String>(usize(1));
    if (target.is_err()) return Err(rstd::move(target).unwrap_err_unchecked());
    if (alias.is_err()) return Err(rstd::move(alias).unwrap_err_unchecked());
    auto dependency = session.external_dependency(*target, alias->as_str());
    if (dependency.is_err()) return Err(binding_error(rstd::move(dependency).unwrap_err()));
    frame.push(*dependency);
    return Ok(usize(1));
}

auto external_tool_callback(ToolActionSession& session, luato::CallFrame& frame)
    -> luato::BindingResult {
    auto dependency = frame.required<luato::OpaqueHandle>(usize {});
    auto name       = frame.required<String>(usize(1));
    if (dependency.is_err()) return Err(rstd::move(dependency).unwrap_err_unchecked());
    if (name.is_err()) return Err(rstd::move(name).unwrap_err_unchecked());
    auto tool = session.external_tool(*dependency, name->as_str());
    if (tool.is_err()) return Err(binding_error(rstd::move(tool).unwrap_err()));
    frame.push(*tool);
    return Ok(usize(1));
}

auto external_dependency_info_callback(ToolActionSession& session, luato::CallFrame& frame)
    -> luato::BindingResult {
    auto dependency = frame.required<luato::OpaqueHandle>(usize {});
    if (dependency.is_err()) return Err(rstd::move(dependency).unwrap_err_unchecked());
    auto information = session.external_dependency_info(*dependency);
    if (information.is_err()) return Err(binding_error(rstd::move(information).unwrap_err()));
    frame.push(rstd::move(information).unwrap());
    return Ok(usize(1));
}

auto preprocessor_environment_callback(ToolActionSession& session, luato::CallFrame& frame)
    -> luato::BindingResult {
    auto target = frame.required<luato::OpaqueHandle>(usize {});
    if (target.is_err()) return Err(rstd::move(target).unwrap_err_unchecked());
    auto environment = session.preprocessor_environment(*target);
    if (environment.is_err()) return Err(binding_error(rstd::move(environment).unwrap_err()));
    frame.push(rstd::move(environment).unwrap());
    return Ok(usize(1));
}

auto add_generated_source_callback(ToolActionSession& session, luato::CallFrame& frame)
    -> luato::BindingResult {
    auto target = frame.required<luato::OpaqueHandle>(usize {});
    auto output = frame.required<luato::OpaqueHandle>(usize(1));
    if (target.is_err()) return Err(rstd::move(target).unwrap_err_unchecked());
    if (output.is_err()) return Err(rstd::move(output).unwrap_err_unchecked());
    auto added = session.add_generated_source(*target, *output);
    if (added.is_err()) return Err(binding_error(rstd::move(added).unwrap_err()));
    frame.push(*added);
    return Ok(usize(1));
}

auto add_generated_include_callback(ToolActionSession& session, luato::CallFrame& frame)
    -> luato::BindingResult {
    auto target   = frame.required<luato::OpaqueHandle>(usize {});
    auto relative = frame.required<String>(usize(1));
    if (target.is_err()) return Err(rstd::move(target).unwrap_err_unchecked());
    if (relative.is_err()) return Err(rstd::move(relative).unwrap_err_unchecked());
    auto added = session.add_generated_include(*target, rstd::move(relative).unwrap_unchecked());
    if (added.is_err()) return Err(binding_error(rstd::move(added).unwrap_err()));
    frame.push(*added);
    return Ok(usize(1));
}

auto add_generated_artifact_callback(ToolActionSession&         session,
                                     luato::CallFrame&          frame,
                                     cpp::GeneratedArtifactRole role) -> luato::BindingResult {
    auto target = frame.required<luato::OpaqueHandle>(usize {});
    auto output = frame.required<luato::OpaqueHandle>(usize(1));
    if (target.is_err()) return Err(rstd::move(target).unwrap_err_unchecked());
    if (output.is_err()) return Err(rstd::move(output).unwrap_err_unchecked());
    auto added = session.add_generated_artifact(*target, *output, role);
    if (added.is_err()) return Err(binding_error(rstd::move(added).unwrap_err()));
    frame.push(*added);
    return Ok(usize(1));
}

auto run_callback(ToolActionSession& session, luato::CallFrame& frame) -> luato::BindingResult {
    auto request = frame.required<luato::Table>(usize {});
    if (request.is_err()) return Err(rstd::move(request).unwrap_err_unchecked());
    auto ran = session.run(*request);
    if (ran.is_err()) return Err(binding_error(rstd::move(ran).unwrap_err()));
    auto result   = luato::Table::make();
    auto inserted = result.set(String::make("changed"_str), ran->changed);
    if (inserted.is_err()) return Err(rstd::move(inserted).unwrap_err_unchecked());
    auto values = Vec<luato::Value>::with_capacity(ran->outputs.len());
    for (auto output : ran->outputs) values.push(luato::Value::Opaque(output.identity));
    inserted = result.set(String::make("outputs"_str), luato::Array::from(rstd::move(values)));
    if (inserted.is_err()) return Err(rstd::move(inserted).unwrap_err_unchecked());
    frame.push(rstd::move(result));
    return Ok(usize(1));
}

auto write_callback(ToolActionSession& session, luato::CallFrame& frame) -> luato::BindingResult {
    auto request = frame.required<luato::Table>(usize {});
    if (request.is_err()) return Err(rstd::move(request).unwrap_err_unchecked());
    auto written = session.write(*request);
    if (written.is_err()) return Err(binding_error(rstd::move(written).unwrap_err()));
    auto result   = luato::Table::make();
    auto inserted = result.set(String::make("changed"_str), written->changed);
    if (inserted.is_err()) return Err(rstd::move(inserted).unwrap_err_unchecked());
    if (written->outputs.len() != usize(1)) {
        return Err(luato::Error::binding(
            String::make("lito.write did not produce exactly one output"_str)));
    }
    inserted = result.set(String::make("output"_str), written->outputs[usize {}]);
    if (inserted.is_err()) return Err(rstd::move(inserted).unwrap_err_unchecked());
    frame.push(rstd::move(result));
    return Ok(usize(1));
}

auto copy_callback(ToolActionSession& session, luato::CallFrame& frame) -> luato::BindingResult {
    auto request = frame.required<luato::Table>(usize {});
    if (request.is_err()) return Err(rstd::move(request).unwrap_err_unchecked());
    auto copied = session.copy(*request);
    if (copied.is_err()) return Err(binding_error(rstd::move(copied).unwrap_err()));
    if (copied->outputs.len() != usize(1)) {
        return Err(luato::Error::binding(
            String::make("lito.copy did not produce exactly one output"_str)));
    }
    auto result   = luato::Table::make();
    auto inserted = result.set(String::make("changed"_str), copied->changed);
    if (inserted.is_err()) return Err(rstd::move(inserted).unwrap_err_unchecked());
    inserted = result.set(String::make("output"_str), copied->outputs[usize {}]);
    if (inserted.is_err()) return Err(rstd::move(inserted).unwrap_err_unchecked());
    frame.push(rstd::move(result));
    return Ok(usize(1));
}

auto transform_callback(ToolActionSession& session, luato::CallFrame& frame)
    -> luato::BindingResult {
    auto request = frame.required<luato::Table>(usize {});
    if (request.is_err()) return Err(rstd::move(request).unwrap_err_unchecked());
    auto transformed = session.transform(*request);
    if (transformed.is_err()) return Err(binding_error(rstd::move(transformed).unwrap_err()));
    auto values = Vec<luato::Value>::with_capacity(transformed->outputs.len());
    for (auto output : transformed->outputs) values.push(luato::Value::Opaque(output.identity));
    auto result   = luato::Table::make();
    auto inserted = result.set(String::make("changed"_str), transformed->changed);
    if (inserted.is_err()) return Err(rstd::move(inserted).unwrap_err_unchecked());
    inserted = result.set(String::make("outputs"_str), luato::Array::from(rstd::move(values)));
    if (inserted.is_err()) return Err(rstd::move(inserted).unwrap_err_unchecked());
    frame.push(rstd::move(result));
    return Ok(usize(1));
}

auto materialize_generated_inputs(cpp::PackageMetadata&             metadata,
                                  cpp::ResolvedNativeTargetPlan&    target_plan,
                                  const BuildLayout&                layout,
                                  const cpp::SourceTargetSelection& selection)
    -> BuildScriptResult<empty> {
    for (auto target_id : selection.target_order) {
        auto& target         = metadata.targets[target_id];
        auto  generated_root = Option<PathBuf> {};
        for (auto& group : target.source_groups) {
            if (! group.generated) continue;
            if (generated_root.is_none()) {
                generated_root =
                    Some(rstd_try(layout.generated_package_directory(target.id.package.as_str())));
            }
            auto inspected = rstd::fs::metadata(generated_root->as_path());
            if (inspected.is_err() || ! inspected->is_dir()) {
                return script_failure<empty>(
                    rstd::format("generated source root '{}' for package '{}' does not exist",
                                 generated_root->as_path(),
                                 target.id.package.as_str()));
            }
            group.root = generated_root->clone();
            group.identity =
                rstd::format("generated:{}:{}", target.id.package.as_str(), layout.output());
        }
        for (const auto& requirement : target.usage.private_include_directory_requirements) {
            if (requirement.root != lito::dependency::IncludeDirectoryRoot::Generated) continue;
            auto generated =
                rstd_try(layout.generated_package_directory(target.id.package.as_str()));
            auto requested = generated.join(requirement.path.as_path());
            auto canonical = rstd::fs::canonicalize(requested.as_path());
            if (canonical.is_err()) {
                return script_failure<empty>(
                    rstd::format("generated private include directory '{}' does not exist",
                                 requested.as_path()));
            }
            if (canonical->as_path().strip_prefix(generated.as_path()).is_none()) {
                return script_failure<empty>(
                    rstd::format("generated private include directory '{}' escapes package root",
                                 requested.as_path()));
            }
            auto inspected = rstd::fs::metadata(canonical->as_path());
            if (inspected.is_err() || ! inspected->is_dir()) {
                return script_failure<empty>(
                    rstd::format("generated private include directory '{}' is not a directory",
                                 canonical->as_path()));
            }
            auto repeated = false;
            for (const auto& include : target.usage.private_include_directories) {
                if (include.as_path() == canonical->as_path()) repeated = true;
            }
            auto include = rstd::move(canonical).unwrap();
            if (! repeated) target.usage.private_include_directories.push(include.clone());
            cpp::add_private_include_directory(target_plan.contexts[target_id],
                                               rstd::move(include));
        }
        target.usage.private_include_directory_requirements.clear();
    }
    return Ok(empty {});
}

auto has_generated_inputs(const cpp::PackageMetadata&       metadata,
                          const cpp::SourceTargetSelection& selection) noexcept -> bool {
    for (auto target : selection.target_order) {
        for (const auto& group : metadata.targets[target].source_groups) {
            if (group.generated) return true;
        }
        if (! metadata.targets[target].usage.private_include_directory_requirements.is_empty()) {
            return true;
        }
    }
    return false;
}

} // namespace lito

namespace lito
{

struct BuildScriptInvocation {
    String         owner;
    usize          owner_index {};
    PathBuf        script;
    PathBuf        root;
    Option<String> package;
    Vec<String>    packages;
};

auto build_script_exists(ref<rstd::path::Path> script) -> BuildScriptResult<bool> {
    auto exists = rstd::fs::exists(script);
    if (exists.is_err()) {
        return script_io_failure<bool>(
            "inspect build script"_str, script, rstd::move(exists).unwrap_err());
    }
    if (! *exists) return Ok(false);
    auto metadata = rstd::fs::metadata(script);
    if (metadata.is_err()) {
        return script_io_failure<bool>(
            "inspect build script"_str, script, rstd::move(metadata).unwrap_err());
    }
    if (! metadata->is_file()) {
        return script_failure<bool>(
            rstd::format("build script '{}' is not a regular file", script));
    }
    return Ok(true);
}

auto copy_package_names(const Vec<String>& packages) -> Vec<String> {
    auto copied = Vec<String>::with_capacity(packages.len());
    for (const auto& package : packages) copied.push(package.clone());
    return copied;
}

auto execute_build_script_invocation(cpp::PackageMetadata&                    metadata,
                                     cpp::ResolvedNativeTargetPlan&           target_plan,
                                     const BuildLayout&                       layout,
                                     ref<str>                                 profile,
                                     BuildScriptInvocation                    invocation,
                                     BuildOutputRegistry&                     output_registry,
                                     const Option<BuildEventSink>&            observer,
                                     const HostInfo&                          host,
                                     const TargetInfo&                        target_info,
                                     lito::tools::ToolResolver&               resolver,
                                     const ResolvedProcessEnvironment&        environment,
                                     const lito::source::PackageSourceConfig& sources,
                                     usize jobs) -> BuildScriptResult<BuildScriptReport> {
    auto default_package = as<Clone>(invocation.package).clone();
    auto session         = ConfigureSession::create(metadata,
                                                    layout,
                                                    invocation.packages,
                                                    invocation.owner.as_str(),
                                                    as<Clone>(default_package).clone(),
                                                    invocation.owner.as_str(),
                                                    output_registry);
    if (session.is_err()) return Err(rstd::move(session).unwrap_err());
    auto configure = rstd::move(session).unwrap();
    auto resolved_tools =
        resolve_host_build_tools(metadata,
                                 invocation.packages,
                                 host,
                                 layout,
                                 resolver,
                                 environment,
                                 sources,
                                 jobs,
                                 observer.is_some() ? *observer : BuildEventSink {});
    if (resolved_tools.is_err()) {
        return Err(rstd::into<BuildScriptError>(rstd::move(resolved_tools).unwrap_err()));
    }
    auto tools           = rstd::move(resolved_tools).unwrap();
    auto action_packages = copy_package_names(invocation.packages);
    auto actions         = ToolActionSession(metadata,
                                             target_plan,
                                             layout,
                                             profile,
                                             invocation.script.as_path(),
                                             rstd::move(default_package),
                                             invocation.owner.clone(),
                                             rstd::move(action_packages),
                                             output_registry,
                                             tools,
                                             target_info,
                                             environment,
                                             observer);

    auto state = luato::State::create(luato::StateOptions::build_script());
    if (state.is_err()) {
        return Err(BuildScriptError::Lua(String::make("create Lua state"_str),
                                         None(),
                                         rstd::move(state).unwrap_err_unchecked()));
    }
    auto lua = rstd::move(state).unwrap_unchecked();
    actions.set_lua_state(lua);
    const auto& script_owner = metadata.build_scripts[invocation.owner_index];
    auto        module_catalog =
        lito::package::ScriptModuleCatalog::make(invocation.root.as_path(),
                                                 script_owner.source_identity.as_str(),
                                                 script_owner.script_dependencies.as_slice(),
                                                 script_owner.script_packages.as_slice(),
                                                 lito::manifest::ScriptHostKind::Build);
    if (module_catalog.is_err()) {
        return Err(rstd::into<BuildScriptError>(rstd::move(module_catalog).unwrap_err()));
    }
    auto modules            = rstd::move(module_catalog).unwrap();
    auto configured_modules = lua.set_module_resolver(luato::ModuleResolverSpec::make(
        [&modules](luato::ModuleRequest request) -> luato::Result<luato::LuaModuleSource> {
            return modules.resolve(rstd::move(request));
        }));
    if (configured_modules.is_err()) {
        return Err(BuildScriptError::Lua(String::make("configure build script modules"_str),
                                         Some(invocation.script.clone()),
                                         rstd::move(configured_modules).unwrap_err_unchecked()));
    }
    auto module = luato::ModuleSpec(String::make("lito"_str));
    module.set(String::make("profile"_str), String::make(profile));
    auto project_root = metadata.root.as_path().to_str();
    if (project_root.is_none()) {
        return script_failure<BuildScriptReport>("project root is not valid UTF-8"_str);
    }
    module.set(String::make("project_root"_str), String::make(*project_root));
    if (invocation.package.is_some()) {
        auto package_root = invocation.root.as_path().to_str();
        if (package_root.is_none()) {
            return script_failure<BuildScriptReport>("package root is not valid UTF-8"_str);
        }
        auto generated =
            rstd_try(layout.create_generated_package_directory(invocation.package->as_str()));
        auto generated_root = generated.as_path().to_str();
        if (generated_root.is_none()) {
            return script_failure<BuildScriptReport>("generated root is not valid UTF-8"_str);
        }
        module.set(String::make("package"_str), invocation.package->clone());
        module.set(String::make("package_root"_str), String::make(*package_root));
        module.set(String::make("generated_root"_str), String::make(*generated_root));
    }
    module.add(luato::NativeFunctionSpec::make(
        String::make("configure_file"_str),
        usize(1),
        [&configure](luato::CallFrame& frame) -> luato::BindingResult {
            return configure_callback(configure, frame);
        }));
    module.add(luato::NativeFunctionSpec::make(
        String::make("tool"_str),
        usize(1),
        [&actions](luato::CallFrame& frame) -> luato::BindingResult {
            return tool_callback(actions, frame);
        }));
    module.add(luato::NativeFunctionSpec::make(
        String::make("target"_str),
        usize(1),
        [&actions](luato::CallFrame& frame) -> luato::BindingResult {
            return target_callback(actions, frame);
        }));
    module.add(luato::NativeFunctionSpec::make(
        String::make("external_dependency"_str),
        usize(2),
        [&actions](luato::CallFrame& frame) -> luato::BindingResult {
            return external_dependency_callback(actions, frame);
        }));
    module.add(luato::NativeFunctionSpec::make(
        String::make("external_tool"_str),
        usize(2),
        [&actions](luato::CallFrame& frame) -> luato::BindingResult {
            return external_tool_callback(actions, frame);
        }));
    module.add(luato::NativeFunctionSpec::make(
        String::make("external_dependency_info"_str),
        usize(1),
        [&actions](luato::CallFrame& frame) -> luato::BindingResult {
            return external_dependency_info_callback(actions, frame);
        }));
    module.add(luato::NativeFunctionSpec::make(
        String::make("target_preprocessor_environment"_str),
        usize(1),
        [&actions](luato::CallFrame& frame) -> luato::BindingResult {
            return preprocessor_environment_callback(actions, frame);
        }));
    module.add(luato::NativeFunctionSpec::make(
        String::make("target_add_generated_source"_str),
        usize(2),
        [&actions](luato::CallFrame& frame) -> luato::BindingResult {
            return add_generated_source_callback(actions, frame);
        }));
    module.add(luato::NativeFunctionSpec::make(
        String::make("target_add_generated_include"_str),
        usize(2),
        [&actions](luato::CallFrame& frame) -> luato::BindingResult {
            return add_generated_include_callback(actions, frame);
        }));
    module.add(luato::NativeFunctionSpec::make(
        String::make("target_add_resource"_str),
        usize(2),
        [&actions](luato::CallFrame& frame) -> luato::BindingResult {
            return add_generated_artifact_callback(
                actions, frame, cpp::GeneratedArtifactRole::Resource);
        }));
    module.add(luato::NativeFunctionSpec::make(
        String::make("target_add_metadata"_str),
        usize(2),
        [&actions](luato::CallFrame& frame) -> luato::BindingResult {
            return add_generated_artifact_callback(
                actions, frame, cpp::GeneratedArtifactRole::Metadata);
        }));
    module.add(luato::NativeFunctionSpec::make(
        String::make("target_add_auxiliary_artifact"_str),
        usize(2),
        [&actions](luato::CallFrame& frame) -> luato::BindingResult {
            return add_generated_artifact_callback(
                actions, frame, cpp::GeneratedArtifactRole::Auxiliary);
        }));
    module.add(luato::NativeFunctionSpec::make(
        String::make("run"_str),
        usize(1),
        [&actions](luato::CallFrame& frame) -> luato::BindingResult {
            return run_callback(actions, frame);
        }));
    module.add(luato::NativeFunctionSpec::make(
        String::make("write"_str),
        usize(1),
        [&actions](luato::CallFrame& frame) -> luato::BindingResult {
            return write_callback(actions, frame);
        }));
    module.add(luato::NativeFunctionSpec::make(
        String::make("copy"_str),
        usize(1),
        [&actions](luato::CallFrame& frame) -> luato::BindingResult {
            return copy_callback(actions, frame);
        }));
    module.add(luato::NativeFunctionSpec::make(
        String::make("transform"_str),
        usize(1),
        [&actions](luato::CallFrame& frame) -> luato::BindingResult {
            return transform_callback(actions, frame);
        }));
    auto native_module = luato::NativeRequireModuleSpec(
        String::make("@lito"_str), String::make(build_host_api_identity), rstd::move(module));
    native_module.set_global_alias(String::make("lito"_str));
    auto registered = lua.register_native_require_module(rstd::move(native_module));
    if (registered.is_err()) {
        return Err(BuildScriptError::Lua(String::make("register build script API"_str),
                                         None(),
                                         rstd::move(registered).unwrap_err_unchecked()));
    }
    auto entry = modules.entry(invocation.script.as_path(), invocation.owner.as_str());
    if (entry.is_err()) {
        return Err(BuildScriptError::Lua(String::make("load build script"_str),
                                         Some(invocation.script.clone()),
                                         rstd::move(entry).unwrap_err_unchecked()));
    }
    auto executed = lua.execute_entry(rstd::move(entry).unwrap_unchecked());
    if (executed.is_err()) {
        return Err(BuildScriptError::Lua(String::make("execute build script"_str),
                                         Some(invocation.script.clone()),
                                         rstd::move(executed).unwrap_err_unchecked()));
    }
    auto finalized = actions.finalize_module_identity();
    if (finalized.is_err()) return Err(rstd::move(finalized).unwrap_err());
    auto generated = actions.execute(jobs);
    if (generated.is_err()) return Err(rstd::move(generated).unwrap_err());
    configure.report().executed = true;
    configure.report().elapsed  = executed->elapsed;
    auto finished               = configure.finish();
    if (finished.is_err()) return Err(rstd::move(finished).unwrap_err());
    finished->executions.push(BuildScriptExecution {
        .owner   = invocation.owner.clone(),
        .script  = invocation.script.clone(),
        .elapsed = executed->elapsed,
    });
    if (observer.is_some() && observer->notify != nullptr) {
        for (const auto& file : finished->files) {
            auto kind = file.write == rstd::fs::WriteOutcome::Unchanged
                            ? BuildEventKind::ConfigureReuse
                            : BuildEventKind::Configure;
            observer->notify(observer->context,
                             BuildEvent { kind, "lito.configure_file"_str, file.output.as_path() });
        }
    }
    return finished;
}

void merge_build_script_report(BuildScriptReport& total, BuildScriptReport report) {
    total.executed = total.executed || report.executed;
    total.elapsed += report.elapsed;
    total.created += report.created;
    total.replaced += report.replaced;
    total.unchanged += report.unchanged;
    total.stale_removed += report.stale_removed;
    for (auto& file : report.files) total.files.push(rstd::move(file));
    for (auto& execution : report.executions) total.executions.push(rstd::move(execution));
}

auto package_has_script(const Vec<String>& packages, ref<str> package) noexcept -> bool {
    for (const auto& candidate : packages) {
        if (candidate == package) return true;
    }
    return false;
}

auto execute_build_script(cpp::PackageMetadata&                    metadata,
                          cpp::ResolvedNativeTargetPlan&           target_plan,
                          const BuildLayout&                       layout,
                          ref<str>                                 profile,
                          const Vec<String>&                       selected_packages,
                          const cpp::SourceTargetSelection&        selection,
                          const Option<BuildEventSink>&            observer,
                          const HostInfo&                          host,
                          const TargetInfo&                        target_info,
                          lito::tools::ToolResolver&               resolver,
                          const ResolvedProcessEnvironment&        environment,
                          const lito::source::PackageSourceConfig& sources,
                          usize jobs) -> BuildScriptResult<BuildScriptReport> {
    auto invocations       = Vec<BuildScriptInvocation>::make();
    auto scripted_packages = Vec<String>::make();
    auto workspace_script  = false;

    for (usize owner_index {}; owner_index < metadata.build_scripts.len(); ++owner_index) {
        const auto& owner = metadata.build_scripts[owner_index];
        if (owner.kind != cpp::BuildScriptOwnerKind::Workspace) continue;
        if (! rstd_try(build_script_exists(owner.script.as_path()))) continue;
        workspace_script = true;
        invocations.push(BuildScriptInvocation {
            .owner       = String::make("workspace"_str),
            .owner_index = owner_index,
            .script      = owner.script.clone(),
            .root        = owner.root.clone(),
            .package     = None(),
            .packages    = copy_package_names(selected_packages),
        });
    }
    for (const auto& package : selected_packages) {
        for (usize owner_index {}; owner_index < metadata.build_scripts.len(); ++owner_index) {
            const auto& owner = metadata.build_scripts[owner_index];
            if (owner.kind != cpp::BuildScriptOwnerKind::Package || owner.package.is_none() ||
                owner.package->as_str() != package.as_str()) {
                continue;
            }
            if (! rstd_try(build_script_exists(owner.script.as_path()))) break;
            auto packages = Vec<String>::make();
            packages.push(package.clone());
            invocations.push(BuildScriptInvocation {
                .owner       = rstd::format("package-{}", package.as_str()),
                .owner_index = owner_index,
                .script      = owner.script.clone(),
                .root        = owner.root.clone(),
                .package     = Some(package.clone()),
                .packages    = rstd::move(packages),
            });
            scripted_packages.push(package.clone());
            break;
        }
    }

    if (has_generated_inputs(metadata, selection) && ! workspace_script) {
        for (auto target : selection.target_order) {
            const auto& candidate = metadata.targets[target];
            auto        requires_script =
                ! candidate.usage.private_include_directory_requirements.is_empty();
            for (const auto& group : candidate.source_groups) {
                if (group.generated) requires_script = true;
            }
            if (! requires_script ||
                package_has_script(scripted_packages, candidate.id.package.as_str())) {
                continue;
            }
            auto expected = Option<ref<rstd::path::Path>> {};
            for (const auto& owner : metadata.build_scripts) {
                if (owner.kind == cpp::BuildScriptOwnerKind::Package && owner.package.is_some() &&
                    owner.package->as_str() == candidate.id.package.as_str()) {
                    expected = Some(owner.script.as_path());
                    break;
                }
            }
            if (expected.is_some()) {
                return script_failure<BuildScriptReport>(rstd::format(
                    "generated build inputs for package '{}' require build script '{}'",
                    candidate.id.package.as_str(),
                    *expected));
            }
            return script_failure<BuildScriptReport>(rstd::format(
                "generated build inputs for package '{}' have no local build-script owner",
                candidate.id.package.as_str()));
        }
    }

    auto output_registry = BuildOutputRegistry {};
    auto report          = BuildScriptReport {};
    for (auto& invocation : invocations) {
        auto result = execute_build_script_invocation(metadata,
                                                      target_plan,
                                                      layout,
                                                      profile,
                                                      rstd::move(invocation),
                                                      output_registry,
                                                      observer,
                                                      host,
                                                      target_info,
                                                      resolver,
                                                      environment,
                                                      sources,
                                                      jobs);
        if (result.is_err()) return Err(rstd::move(result).unwrap_err());
        merge_build_script_report(report, rstd::move(result).unwrap());
    }
    auto materialized = materialize_generated_inputs(metadata, target_plan, layout, selection);
    if (materialized.is_err()) return Err(rstd::move(materialized).unwrap_err());
    return Ok(rstd::move(report));
}

} // namespace lito
