module;
#include <rstd/macro.hpp>

module lito.driver:build.configure;

import rstd;
import lito.core;
import lito.cpp;
import lito.system;
import :build.script.support;
import :build.layout;
import :build.event;
import :build.artifact;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::system;

namespace lito
{

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
                return Err(BuildScriptError::Message(rstd::format(
                    "selected build-script package '{}' has no package root", name.as_str())));
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
            return Err(BuildScriptError::Message(
                rstd::format("package '{}' is not a selected root package", package_name)));
        }

        auto input_relative =
            normal_relative_path(rstd::move(input_text), "configure_file.input"_str);
        if (input_relative.is_err()) return Err(rstd::move(input_relative).unwrap_err());
        auto input_requested = owner->source_root.join(input_relative->as_path());
        auto input           = rstd::fs::canonicalize(input_requested.as_path());
        if (input.is_err()) {
            return Err(BuildScriptError::Io("resolve configure_file input"_Str,
                                            PathBuf::from(input_requested.as_path()),
                                            rstd::move(input).unwrap_err()));
        }
        if (input->as_path().strip_prefix(owner->source_root.as_path()).is_none()) {
            return Err(BuildScriptError::Message(
                rstd::format("configure_file input '{}' escapes package '{}'",
                             input_requested.as_path(),
                             package_name)));
        }
        auto input_metadata = rstd::fs::metadata(input->as_path());
        if (input_metadata.is_err()) {
            return Err(BuildScriptError::Io("inspect configure_file input"_Str,
                                            PathBuf::from(input->as_path()),
                                            rstd::move(input_metadata).unwrap_err()));
        }
        if (! input_metadata->is_file()) {
            return Err(BuildScriptError::Message(
                rstd::format("configure_file input '{}' is not a regular file", input->as_path())));
        }
        auto template_text = rstd::fs::read_to_string(input->as_path());
        if (template_text.is_err()) {
            return Err(BuildScriptError::Io("read configure_file input"_Str,
                                            PathBuf::from(input->as_path()),
                                            rstd::move(template_text).unwrap_err()));
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
            return Err(BuildScriptError::Message("configure_file output has no parent"_Str));
        }
        auto parent_created = rstd::fs::create_dir_all(*parent);
        if (parent_created.is_err()) {
            return Err(BuildScriptError::Io("create configure_file output parent"_Str,
                                            PathBuf::from(*parent),
                                            rstd::move(parent_created).unwrap_err()));
        }
        auto canonical_parent = rstd::fs::canonicalize(*parent);
        if (canonical_parent.is_err()) {
            return Err(BuildScriptError::Io("resolve configure_file output parent"_Str,
                                            PathBuf::from(*parent),
                                            rstd::move(canonical_parent).unwrap_err()));
        }
        if (canonical_parent->as_path().strip_prefix(owner->generated_root.as_path()).is_none()) {
            return Err(BuildScriptError::Message(rstd::format(
                "configure_file output '{}' escapes generated package root", requested.as_path())));
        }
        auto file_name = requested.as_path().file_name();
        if (file_name.is_none()) {
            return Err(BuildScriptError::Message("configure_file output has no file name"_Str));
        }
        auto output   = canonical_parent->join(PathBuf::from(*file_name).as_path());
        auto existing = rstd::fs::symlink_metadata(output.as_path());
        if (existing.is_ok() && (! existing->is_file() || existing->is_symlink())) {
            return Err(BuildScriptError::Message(rstd::format(
                "configure_file output '{}' is not a regular non-symlink file", output.as_path())));
        }
        if (existing.is_err()) {
            auto error = rstd::move(existing).unwrap_err();
            if (error.kind() !=
                rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound }) {
                return Err(BuildScriptError::Io("inspect configure_file output"_Str,
                                                PathBuf::from(output.as_path()),
                                                rstd::move(error)));
            }
        }
        for (const auto& claimed : claimed_) {
            if (claimed.as_path() == output.as_path()) {
                return Err(BuildScriptError::Message(rstd::format(
                    "configure_file output '{}' is claimed more than once", output.as_path())));
            }
        }
        rstd_try(output_registry_->claim(
            owner->name.as_str(), output_relative->as_path(), script_owner_.as_str()));

        auto written =
            rstd::fs::write_atomic_if_changed(output.as_path(), rendered->as_str().as_bytes());
        if (written.is_err()) {
            return Err(BuildScriptError::Io("write configure_file output"_Str,
                                            PathBuf::from(output.as_path()),
                                            rstd::move(written).unwrap_err()));
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
                return Err(BuildScriptError::Io("inspect stale configure output"_Str,
                                                PathBuf::from(requested.as_path()),
                                                rstd::move(error)));
            }
            auto parent = requested.as_path().parent();
            if (parent.is_none()) {
                return Err(BuildScriptError::Message("stale configure output has no parent"_Str));
            }
            auto canonical_parent = rstd::fs::canonicalize(*parent);
            if (canonical_parent.is_err()) {
                return Err(BuildScriptError::Io("resolve stale configure output parent"_Str,
                                                PathBuf::from(*parent),
                                                rstd::move(canonical_parent).unwrap_err()));
            }
            if (canonical_parent->as_path()
                    .strip_prefix(owner->generated_root.as_path())
                    .is_none()) {
                return Err(BuildScriptError::Message(
                    rstd::format("stale configure output '{}' escapes generated package root",
                                 requested.as_path())));
            }
            if (! metadata->is_file() || metadata->is_symlink()) {
                return Err(BuildScriptError::Message(
                    rstd::format("stale configure output '{}' is not an owned regular file",
                                 requested.as_path())));
            }
            auto removed = rstd::fs::remove_file(requested.as_path());
            if (removed.is_err()) {
                return Err(BuildScriptError::Io("remove stale configure output"_Str,
                                                PathBuf::from(requested.as_path()),
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
        if (parent.is_none()) return Err(BuildScriptError::Message("receipt has no parent"_Str));
        auto created = rstd::fs::create_dir_all(*parent);
        if (created.is_err()) {
            return Err(BuildScriptError::Io("create configure receipt directory"_Str,
                                            PathBuf::from(*parent),
                                            rstd::move(created).unwrap_err()));
        }
        auto text = encode_receipt(current_);
        auto written =
            rstd::fs::write_atomic_if_changed(receipt_.as_path(), text.as_str().as_bytes());
        if (written.is_err()) {
            return Err(BuildScriptError::Io("write configure receipt"_Str,
                                            PathBuf::from(receipt_.as_path()),
                                            rstd::move(written).unwrap_err()));
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

} // namespace lito
