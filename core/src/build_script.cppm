export module lito.build_script;

import rstd;
import rstd.json;
import luato;
import lito.model;
import lito.build_layout;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

enum class ConfigureValueKind
{
    String,
    Integer,
    Boolean,
};

class ConfigureValue {
public:
    static auto from_string(String value) -> ConfigureValue {
        auto result    = ConfigureValue {};
        result.kind_   = ConfigureValueKind::String;
        result.string_ = rstd::move(value);
        return result;
    }
    static auto from_integer(i64 value) -> ConfigureValue {
        auto result     = ConfigureValue {};
        result.kind_    = ConfigureValueKind::Integer;
        result.integer_ = value;
        return result;
    }
    static auto from_boolean(bool value) -> ConfigureValue {
        auto result     = ConfigureValue {};
        result.kind_    = ConfigureValueKind::Boolean;
        result.boolean_ = value;
        return result;
    }
    auto kind() const noexcept -> ConfigureValueKind { return kind_; }
    auto string() const noexcept -> ref<str> { return string_.as_str(); }
    auto integer() const noexcept -> i64 { return integer_; }
    auto boolean() const noexcept -> bool { return boolean_; }

private:
    ConfigureValueKind kind_ { ConfigureValueKind::String };
    String             string_;
    i64                integer_ {};
    bool               boolean_ { false };
};

} // namespace lito

namespace lito
{

using ConfigureValues = rstd::collections::BTreeMap<String, ConfigureValue>;
using Json            = rstd::json::Value;

template<typename T>
auto script_failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Script, rstd::move(message)));
}

template<typename T>
auto script_failure(ref<str> message) -> Result<T> {
    return Err(Error::make(ErrorKind::Script, message));
}

