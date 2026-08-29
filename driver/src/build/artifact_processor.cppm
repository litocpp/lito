module;
#include <rstd/macro.hpp>

export module lito.driver:build.artifact_processor;

import rstd;
import rstd.json;
import licrypto;
import lito.core;
import lito.cpp;
import lito.system;
import :build.artifact;
import :build.layout;
import :build.artifact_processor_error;

using namespace rstd::prelude;
using namespace rstd::literals;
using Json = rstd::json::Value;

namespace lito
{

template<typename T>
auto processor_failure(String message) -> ArtifactProcessorResult<T> {
    return Err(ArtifactProcessorError::Invalid(rstd::move(message)));
}

template<typename T>
auto processor_failure(ref<str> message) -> ArtifactProcessorResult<T> {
    return processor_failure<T>(String::make(message));
}

template<typename T>
auto processor_io_failure(ref<str>               operation,
                          ref<rstd::path::Path>  path,
                          rstd::io::error::Error error) -> ArtifactProcessorResult<T> {
    return Err(ArtifactProcessorError::System(lito::system::SystemError::Io(
        String::make(operation), PathBuf::from(path), rstd::move(error))));
}

auto processor_file_digest(ref<rstd::path::Path> path) -> ArtifactProcessorResult<String> {
    auto bytes = rstd::fs::read(path);
    if (bytes.is_err()) {
        return processor_io_failure<String>(
            "read artifact processor file"_str, path, rstd::move(bytes).unwrap_err());
    }
    return Ok(licrypto::sha256_hex(bytes->as_slice()));
}

struct ArtifactProcessorFileDeclaration {
    PathBuf          relative;
    ArtifactFileRole role { ArtifactFileRole::Runtime };
    String           content_type;
    bool             primary { false };
    bool             publish { true };
    String           digest;
};

struct ArtifactProcessorFiles {
    Vec<ArtifactProcessorFileDeclaration> declarations;
    String                                identity;
};

auto parse_processor_response(ref<rstd::path::Path> root, ref<rstd::path::Path> response)
    -> ArtifactProcessorResult<ArtifactProcessorFiles> {
    auto contents = rstd::fs::read_to_string(response);
    if (contents.is_err()) {
        return processor_io_failure<ArtifactProcessorFiles>(
            "read artifact processor response"_str, response, rstd::move(contents).unwrap_err());
    }
    auto parsed = rstd::json::from_str(contents->as_str());
    if (parsed.is_err()) {
        return Err(
            ArtifactProcessorError::Json(PathBuf::from(response), rstd::move(parsed).unwrap_err()));
    }
    auto object = parsed->as_object();
    if (object.is_none() || (**object).len() != usize(2)) {
        return processor_failure<ArtifactProcessorFiles>(
            "artifact processor response must contain only protocol and files"_str);
    }
    auto protocol = parsed->get("protocol"_str);
    auto files    = parsed->get("files"_str);
    if (protocol.is_none() || (**protocol).as_u64() != Some(u64(1)) || files.is_none() ||
        (**files).as_array().is_none() || (**(**files).as_array()).is_empty()) {
        return processor_failure<ArtifactProcessorFiles>(
            "artifact processor response has an unsupported protocol or empty file set"_str);
    }

    auto canonical_root = rstd::fs::canonicalize(root);
    if (canonical_root.is_err()) {
        return processor_io_failure<ArtifactProcessorFiles>(
            "resolve artifact processor output directory"_str,
            root,
            rstd::move(canonical_root).unwrap_err());
    }
    auto declarations  = Vec<ArtifactProcessorFileDeclaration>::make();
    auto primary_count = usize {};
    for (const auto& value : **(**files).as_array()) {
        auto item = value.as_object();
        if (item.is_none() || (**item).len() != usize(5)) {
            return processor_failure<ArtifactProcessorFiles>(
                "artifact processor file entry must contain path, role, content-type, primary, "
                "and publish"_str);
        }
        auto path_value    = value.get("path"_str);
        auto role_value    = value.get("role"_str);
        auto content_value = value.get("content-type"_str);
        auto primary_value = value.get("primary"_str);
        auto publish_value = value.get("publish"_str);
        if (path_value.is_none() || (**path_value).as_str().is_none() || role_value.is_none() ||
            (**role_value).as_str().is_none() || content_value.is_none() ||
            (**content_value).as_str().is_none() || primary_value.is_none() ||
            (**primary_value).as_bool().is_none() || publish_value.is_none() ||
            (**publish_value).as_bool().is_none()) {
            return processor_failure<ArtifactProcessorFiles>(
                "artifact processor file entry has an invalid field type"_str);
        }
        auto relative = PathBuf::from(*(**path_value).as_str());
        if (! relative.as_path().is_safe_relative()) {
            return processor_failure<ArtifactProcessorFiles>(
                rstd::format("artifact processor output path '{}' is not a safe relative path",
                             relative.as_path()));
        }
        if (relative.as_path() == PathBuf::from("response.json"_str).as_path() ||
            relative.as_path() == PathBuf::from("receipt.json"_str).as_path()) {
            return processor_failure<ArtifactProcessorFiles>(rstd::format(
                "artifact processor output path '{}' is reserved", relative.as_path()));
        }
        for (const auto& prior : declarations) {
            if (prior.relative.as_path() == relative.as_path()) {
                return processor_failure<ArtifactProcessorFiles>(
                    rstd::format("artifact processor output path '{}' is declared more than once",
                                 relative.as_path()));
            }
        }
        auto role = artifact_file_role_from_name(*(**role_value).as_str());
        if (role.is_none()) {
            return processor_failure<ArtifactProcessorFiles>(rstd::format(
                "artifact processor output role '{}' is unknown", *(**role_value).as_str()));
        }
        auto content_type = String::make(*(**content_value).as_str());
        if (content_type.is_empty()) {
            return processor_failure<ArtifactProcessorFiles>(
                "artifact processor output content-type must not be empty"_str);
        }
        auto requested = PathBuf::from(root).join(relative.as_path());
        auto metadata  = rstd::fs::symlink_metadata(requested.as_path());
        if (metadata.is_err()) {
            return processor_io_failure<ArtifactProcessorFiles>(
                "inspect artifact processor output"_str,
                requested.as_path(),
                rstd::move(metadata).unwrap_err());
        }
        if (! metadata->is_file() || metadata->is_symlink()) {
            return processor_failure<ArtifactProcessorFiles>(
                rstd::format("artifact processor output '{}' is not a regular non-symlink file",
                             requested.as_path()));
        }
        auto canonical = rstd::fs::canonicalize(requested.as_path());
        if (canonical.is_err()) {
            return processor_io_failure<ArtifactProcessorFiles>(
                "resolve artifact processor output"_str,
                requested.as_path(),
                rstd::move(canonical).unwrap_err());
        }
        if (canonical->as_path().strip_prefix(canonical_root->as_path()).is_none()) {
            return processor_failure<ArtifactProcessorFiles>(
                rstd::format("artifact processor output '{}' escapes its staging directory",
                             requested.as_path()));
        }
        const auto is_primary = *(**primary_value).as_bool();
        if (is_primary) ++primary_count;
        declarations.push(ArtifactProcessorFileDeclaration {
            .relative     = rstd::move(relative),
            .role         = *role,
            .content_type = rstd::move(content_type),
            .primary      = is_primary,
            .publish      = *(**publish_value).as_bool(),
            .digest       = rstd_try(processor_file_digest(canonical->as_path())),
        });
    }
    if (primary_count != usize(1)) {
        return processor_failure<ArtifactProcessorFiles>(
            "artifact processor response must declare exactly one primary file"_str);
    }
    for (const auto& declaration : declarations) {
        if (declaration.primary &&
            (declaration.role != ArtifactFileRole::Runtime || ! declaration.publish)) {
            return processor_failure<ArtifactProcessorFiles>(
                "artifact processor primary file must be a published runtime"_str);
        }
    }
    auto identity = String::make("lito-artifact-processor-output-v1\n"_str);
    for (const auto& declaration : declarations) {
        identity.push_str(declaration.relative.as_path().to_string_lossy().as_str());
        identity.push_ascii('\n');
        identity.push_str(artifact_file_role_name(declaration.role));
        identity.push_ascii('\n');
        identity.push_str(declaration.content_type.as_str());
        identity.push_ascii('\n');
        identity.push_str(declaration.primary ? "primary\n"_str : "companion\n"_str);
        identity.push_str(declaration.publish ? "publish\n"_str : "private\n"_str);
        identity.push_str(declaration.digest.as_str());
        identity.push_ascii('\n');
    }
    return Ok(ArtifactProcessorFiles {
        .declarations = rstd::move(declarations),
        .identity     = licrypto::sha256_hex(identity.as_str()),
    });
}

auto artifact_processor_receipt(ref<str> identity, ref<str> output_identity) -> String {
    auto document = rstd::json::Map::make();
    document.insert(String::make("protocol"_str),
                    Json::Number(rstd::json::Number::from_u64(u64(1))));
    document.insert(String::make("identity"_str), Json::String(String::make(identity)));
    document.insert(String::make("output-identity"_str),
                    Json::String(String::make(output_identity)));
    auto text = rstd::json::to_string(Json::Object(rstd::move(document)),
                                      rstd::json::FormatOptions {
                                          .pretty = true,
                                          .indent = usize(2),
                                      });
    text.push_ascii('\n');
    return text;
}

auto cached_processor_files(ref<rstd::path::Path> root, ref<str> identity)
    -> ArtifactProcessorResult<Option<ArtifactProcessorFiles>> {
    auto response = PathBuf::from(root).join(PathBuf::from("response.json"_str).as_path());
    auto receipt  = PathBuf::from(root).join(PathBuf::from("receipt.json"_str).as_path());
    auto contents = rstd::fs::read_to_string(receipt.as_path());
    if (contents.is_err()) return Ok(None());
    auto parsed = rstd::json::from_str(contents->as_str());
    if (parsed.is_err() || parsed->as_object().is_none() ||
        (**parsed->as_object()).len() != usize(3)) {
        return Ok(None());
    }
    auto protocol        = parsed->get("protocol"_str);
    auto recorded        = parsed->get("identity"_str);
    auto output_identity = parsed->get("output-identity"_str);
    if (protocol.is_none() || (**protocol).as_u64() != Some(u64(1)) || recorded.is_none() ||
        (**recorded).as_str() != Some(identity) || output_identity.is_none() ||
        (**output_identity).as_str().is_none()) {
        return Ok(None());
    }
    auto files = parse_processor_response(root, response.as_path());
    if (files.is_err() || files->identity.as_str() != *(**output_identity).as_str()) {
        return Ok(None());
    }
    return Ok(Some(rstd::move(files).unwrap()));
}

auto materialize_processor_artifact(BuiltArtifact          raw,
                                    ArtifactProcessorFiles files,
                                    ref<rstd::path::Path>  root) -> BuiltArtifact {
    auto primary    = BuiltArtifactFile {};
    auto companions = Vec<BuiltArtifactFile>::with_capacity(files.declarations.len() - usize(1));
    for (auto& declaration : files.declarations) {
        auto file = BuiltArtifactFile {
            .role             = declaration.role,
            .path             = PathBuf::from(root).join(declaration.relative.as_path()),
            .content_type     = rstd::move(declaration.content_type),
            .content_identity = rstd::move(declaration.digest),
            .publish          = declaration.publish,
        };
        if (declaration.primary)
            primary = rstd::move(file);
        else
            companions.push(rstd::move(file));
    }
    raw.primary    = rstd::move(primary);
    raw.companions = rstd::move(companions);
    raw.format     = lito::artifact::Format::WebAssembly;
    return raw;
}

auto artifact_processor_identity(const BuiltArtifact&                   raw,
                                 const BuiltArtifact&                   processor,
                                 const lito::config::WasmToolchainSpec& options,
                                 ref<str>                               profile,
                                 ref<str>                               target,
                                 ref<str>                               raw_digest,
                                 ref<str> processor_digest) -> String {
    auto data = String::make("lito-artifact-processor-v1\n"_str);
    data.push_str(lito::package::package_target_id_text(raw.target).as_str());
    data.push_ascii('\n');
    data.push_str(profile);
    data.push_ascii('\n');
    data.push_str(target);
    data.push_ascii('\n');
    data.push_str(options.entry == lito::config::WasmEntry::None ? "entry=none\n"_str
                                                                 : "entry=main\n"_str);
    data.push_str(options.export_memory ? "export-memory=true\n"_str : "export-memory=false\n"_str);
    data.push_str(raw_digest);
    data.push_ascii('\n');
    data.push_str(lito::package::package_target_id_text(processor.target).as_str());
    data.push_ascii('\n');
    data.push_str(processor.link_identity.as_str());
    data.push_ascii('\n');
    data.push_str(processor_digest);
    data.push_ascii('\n');
    return licrypto::sha256_hex(data.as_str());
}

} // namespace lito

