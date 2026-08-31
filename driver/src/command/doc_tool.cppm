module;
#include <rstd/macro.hpp>

module lito.driver:command.doc_tool;

import rstd;
import licrypto;
import lito.tools;
import rstd.json;
import lito.core;
import :build;
import :build.event;
import :command.doc.event;
import :command.doc.request;
import :command.doc_error;
import :package;
import lito.cpp;
import lito.system;
import lito.toolchain;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;
using Json    = rstd::json::Value;
using JsonMap = rstd::json::Map;

namespace lito
{

inline constexpr auto LITODOC_REPOSITORY = "https://github.com/litocpp/litodoc.git"_str;
inline constexpr auto LITODOC_COMMIT     = "56dd1b4d1a0c2145b296690c2c58850a5004032c"_str;

template<typename T>
auto doc_tool_failure(String message) -> DocResult<T> {
    return Err(DocError::Message(rstd::move(message)));
}

template<typename T>
auto doc_tool_failure(ref<str> message) -> DocResult<T> {
    return doc_tool_failure<T>(String::make(message));
}

auto json_protocol_contains(const Json& value, ref<str> key, u64 expected) -> bool {
    auto member = value.get(key);
    if (member.is_none() || (**member).as_array().is_none()) return false;
    for (const auto& item : **(**member).as_array()) {
        if (item.as_u64() == Some(expected)) return true;
    }
    return false;
}

auto json_feature_contains(const Json& value, ref<str> expected) -> bool {
    auto member = value.get("features"_str);
    if (member.is_none() || (**member).as_array().is_none()) return false;
    for (const auto& item : **(**member).as_array()) {
        if (item.as_str() == Some(expected)) return true;
    }
    return false;
}

struct DocToolCapabilities {
    String litodoc_build;
    String clang_version;
    String clang_build;
    String json;
};

auto parse_capabilities(String                  text,
                        ref<rstd::path::Path>   executable,
                        const CompilerIdentity& compiler) -> DocResult<DocToolCapabilities> {
    auto parsed = rstd::json::from_str(text.as_str());
    if (parsed.is_err()) {
        return Err(DocError::Json(PathBuf::from(executable), rstd::move(parsed).unwrap_err()));
    }
    auto format        = parsed->get("format"_str);
    auto version       = parsed->get("version"_str);
    auto build         = parsed->get("litodoc_build"_str);
    auto clang_version = parsed->get("clang_version"_str);
    auto clang_build   = parsed->get("clang_build"_str);
    if (format.is_none() || (**format).as_str() != Some("litodoc-capabilities"_str) ||
        version.is_none() || (**version).as_u64() != Some(u64(1)) || build.is_none() ||
        (**build).as_str().is_none() || clang_version.is_none() ||
        (**clang_version).as_str().is_none() || clang_build.is_none() ||
        (**clang_build).as_str().is_none() ||
        ! json_protocol_contains(*parsed, "extract_protocols"_str, u64(1)) ||
        ! json_protocol_contains(*parsed, "site_manifest_versions"_str, u64(1)) ||
        ! json_protocol_contains(*parsed, "data_api_versions"_str, u64(4)) ||
        ! json_feature_contains(*parsed, "embedded-default-frontend"_str) ||
        ! json_feature_contains(*parsed, "package-publications-v1"_str)) {
        return Err(DocError::Protocol(PathBuf::from(executable),
                                      String::make("unsupported litodoc capabilities"_str)));
    }
    if (! compiler.version.as_str().contains(*(**clang_build).as_str())) {
        return Err(
            DocError::Protocol(PathBuf::from(executable),
                               rstd::format("litodoc Clang '{}' does not match compiler '{}'",
                                            *(**clang_build).as_str(),
                                            compiler.version.as_str())));
    }
    return Ok(DocToolCapabilities {
        .litodoc_build = String::make(*(**build).as_str()),
        .clang_version = String::make(*(**clang_version).as_str()),
        .clang_build   = String::make(*(**clang_build).as_str()),
        .json          = rstd::move(text),
    });
}

auto probe_doc_tool(ref<rstd::path::Path>             executable,
                    const CompilerIdentity&           compiler,
                    const ResolvedProcessEnvironment& environment)
    -> DocResult<DocToolCapabilities> {
    auto executable_text = executable.to_str();
    if (executable_text.is_none()) {
        return doc_tool_failure<DocToolCapabilities>(
            rstd::format("litodoc executable '{}' is not valid UTF-8", executable));
    }
    auto arguments = Vec<String>::make();
    arguments.push(String::make(*executable_text));
    arguments.push(String::make("capabilities"_str));
    arguments.push(String::make("--json"_str));
    auto executed = run_command(arguments, environment);
    if (executed.is_err()) return Err(rstd::into<DocError>(rstd::move(executed).unwrap_err()));
    if (executed->exit_code != i32 {}) {
        return Err(DocError::Execution(String::make("litodoc capabilities"_str),
                                       PathBuf::from(executable),
                                       executed->exit_code,
                                       rstd::move(executed->standard_output),
                                       rstd::move(executed->standard_error)));
    }
    return parse_capabilities(rstd::move(executed->standard_output), executable, compiler);
}

auto tool_source_requirement(const lito::config::DocConfig& config)
    -> lito::source::PackageSourceRequirement {
    if (config.litodoc_path.is_some()) {
        return lito::source::PackageSourceRequirement::Path(config.litodoc_path->clone());
    }
    return lito::source::PackageSourceRequirement::Git(
        String::make(LITODOC_REPOSITORY),
        lito::source::GitReference { .kind  = lito::source::GitReferenceKind::Commit,
                                     .value = String::make(LITODOC_COMMIT) });
}

auto acquire_doc_tool_source(const BuildRequest&               request,
                             const lito::config::DocConfig&    config,
                             lito::tools::ToolResolver&        resolver,
                             const ResolvedProcessEnvironment& environment)
    -> DocResult<lito::source::AcquiredSource> {
    auto options = lito::source::SourceResolutionOptions {
        .locked = false,
        .git    = lito::source::GitResolutionMode::ReuseLocked,
        .sources =
            lito::source::PackageSourceConfig {
                .source_bundles = as<Clone>(request.sources.source_bundles).clone(),
                .network        = request.sources.network,
            },
    };
    auto manager  = lito::source::SourceManager(request.selection.root.as_path(),
                                                rstd::move(options),
                                                resolver,
                                                environment,
                                                source_observer(request.observer));
    auto source   = tool_source_requirement(config);
    auto acquired = manager.acquire_external(source, request.selection.root.as_path());
    if (acquired.is_err()) return Err(rstd::into<DocError>(rstd::move(acquired).unwrap_err()));
    return Ok(rstd::move(acquired).unwrap());
}

auto create_doc_tool_root(ref<str> key) -> DocResult<PathBuf> {
    auto data = LitoDataRoot::resolve();
    if (data.is_err()) return Err(rstd::into<DocError>(rstd::move(data).unwrap_err()));
    auto root    = PathBuf::from(data->root())
                       .join(PathBuf::from("tools/litodoc"_str).as_path())
                       .join(PathBuf::from(key).as_path());
    auto created = rstd::fs::create_dir_all(root.as_path());
    if (created.is_err()) {
        return Err(doc_io_failure(
            "create litodoc tool cache"_str, root.as_path(), rstd::move(created).unwrap_err()));
    }
    auto canonical = rstd::fs::canonicalize(root.as_path());
    if (canonical.is_err()) {
        return Err(doc_io_failure(
            "resolve litodoc tool cache"_str, root.as_path(), rstd::move(canonical).unwrap_err()));
    }
    return Ok(rstd::move(canonical).unwrap());
}

auto acquire_doc_tool_lock(ref<rstd::path::Path> root) -> DocResult<rstd::fs::FileLock> {
    auto path = PathBuf::from(root).join(PathBuf::from("build.lock"_str).as_path());
    auto opened =
        rstd::fs::OpenOptions::make().read(true).write(true).create(true).open(path.as_path());
    if (opened.is_err()) {
        return Err(doc_io_failure(
            "open litodoc tool lock"_str, path.as_path(), rstd::move(opened).unwrap_err()));
    }
    auto locked =
        rstd::fs::FileLock::acquire(rstd::move(opened).unwrap(), rstd::fs::FileLockMode::Exclusive);
    if (locked.is_err()) {
        return Err(doc_io_failure(
            "lock litodoc tool cache"_str, path.as_path(), rstd::move(locked).unwrap_err()));
    }
    return Ok(rstd::move(locked).unwrap());
}

struct CachedDocTool {
    PathBuf executable;
    String  executable_digest;
};

auto doc_tool_file_digest(ref<rstd::path::Path> path) -> DocResult<String>;

auto receipt_tool(ref<rstd::path::Path> receipt, ref<str> expected_key)
    -> DocResult<Option<CachedDocTool>> {
    auto contents = rstd::fs::read_to_string(receipt);
    if (contents.is_err()) {
        auto error = rstd::move(contents).unwrap_err();
        if (error.kind() == rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound }) {
            return Ok(None());
        }
        return Err(doc_io_failure("read litodoc tool receipt"_str, receipt, rstd::move(error)));
    }
    auto parsed = rstd::json::from_str(contents->as_str());
    if (parsed.is_err()) return Ok(None());
    auto format            = parsed->get("format"_str);
    auto version           = parsed->get("version"_str);
    auto key               = parsed->get("key"_str);
    auto executable        = parsed->get("executable"_str);
    auto executable_digest = parsed->get("executable_digest"_str);
    if (format.is_none() || (**format).as_str() != Some("lito-doc-tool"_str) || version.is_none() ||
        (**version).as_u64() != Some(u64(2)) || key.is_none() ||
        (**key).as_str() != Some(expected_key) || executable.is_none() ||
        (**executable).as_str().is_none() || executable_digest.is_none() ||
        (**executable_digest).as_str().is_none()) {
        return Ok(None());
    }
    auto path     = PathBuf::from(*(**executable).as_str());
    auto metadata = rstd::fs::metadata(path.as_path());
    if (metadata.is_err() || ! metadata->is_file()) return Ok(None());
    auto actual_executable = doc_tool_file_digest(path.as_path());
    if (actual_executable.is_err() ||
        actual_executable->as_str() != *(**executable_digest).as_str()) {
        return Ok(None());
    }
    return Ok(Some(CachedDocTool {
        .executable        = rstd::move(path),
        .executable_digest = String::make(*(**executable_digest).as_str()),
    }));
}