auto normal_relative_path(String text, ref<str> context) -> Result<PathBuf> {
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

auto placeholder_name_is_valid(ref<str> name) noexcept -> bool {
    if (name.is_empty()) return false;
    auto first = name[usize()].to_primitive();
    auto alpha = (first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z');
    if (! (alpha || first == '_')) return false;
    for (auto index = usize(1); index < name.len(); ++index) {
        auto value  = name[index].to_primitive();
        auto letter = (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
        auto digit  = value >= '0' && value <= '9';
        if (! (letter || digit || value == '_')) return false;
    }
    return true;
}

void append_configure_value(String& output, const ConfigureValue& value) {
    switch (value.kind()) {
    case ConfigureValueKind::String: output.push_str(value.string()); break;
    case ConfigureValueKind::Integer: {
        auto text = rstd::format("{}", value.integer());
        output.push_str(text.as_str());
        break;
    }
    case ConfigureValueKind::Boolean:
        output.push_str(value.boolean() ? "true"_str : "false"_str);
        break;
    }
}

auto render_template(ref<str> input, const ConfigureValues& values, ref<rstd::path::Path> source)
    -> Result<String> {
    auto output        = String::make();
    auto used          = rstd::collections::BTreeMap<String, empty>::make();
    auto literal_begin = usize();
    auto index         = usize();
    while (index < input.len()) {
        if (input[index] != u8('@')) {
            ++index;
            continue;
        }
        auto literal = input.get(literal_begin, index);
        if (literal.is_some()) output.push_str(*literal);
        if (index + usize(1) < input.len() && input[index + usize(1)] == u8('@')) {
            output.push_ascii('@');
            index += usize(2);
            literal_begin = index;
            continue;
        }

        auto end = index + usize(1);
        while (end < input.len() && input[end] != u8('@')) ++end;
        if (end == input.len()) {
            return script_failure<String>(rstd::format(
                "template '{}' has an unclosed placeholder at byte {}", source, index));
        }
        auto name = input.get(index + usize(1), end);
        if (name.is_none() || ! placeholder_name_is_valid(*name)) {
            return script_failure<String>(
                rstd::format("template '{}' has an invalid placeholder at byte {}", source, index));
        }
        auto value = values.get(*name);
        if (value.is_none()) {
            return script_failure<String>(
                rstd::format("template '{}' is missing value '{}'", source, *name));
        }
        append_configure_value(output, **value);
        used.insert(String::make(*name), empty {});
        index         = end + usize(1);
        literal_begin = index;
    }
    auto tail = input.get(literal_begin, input.len());
    if (tail.is_some()) output.push_str(*tail);

    auto keys = values.keys();
    for (auto key = keys.next(); key.is_some(); key = keys.next()) {
        if (used.contains_key((**key).as_str())) continue;
        return script_failure<String>(
            rstd::format("template '{}' does not use value '{}'", source, (**key).as_str()));
    }
    return Ok(rstd::move(output));
}

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

auto load_receipt(ref<rstd::path::Path> path) -> Result<Vec<OwnedOutput>> {
    auto exists = rstd::fs::exists(path);
    if (exists.is_err()) {
        return script_failure<Vec<OwnedOutput>>(rstd::format(
            "cannot inspect configure receipt '{}': {}", path, rstd::move(exists).unwrap_err()));
    }
    if (! *exists) return Ok(Vec<OwnedOutput>::make());
    auto contents = rstd::fs::read_to_string(path);
    if (contents.is_err()) {
        return script_failure<Vec<OwnedOutput>>(rstd::format(
            "cannot read configure receipt '{}': {}", path, rstd::move(contents).unwrap_err()));
    }
    auto parsed = rstd::json::from_str(contents->as_str());
    if (parsed.is_err()) {
        return script_failure<Vec<OwnedOutput>>(rstd::format(
            "cannot parse configure receipt '{}': {}", path, rstd::move(parsed).unwrap_err()));
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

auto find_package_root(const PackageMetadata& metadata, ref<str> name)
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
    static auto create(const PackageMetadata& metadata,
                       const BuildLayout&     layout,
                       const Vec<String>&     selected_packages) -> Result<ConfigureSession> {
        auto packages = Vec<ConfigurePackage>::make();
        for (const auto& name : selected_packages) {
            auto source_root = find_package_root(metadata, name.as_str());
            if (source_root.is_none()) {
                return script_failure<ConfigureSession>(rstd::format(
                    "selected build-script package '{}' has no package root", name.as_str()));
            }
            auto generated = layout.create_generated_package_directory(name.as_str());
            if (generated.is_err()) {
                return script_failure<ConfigureSession>(rstd::move(generated).unwrap_err().message);
            }
            packages.push(ConfigurePackage {
                .name           = name.clone(),
                .source_root    = PathBuf::from(*source_root),
                .generated_root = rstd::move(generated).unwrap(),
            });
        }
        auto receipt  = layout.configure_receipt();
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
                                   rstd::move(current)));
    }

    auto configure(ref<str>               package_name,
                   String                 input_text,
                   String                 output_text,
                   const ConfigureValues& values) -> Result<ConfigureOutcome> {
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
            return script_failure<ConfigureOutcome>(
                rstd::format("cannot resolve configure_file input '{}': {}",
                             input_requested.as_path(),
                             rstd::move(input).unwrap_err()));
        }
        if (input->as_path().strip_prefix(owner->source_root.as_path()).is_none()) {
            return script_failure<ConfigureOutcome>(
                rstd::format("configure_file input '{}' escapes package '{}'",
                             input_requested.as_path(),
                             package_name));
        }
        auto input_metadata = rstd::fs::metadata(input->as_path());
        if (input_metadata.is_err() || ! input_metadata->is_file()) {
            return script_failure<ConfigureOutcome>(
                rstd::format("configure_file input '{}' is not a regular file", input->as_path()));
        }
        auto template_text = rstd::fs::read_to_string(input->as_path());
        if (template_text.is_err()) {
            return script_failure<ConfigureOutcome>(
                rstd::format("cannot read configure_file input '{}': {}",
                             input->as_path(),
                             rstd::move(template_text).unwrap_err()));
        }
        auto rendered = render_template(template_text->as_str(), values, input->as_path());
        if (rendered.is_err()) return Err(rstd::move(rendered).unwrap_err());

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
            return script_failure<ConfigureOutcome>(
                rstd::format("cannot create configure_file output parent '{}': {}",
                             *parent,
                             rstd::move(parent_created).unwrap_err()));
        }
        auto canonical_parent = rstd::fs::canonicalize(*parent);
        if (canonical_parent.is_err() ||
            canonical_parent->as_path().strip_prefix(owner->generated_root.as_path()).is_none()) {
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
                return script_failure<ConfigureOutcome>(rstd::format(
                    "cannot inspect configure_file output '{}': {}", output.as_path(), error));
            }
        }
        for (const auto& claimed : claimed_) {
            if (claimed.as_path() == output.as_path()) {
                return script_failure<ConfigureOutcome>(rstd::format(
                    "configure_file output '{}' is claimed more than once", output.as_path()));
            }
        }

        auto written =
            rstd::fs::write_atomic_if_changed(output.as_path(), rendered->as_str().as_bytes());
        if (written.is_err()) {
            return script_failure<ConfigureOutcome>(
                rstd::format("cannot write configure_file output '{}': {}",
                             output.as_path(),
                             rstd::move(written).unwrap_err()));
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

    auto finish() -> Result<BuildScriptReport> {
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
                return script_failure<BuildScriptReport>(rstd::format(
                    "cannot inspect stale configure output '{}': {}", requested.as_path(), error));
            }
            auto parent = requested.as_path().parent();
            if (parent.is_none()) {
                return script_failure<BuildScriptReport>(
                    "stale configure output has no parent"_str);
            }
            auto canonical_parent = rstd::fs::canonicalize(*parent);
            if (canonical_parent.is_err() || canonical_parent->as_path()
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
                return script_failure<BuildScriptReport>(
                    rstd::format("cannot remove stale configure output '{}': {}",
                                 requested.as_path(),
                                 rstd::move(removed).unwrap_err()));
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
            return script_failure<BuildScriptReport>(
                rstd::format("cannot create configure receipt directory '{}': {}",
                             *parent,
                             rstd::move(created).unwrap_err()));
        }
        auto text = encode_receipt(current_);
        auto written =
            rstd::fs::write_atomic_if_changed(receipt_.as_path(), text.as_str().as_bytes());
        if (written.is_err()) {
            return script_failure<BuildScriptReport>(
                rstd::format("cannot write configure receipt '{}': {}",
                             receipt_.as_path(),
                             rstd::move(written).unwrap_err()));
        }
        return Ok(rstd::move(report_));
    }

    auto report() noexcept -> BuildScriptReport& { return report_; }

private:
    ConfigureSession(Vec<ConfigurePackage> packages,
                     PathBuf               receipt,
                     Vec<OwnedOutput>      previous,
                     Vec<OwnedOutput>      current)
        : packages_(rstd::move(packages)),
          receipt_(rstd::move(receipt)),
          previous_(rstd::move(previous)),
          current_(rstd::move(current)) {}

    Vec<ConfigurePackage> packages_;
    PathBuf               receipt_;
    Vec<OwnedOutput>      previous_;
    Vec<OwnedOutput>      current_;
    Vec<PathBuf>          claimed_;
    BuildScriptReport     report_;
};