export namespace lito
{

struct ArtifactProcessorOutcome {
    BuiltArtifact artifact;
    bool          reused { false };
};

auto execute_artifact_processor(BuiltArtifact                                   raw,
                                const BuiltArtifact&                            processor,
                                const lito::config::WasmToolchainSpec&          options,
                                const BuildLayout&                              layout,
                                const lito::system::ResolvedProcessEnvironment& environment,
                                ref<str>                                        profile,
                                ref<str>                                        target)
    -> ArtifactProcessorResult<ArtifactProcessorOutcome> {
    auto raw_digest       = rstd_try(processor_file_digest(raw.primary.path.as_path()));
    auto processor_digest = rstd_try(processor_file_digest(processor.primary.path.as_path()));
    auto identity         = artifact_processor_identity(
        raw, processor, options, profile, target, raw_digest.as_str(), processor_digest.as_str());
    auto final  = layout.artifact_processor_result(raw.target, identity.as_str());
    auto cached = rstd_try(cached_processor_files(final.as_path(), identity.as_str()));
    if (cached.is_some()) {
        return Ok(ArtifactProcessorOutcome {
            .artifact = materialize_processor_artifact(
                rstd::move(raw), rstd::move(*cached), final.as_path()),
            .reused = true,
        });
    }

    auto staging_identity = identity.clone();
    staging_identity.push_str(rstd::format(".staging-{}", rstd::process::id()).as_str());
    auto staging = layout.artifact_processor_result(raw.target, staging_identity.as_str());
    auto exists  = rstd::fs::exists(staging.as_path());
    if (exists.is_err()) {
        return processor_io_failure<ArtifactProcessorOutcome>(
            "inspect artifact processor staging directory"_str,
            staging.as_path(),
            rstd::move(exists).unwrap_err());
    }
    if (*exists) {
        auto removed = rstd::fs::remove_dir_all(staging.as_path());
        if (removed.is_err()) {
            return processor_io_failure<ArtifactProcessorOutcome>(
                "remove stale artifact processor staging directory"_str,
                staging.as_path(),
                rstd::move(removed).unwrap_err());
        }
    }
    auto created = rstd::fs::create_dir_all(staging.as_path());
    if (created.is_err()) {
        return processor_io_failure<ArtifactProcessorOutcome>(
            "create artifact processor staging directory"_str,
            staging.as_path(),
            rstd::move(created).unwrap_err());
    }
    auto response = staging.join(PathBuf::from("response.json"_str).as_path());
    auto command  = rstd::process::Command::make(processor.primary.path.as_path().as_os_str());
    command.arg("--lito-artifact-processor"_str)
        .arg("1"_str)
        .arg("--input"_str)
        .arg(raw.primary.path.as_path().as_os_str())
        .arg("--output"_str)
        .arg(staging.as_path().as_os_str())
        .arg("--response"_str)
        .arg(response.as_path().as_os_str())
        .arg("--target"_str)
        .arg(target)
        .arg("--profile"_str)
        .arg(profile)
        .arg("--package"_str)
        .arg(raw.target.package.as_str())
        .arg("--kind"_str)
        .arg(lito::package::package_target_kind_name(raw.target.kind))
        .arg("--name"_str)
        .arg(raw.target.name.as_str());
    command.current_dir(raw.package_root.as_path());
    lito::system::apply_command_environment(command, environment);
    auto status = command.status();
    if (status.is_err()) {
        return processor_io_failure<ArtifactProcessorOutcome>("execute artifact processor"_str,
                                                              processor.primary.path.as_path(),
                                                              rstd::move(status).unwrap_err());
    }
    if (! status->success()) {
        auto code = status->code();
        return Err(ArtifactProcessorError::Execution(processor.primary.path.clone(),
                                                     code.is_some() ? *code : i32(-1)));
    }
    auto files        = rstd_try(parse_processor_response(staging.as_path(), response.as_path()));
    auto receipt      = staging.join(PathBuf::from("receipt.json"_str).as_path());
    auto receipt_text = artifact_processor_receipt(identity.as_str(), files.identity.as_str());
    auto written      = rstd::fs::write_atomic(receipt.as_path(), receipt_text.as_str().as_bytes());
    if (written.is_err()) {
        return processor_io_failure<ArtifactProcessorOutcome>(
            "write artifact processor receipt"_str,
            receipt.as_path(),
            rstd::move(written).unwrap_err());
    }
    auto final_exists = rstd::fs::exists(final.as_path());
    if (final_exists.is_err()) {
        return processor_io_failure<ArtifactProcessorOutcome>(
            "inspect artifact processor result directory"_str,
            final.as_path(),
            rstd::move(final_exists).unwrap_err());
    }
    if (*final_exists) {
        auto removed = rstd::fs::remove_dir_all(final.as_path());
        if (removed.is_err()) {
            return processor_io_failure<ArtifactProcessorOutcome>(
                "replace artifact processor result directory"_str,
                final.as_path(),
                rstd::move(removed).unwrap_err());
        }
    }
    auto published = rstd::fs::rename(staging.as_path(), final.as_path());
    if (published.is_err()) {
        return processor_io_failure<ArtifactProcessorOutcome>(
            "publish artifact processor result directory"_str,
            final.as_path(),
            rstd::move(published).unwrap_err());
    }
    return Ok(ArtifactProcessorOutcome {
        .artifact =
            materialize_processor_artifact(rstd::move(raw), rstd::move(files), final.as_path()),
    });
}

} // namespace lito