auto write_tool_receipt(ref<rstd::path::Path>      path,
                        ref<str>                   key,
                        ref<str>                   source_identity,
                        ref<rstd::path::Path>      executable,
                        ref<str>                   executable_digest,
                        const DocToolCapabilities& capabilities) -> DocResult<empty> {
    auto executable_text = executable.to_str();
    if (executable_text.is_none()) {
        return doc_tool_failure<empty>(
            rstd::format("litodoc executable '{}' is not valid UTF-8", executable));
    }
    auto root = JsonMap::make();
    root.insert(String::make("format"_str), Json::String(String::make("lito-doc-tool"_str)));
    root.insert(String::make("version"_str), Json::Number(rstd::json::Number::from_u64(u64(2))));
    root.insert(String::make("key"_str), Json::String(String::make(key)));
    root.insert(String::make("source"_str), Json::String(String::make(source_identity)));
    root.insert(String::make("executable"_str), Json::String(String::make(*executable_text)));
    root.insert(String::make("executable_digest"_str),
                Json::String(String::make(executable_digest)));
    root.insert(String::make("capabilities"_str), Json::String(capabilities.json.clone()));
    auto text =
        rstd::json::to_string(Json::Object(rstd::move(root)),
                              rstd::json::FormatOptions { .pretty = true, .indent = usize(2) });
    auto written = rstd::fs::write_atomic(path, text.as_str().as_bytes());
    if (written.is_err()) {
        return Err(doc_io_failure(
            "write litodoc tool receipt"_str, path, rstd::move(written).unwrap_err()));
    }
    return Ok(empty {});
}

