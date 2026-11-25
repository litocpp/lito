module;
#include <rstd/macro.hpp>

module lito.driver:build.safety;

import rstd;
import licrypto;
import lito.pack;
import lito.system;
import lito.toolchain;
import lito.toolchain.common;
import lito.tools;
import :build;
import :config.registry;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::system;

namespace lito
{
namespace
{

auto safety_cache_path(ref<rstd::path::Path> root, ref<str> name) -> PathBuf {
    return PathBuf::from(root).join(PathBuf::from(name).as_path());
}

template<typename T>
auto safety_io(rstd::io::Result<T> result, ref<str> operation, ref<rstd::path::Path> path)
    -> BuildResult<T> {
    return rstd::move(result).map_err([&](rstd::io::Error error) {
        return BuildError::System(
            SystemError::Io(operation.into(), PathBuf::from(path), rstd::move(error)));
    });
}

auto safety_cache_lock(ref<rstd::path::Path> root) -> BuildResult<rstd::fs::FileLock> {
    auto path = safety_cache_path(root, "build.lock"_str);
    auto file = rstd_try(safety_io(
        rstd::fs::OpenOptions::make().read(true).write(true).create(true).open(path.as_path()),
        "open safety plugin cache lock"_str,
        path.as_path()));
    return safety_io(
        rstd::fs::FileLock::acquire(rstd::move(file), rstd::fs::FileLockMode::Exclusive),
        "lock safety plugin cache"_str,
        path.as_path());
}

auto materialize_safety_source(const lito::registry::EmbeddedPackageInput&              input,
                               const lito::registry::VerifiedRegistryPackageDescriptor& recipe,
                               ref<rstd::path::Path> root) -> BuildResult<PathBuf> {
    auto archive = safety_cache_path(root, "source.tar.zst"_str);
    rstd_try(safety_io(rstd::fs::write_atomic(archive.as_path(), input.archive),
                       "write embedded safety sources"_str,
                       archive.as_path()));
    auto blob = lito::registry::verify_registry_blob_file(
        rstd::move(archive), recipe.package, recipe.archive.checksum);
    if (blob.is_err()) {
        return Err(BuildError::Message(rstd::format("cannot verify embedded safety sources: {}",
                                                    blob.unwrap_err().message.as_str())));
    }
    auto source    = safety_cache_path(root, "source"_str);
    auto inspected = lito::registry::PackageArchiveInspector::inspect_at_root(
        *blob, recipe.package, recipe.version, source.as_path());
    if (inspected.is_err()) {
        return Err(BuildError::Message(rstd::format("cannot read embedded safety sources: {}",
                                                    inspected.unwrap_err().message.as_str())));
    }
    auto exists = rstd_try(safety_io(
        rstd::fs::exists(source.as_path()), "inspect cached safety sources"_str, source.as_path()));
    if (exists) {
        rstd_try(safety_io(rstd::fs::remove_dir_all(source.as_path()),
                           "remove cached safety sources"_str,
                           source.as_path()));
    }
    auto materialized = lito::source::materialize_source_tree(inspected->tree, source.as_path());
    if (materialized.is_err()) {
        return Err(BuildError::Message(rstd::format(
            "cannot materialize embedded safety sources: {}", materialized.unwrap_err())));
    }
    return Ok(rstd::move(source));
}

auto safety_artifact_path(ref<rstd::path::Path> root, ref<str> digest, ref<str> filename)
    -> PathBuf {
    auto artifacts = safety_cache_path(root, "artifacts"_str);
    auto directory = safety_cache_path(artifacts.as_path(), digest);
    return safety_cache_path(directory.as_path(), filename);
}

auto safety_usage(PathBuf artifact, ref<str> key, ref<str> digest) -> ResolvedCompilerPluginUsage {
    return ResolvedCompilerPluginUsage {
        .plugin   = rstd::move(artifact),
        .name     = "safety"_Str,
        .identity = rstd::format("lito-safety-plugin-v1\n{}\n{}", key, digest),
    };
}

auto cached_safety_usage(ref<rstd::path::Path> root,
                         ref<rstd::path::Path> receipt,
                         ref<str>              key,
                         ref<str> filename) -> BuildResult<Option<ResolvedCompilerPluginUsage>> {
    auto recorded = rstd::fs::read_to_string(receipt);
    if (recorded.is_err()) return Ok(None());
    auto checksum = lito::registry::PackageChecksum::parse(recorded->as_str());
    if (checksum.is_err()) return Ok(None());
    auto artifact = safety_artifact_path(root, recorded->as_str(), filename);
    auto contents = rstd::fs::read(artifact.as_path());
    if (contents.is_err()) return Ok(None());
    auto digest = rstd_try(licrypto::sha256_hex(contents->as_slice()));
    if (digest != recorded->as_str()) return Ok(None());
    return Ok(Some(safety_usage(rstd::move(artifact), key, digest.as_str())));
}

auto publish_safety_usage(ref<rstd::path::Path> root,
                          ref<rstd::path::Path> receipt,
                          ref<str>              key,
                          ref<str>              filename,
                          ref<rstd::path::Path> compiled)
    -> BuildResult<ResolvedCompilerPluginUsage> {
    auto contents =
        rstd_try(safety_io(rstd::fs::read(compiled), "read built safety plugin"_str, compiled));
    auto digest    = rstd_try(licrypto::sha256_hex(contents.as_slice()));
    auto artifact  = safety_artifact_path(root, digest.as_str(), filename);
    auto directory = artifact.as_path().parent().unwrap();
    rstd_try(safety_io(rstd::fs::create_dir_all(directory),
                       "create safety plugin artifact directory"_str,
                       directory));
    rstd_try(safety_io(rstd::fs::write_atomic_if_changed(artifact.as_path(), contents.as_slice()),
                       "publish safety plugin artifact"_str,
                       artifact.as_path()));
    rstd_try(safety_io(rstd::fs::write_atomic(receipt, digest.as_str().as_bytes()),
                       "write safety plugin cache receipt"_str,
                       receipt));
    return Ok(safety_usage(rstd::move(artifact), key, digest.as_str()));
}

} // namespace

auto resolve_safety_plugin(const Option<lito::config::LitoBootstrapConfig>& registries,
                           const lito::tools::ToolSpec&                     tools,
                           const ClangToolchain&                            toolchain,
                           const ResolvedProcessEnvironment&                environment)
    -> BuildResult<ResolvedCompilerPluginUsage> {
    auto provider = registries.is_some() ? registries->embedded_packages()
                                         : lito::registry::EmbeddedPackageProvider {};
    auto input    = provider.resolve != nullptr ? provider.resolve(provider.context, "safety"_str)
                                                : Option<lito::registry::EmbeddedPackageInput> {};
    if (input.is_none()) {
        return Err(
            BuildError::Message("build.safety requires the embedded safety plugin sources"_Str));
    }
    auto recipe = lito::registry::parse_verified_publish_candidate(input->descriptor);
    if (recipe.is_err()) {
        return Err(BuildError::Message(rstd::format("invalid embedded safety plugin descriptor: {}",
                                                    recipe.unwrap_err().message.as_str())));
    }
    const auto& host_target = toolchain.compiler_default_target_info();
    auto        host        = detect_host_info();
    if (host.is_err()) {
        return Err(BuildError::Message(
            rstd::format("cannot detect safety plugin host: {}", host.unwrap_err())));
    }
    if (host_target.architecture != host->architecture || host_target.platform_name() != host->os) {
        return Err(BuildError::Message(rstd::format("build.safety needs a native Clang SDK, but "
                                                    "the compiler defaults to '{}' on host '{}-{}'",
                                                    host_target.triple.as_str(),
                                                    architecture_name(host->architecture),
                                                    host->os.as_str())));
    }
    auto compiler   = toolchain.compiler_identity().clone();
    compiler.target = host_target.triple.clone();
    auto sdk        = resolve_clang_sdk(compiler, environment);
    if (sdk.is_err()) {
        return Err(BuildError::Message(
            rstd::format("build.safety requires the matching Clang SDK for '{}': {}",
                         compiler.path.as_path(),
                         sdk.unwrap_err())));
    }
    auto matched = verify_clang_sdk_version(*sdk, compiler, environment);
    if (matched.is_err()) {
        return Err(BuildError::Message(rstd::format("build.safety: {}", matched.unwrap_err())));
    }
    auto source_digest = recipe->archive.checksum.text();
    auto key_input     = rstd::format("lito-safety-build-v1\nsource={}\nhost={}\n{}",
                                      source_digest.as_str(),
                                      host_target.triple.as_str(),
                                      sdk->identity.as_str());
    auto key           = rstd_try(licrypto::sha256_hex(key_input.as_str()));
    auto data          = LitoDataRoot::resolve();
    if (data.is_err()) return Err(rstd::into<BuildError>(rstd::move(data).unwrap_err()));
    auto cache = safety_cache_path(data->root(), "tools/safety"_str);
    auto root  = safety_cache_path(cache.as_path(), key.as_str());
    rstd_try(safety_io(rstd::fs::create_dir_all(root.as_path()),
                       "create safety plugin cache"_str,
                       root.as_path()));
    root      = rstd_try(safety_io(
        rstd::fs::canonicalize(root.as_path()), "resolve safety plugin cache"_str, root.as_path()));
    auto lock = rstd_try(safety_cache_lock(root.as_path()));
    (void)lock;
    auto receipt  = safety_cache_path(root.as_path(), "artifact.sha256"_str);
    auto filename = plugin_filename("lito-safety"_str, host_target);
    auto cached   = rstd_try(
        cached_safety_usage(root.as_path(), receipt.as_path(), key.as_str(), filename.as_str()));
    if (cached.is_some()) return Ok(rstd::move(cached).unwrap());

    auto request           = BuildRequest {};
    request.selection.root = rstd_try(materialize_safety_source(*input, *recipe, root.as_path()));
    request.selection.packages.push("safety"_Str);
    request.build_directory             = safety_cache_path(root.as_path(), "build"_str);
    request.lock.path                   = safety_cache_path(root.as_path(), "lito.lock"_str);
    request.configuration.toolchain.cxx = compiler.path.clone();
    request.configuration.toolchain.cc  = compiler.c_path.clone();
    request.configuration.toolchain.ar  = PathBuf::from(toolchain.ar_path());
    request.configuration.standard_library =
        lito::config::standard_library_selection(sdk->standard_library);
    request.configuration.language_standard = "c++20"_Str;
    request.sources.network                 = lito::source::NetworkPolicy::Offline;
    request.tools                           = tools.clone();
    request.profile = Some(lito::manifest::BuildProfileName { .value = "release"_Str });
    auto built      = rstd_try(build_with_environment(request, environment));
    for (const auto& plugin : built.compiler_plugins) {
        if (plugin.target.package != "safety"_str) continue;
        return publish_safety_usage(root.as_path(),
                                    receipt.as_path(),
                                    key.as_str(),
                                    filename.as_str(),
                                    plugin.plugin.as_path());
    }
    return Err(BuildError::Message("safety plugin build did not produce a Clang plugin"_Str));
}

} // namespace lito