auto binding_error(Error error) -> luato::Error {
    return luato::Error::binding(rstd::move(error.message));
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
    auto package    = table.required<String>("package"_str);
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
        if (! placeholder_name_is_valid(entry.key.as_str())) {
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

auto materialize_generated_includes(PackageMetadata&             metadata,
                                    const BuildLayout&           layout,
                                    const SourceTargetSelection& selection) -> Result<empty> {
    for (auto target_id : selection.target_order) {
        auto& target = metadata.targets[target_id];
        for (const auto& requirement : target.usage.private_include_directory_requirements) {
            if (requirement.root != IncludeDirectoryRoot::Generated) continue;
            auto generated = layout.generated_package_directory(target.id.package.as_str());
            if (generated.is_err()) {
                return script_failure<empty>(rstd::move(generated).unwrap_err().message);
            }
            auto requested = generated->join(requirement.path.as_path());
            auto canonical = rstd::fs::canonicalize(requested.as_path());
            if (canonical.is_err()) {
                return script_failure<empty>(
                    rstd::format("generated private include directory '{}' does not exist",
                                 requested.as_path()));
            }
            if (canonical->as_path().strip_prefix(generated->as_path()).is_none()) {
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

auto has_generated_includes(const PackageMetadata&       metadata,
                            const SourceTargetSelection& selection) noexcept -> bool {
    for (auto target : selection.target_order) {
        if (! metadata.targets[target].usage.private_include_directory_requirements.is_empty()) {
            return true;
        }
    }
    return false;
}

} // namespace lito

export namespace lito
{

auto execute_build_script(PackageMetadata&             metadata,
                          const BuildLayout&           layout,
                          ref<str>                     profile,
                          const Vec<String>&           selected_packages,
                          const SourceTargetSelection& selection,
                          const Option<BuildObserver>& observer) -> Result<BuildScriptReport> {
    auto script = metadata.root.join(PathBuf::from("build.lua"_str).as_path());
    auto exists = rstd::fs::exists(script.as_path());
    if (exists.is_err()) {
        return script_failure<BuildScriptReport>(
            rstd::format("cannot inspect build script '{}': {}",
                         script.as_path(),
                         rstd::move(exists).unwrap_err()));
    }
    if (! *exists) {
        if (has_generated_includes(metadata, selection)) {
            return script_failure<BuildScriptReport>(
                rstd::format("generated private include directories require build script '{}'",
                             script.as_path()));
        }
        return Ok(BuildScriptReport {});
    }
    auto script_metadata = rstd::fs::metadata(script.as_path());
    if (script_metadata.is_err() || ! script_metadata->is_file()) {
        return script_failure<BuildScriptReport>(
            rstd::format("build script '{}' is not a regular file", script.as_path()));
    }
    auto session = ConfigureSession::create(metadata, layout, selected_packages);
    if (session.is_err()) return Err(rstd::move(session).unwrap_err());
    auto configure = rstd::move(session).unwrap();

    auto state = luato::State::create(luato::StateOptions::base());
    if (state.is_err()) {
        auto error = rstd::move(state).unwrap_err_unchecked();
        return script_failure<BuildScriptReport>(
            rstd::format("cannot create Lua state: {}", error.message.as_str()));
    }
    auto lua    = rstd::move(state).unwrap_unchecked();
    auto module = luato::ModuleSpec(String::make("lito"_str));
    module.set(String::make("profile"_str), String::make(profile));
    auto root_text = metadata.root.as_path().to_str();
    if (root_text.is_none()) {
        return script_failure<BuildScriptReport>("project root is not valid UTF-8"_str);
    }
    module.set(String::make("project_root"_str), String::make(*root_text));
    module.add(luato::NativeFunctionSpec::make(
        String::make("configure_file"_str),
        usize(1),
        [&configure](luato::CallFrame& frame) -> luato::BindingResult {
            return configure_callback(configure, frame);
        }));
    auto registered = lua.register_module(rstd::move(module));
    if (registered.is_err()) {
        auto error = rstd::move(registered).unwrap_err_unchecked();
        return script_failure<BuildScriptReport>(
            rstd::format("cannot register build script API: {}", error.message.as_str()));
    }
    auto executed = lua.execute_file(script.as_path());
    if (executed.is_err()) {
        auto error = rstd::move(executed).unwrap_err_unchecked();
        auto message =
            rstd::format("build script '{}': {}", script.as_path(), error.message.as_str());
        if (! error.traceback.is_empty()) {
            message.push_str("\n"_str);
            message.push_str(error.traceback.as_str());
        }
        return script_failure<BuildScriptReport>(rstd::move(message));
    }
    configure.report().executed = true;
    configure.report().elapsed  = executed->elapsed;
    auto finished               = configure.finish();
    if (finished.is_err()) return Err(rstd::move(finished).unwrap_err());
    auto materialized = materialize_generated_includes(metadata, layout, selection);
    if (materialized.is_err()) return Err(rstd::move(materialized).unwrap_err());
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

} // namespace lito