auto append_unique_path(Vec<PathBuf>& paths, ref<rstd::path::Path> path) -> void {
    for (const auto& current : paths) {
        if (current.as_path() == path) return;
    }
    paths.push(PathBuf::from(path));
}

auto doc_tool_file_digest(ref<rstd::path::Path> path) -> DocResult<String> {
    auto opened = rstd::fs::File::open(path);
    if (opened.is_err()) {
        return Err(
            doc_io_failure("open litodoc executable"_str, path, rstd::move(opened).unwrap_err()));
    }
    auto file   = rstd::move(opened).unwrap();
    auto state  = licrypto::Sha256::make();
    auto buffer = array<u8, 65536> {};
    while (true) {
        auto read = file.read(buffer.as_mut_slice());
        if (read.is_err()) {
            return Err(
                doc_io_failure("read litodoc executable"_str, path, rstd::move(read).unwrap_err()));
        }
        if (*read == usize {}) break;
        state.update(slice<u8>::from_raw_parts(buffer.as_ptr(), *read));
    }
    return Ok(licrypto::sha256_hex(rstd::move(state).finalize()));
}

struct PublishedDocTool {
    PathBuf executable;
    String  identity;
};

auto copy_doc_tool_file(ref<rstd::path::Path> source, ref<rstd::path::Path> destination)
    -> DocResult<empty> {
    auto parent  = destination.parent().unwrap();
    auto created = rstd::fs::create_dir_all(parent);
    if (created.is_err()) {
        return Err(doc_io_failure(
            "create litodoc artifact directory"_str, parent, rstd::move(created).unwrap_err()));
    }
    auto copied = rstd::fs::copy(source, destination);
    if (copied.is_err()) {
        return Err(doc_io_failure(
            "copy litodoc artifact"_str, destination, rstd::move(copied).unwrap_err()));
    }
    return Ok(empty {});
}

