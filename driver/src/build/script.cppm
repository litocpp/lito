module;
#include <rstd/macro.hpp>

module lito.driver:build.script;

import rstd;
import lito.tools;
import rstd.json;
import luato;
import lito.core;
import :build.event;
import :build.artifact;
import lito.cpp;
import :build.layout;
import :build.host_tool;
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
    for (auto component = components.next(); component.is_some(); component = components.next()) {
        if (component->is_normal()) continue;
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
        report_.files.push(ConfiguredFile { .output = output.clone(), .write = *written });
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

auto action_file_digest(ref<rstd::path::Path> path) -> BuildScriptResult<String> {
    auto data = rstd::fs::read(path);
    if (data.is_err()) {
        return script_io_failure<String>(
            "read build-tool action file"_str, path, rstd::move(data).unwrap_err());
    }
    return Ok(rstd::crypto::sha256_hex(data->as_slice()));
}

auto action_receipt_matches(ref<rstd::path::Path> receipt,
                            ref<str>              identity,
                            const Vec<PathBuf>&   outputs,
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
    auto recorded_identity = parsed->get("identity"_str);
    auto recorded_outputs  = parsed->get("outputs"_str);
    if (recorded_identity.is_none() || (**recorded_identity).as_str() != Some(identity) ||
        recorded_outputs.is_none() || (**recorded_outputs).as_array().is_none() ||
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
    return Ok(true);
}

auto action_receipt_text(ref<str> identity, const Vec<PathBuf>& outputs, const Vec<String>& digests)
    -> String {
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
                    Json::Number(rstd::json::Number::from_u64(u64(1))));
    document.insert(String::make("identity"_str), Json::String(String::make(identity)));
    document.insert(String::make("outputs"_str), Json::Array(rstd::move(entries)));
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
    for (auto next = entries.next(); next.is_some(); next = entries.next()) {
        auto item = rstd::move(next).unwrap();
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

class ToolActionSession {
public:
    ToolActionSession(const cpp::PackageMetadata&       metadata,
                      const BuildLayout&                layout,
                      ref<str>                          profile,
                      ref<rstd::path::Path>             script,
                      Option<String>                    default_package,
                      String                            script_owner,
                      BuildOutputRegistry&              outputs,
                      const ResolvedHostBuildTools&     tools,
                      const ResolvedProcessEnvironment& environment,
                      const Option<BuildEventSink>&     observer)
        : metadata_(rstd::addressof(metadata)),
          layout_(rstd::addressof(layout)),
          profile_(String::make(profile)),
          script_(PathBuf::from(script)),
          default_package_(rstd::move(default_package)),
          script_owner_(rstd::move(script_owner)),
          output_registry_(rstd::addressof(outputs)),
          tools_(rstd::addressof(tools)),
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

    auto run(const luato::Table& request) -> BuildScriptResult<bool> {
        auto known = Vec<String>::make();
        known.push(String::make("tool"_str));
        known.push(String::make("package"_str));
        known.push(String::make("cwd"_str));
        known.push(String::make("args"_str));
        known.push(String::make("inputs"_str));
        known.push(String::make("outputs"_str));
        auto checked = request.reject_unknown_fields(known.as_slice());
        if (checked.is_err()) {
            return action_request_failure<bool>(rstd::format("{}", checked.unwrap_err()));
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
            return action_request_failure<bool>(rstd::format("{}", handle.unwrap_err()));
        if (package.is_err())
            return action_request_failure<bool>(rstd::format("{}", package.unwrap_err()));
        if (cwd.is_err()) return action_request_failure<bool>(rstd::format("{}", cwd.unwrap_err()));
        if (args.is_err())
            return action_request_failure<bool>(rstd::format("{}", args.unwrap_err()));
        if (inputs.is_err())
            return action_request_failure<bool>(rstd::format("{}", inputs.unwrap_err()));
        if (outputs.is_err())
            return action_request_failure<bool>(rstd::format("{}", outputs.unwrap_err()));
        auto resolved_tool = tools_->from_identity(handle->identity);
        if (resolved_tool.is_none()) {
            return action_request_failure<bool>(
                "build-tool handle does not belong to this build script"_str);
        }
        if ((**resolved_tool).package != package->as_str()) {
            return action_request_failure<bool>(
                rstd::format("build-tool '{}' belongs to package '{}', not '{}'",
                             (**resolved_tool).alias.as_str(),
                             (**resolved_tool).package.as_str(),
                             package->as_str()));
        }
        auto package_root = find_package_root(*metadata_, package->as_str());
        if (package_root.is_none()) {
            return action_request_failure<bool>(
                rstd::format("build-tool action package '{}' is not selected", package->as_str()));
        }
        auto cwd_relative =
            rstd_try(normal_relative_path(rstd::move(cwd).unwrap(), "build-tool action cwd"_str));
        auto cwd_requested     = PathBuf::from(*package_root).join(cwd_relative.as_path());
        auto working_directory = rstd::fs::canonicalize(cwd_requested.as_path());
        if (working_directory.is_err()) {
            return script_io_failure<bool>("resolve build-tool action cwd"_str,
                                           cwd_requested.as_path(),
                                           rstd::move(working_directory).unwrap_err());
        }
        if (working_directory->as_path().strip_prefix(*package_root).is_none()) {
            return action_request_failure<bool>("build-tool action cwd escapes package root"_str);
        }
        auto arguments        = rstd_try(action_string_array(*args, "lito.run.args"_str));
        auto declared_inputs  = rstd_try(action_string_array(*inputs, "lito.run.inputs"_str));
        auto declared_outputs = rstd_try(action_string_array(*outputs, "lito.run.outputs"_str));
        if (arguments.is_empty() || declared_inputs.is_empty() || declared_outputs.is_empty()) {
            return action_request_failure<bool>(
                "build-tool action requires args, inputs, and outputs"_str);
        }
        auto input_paths   = Vec<PathBuf>::make();
        auto input_digests = Vec<String>::make();
        for (auto& input : declared_inputs) {
            auto relative =
                rstd_try(normal_relative_path(rstd::move(input), "build-tool action input"_str));
            auto requested = working_directory->join(relative.as_path());
            auto canonical = rstd::fs::canonicalize(requested.as_path());
            if (canonical.is_err()) {
                return script_io_failure<bool>("resolve build-tool action input"_str,
                                               requested.as_path(),
                                               rstd::move(canonical).unwrap_err());
            }
            if (canonical->as_path().strip_prefix(*package_root).is_none()) {
                return action_failure<bool>(BuildToolActionError::InvalidInput(
                    requested.clone(), String::make("path escapes package root"_str)));
            }
            auto metadata = rstd::fs::symlink_metadata(canonical->as_path());
            if (metadata.is_err() || metadata->is_symlink() || ! metadata->is_file()) {
                return action_failure<bool>(BuildToolActionError::InvalidInput(
                    canonical->clone(), String::make("path is not a regular file"_str)));
            }
            input_digests.push(rstd_try(action_file_digest(canonical->as_path())));
            input_paths.push(rstd::move(canonical).unwrap());
        }
        auto output_paths = Vec<PathBuf>::make();
        for (auto& output : declared_outputs) {
            auto relative =
                rstd_try(normal_relative_path(rstd::move(output), "build-tool action output"_str));
            for (const auto& existing : output_paths) {
                if (existing.as_path() == relative.as_path()) {
                    return action_failure<bool>(BuildToolActionError::InvalidOutput(
                        relative.clone(), String::make("path is declared more than once"_str)));
                }
            }
            rstd_try(output_registry_->claim(
                package->as_str(), relative.as_path(), script_owner_.as_str()));
            output_paths.push(rstd::move(relative));
        }
        auto script_digest = rstd_try(action_file_digest(script_.as_path()));
        auto identity_text = rstd::format("build-tool-action-v1\n{}\n{}\n{}\n{}\n{}",
                                          package->as_str(),
                                          profile_.as_str(),
                                          (**resolved_tool).receipt_identity.as_str(),
                                          cwd_relative.as_path(),
                                          script_digest.as_str());
        for (const auto& argument : arguments) {
            identity_text.push_ascii('\n');
            identity_text.push_str(argument.as_str());
        }
        for (usize index {}; index < input_paths.len(); ++index) {
            identity_text.push_ascii('\n');
            identity_text.push_str(input_paths[index].as_path().to_string_lossy().as_str());
            identity_text.push_ascii(':');
            identity_text.push_str(input_digests[index].as_str());
        }
        for (const auto& output : output_paths) {
            identity_text.push_str("\noutput:"_str);
            identity_text.push_str(output.as_path().to_string_lossy().as_str());
        }
        identity_text.push_ascii('\n');
        identity_text.push_str(environment_->child_path().to_string_lossy().as_str());
        auto identity = rstd::crypto::sha256_hex(identity_text.as_str());
        auto action_root =
            layout_->build_tool_action_root().join(PathBuf::from(identity.clone()).as_path());
        auto receipt = action_root.join(PathBuf::from("receipt.json"_str).as_path());
        auto generated_root =
            rstd_try(layout_->create_generated_package_directory(package->as_str()));
        auto created = rstd::fs::create_dir_all(action_root.as_path());
        if (created.is_err()) {
            return script_io_failure<bool>("create build-tool action directory"_str,
                                           action_root.as_path(),
                                           rstd::move(created).unwrap_err());
        }
        auto lock_path = action_root.join(PathBuf::from("lock"_str).as_path());
        auto opened    = rstd::fs::OpenOptions::make().read(true).write(true).create(true).open(
            lock_path.as_path());
        if (opened.is_err()) {
            return script_io_failure<bool>("open build-tool action lock"_str,
                                           lock_path.as_path(),
                                           rstd::move(opened).unwrap_err());
        }
        auto locked = rstd::fs::FileLock::acquire(rstd::move(opened).unwrap(),
                                                  rstd::fs::FileLockMode::Exclusive);
        if (locked.is_err()) {
            return script_io_failure<bool>(
                "lock build-tool action"_str, lock_path.as_path(), rstd::move(locked).unwrap_err());
        }
        auto reusable = rstd_try(action_receipt_matches(
            receipt.as_path(), identity.as_str(), output_paths, generated_root.as_path()));
        if (reusable) {
            emit(BuildEventKind::BuildToolRunReuse,
                 (**resolved_tool).alias.as_str(),
                 output_paths.len() == usize(1)
                     ? generated_root.join(output_paths[usize {}].as_path()).as_path()
                     : generated_root.as_path());
            return Ok(false);
        }
        auto staging        = action_root.join(PathBuf::from("staging"_str).as_path());
        auto staging_exists = rstd::fs::exists(staging.as_path());
        if (staging_exists.is_err()) {
            return script_io_failure<bool>("inspect build-tool action staging"_str,
                                           staging.as_path(),
                                           rstd::move(staging_exists).unwrap_err());
        }
        if (*staging_exists) {
            auto removed = rstd::fs::remove_dir_all(staging.as_path());
            if (removed.is_err()) {
                return script_io_failure<bool>("clear build-tool action staging"_str,
                                               staging.as_path(),
                                               rstd::move(removed).unwrap_err());
            }
        }
        created = rstd::fs::create_dir_all(staging.as_path());
        if (created.is_err()) {
            return script_io_failure<bool>("create build-tool output staging"_str,
                                           staging.as_path(),
                                           rstd::move(created).unwrap_err());
        }
        if (output_paths.len() == usize(1)) {
            auto staged_output = staging.join(output_paths[usize {}].as_path());
            auto staged_parent = staged_output.as_path().parent().unwrap();
            created            = rstd::fs::create_dir_all(staged_parent);
            if (created.is_err()) {
                return script_io_failure<bool>("create build-tool output parent"_str,
                                               staged_parent,
                                               rstd::move(created).unwrap_err());
            }
        }
        auto placeholder_output = output_paths.len() == usize(1)
                                      ? staging.join(output_paths[usize {}].as_path())
                                      : staging.clone();
        auto staged_text        = placeholder_output.as_path().to_str();
        if (staged_text.is_none()) {
            return action_request_failure<bool>("build-tool staging path is not valid UTF-8"_str);
        }
        auto invocation = Vec<String>::make();
        invocation.push((**resolved_tool).executable.as_path().to_string_lossy());
        auto replaced_output = false;
        for (auto& argument : arguments) {
            auto placeholder = Option<usize> {};
            for (usize index {}; index + usize(8) <= argument.len(); ++index) {
                if (argument.as_str().get(index, index + usize(8)).unwrap() == "@OUTPUT@"_str) {
                    placeholder = Some(index);
                    break;
                }
            }
            if (placeholder.is_some()) {
                if (replaced_output) {
                    return action_request_failure<bool>(
                        "build-tool action may use '@OUTPUT@' only once"_str);
                }
                argument.replace_range(*placeholder, *placeholder + usize(8), *staged_text);
                if (argument.as_str().contains("@OUTPUT@"_str)) {
                    return action_request_failure<bool>(
                        "build-tool action may use '@OUTPUT@' only once"_str);
                }
                invocation.push(rstd::move(argument));
                replaced_output = true;
            } else {
                invocation.push(rstd::move(argument));
            }
        }
        if (! replaced_output) {
            return action_request_failure<bool>(
                "build-tool action args must contain '@OUTPUT@'"_str);
        }
        auto executed = run_command(invocation, *environment_, Some(working_directory->as_path()));
        if (executed.is_err()) {
            return action_failure<bool>(BuildToolActionError::Process(
                (**resolved_tool).alias.clone(), rstd::move(executed).unwrap_err()));
        }
        if (executed->exit_code != i32 {}) {
            return action_failure<bool>(
                BuildToolActionError::Execution((**resolved_tool).alias.clone(),
                                                executed->exit_code,
                                                rstd::move(executed->standard_output),
                                                rstd::move(executed->standard_error)));
        }
        auto actual_outputs = Vec<PathBuf>::make();
        rstd_try(collect_action_outputs(staging.as_path(), staging.as_path(), actual_outputs));
        auto       expected_outputs = output_paths.clone();
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
            return action_failure<bool>(BuildToolActionError::InvalidOutput(
                staging.clone(), String::make("produced files do not match the declared set"_str)));
        }
        auto digests = Vec<String>::make();
        auto changed = false;
        for (const auto& output : output_paths) {
            auto staged_output = staging.join(output.as_path());
            auto bytes         = rstd::fs::read(staged_output.as_path());
            if (bytes.is_err()) {
                return script_io_failure<bool>("read staged build-tool output"_str,
                                               staged_output.as_path(),
                                               rstd::move(bytes).unwrap_err());
            }
            auto final        = generated_root.join(output.as_path());
            auto final_parent = final.as_path().parent().unwrap();
            created           = rstd::fs::create_dir_all(final_parent);
            if (created.is_err()) {
                return script_io_failure<bool>("create build-tool output parent"_str,
                                               final_parent,
                                               rstd::move(created).unwrap_err());
            }
            auto written = rstd::fs::write_atomic_if_changed(final.as_path(), bytes->as_slice());
            if (written.is_err()) {
                return action_publication_failure<bool>("publish build-tool output"_str,
                                                        final.as_path(),
                                                        rstd::move(written).unwrap_err());
            }
            if (*written != rstd::fs::WriteOutcome::Unchanged) changed = true;
            digests.push(rstd::crypto::sha256_hex(bytes->as_slice()));
        }
        auto receipt_text = action_receipt_text(identity.as_str(), output_paths, digests);
        auto receipt_written =
            rstd::fs::write_atomic(receipt.as_path(), receipt_text.as_str().as_bytes());
        if (receipt_written.is_err()) {
            return action_receipt_failure<bool>("write build-tool action receipt"_str,
                                                receipt.as_path(),
                                                rstd::move(receipt_written).unwrap_err());
        }
        emit(BuildEventKind::BuildToolRun,
             (**resolved_tool).alias.as_str(),
             output_paths.len() == usize(1)
                 ? generated_root.join(output_paths[usize {}].as_path()).as_path()
                 : generated_root.as_path());
        return Ok(changed);
    }

private:
    void emit(BuildEventKind kind, ref<str> target, ref<rstd::path::Path> path) const noexcept {
        if (observer_->is_some() && (*observer_)->notify != nullptr)
            (*observer_)->notify((*observer_)->context, BuildEvent { kind, target, path });
    }

    const cpp::PackageMetadata*       metadata_ {};
    const BuildLayout*                layout_ {};
    String                            profile_;
    PathBuf                           script_;
    Option<String>                    default_package_;
    String                            script_owner_;
    BuildOutputRegistry*              output_registry_ {};
    const ResolvedHostBuildTools*     tools_ {};
    const ResolvedProcessEnvironment* environment_ {};
    const Option<BuildEventSink>*     observer_ {};
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

auto run_callback(ToolActionSession& session, luato::CallFrame& frame) -> luato::BindingResult {
    auto request = frame.required<luato::Table>(usize {});
    if (request.is_err()) return Err(rstd::move(request).unwrap_err_unchecked());
    auto ran = session.run(*request);
    if (ran.is_err()) return Err(binding_error(rstd::move(ran).unwrap_err()));
    auto result   = luato::Table::make();
    auto inserted = result.set(String::make("changed"_str), *ran);
    if (inserted.is_err()) return Err(rstd::move(inserted).unwrap_err_unchecked());
    frame.push(rstd::move(result));
    return Ok(usize(1));
}

auto materialize_generated_inputs(cpp::PackageMetadata&             metadata,
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
            if (! repeated)
                target.usage.private_include_directories.push(rstd::move(canonical).unwrap());
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

auto execute_build_script_invocation(const cpp::PackageMetadata&              metadata,
                                     const BuildLayout&                       layout,
                                     ref<str>                                 profile,
                                     BuildScriptInvocation                    invocation,
                                     BuildOutputRegistry&                     output_registry,
                                     const Option<BuildEventSink>&            observer,
                                     const HostInfo&                          host,
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
    auto tools   = rstd::move(resolved_tools).unwrap();
    auto actions = ToolActionSession(metadata,
                                     layout,
                                     profile,
                                     invocation.script.as_path(),
                                     rstd::move(default_package),
                                     invocation.owner.clone(),
                                     output_registry,
                                     tools,
                                     environment,
                                     observer);

    auto state = luato::State::create(luato::StateOptions::base());
    if (state.is_err()) {
        return Err(BuildScriptError::Lua(String::make("create Lua state"_str),
                                         None(),
                                         rstd::move(state).unwrap_err_unchecked()));
    }
    auto lua    = rstd::move(state).unwrap_unchecked();
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
        String::make("run"_str),
        usize(1),
        [&actions](luato::CallFrame& frame) -> luato::BindingResult {
            return run_callback(actions, frame);
        }));
    auto registered = lua.register_module(rstd::move(module));
    if (registered.is_err()) {
        return Err(BuildScriptError::Lua(String::make("register build script API"_str),
                                         None(),
                                         rstd::move(registered).unwrap_err_unchecked()));
    }
    auto executed = lua.execute_file(invocation.script.as_path());
    if (executed.is_err()) {
        return Err(BuildScriptError::Lua(String::make("execute build script"_str),
                                         Some(invocation.script.clone()),
                                         rstd::move(executed).unwrap_err_unchecked()));
    }
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
                          const BuildLayout&                       layout,
                          ref<str>                                 profile,
                          const Vec<String>&                       selected_packages,
                          const cpp::SourceTargetSelection&        selection,
                          const Option<BuildEventSink>&            observer,
                          const HostInfo&                          host,
                          lito::tools::ToolResolver&               resolver,
                          const ResolvedProcessEnvironment&        environment,
                          const lito::source::PackageSourceConfig& sources,
                          usize jobs) -> BuildScriptResult<BuildScriptReport> {
    auto invocations       = Vec<BuildScriptInvocation>::make();
    auto scripted_packages = Vec<String>::make();
    auto workspace_script  = false;

    for (const auto& owner : metadata.build_scripts) {
        if (owner.kind != cpp::BuildScriptOwnerKind::Workspace) continue;
        if (! rstd_try(build_script_exists(owner.script.as_path()))) continue;
        workspace_script = true;
        invocations.push(BuildScriptInvocation {
            .owner    = String::make("workspace"_str),
            .script   = owner.script.clone(),
            .root     = owner.root.clone(),
            .package  = None(),
            .packages = copy_package_names(selected_packages),
        });
    }
    for (const auto& package : selected_packages) {
        for (const auto& owner : metadata.build_scripts) {
            if (owner.kind != cpp::BuildScriptOwnerKind::Package || owner.package.is_none() ||
                owner.package->as_str() != package.as_str()) {
                continue;
            }
            if (! rstd_try(build_script_exists(owner.script.as_path()))) break;
            auto packages = Vec<String>::make();
            packages.push(package.clone());
            invocations.push(BuildScriptInvocation {
                .owner    = rstd::format("package-{}", package.as_str()),
                .script   = owner.script.clone(),
                .root     = owner.root.clone(),
                .package  = Some(package.clone()),
                .packages = rstd::move(packages),
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
                                                      layout,
                                                      profile,
                                                      rstd::move(invocation),
                                                      output_registry,
                                                      observer,
                                                      host,
                                                      resolver,
                                                      environment,
                                                      sources,
                                                      jobs);
        if (result.is_err()) return Err(rstd::move(result).unwrap_err());
        merge_build_script_report(report, rstd::move(result).unwrap());
    }
    auto materialized = materialize_generated_inputs(metadata, layout, selection);
    if (materialized.is_err()) return Err(rstd::move(materialized).unwrap_err());
    return Ok(rstd::move(report));
}

} // namespace lito