auto published_doc_tool(ref<rstd::path::Path> tool_root,
                        ref<rstd::path::Path> executable,
                        ref<str>              executable_digest) -> DocResult<PublishedDocTool> {
    auto identity =
        licrypto::sha256_hex(rstd::format("litodoc-artifact-v2\n{}", executable_digest).as_str());
    auto artifacts        = PathBuf::from(tool_root).join(PathBuf::from("artifacts"_str).as_path());
    auto final            = artifacts.join(PathBuf::from(identity.clone()).as_path());
    auto final_executable = final.join(PathBuf::from("bin/litodoc"_str).as_path());
    auto exists           = rstd::fs::exists(final.as_path());
    if (exists.is_err()) {
        return Err(doc_io_failure(
            "inspect litodoc artifact"_str, final.as_path(), rstd::move(exists).unwrap_err()));
    }
    if (*exists) {
        auto actual_executable = doc_tool_file_digest(final_executable.as_path());
        if (actual_executable.is_ok() && actual_executable->as_str() == executable_digest) {
            return Ok(PublishedDocTool {
                .executable = rstd::move(final_executable),
                .identity   = rstd::move(identity),
            });
        }
        auto removed = rstd::fs::remove_dir_all(final.as_path());
        if (removed.is_err()) {
            return Err(doc_io_failure("replace invalid litodoc artifact"_str,
                                      final.as_path(),
                                      rstd::move(removed).unwrap_err()));
        }
    }
    auto created = rstd::fs::create_dir_all(artifacts.as_path());
    if (created.is_err()) {
        return Err(doc_io_failure("create litodoc artifact store"_str,
                                  artifacts.as_path(),
                                  rstd::move(created).unwrap_err()));
    }
    auto staging_name = identity.clone();
    staging_name.push_str(".staging"_str);
    auto staging        = artifacts.join(PathBuf::from(rstd::move(staging_name)).as_path());
    auto staging_exists = rstd::fs::exists(staging.as_path());
    if (staging_exists.is_err()) {
        return Err(doc_io_failure("inspect litodoc artifact staging"_str,
                                  staging.as_path(),
                                  rstd::move(staging_exists).unwrap_err()));
    }
    if (*staging_exists) {
        auto removed = rstd::fs::remove_dir_all(staging.as_path());
        if (removed.is_err()) {
            return Err(doc_io_failure("clear litodoc artifact staging"_str,
                                      staging.as_path(),
                                      rstd::move(removed).unwrap_err()));
        }
    }
    auto staged_executable = staging.join(PathBuf::from("bin/litodoc"_str).as_path());
    rstd_try(copy_doc_tool_file(executable, staged_executable.as_path()));
    auto staged_executable_digest = rstd_try(doc_tool_file_digest(staged_executable.as_path()));
    if (staged_executable_digest.as_str() != executable_digest) {
        return doc_tool_failure<PublishedDocTool>(
            "published litodoc artifact identity changed while copying"_str);
    }
    auto renamed = rstd::fs::rename(staging.as_path(), final.as_path());
    if (renamed.is_err()) {
        return Err(doc_io_failure(
            "publish litodoc artifact"_str, final.as_path(), rstd::move(renamed).unwrap_err()));
    }
    return Ok(PublishedDocTool {
        .executable = rstd::move(final_executable),
        .identity   = rstd::move(identity),
    });
}

} // namespace lito

namespace lito
{

struct ResolvedDocTool {
    PathBuf  executable;
    String   source_identity;
    String   build_identity;
    ClangSdk sdk;
};

auto resolve_doc_tool(const BuildRequest&               request,
                      const lito::config::DocConfig&    config,
                      const BuildSummary&               project,
                      const ResolvedProcessEnvironment& environment,
                      const Option<DocEventSink>&       observer) -> DocResult<ResolvedDocTool> {
    auto sdk = resolve_clang_sdk(project.compiler, environment);
    if (sdk.is_err()) return Err(rstd::into<DocError>(rstd::move(sdk).unwrap_err()));
    if (config.litodoc_executable.is_some()) {
        auto capabilities =
            probe_doc_tool(config.litodoc_executable->as_path(), project.compiler, environment);
        if (capabilities.is_err()) return Err(rstd::move(capabilities).unwrap_err());
        auto executable_digest = doc_tool_file_digest(config.litodoc_executable->as_path());
        if (executable_digest.is_err()) return Err(rstd::move(executable_digest).unwrap_err());
        auto identity = rstd::format("executable:sha256:{}", executable_digest->as_str());
        return Ok(ResolvedDocTool {
            .executable      = config.litodoc_executable->clone(),
            .source_identity = identity.clone(),
            .build_identity  = rstd::move(identity),
            .sdk             = rstd::move(sdk).unwrap(),
        });
    }
    auto resolver =
        lito::tools::ToolResolver(environment, request.tools.clone(), request.tool_reporter);
    auto source = acquire_doc_tool_source(request, config, resolver, environment);
    if (source.is_err()) return Err(rstd::move(source).unwrap_err());
    auto key_material = rstd::format("lito-doc-tool-v3\n{}\n{}\n{}\n{}\n{}",
                                     source->identity.as_str(),
                                     sdk->identity.as_str(),
                                     project.compiler.target.as_str(),
                                     config.litodoc_path.is_some() ? "path"_str : "git"_str,
                                     lito::lock::LOCK_FORMAT_VERSION);
    auto key          = licrypto::sha256_hex(key_material.as_str());
    auto tool_root    = create_doc_tool_root(key.as_str());
    if (tool_root.is_err()) return Err(rstd::move(tool_root).unwrap_err());
    auto lock = acquire_doc_tool_lock(tool_root->as_path());
    if (lock.is_err()) return Err(rstd::move(lock).unwrap_err());
    auto receipt = tool_root->join(PathBuf::from("receipt.json"_str).as_path());

    if (source->cacheable) {
        auto cached = receipt_tool(receipt.as_path(), key.as_str());
        if (cached.is_err()) return Err(rstd::move(cached).unwrap_err());
        if (cached->is_some()) {
            auto cached_tool = rstd::move(cached).unwrap().unwrap();
            auto capabilities =
                probe_doc_tool(cached_tool.executable.as_path(), project.compiler, environment);
            if (capabilities.is_ok()) {
                emit_doc(observer,
                         DocEventKind::ToolReuse,
                         source->identity.as_str(),
                         cached_tool.executable.as_path());
                return Ok(ResolvedDocTool {
                    .executable      = rstd::move(cached_tool.executable),
                    .source_identity = source->identity.clone(),
                    .build_identity  = rstd::move(key),
                    .sdk             = rstd::move(sdk).unwrap(),
                });
            }
        }
    }

    emit_doc(observer, DocEventKind::ToolBuild, source->identity.as_str(), source->root.as_path());
    auto tool_request           = BuildRequest {};
    tool_request.selection.root = source->root.clone();
    tool_request.selection.packages.push(String::make("litodoc"_str));
    tool_request.exact_targets.push(lito::package::PackageTargetId {
        .package = String::make("litodoc"_str),
        .kind    = lito::package::PackageTargetKind::Binary,
        .name    = String::make("litodoc"_str),
    });
    tool_request.build_directory = tool_root->join(PathBuf::from("build"_str).as_path());
    tool_request.environment     = request.environment.clone();
    tool_request.tools           = request.tools.clone();
    tool_request.configuration   = request.configuration.clone();
    tool_request.configuration.toolchain.cxx = project.compiler.path.clone();
    tool_request.configuration.standard_library =
        lito::config::standard_library_selection(sdk->standard_library);
    tool_request.lock.path              = tool_root->join(PathBuf::from("lito.lock"_str).as_path());
    tool_request.sources.network        = request.sources.network;
    tool_request.sources.source_bundles = as<Clone>(request.sources.source_bundles).clone();
    tool_request.cargo                  = request.cargo;
    tool_request.pkg_config             = request.pkg_config.clone();
    tool_request.cmake                  = request.cmake.clone();
    append_unique_path(tool_request.cmake.search_paths, sdk->cmake_search_path.as_path());
    tool_request.profile =
        Some(lito::manifest::BuildProfileName { .value = String::make("release"_str) });
    tool_request.execution.scan    = request.execution.scan;
    tool_request.execution.compile = request.execution.compile;
    tool_request.observer          = request.observer;
    auto built                     = build_with_environment(tool_request, environment);
    if (built.is_err()) return Err(rstd::into<DocError>(rstd::move(built).unwrap_err()));
    auto executable = Option<PathBuf> {};
    for (const auto& artifact : built->product.artifacts) {
        if (artifact.kind == cpp::ArtifactKind::Executable &&
            artifact.target.kind == lito::package::PackageTargetKind::Binary &&
            artifact.target.package.as_str() == "litodoc"_str &&
            artifact.target.name.as_str() == "litodoc"_str) {
            executable = Some(artifact.primary.path.clone());
        }
    }
    if (executable.is_none()) {
        return doc_tool_failure<ResolvedDocTool>(
            "litodoc tool build did not produce the litodoc executable"_str);
    }
    auto capabilities = probe_doc_tool(executable->as_path(), project.compiler, environment);
    if (capabilities.is_err()) return Err(rstd::move(capabilities).unwrap_err());
    auto executable_digest = doc_tool_file_digest(executable->as_path());
    if (executable_digest.is_err()) return Err(rstd::move(executable_digest).unwrap_err());
    auto published = published_doc_tool(
        tool_root->as_path(), executable->as_path(), executable_digest->as_str());
    if (published.is_err()) return Err(rstd::move(published).unwrap_err());
    capabilities = probe_doc_tool(published->executable.as_path(), project.compiler, environment);
    if (capabilities.is_err()) return Err(rstd::move(capabilities).unwrap_err());
    rstd_try(write_tool_receipt(receipt.as_path(),
                                key.as_str(),
                                source->identity.as_str(),
                                published->executable.as_path(),
                                executable_digest->as_str(),
                                *capabilities));
    return Ok(ResolvedDocTool {
        .executable      = rstd::move(published->executable),
        .source_identity = source->identity.clone(),
        .build_identity  = source->cacheable
                               ? rstd::move(key)
                               : rstd::format("{}:{}", key.as_str(), published->identity.as_str()),
        .sdk             = rstd::move(sdk).unwrap(),
    });
}

} // namespace lito
