module;
#include <rstd/macro.hpp>

export module lito.driver:build.product;

import rstd;
import rstd.json;
import lito.core;
import lito.cpp;
import :build.product_error;
import :build.request;
import :build.artifact;
import :build.layout;
import :dependency.preparation;
import :dependency.cmake;

using namespace rstd::prelude;
using namespace rstd::literals;
using Json      = rstd::json::Value;
using JsonMap   = rstd::json::Map;
using JsonArray = rstd::json::Array;

export namespace lito
{

struct BuildProductFileIdentity {
    PathBuf                    path;
    u64                        size {};
    rstd::crypto::Sha256Digest sha256;
};

struct CompletedBuildProduct {
    String                              id;
    PathBuf                             project_root;
    String                              package;
    String                              profile;
    String                              target;
    String                              target_architecture;
    String                              target_kind;
    String                              android_abi;
    u32                                 android_minimum_api {};
    PathBuf                             base_directory;
    PathBuf                             build_directory;
    Vec<String>                         requested_packages;
    lito::package::FeatureSelection     features;
    Vec<lito::package::PackageTargetId> selected_targets;
    Vec<cpp::SelectedPackageMetadata>   selected_packages;
    Vec<BuiltArtifact>                  artifacts;
    Vec<BuiltRuntimeResource>           runtime_resources;
    Vec<BuiltTargetRuntime>             target_runtimes;
    ExternalAssetCatalog                external_assets;
    Vec<ExternalSourceProvenance>       external_source_provenance;
    String                              compiler_identity;
    String                              compiler_version;
    Vec<BuildProductFileIdentity>       files;
};

struct BuildProductPublication {
    PathBuf base_directory;
    PathBuf state;
    PathBuf lock;
    String  generation;
};

auto resolve_build_base_directory(ref<rstd::path::Path> project_root,
                                  ref<rstd::path::Path> requested,
                                  ref<str>              profile) -> PathBuf;

auto begin_build_product_publication(ref<rstd::path::Path> project_root,
                                     ref<rstd::path::Path> requested,
                                     ref<str>              profile)
    -> BuildProductResult<BuildProductPublication>;

auto finalize_completed_build_product(CompletedBuildProduct   product,
                                      const lock::LockConfig& lock,
                                      const Vec<PathBuf>&     inputs)
    -> BuildProductResult<CompletedBuildProduct>;

auto complete_build_product_publication(const BuildProductPublication& publication,
                                        const CompletedBuildProduct&   product)
    -> BuildProductResult<empty>;

auto load_completed_build_product(ref<rstd::path::Path> project_root,
                                  ref<rstd::path::Path> requested,
                                  ref<str> profile) -> BuildProductResult<CompletedBuildProduct>;

auto validate_completed_build_product(const CompletedBuildProduct& product,
                                      const BuildRequest&          request,
                                      ref<str>                     profile,
                                      ref<str> target) -> BuildProductResult<empty>;

} // namespace lito

namespace lito
{

inline constexpr auto BUILD_PRODUCT_SCHEMA = u64(1);

template<typename T>
auto product_failure(String message) -> BuildProductResult<T> {
    return Err(BuildProductError::Message(rstd::move(message)));
}

template<typename T>
auto product_failure(ref<str> message) -> BuildProductResult<T> {
    return product_failure<T>(String::make(message));
}

template<typename T>
auto product_io_failure(ref<str>               operation,
                        ref<rstd::path::Path>  path,
                        rstd::io::error::Error error) -> BuildProductResult<T> {
    return Err(
        BuildProductError::Io(String::make(operation), PathBuf::from(path), rstd::move(error)));
}

auto product_string(ref<str> value) -> Json {
    return Json::String(String::make(value));
}

auto product_path(ref<rstd::path::Path> path) -> BuildProductResult<Json> {
    auto text = path.to_str();
    if (text.is_none()) {
        return product_failure<Json>(
            rstd::format("build product path '{}' is not valid UTF-8", path));
    }
    return Ok(product_string(*text));
}

auto product_known_fields(const Json& value, ref<str> context, initializer_list<ref<str>> names)
    -> BuildProductResult<empty> {
    return Ok(rstd_try(
        lito::parse::json::reject_unknown(value, lito::parse::NodePath::root(context), names)));
}

auto product_member(const Json& value, ref<str> key, ref<str> context)
    -> BuildProductResult<ref<Json>> {
    return Ok(rstd_try(
        lito::parse::json::required_member(value, key, lito::parse::NodePath::root(context))));
}

auto product_required_string(const Json& value, ref<str> key, ref<str> context)
    -> BuildProductResult<String> {
    return Ok(rstd_try(lito::parse::json::required_non_empty_string(
        value, key, lito::parse::NodePath::root(context))));
}

auto product_required_text(const Json& value, ref<str> key, ref<str> context)
    -> BuildProductResult<String> {
    auto member = rstd_try(product_member(value, key, context));
    auto text   = rstd_try(
        lito::parse::json::string(*member, lito::parse::NodePath::root(context).field(key)));
    return Ok(String::make(text));
}

auto product_required_path(const Json& value, ref<str> key, ref<str> context)
    -> BuildProductResult<PathBuf> {
    return Ok(PathBuf::from(rstd_try(product_required_string(value, key, context))));
}

auto product_build_path(const CompletedBuildProduct& product,
                        ref<rstd::path::Path>        path,
                        ref<str>                     context) -> BuildProductResult<Json> {
    auto relative = path.strip_prefix(product.base_directory.as_path());
    if (relative.is_none()) {
        return product_failure<Json>(rstd::format("{} '{}' escapes build directory '{}'",
                                                  context,
                                                  path,
                                                  product.base_directory.as_path()));
    }
    if (relative->is_empty()) return Ok(product_string("."_str));
    if (! relative->is_safe_relative()) {
        return product_failure<Json>(
            rstd::format("{} '{}' is not a safe build-relative path", context, *relative));
    }
    return product_path(*relative);
}

auto resolve_product_build_path(ref<rstd::path::Path> base,
                                const Json&           value,
                                ref<str>              key,
                                ref<str>              context) -> BuildProductResult<PathBuf> {
    auto text = rstd_try(product_required_string(value, key, context));
    if (text == "."_str) return Ok(PathBuf::from(base));
    auto relative = PathBuf::from(text);
    if (! relative.as_path().is_safe_relative()) {
        return product_failure<PathBuf>(rstd::format(
            "{}.{} '{}' is not a safe build-relative path", context, key, relative.as_path()));
    }
    return Ok(PathBuf::from(base).join(relative.as_path()));
}

auto product_required_array(const Json& value, ref<str> key, ref<str> context)
    -> BuildProductResult<ref<JsonArray>> {
    return Ok(rstd_try(
        lito::parse::json::required_array(value, key, lito::parse::NodePath::root(context))));
}

auto product_string_array(const Vec<String>& values) -> Json {
    auto result = JsonArray::with_capacity(values.len());
    for (const auto& value : values) result.push(product_string(value.as_str()));
    return Json::Array(rstd::move(result));
}

auto parse_product_string_array(const Json& value, ref<str> key, ref<str> context)
    -> BuildProductResult<Vec<String>> {
    auto input  = rstd_try(product_required_array(value, key, context));
    auto result = Vec<String>::with_capacity(input->len());
    for (const auto& item : *input) {
        auto text = item.as_str();
        if (text.is_none() || text->is_empty()) {
            return product_failure<Vec<String>>(
                rstd::format("{}.{} must contain non-empty strings", context, key));
        }
        result.push(String::make(*text));
    }
    return Ok(rstd::move(result));
}

auto package_target_kind_text(lito::package::PackageTargetKind kind) -> ref<str> {
    return lito::package::package_target_kind_name(kind);
}

auto parse_package_target_kind(ref<str> value)
    -> BuildProductResult<lito::package::PackageTargetKind> {
    if (value == "lib"_str) return Ok(lito::package::PackageTargetKind::Library);
    if (value == "bin"_str) return Ok(lito::package::PackageTargetKind::Binary);
    if (value == "test"_str) return Ok(lito::package::PackageTargetKind::Test);
    if (value == "bench"_str) return Ok(lito::package::PackageTargetKind::Benchmark);
    if (value == "test-attachment"_str) {
        return Ok(lito::package::PackageTargetKind::TestAttachment);
    }
    if (value == "compile-test"_str) return Ok(lito::package::PackageTargetKind::CompileTest);
    return product_failure<lito::package::PackageTargetKind>(
        rstd::format("unknown build product target kind '{}'", value));
}

auto artifact_kind_text(cpp::ArtifactKind kind) -> ref<str> {
    switch (kind) {
    case cpp::ArtifactKind::StaticLibrary: return "static-library"_str;
    case cpp::ArtifactKind::SharedLibrary: return "shared-library"_str;
    case cpp::ArtifactKind::TestAttachmentArchive: return "test-attachment-archive"_str;
    case cpp::ArtifactKind::Executable: return "executable"_str;
    case cpp::ArtifactKind::TestExecutable: return "test-executable"_str;
    case cpp::ArtifactKind::BenchmarkExecutable: return "benchmark-executable"_str;
    case cpp::ArtifactKind::CompileTest: return "compile-test"_str;
    }
    return "unknown"_str;
}

auto parse_artifact_kind(ref<str> value) -> BuildProductResult<cpp::ArtifactKind> {
    if (value == "static-library"_str) return Ok(cpp::ArtifactKind::StaticLibrary);
    if (value == "shared-library"_str) return Ok(cpp::ArtifactKind::SharedLibrary);
    if (value == "test-attachment-archive"_str) {
        return Ok(cpp::ArtifactKind::TestAttachmentArchive);
    }
    if (value == "executable"_str) return Ok(cpp::ArtifactKind::Executable);
    if (value == "test-executable"_str) return Ok(cpp::ArtifactKind::TestExecutable);
    if (value == "benchmark-executable"_str) return Ok(cpp::ArtifactKind::BenchmarkExecutable);
    if (value == "compile-test"_str) return Ok(cpp::ArtifactKind::CompileTest);
    return product_failure<cpp::ArtifactKind>(
        rstd::format("unknown build product artifact kind '{}'", value));
}

auto target_json(const lito::package::PackageTargetId& target) -> Json {
    auto result = JsonMap::make();
    result.insert(String::make("package"_str), product_string(target.package.as_str()));
    result.insert(String::make("kind"_str), product_string(package_target_kind_text(target.kind)));
    result.insert(String::make("name"_str), product_string(target.name.as_str()));
    return Json::Object(rstd::move(result));
}

auto parse_target(const Json& value, ref<str> context)
    -> BuildProductResult<lito::package::PackageTargetId> {
    rstd_try(product_known_fields(value, context, { "package"_str, "kind"_str, "name"_str }));
    auto package = rstd_try(product_required_string(value, "package"_str, context));
    auto kind    = rstd_try(product_required_string(value, "kind"_str, context));
    auto name    = rstd_try(product_required_string(value, "name"_str, context));
    return Ok(lito::package::PackageTargetId {
        .package = rstd::move(package),
        .kind    = rstd_try(parse_package_target_kind(kind.as_str())),
        .name    = rstd::move(name),
    });
}

auto runpath_json(const lito::artifact::ElfRunpath& runpath) -> BuildProductResult<Json> {
    auto values = JsonArray::with_capacity(runpath.paths.len());
    for (const auto& path : runpath.paths) values.push(rstd_try(product_path(path.path.as_path())));
    return Ok(Json::Array(rstd::move(values)));
}

auto parse_runpath(const Json& value, ref<str> context)
    -> BuildProductResult<lito::artifact::ElfRunpath> {
    auto array = value.as_array();
    if (array.is_none() || (**array).is_empty()) {
        return product_failure<lito::artifact::ElfRunpath>(
            rstd::format("{} must be a non-empty array", context));
    }
    auto paths = Vec<lito::artifact::OriginRelativeRuntimePath>::with_capacity((**array).len());
    for (const auto& item : **array) {
        auto text = item.as_str();
        if (text.is_none()) {
            return product_failure<lito::artifact::ElfRunpath>(
                rstd::format("{} must contain strings", context));
        }
        auto path = lito::artifact::make_origin_relative_runtime_path(PathBuf::from(*text));
        if (path.is_err()) {
            return product_failure<lito::artifact::ElfRunpath>(
                rstd::format("{} contains invalid path '{}': {}",
                             context,
                             *text,
                             rstd::move(path).unwrap_err()));
        }
        paths.push(rstd::move(path).unwrap());
    }
    auto runpath = lito::artifact::make_elf_runpath(rstd::move(paths));
    if (runpath.is_err()) {
        return product_failure<lito::artifact::ElfRunpath>(
            rstd::format("{} is invalid: {}", context, rstd::move(runpath).unwrap_err()));
    }
    return Ok(rstd::move(runpath).unwrap());
}

auto artifact_json(const CompletedBuildProduct& product, const BuiltArtifact& artifact)
    -> BuildProductResult<Json> {
    auto result = JsonMap::make();
    result.insert(String::make("target"_str), target_json(artifact.target));
    result.insert(String::make("kind"_str), product_string(artifact_kind_text(artifact.kind)));
    result.insert(String::make("path"_str),
                  rstd_try(product_build_path(
                      product, artifact.path.as_path(), "build product artifact"_str)));
    result.insert(String::make("package-root"_str),
                  rstd_try(product_path(artifact.package_root.as_path())));
    result.insert(String::make("link-identity"_str),
                  product_string(artifact.link_identity.as_str()));
    if (artifact.install_link.is_some()) {
        auto link = JsonMap::make();
        link.insert(String::make("identity"_str),
                    product_string(artifact.install_link->identity.as_str()));
        link.insert(String::make("runtime-search"_str),
                    rstd_try(runpath_json(artifact.install_link->runtime_search)));
        result.insert(String::make("install-link"_str), Json::Object(rstd::move(link)));
    }
    return Ok(Json::Object(rstd::move(result)));
}

auto parse_artifact(const Json& value, ref<rstd::path::Path> base, ref<str> context)
    -> BuildProductResult<BuiltArtifact> {
    rstd_try(product_known_fields(value,
                                  context,
                                  { "target"_str,
                                    "kind"_str,
                                    "path"_str,
                                    "package-root"_str,
                                    "link-identity"_str,
                                    "install-link"_str }));
    auto target_value = rstd_try(product_member(value, "target"_str, context));
    auto kind_text    = rstd_try(product_required_string(value, "kind"_str, context));
    auto install_link = Option<InstallArtifactLinkPolicy> {};
    auto link_value   = value.get("install-link"_str);
    if (link_value.is_some()) {
        auto link_context = rstd::format("{}.install-link", context);
        rstd_try(product_known_fields(
            **link_value, link_context.as_str(), { "identity"_str, "runtime-search"_str }));
        auto runpath_value =
            rstd_try(product_member(**link_value, "runtime-search"_str, link_context.as_str()));
        install_link = Some(InstallArtifactLinkPolicy {
            .runtime_search = rstd_try(
                parse_runpath(*runpath_value, "build product artifact runtime-search"_str)),
            .identity = rstd_try(
                product_required_string(**link_value, "identity"_str, link_context.as_str())),
        });
    }
    return Ok(BuiltArtifact {
        .target        = rstd_try(parse_target(*target_value, "build product artifact.target"_str)),
        .kind          = rstd_try(parse_artifact_kind(kind_text.as_str())),
        .path          = rstd_try(resolve_product_build_path(base, value, "path"_str, context)),
        .package_root  = rstd_try(product_required_path(value, "package-root"_str, context)),
        .install_link  = rstd::move(install_link),
        .link_identity = rstd_try(product_required_text(value, "link-identity"_str, context)),
    });
}

auto runtime_resource_json(const CompletedBuildProduct& product,
                           const BuiltRuntimeResource&  resource) -> BuildProductResult<Json> {
    auto result = JsonMap::make();
    result.insert(String::make("target"_str), target_json(resource.target));
    result.insert(String::make("name"_str), product_string(resource.name.as_str()));
    result.insert(String::make("root"_str),
                  rstd_try(product_build_path(
                      product, resource.root.as_path(), "build product runtime resource"_str)));
    result.insert(String::make("identity"_str), product_string(resource.identity.as_str()));
    auto files = JsonArray::with_capacity(resource.files.len());
    for (const auto& file : resource.files) files.push(rstd_try(product_path(file.as_path())));
    result.insert(String::make("files"_str), Json::Array(rstd::move(files)));
    return Ok(Json::Object(rstd::move(result)));
}

auto parse_runtime_resource(const Json& value, ref<rstd::path::Path> base, ref<str> context)
    -> BuildProductResult<BuiltRuntimeResource> {
    rstd_try(product_known_fields(
        value, context, { "target"_str, "name"_str, "root"_str, "identity"_str, "files"_str }));
    auto target_value = rstd_try(product_member(value, "target"_str, context));
    auto file_values  = rstd_try(product_required_array(value, "files"_str, context));
    auto files        = Vec<PathBuf>::with_capacity(file_values->len());
    for (const auto& item : *file_values) {
        auto text = item.as_str();
        if (text.is_none() || text->is_empty()) {
            return product_failure<BuiltRuntimeResource>(
                rstd::format("{}.files must contain non-empty strings", context));
        }
        files.push(PathBuf::from(*text));
    }
    return Ok(BuiltRuntimeResource {
        .target   = rstd_try(parse_target(*target_value, "build product runtime target"_str)),
        .name     = rstd_try(product_required_string(value, "name"_str, context)),
        .root     = rstd_try(resolve_product_build_path(base, value, "root"_str, context)),
        .identity = rstd_try(product_required_string(value, "identity"_str, context)),
        .files    = rstd::move(files),
    });
}

auto target_runtime_json(const BuiltTargetRuntime& runtime) -> BuildProductResult<Json> {
    auto result = JsonMap::make();
    result.insert(String::make("name"_str), product_string(runtime.name.as_str()));
    result.insert(String::make("path"_str), rstd_try(product_path(runtime.path.as_path())));
    result.insert(String::make("identity"_str), product_string(runtime.identity.as_str()));
    return Ok(Json::Object(rstd::move(result)));
}

auto parse_target_runtime(const Json& value, ref<str> context)
    -> BuildProductResult<BuiltTargetRuntime> {
    rstd_try(product_known_fields(value, context, { "name"_str, "path"_str, "identity"_str }));
    return Ok(BuiltTargetRuntime {
        .name     = rstd_try(product_required_string(value, "name"_str, context)),
        .path     = rstd_try(product_required_path(value, "path"_str, context)),
        .identity = rstd_try(product_required_string(value, "identity"_str, context)),
    });
}

auto external_assets_json(const CompletedBuildProduct& product, const ExternalAssetCatalog& catalog)
    -> BuildProductResult<Json> {
    auto sets = JsonArray::with_capacity(catalog.sets.len());
    for (const auto& set : catalog.sets) {
        auto value = JsonMap::make();
        value.insert(String::make("alias"_str), product_string(set.alias.as_str()));
        value.insert(String::make("name"_str), product_string(set.name.as_str()));
        value.insert(String::make("disposition"_str),
                     product_string(set.disposition == ExternalAssetDisposition::Materialized
                                        ? "materialized"_str
                                        : "provided"_str));
        auto entries = JsonArray::with_capacity(set.entries.len());
        for (const auto& entry : set.entries) {
            auto item = JsonMap::make();
            item.insert(String::make("logical-path"_str),
                        rstd_try(product_path(entry.logical_path.as_path())));
            item.insert(String::make("source"_str),
                        set.disposition == ExternalAssetDisposition::Materialized
                            ? rstd_try(product_build_path(product,
                                                          entry.source.as_path(),
                                                          "build product external asset"_str))
                            : rstd_try(product_path(entry.source.as_path())));
            entries.push(Json::Object(rstd::move(item)));
        }
        value.insert(String::make("entries"_str), Json::Array(rstd::move(entries)));
        sets.push(Json::Object(rstd::move(value)));
    }
    return Ok(Json::Array(rstd::move(sets)));
}

auto parse_external_assets(const Json& value, ref<rstd::path::Path> base, ref<str> context)
    -> BuildProductResult<ExternalAssetCatalog> {
    auto values = value.as_array();
    if (values.is_none()) {
        return product_failure<ExternalAssetCatalog>(rstd::format("{} must be an array", context));
    }
    auto result = ExternalAssetCatalog {};
    for (const auto& set_value : **values) {
        rstd_try(
            product_known_fields(set_value,
                                 "build product external asset set"_str,
                                 { "alias"_str, "name"_str, "disposition"_str, "entries"_str }));
        auto disposition        = rstd_try(product_required_string(
            set_value, "disposition"_str, "build product external asset set"_str));
        auto parsed_disposition = ExternalAssetDisposition::Materialized;
        if (disposition == "provided"_str) {
            parsed_disposition = ExternalAssetDisposition::Provided;
        } else if (disposition != "materialized"_str) {
            return product_failure<ExternalAssetCatalog>(rstd::format(
                "unknown build product external asset disposition '{}'", disposition.as_str()));
        }
        auto entry_values = rstd_try(product_required_array(
            set_value, "entries"_str, "build product external asset set"_str));
        auto entries      = Vec<ExternalAssetEntry>::with_capacity(entry_values->len());
        for (const auto& entry : *entry_values) {
            rstd_try(product_known_fields(entry,
                                          "build product external asset entry"_str,
                                          { "logical-path"_str, "source"_str }));
            auto source =
                parsed_disposition == ExternalAssetDisposition::Materialized
                    ? rstd_try(resolve_product_build_path(
                          base, entry, "source"_str, "build product external asset entry"_str))
                    : rstd_try(product_required_path(
                          entry, "source"_str, "build product external asset entry"_str));
            entries.push(ExternalAssetEntry {
                .logical_path = rstd_try(product_required_path(
                    entry, "logical-path"_str, "build product external asset entry"_str)),
                .source       = rstd::move(source),
            });
        }
        result.sets.push(ExternalAssetSet {
            .alias       = rstd_try(product_required_string(
                set_value, "alias"_str, "build product external asset set"_str)),
            .name        = rstd_try(product_required_string(
                set_value, "name"_str, "build product external asset set"_str)),
            .disposition = parsed_disposition,
            .entries     = rstd::move(entries),
        });
    }
    return Ok(rstd::move(result));
}

auto selected_package_json(const cpp::SelectedPackageMetadata& package)
    -> BuildProductResult<Json> {
    auto result = JsonMap::make();
    result.insert(String::make("name"_str), product_string(package.name.as_str()));
    result.insert(String::make("source"_str), product_string(package.source_identity.as_str()));
    result.insert(String::make("root"_str), rstd_try(product_path(package.root.as_path())));
    if (package.version.is_some()) {
        result.insert(String::make("version"_str), product_string(package.version->as_str()));
    }
    return Ok(Json::Object(rstd::move(result)));
}

auto parse_selected_package(const Json& value, ref<str> context)
    -> BuildProductResult<cpp::SelectedPackageMetadata> {
    rstd_try(product_known_fields(
        value, context, { "name"_str, "version"_str, "source"_str, "root"_str }));
    auto version       = Option<String> {};
    auto version_value = value.get("version"_str);
    if (version_value.is_some()) {
        auto text = (**version_value).as_str();
        if (text.is_none() || text->is_empty()) {
            return product_failure<cpp::SelectedPackageMetadata>(
                rstd::format("{}.version must be a non-empty string", context));
        }
        version = Some(String::make(*text));
    }
    return Ok(cpp::SelectedPackageMetadata {
        .name            = rstd_try(product_required_string(value, "name"_str, context)),
        .version         = rstd::move(version),
        .source_identity = rstd_try(product_required_string(value, "source"_str, context)),
        .root            = rstd_try(product_required_path(value, "root"_str, context)),
    });
}

auto provenance_json(const ExternalSourceProvenance& provenance) -> BuildProductResult<Json> {
    auto result = JsonMap::make();
    result.insert(String::make("package"_str), product_string(provenance.package.as_str()));
    result.insert(String::make("name"_str), product_string(provenance.name.as_str()));
    result.insert(String::make("root"_str),
                  rstd_try(product_path(provenance.materialized_root.as_path())));
    result.insert(String::make("identity"_str),
                  product_string(provenance.stable_source_identity.as_str()));
    return Ok(Json::Object(rstd::move(result)));
}

auto parse_provenance(const Json& value, ref<str> context)
    -> BuildProductResult<ExternalSourceProvenance> {
    rstd_try(product_known_fields(
        value, context, { "package"_str, "name"_str, "root"_str, "identity"_str }));
    return Ok(ExternalSourceProvenance {
        .package                = rstd_try(product_required_string(value, "package"_str, context)),
        .name                   = rstd_try(product_required_string(value, "name"_str, context)),
        .materialized_root      = rstd_try(product_required_path(value, "root"_str, context)),
        .stable_source_identity = rstd_try(product_required_string(value, "identity"_str, context)),
    });
}

auto file_identity_json(const CompletedBuildProduct& product, const BuildProductFileIdentity& file)
    -> BuildProductResult<Json> {
    auto result = JsonMap::make();
    if (file.path.as_path().strip_prefix(product.base_directory.as_path()).is_some()) {
        result.insert(String::make("owner"_str), product_string("build"_str));
        result.insert(
            String::make("path"_str),
            rstd_try(product_build_path(product, file.path.as_path(), "build product file"_str)));
    } else {
        result.insert(String::make("owner"_str), product_string("external"_str));
        result.insert(String::make("path"_str), rstd_try(product_path(file.path.as_path())));
    }
    result.insert(String::make("size"_str), Json::Number(rstd::json::Number::from_u64(file.size)));
    result.insert(String::make("sha256"_str), product_string(file.sha256.to_hex().as_str()));
    return Ok(Json::Object(rstd::move(result)));
}

auto parse_file_identity(const Json& value, ref<rstd::path::Path> base, ref<str> context)
    -> BuildProductResult<BuildProductFileIdentity> {
    rstd_try(product_known_fields(
        value, context, { "owner"_str, "path"_str, "size"_str, "sha256"_str }));
    auto size_value = rstd_try(product_member(value, "size"_str, context));
    auto size       = size_value->as_u64();
    if (size.is_none()) {
        return product_failure<BuildProductFileIdentity>(
            rstd::format("{}.size must be an unsigned integer", context));
    }
    auto digest =
        rstd_try(lito::parse::json::required_sha256(value,
                                                    "sha256"_str,
                                                    lito::parse::NodePath::root(context),
                                                    lito::parse::Sha256TextMode::Canonical));
    auto owner = rstd_try(product_required_string(value, "owner"_str, context));
    auto path  = PathBuf::make();
    if (owner == "build"_str) {
        path = rstd_try(resolve_product_build_path(base, value, "path"_str, context));
    } else if (owner == "external"_str) {
        path = rstd_try(product_required_path(value, "path"_str, context));
        if (! path.as_path().is_absolute()) {
            return product_failure<BuildProductFileIdentity>(
                rstd::format("{}.path '{}' is not absolute", context, path.as_path()));
        }
    } else {
        return product_failure<BuildProductFileIdentity>(
            rstd::format("{}.owner '{}' is unknown", context, owner.as_str()));
    }
    return Ok(BuildProductFileIdentity {
        .path   = rstd::move(path),
        .size   = *size,
        .sha256 = rstd::move(digest),
    });
}

auto product_payload_json(const CompletedBuildProduct& product) -> BuildProductResult<Json> {
    auto root = JsonMap::make();
    root.insert(String::make("project-root"_str),
                rstd_try(product_path(product.project_root.as_path())));
    root.insert(String::make("package"_str), product_string(product.package.as_str()));
    root.insert(String::make("profile"_str), product_string(product.profile.as_str()));
    root.insert(String::make("target"_str), product_string(product.target.as_str()));
    root.insert(String::make("target-architecture"_str),
                product_string(product.target_architecture.as_str()));
    root.insert(String::make("target-kind"_str), product_string(product.target_kind.as_str()));
    root.insert(String::make("android-abi"_str), product_string(product.android_abi.as_str()));
    root.insert(String::make("android-minimum-api"_str),
                Json::Number(
                    rstd::json::Number::from_u64(rstd::as_cast<u64>(product.android_minimum_api))));
    root.insert(String::make("build-directory"_str),
                rstd_try(product_build_path(
                    product, product.build_directory.as_path(), "build product directory"_str)));
    root.insert(String::make("requested-packages"_str),
                product_string_array(product.requested_packages));
    root.insert(String::make("features"_str), product_string_array(product.features.enabled));
    root.insert(String::make("default-features"_str),
                Json::Bool(product.features.default_features));
    root.insert(String::make("compiler-identity"_str),
                product_string(product.compiler_identity.as_str()));
    root.insert(String::make("compiler-version"_str),
                product_string(product.compiler_version.as_str()));

    auto targets = JsonArray::with_capacity(product.selected_targets.len());
    for (const auto& target : product.selected_targets) targets.push(target_json(target));
    root.insert(String::make("selected-targets"_str), Json::Array(rstd::move(targets)));

    auto packages = JsonArray::with_capacity(product.selected_packages.len());
    for (const auto& package : product.selected_packages) {
        packages.push(rstd_try(selected_package_json(package)));
    }
    root.insert(String::make("selected-packages"_str), Json::Array(rstd::move(packages)));

    auto artifacts = JsonArray::with_capacity(product.artifacts.len());
    for (const auto& artifact : product.artifacts) {
        artifacts.push(rstd_try(artifact_json(product, artifact)));
    }
    root.insert(String::make("artifacts"_str), Json::Array(rstd::move(artifacts)));

    auto resources = JsonArray::with_capacity(product.runtime_resources.len());
    for (const auto& resource : product.runtime_resources) {
        resources.push(rstd_try(runtime_resource_json(product, resource)));
    }
    root.insert(String::make("runtime-resources"_str), Json::Array(rstd::move(resources)));

    auto runtimes = JsonArray::with_capacity(product.target_runtimes.len());
    for (const auto& runtime : product.target_runtimes) {
        runtimes.push(rstd_try(target_runtime_json(runtime)));
    }
    root.insert(String::make("target-runtimes"_str), Json::Array(rstd::move(runtimes)));
    root.insert(String::make("external-assets"_str),
                rstd_try(external_assets_json(product, product.external_assets)));

    auto provenance = JsonArray::with_capacity(product.external_source_provenance.len());
    for (const auto& source : product.external_source_provenance) {
        provenance.push(rstd_try(provenance_json(source)));
    }
    root.insert(String::make("external-source-provenance"_str),
                Json::Array(rstd::move(provenance)));

    auto files = JsonArray::with_capacity(product.files.len());
    for (const auto& file : product.files) {
        files.push(rstd_try(file_identity_json(product, file)));
    }
    root.insert(String::make("files"_str), Json::Array(rstd::move(files)));
    return Ok(Json::Object(rstd::move(root)));
}

auto product_identity(const CompletedBuildProduct& product) -> BuildProductResult<String> {
    auto payload = rstd_try(product_payload_json(product));
    auto text    = rstd::json::to_string(payload);
    return Ok(rstd::crypto::sha256_hex(text.as_str()));
}

auto product_json(const CompletedBuildProduct& product) -> BuildProductResult<Json> {
    auto payload = rstd_try(product_payload_json(product));
    (**payload.as_object_mut()).insert(String::make("id"_str), product_string(product.id.as_str()));
    return Ok(rstd::move(payload));
}

auto parse_product(const Json& value, ref<rstd::path::Path> base)
    -> BuildProductResult<CompletedBuildProduct> {
    rstd_try(product_known_fields(value,
                                  "build product"_str,
                                  { "id"_str,
                                    "project-root"_str,
                                    "package"_str,
                                    "profile"_str,
                                    "target"_str,
                                    "target-architecture"_str,
                                    "target-kind"_str,
                                    "android-abi"_str,
                                    "android-minimum-api"_str,
                                    "build-directory"_str,
                                    "requested-packages"_str,
                                    "features"_str,
                                    "default-features"_str,
                                    "selected-targets"_str,
                                    "selected-packages"_str,
                                    "artifacts"_str,
                                    "runtime-resources"_str,
                                    "target-runtimes"_str,
                                    "external-assets"_str,
                                    "external-source-provenance"_str,
                                    "compiler-identity"_str,
                                    "compiler-version"_str,
                                    "files"_str }));
    auto default_value =
        rstd_try(product_member(value, "default-features"_str, "build product"_str));
    auto default_features = default_value->as_bool();
    if (default_features.is_none()) {
        return product_failure<CompletedBuildProduct>(
            "build product.default-features must be a boolean"_str);
    }
    auto android_api_value =
        rstd_try(product_member(value, "android-minimum-api"_str, "build product"_str));
    auto android_api = android_api_value->as_u64();
    if (android_api.is_none() || *android_api > rstd::as_cast<u64>(u32::MAX)) {
        return product_failure<CompletedBuildProduct>(
            "build product.android-minimum-api must be a 32-bit unsigned integer"_str);
    }

    auto selected_target_values =
        rstd_try(product_required_array(value, "selected-targets"_str, "build product"_str));
    auto selected_targets =
        Vec<lito::package::PackageTargetId>::with_capacity(selected_target_values->len());
    for (const auto& target : *selected_target_values) {
        selected_targets.push(rstd_try(parse_target(target, "build product selected target"_str)));
    }

    auto selected_package_values =
        rstd_try(product_required_array(value, "selected-packages"_str, "build product"_str));
    auto selected_packages =
        Vec<cpp::SelectedPackageMetadata>::with_capacity(selected_package_values->len());
    for (const auto& package : *selected_package_values) {
        selected_packages.push(
            rstd_try(parse_selected_package(package, "build product selected package"_str)));
    }

    auto artifact_values =
        rstd_try(product_required_array(value, "artifacts"_str, "build product"_str));
    auto artifacts = Vec<BuiltArtifact>::with_capacity(artifact_values->len());
    for (const auto& artifact : *artifact_values) {
        artifacts.push(rstd_try(parse_artifact(artifact, base, "build product artifact"_str)));
    }

    auto resource_values =
        rstd_try(product_required_array(value, "runtime-resources"_str, "build product"_str));
    auto runtime_resources = Vec<BuiltRuntimeResource>::with_capacity(resource_values->len());
    for (const auto& resource : *resource_values) {
        runtime_resources.push(
            rstd_try(parse_runtime_resource(resource, base, "build product runtime resource"_str)));
    }

    auto runtime_values =
        rstd_try(product_required_array(value, "target-runtimes"_str, "build product"_str));
    auto target_runtimes = Vec<BuiltTargetRuntime>::with_capacity(runtime_values->len());
    for (const auto& runtime : *runtime_values) {
        target_runtimes.push(
            rstd_try(parse_target_runtime(runtime, "build product target runtime"_str)));
    }

    auto asset_value = rstd_try(product_member(value, "external-assets"_str, "build product"_str));
    auto provenance_values = rstd_try(
        product_required_array(value, "external-source-provenance"_str, "build product"_str));
    auto provenance = Vec<ExternalSourceProvenance>::with_capacity(provenance_values->len());
    for (const auto& source : *provenance_values) {
        provenance.push(rstd_try(parse_provenance(source, "build product external source"_str)));
    }

    auto file_values = rstd_try(product_required_array(value, "files"_str, "build product"_str));
    auto files       = Vec<BuildProductFileIdentity>::with_capacity(file_values->len());
    for (const auto& file : *file_values) {
        files.push(rstd_try(parse_file_identity(file, base, "build product file"_str)));
    }

    auto result = CompletedBuildProduct {
        .id = rstd_try(product_required_string(value, "id"_str, "build product"_str)),
        .project_root =
            rstd_try(product_required_path(value, "project-root"_str, "build product"_str)),
        .package = rstd_try(product_required_string(value, "package"_str, "build product"_str)),
        .profile = rstd_try(product_required_string(value, "profile"_str, "build product"_str)),
        .target  = rstd_try(product_required_string(value, "target"_str, "build product"_str)),
        .target_architecture = rstd_try(
            product_required_string(value, "target-architecture"_str, "build product"_str)),
        .target_kind =
            rstd_try(product_required_string(value, "target-kind"_str, "build product"_str)),
        .android_abi =
            rstd_try(product_required_text(value, "android-abi"_str, "build product"_str)),
        .android_minimum_api = rstd::as_cast<u32>(*android_api),
        .base_directory      = PathBuf::from(base),
        .build_directory     = rstd_try(
            resolve_product_build_path(base, value, "build-directory"_str, "build product"_str)),
        .requested_packages = rstd_try(
            parse_product_string_array(value, "requested-packages"_str, "build product"_str)),
        .features =
            lito::package::FeatureSelection {
                .enabled = rstd_try(
                    parse_product_string_array(value, "features"_str, "build product"_str)),
                .default_features = *default_features,
            },
        .selected_targets  = rstd::move(selected_targets),
        .selected_packages = rstd::move(selected_packages),
        .artifacts         = rstd::move(artifacts),
        .runtime_resources = rstd::move(runtime_resources),
        .target_runtimes   = rstd::move(target_runtimes),
        .external_assets   = rstd_try(
            parse_external_assets(*asset_value, base, "build product external-assets"_str)),
        .external_source_provenance = rstd::move(provenance),
        .compiler_identity =
            rstd_try(product_required_string(value, "compiler-identity"_str, "build product"_str)),
        .compiler_version =
            rstd_try(product_required_string(value, "compiler-version"_str, "build product"_str)),
        .files = rstd::move(files),
    };
    if (result.target_kind != "default"_str && result.target_kind != "android"_str) {
        return product_failure<CompletedBuildProduct>(
            rstd::format("build product.target-kind '{}' is unknown", result.target_kind.as_str()));
    }
    if (result.target_kind == "default"_str &&
        (! result.android_abi.is_empty() || result.android_minimum_api != u32 {})) {
        return product_failure<CompletedBuildProduct>(
            "default build product contains Android target fields"_str);
    }
    if (result.target_kind == "android"_str &&
        (result.android_abi.is_empty() || result.android_minimum_api == u32 {})) {
        return product_failure<CompletedBuildProduct>(
            "Android build product is missing target fields"_str);
    }
    auto expected = rstd_try(product_identity(result));
    if (result.id != expected.as_str()) {
        return product_failure<CompletedBuildProduct>(
            rstd::format("build product identity '{}' does not match payload identity '{}'",
                         result.id.as_str(),
                         expected.as_str()));
    }
    return Ok(rstd::move(result));
}

struct BuildProductLayout {
    PathBuf base;
    PathBuf state;
    PathBuf lock;
};

auto product_layout(ref<rstd::path::Path> base) -> BuildProductLayout {
    auto metadata = PathBuf::from(base).join(PathBuf::from(".lito"_str).as_path());
    return BuildProductLayout {
        .base  = PathBuf::from(base),
        .state = metadata.join(PathBuf::from("build-product.json"_str).as_path()),
        .lock  = metadata.join(PathBuf::from("build-product.lock"_str).as_path()),
    };
}

auto acquire_product_lock(ref<rstd::path::Path> path, rstd::fs::FileLockMode mode, bool create)
    -> BuildProductResult<rstd::fs::FileLock> {
    auto options = rstd::fs::OpenOptions::make().read(true).write(true);
    if (create) options.create(true);
    auto opened = options.open(path);
    if (opened.is_err()) {
        return product_io_failure<rstd::fs::FileLock>(
            "open build product lock"_str, path, rstd::move(opened).unwrap_err());
    }
    auto locked = rstd::fs::FileLock::acquire(rstd::move(opened).unwrap(), mode);
    if (locked.is_err()) {
        return product_io_failure<rstd::fs::FileLock>(
            "lock build product"_str, path, rstd::move(locked).unwrap_err());
    }
    return Ok(rstd::move(locked).unwrap());
}

auto read_product_state(ref<rstd::path::Path> path) -> BuildProductResult<Json> {
    auto contents = rstd::fs::read_to_string(path);
    if (contents.is_err()) {
        return product_io_failure<Json>(
            "read build product"_str, path, rstd::move(contents).unwrap_err());
    }
    auto parsed = rstd::json::from_str(contents->as_str(),
                                       rstd::json::ParseOptions { .reject_duplicate_keys = true });
    if (parsed.is_err()) {
        return Err(BuildProductError::Json(PathBuf::from(path), rstd::move(parsed).unwrap_err()));
    }
    return Ok(rstd::move(parsed).unwrap());
}

auto state_generation(const Json& value, ref<str> expected_state) -> BuildProductResult<String> {
    rstd_try(product_known_fields(
        value,
        "build product state"_str,
        expected_state == "complete"_str
            ? initializer_list<ref<str>> { "schema"_str,
                                           "state"_str,
                                           "generation"_str,
                                           "product"_str }
            : initializer_list<ref<str>> { "schema"_str, "state"_str, "generation"_str }));
    auto schema_value = rstd_try(product_member(value, "schema"_str, "build product state"_str));
    auto schema       = schema_value->as_u64();
    if (schema.is_none() || *schema != BUILD_PRODUCT_SCHEMA) {
        return product_failure<String>("build product uses an unsupported schema"_str);
    }
    auto state = rstd_try(product_required_string(value, "state"_str, "build product state"_str));
    if (state != expected_state) {
        return product_failure<String>(rstd::format(
            "build product state is '{}', expected '{}'", state.as_str(), expected_state));
    }
    return product_required_string(value, "generation"_str, "build product state"_str);
}

auto write_product_state(ref<rstd::path::Path> path, Json value) -> BuildProductResult<empty> {
    auto text = rstd::json::to_string(
        value, rstd::json::FormatOptions { .pretty = true, .indent = usize(2) });
    text.push_ascii('\n');
    auto written = rstd::fs::write_atomic(path, text.as_str().as_bytes());
    if (written.is_err()) {
        return product_io_failure<empty>(
            "write build product"_str, path, rstd::move(written).unwrap_err());
    }
    return Ok(empty {});
}

auto append_file_identity(Vec<BuildProductFileIdentity>& files, ref<rstd::path::Path> requested)
    -> BuildProductResult<empty> {
    auto inspected = rstd::fs::symlink_metadata(requested);
    if (inspected.is_err()) {
        return product_io_failure<empty>(
            "inspect build product file"_str, requested, rstd::move(inspected).unwrap_err());
    }
    if (! inspected->is_file() || inspected->is_symlink()) {
        return product_failure<empty>(
            rstd::format("build product file '{}' is not a regular non-symlink file", requested));
    }
    auto canonical = rstd::fs::canonicalize(requested);
    if (canonical.is_err()) {
        return product_io_failure<empty>(
            "resolve build product file"_str, requested, rstd::move(canonical).unwrap_err());
    }
    for (const auto& file : files) {
        if (file.path.as_path() == canonical->as_path()) return Ok(empty {});
    }
    auto contents = rstd::fs::read(canonical->as_path());
    if (contents.is_err()) {
        return product_io_failure<empty>(
            "read build product file"_str, canonical->as_path(), rstd::move(contents).unwrap_err());
    }
    files.push(BuildProductFileIdentity {
        .path   = rstd::move(canonical).unwrap(),
        .size   = inspected->len(),
        .sha256 = rstd::crypto::sha256_digest(contents->as_slice()),
    });
    return Ok(empty {});
}

auto append_optional_input(Vec<BuildProductFileIdentity>& files, ref<rstd::path::Path> path)
    -> BuildProductResult<empty> {
    auto exists = rstd::fs::exists(path);
    if (exists.is_err()) {
        return product_io_failure<empty>(
            "inspect build product input"_str, path, rstd::move(exists).unwrap_err());
    }
    if (! *exists) return Ok(empty {});
    return append_file_identity(files, path);
}

auto same_strings(const Vec<String>& left, const Vec<String>& right) -> bool {
    if (left.len() != right.len()) return false;
    auto left_values  = as<Clone>(left).clone();
    auto right_values = as<Clone>(right).clone();
    rstd::slice_::sort_unstable(left_values.as_mut_slice().as_mut_ref());
    rstd::slice_::sort_unstable(right_values.as_mut_slice().as_mut_ref());
    for (usize index {}; index < left_values.len(); ++index) {
        if (left_values[index] != right_values[index].as_str()) return false;
    }
    return true;
}

auto validate_canonical_directory(ref<rstd::path::Path> path, ref<str> context)
    -> BuildProductResult<empty> {
    if (! path.is_absolute()) {
        return product_failure<empty>(rstd::format("{} '{}' is not absolute", context, path));
    }
    auto metadata = rstd::fs::symlink_metadata(path);
    if (metadata.is_err()) {
        return product_io_failure<empty>(
            "inspect completed build directory"_str, path, rstd::move(metadata).unwrap_err());
    }
    if (! metadata->is_dir() || metadata->is_symlink()) {
        return product_failure<empty>(
            rstd::format("{} '{}' is not a non-symlink directory", context, path));
    }
    auto canonical = rstd::fs::canonicalize(path);
    if (canonical.is_err()) {
        return product_io_failure<empty>(
            "resolve completed build directory"_str, path, rstd::move(canonical).unwrap_err());
    }
    if (canonical->as_path() != path) {
        return product_failure<empty>(
            rstd::format("{} '{}' is not its canonical path", context, path));
    }
    return Ok(empty {});
}

auto validate_normal_relative_path(ref<rstd::path::Path> path, ref<str> context)
    -> BuildProductResult<empty> {
    if (path.is_empty() || path.is_absolute() || path.has_root()) {
        return product_failure<empty>(rstd::format("{} '{}' is not relative", context, path));
    }
    auto components = path.components();
    for (auto component = components.next(); component.is_some(); component = components.next()) {
        if (! component->is_normal()) {
            return product_failure<empty>(
                rstd::format("{} '{}' contains a non-normal component", context, path));
        }
    }
    return Ok(empty {});
}

auto validate_canonical_file(ref<rstd::path::Path> path, ref<str> context)
    -> BuildProductResult<empty> {
    if (! path.is_absolute()) {
        return product_failure<empty>(rstd::format("{} '{}' is not absolute", context, path));
    }
    auto metadata = rstd::fs::symlink_metadata(path);
    if (metadata.is_err()) {
        return product_io_failure<empty>(
            "inspect completed build file"_str, path, rstd::move(metadata).unwrap_err());
    }
    if (! metadata->is_file() || metadata->is_symlink()) {
        return product_failure<empty>(
            rstd::format("{} '{}' is not a regular non-symlink file", context, path));
    }
    auto canonical = rstd::fs::canonicalize(path);
    if (canonical.is_err()) {
        return product_io_failure<empty>(
            "resolve completed build file"_str, path, rstd::move(canonical).unwrap_err());
    }
    if (canonical->as_path() != path) {
        return product_failure<empty>(
            rstd::format("{} '{}' is not its canonical path", context, path));
    }
    return Ok(empty {});
}

auto validate_build_owned_file(const CompletedBuildProduct& product,
                               ref<rstd::path::Path>        path,
                               ref<str>                     context) -> BuildProductResult<empty> {
    rstd_try(validate_canonical_file(path, context));
    if (path.strip_prefix(product.build_directory.as_path()).is_none()) {
        return product_failure<empty>(rstd::format("{} '{}' escapes build directory '{}'",
                                                   context,
                                                   path,
                                                   product.build_directory.as_path()));
    }
    return Ok(empty {});
}

auto validate_product_path_ownership(const CompletedBuildProduct& product)
    -> BuildProductResult<empty> {
    rstd_try(validate_canonical_directory(product.project_root.as_path(),
                                          "build product project root"_str));
    rstd_try(validate_canonical_directory(product.base_directory.as_path(),
                                          "build product base directory"_str));
    rstd_try(validate_canonical_directory(product.build_directory.as_path(),
                                          "build product directory"_str));
    if (product.build_directory.as_path()
            .strip_prefix(product.base_directory.as_path())
            .is_none()) {
        return product_failure<empty>(
            rstd::format("build product directory '{}' escapes base directory '{}'",
                         product.build_directory.as_path(),
                         product.base_directory.as_path()));
    }
    for (const auto& package : product.selected_packages) {
        rstd_try(
            validate_canonical_directory(package.root.as_path(), "build product package root"_str));
    }
    for (const auto& artifact : product.artifacts) {
        rstd_try(validate_build_owned_file(
            product, artifact.path.as_path(), "completed build artifact"_str));
        rstd_try(validate_canonical_directory(artifact.package_root.as_path(),
                                              "completed build artifact package root"_str));
    }
    for (const auto& resource : product.runtime_resources) {
        rstd_try(validate_canonical_directory(resource.root.as_path(),
                                              "completed build runtime resource root"_str));
        if (resource.root.as_path().strip_prefix(product.build_directory.as_path()).is_none()) {
            return product_failure<empty>(rstd::format(
                "completed build runtime resource root '{}' escapes build directory '{}'",
                resource.root.as_path(),
                product.build_directory.as_path()));
        }
        for (const auto& relative : resource.files) {
            rstd_try(validate_normal_relative_path(relative.as_path(),
                                                   "completed build runtime resource path"_str));
            auto file = resource.root.join(relative.as_path());
            rstd_try(validate_build_owned_file(
                product, file.as_path(), "completed build runtime resource"_str));
        }
    }
    for (const auto& runtime : product.target_runtimes) {
        rstd_try(
            validate_canonical_file(runtime.path.as_path(), "completed build target runtime"_str));
    }
    for (const auto& set : product.external_assets.sets) {
        for (const auto& entry : set.entries) {
            rstd_try(validate_normal_relative_path(entry.logical_path.as_path(),
                                                   "completed build external asset path"_str));
            if (set.disposition == ExternalAssetDisposition::Materialized) {
                rstd_try(validate_build_owned_file(
                    product, entry.source.as_path(), "completed build external asset"_str));
            }
        }
    }
    for (const auto& source : product.external_source_provenance) {
        rstd_try(validate_canonical_directory(source.materialized_root.as_path(),
                                              "completed build external source"_str));
    }
    return Ok(empty {});
}

auto validate_recorded_file_reference(const CompletedBuildProduct& product,
                                      ref<rstd::path::Path>        path,
                                      ref<str> context) -> BuildProductResult<empty> {
    auto metadata = rstd::fs::symlink_metadata(path);
    if (metadata.is_err()) {
        return product_io_failure<empty>(
            "inspect completed build reference"_str, path, rstd::move(metadata).unwrap_err());
    }
    if (! metadata->is_file() || metadata->is_symlink()) {
        return product_failure<empty>(
            rstd::format("{} '{}' is not a regular non-symlink file", context, path));
    }
    auto canonical = rstd::fs::canonicalize(path);
    if (canonical.is_err()) {
        return product_io_failure<empty>(
            "resolve completed build reference"_str, path, rstd::move(canonical).unwrap_err());
    }
    for (const auto& file : product.files) {
        if (file.path.as_path() == canonical->as_path()) return Ok(empty {});
    }
    return product_failure<empty>(rstd::format("{} '{}' has no file identity", context, path));
}

auto validate_product_file_identities(const CompletedBuildProduct& product)
    -> BuildProductResult<empty> {
    rstd_try(validate_product_path_ownership(product));
    for (const auto& file : product.files) {
        rstd_try(validate_canonical_file(file.path.as_path(), "completed build file identity"_str));
        auto inspected = rstd::fs::symlink_metadata(file.path.as_path());
        if (inspected.is_err()) {
            return product_io_failure<empty>("inspect completed build file"_str,
                                             file.path.as_path(),
                                             rstd::move(inspected).unwrap_err());
        }
        if (inspected->len() != file.size) {
            return product_failure<empty>(
                rstd::format("completed build file '{}' size changed from {} to {}",
                             file.path.as_path(),
                             file.size,
                             inspected->len()));
        }
        auto contents = rstd::fs::read(file.path.as_path());
        if (contents.is_err()) {
            return product_io_failure<empty>("read completed build file"_str,
                                             file.path.as_path(),
                                             rstd::move(contents).unwrap_err());
        }
        auto digest = rstd::crypto::sha256_digest(contents->as_slice());
        if (digest != file.sha256) {
            return product_failure<empty>(
                rstd::format("completed build file '{}' content changed", file.path.as_path()));
        }
    }
    for (const auto& artifact : product.artifacts) {
        rstd_try(validate_recorded_file_reference(
            product, artifact.path.as_path(), "completed build artifact"_str));
    }
    for (const auto& resource : product.runtime_resources) {
        for (const auto& relative : resource.files) {
            auto path = resource.root.join(relative.as_path());
            rstd_try(validate_recorded_file_reference(
                product, path.as_path(), "completed build runtime resource"_str));
        }
    }
    for (const auto& runtime : product.target_runtimes) {
        rstd_try(validate_recorded_file_reference(
            product, runtime.path.as_path(), "completed build target runtime"_str));
    }
    for (const auto& set : product.external_assets.sets) {
        if (set.disposition == ExternalAssetDisposition::Provided) continue;
        for (const auto& entry : set.entries) {
            rstd_try(validate_recorded_file_reference(
                product, entry.source.as_path(), "completed build external asset"_str));
        }
    }
    return Ok(empty {});
}

} // namespace lito

namespace lito
{

auto resolve_build_base_directory(ref<rstd::path::Path> project_root,
                                  ref<rstd::path::Path> requested,
                                  ref<str>              profile) -> PathBuf {
    return PathBuf::from(BuildDirectory::resolve(project_root, requested, profile).path());
}

auto begin_build_product_publication(ref<rstd::path::Path> project_root,
                                     ref<rstd::path::Path> requested,
                                     ref<str>              profile)
    -> BuildProductResult<BuildProductPublication> {
    auto base    = resolve_build_base_directory(project_root, requested, profile);
    auto layout  = product_layout(base.as_path());
    auto parent  = layout.state.as_path().parent().unwrap();
    auto created = rstd::fs::create_dir_all(parent);
    if (created.is_err()) {
        return product_io_failure<BuildProductPublication>(
            "create build product directory"_str, parent, rstd::move(created).unwrap_err());
    }
    auto canonical_base = rstd::fs::canonicalize(layout.base.as_path());
    if (canonical_base.is_err()) {
        return product_io_failure<BuildProductPublication>("resolve build product directory"_str,
                                                           layout.base.as_path(),
                                                           rstd::move(canonical_base).unwrap_err());
    }
    layout    = product_layout(canonical_base->as_path());
    auto lock = rstd_try(
        acquire_product_lock(layout.lock.as_path(), rstd::fs::FileLockMode::Exclusive, true));
    auto time       = rstd::time::SystemTime::now().as_unix_time();
    auto generation = rstd::format("{}-{}-{}", rstd::process::id(), time.seconds, time.nanoseconds);
    auto state      = JsonMap::make();
    state.insert(String::make("schema"_str),
                 Json::Number(rstd::json::Number::from_u64(BUILD_PRODUCT_SCHEMA)));
    state.insert(String::make("state"_str), product_string("building"_str));
    state.insert(String::make("generation"_str), product_string(generation.as_str()));
    rstd_try(write_product_state(layout.state.as_path(), Json::Object(rstd::move(state))));
    return Ok(BuildProductPublication {
        .base_directory = rstd::move(layout.base),
        .state          = rstd::move(layout.state),
        .lock           = rstd::move(layout.lock),
        .generation     = rstd::move(generation),
    });
}

auto finalize_completed_build_product(CompletedBuildProduct   result,
                                      const lock::LockConfig& lock_config,
                                      const Vec<PathBuf>&     inputs)
    -> BuildProductResult<CompletedBuildProduct> {
    auto project_root = rstd::fs::canonicalize(result.project_root.as_path());
    if (project_root.is_err()) {
        return product_io_failure<CompletedBuildProduct>("resolve build product project root"_str,
                                                         result.project_root.as_path(),
                                                         rstd::move(project_root).unwrap_err());
    }
    result.project_root = rstd::move(project_root).unwrap();
    auto base_directory = rstd::fs::canonicalize(result.base_directory.as_path());
    if (base_directory.is_err()) {
        return product_io_failure<CompletedBuildProduct>("resolve build product base directory"_str,
                                                         result.base_directory.as_path(),
                                                         rstd::move(base_directory).unwrap_err());
    }
    result.base_directory = rstd::move(base_directory).unwrap();
    auto build_directory  = rstd::fs::canonicalize(result.build_directory.as_path());
    if (build_directory.is_err()) {
        return product_io_failure<CompletedBuildProduct>("resolve completed build directory"_str,
                                                         result.build_directory.as_path(),
                                                         rstd::move(build_directory).unwrap_err());
    }
    result.build_directory = rstd::move(build_directory).unwrap();
    rstd_try(validate_product_path_ownership(result));

    for (const auto& artifact : result.artifacts) {
        rstd_try(append_file_identity(result.files, artifact.path.as_path()));
    }
    for (const auto& resource : result.runtime_resources) {
        for (const auto& relative : resource.files) {
            auto path = resource.root.join(relative.as_path());
            rstd_try(append_file_identity(result.files, path.as_path()));
        }
    }
    for (const auto& runtime : result.target_runtimes) {
        rstd_try(append_file_identity(result.files, runtime.path.as_path()));
    }
    for (const auto& set : result.external_assets.sets) {
        if (set.disposition == ExternalAssetDisposition::Provided) continue;
        for (const auto& entry : set.entries) {
            rstd_try(append_file_identity(result.files, entry.source.as_path()));
        }
    }
    for (const auto& input : inputs) {
        rstd_try(append_file_identity(result.files, input.as_path()));
    }
    auto root_manifest = result.project_root.join(PathBuf::from("lito.toml"_str).as_path());
    rstd_try(append_optional_input(result.files, root_manifest.as_path()));
    for (const auto& package : result.selected_packages) {
        auto manifest = package.root.join(PathBuf::from("lito.toml"_str).as_path());
        rstd_try(append_optional_input(result.files, manifest.as_path()));
        auto build_script = package.root.join(PathBuf::from("build.lua"_str).as_path());
        rstd_try(append_optional_input(result.files, build_script.as_path()));
        auto install_script = package.root.join(PathBuf::from("install.lua"_str).as_path());
        rstd_try(append_optional_input(result.files, install_script.as_path()));
    }
    auto lock_path = lito::lock::resolve_lock_path(result.project_root.as_path(), lock_config);
    rstd_try(append_optional_input(result.files, lock_path.as_path()));
    result.id = rstd_try(product_identity(result));
    return Ok(rstd::move(result));
}

auto complete_build_product_publication(const BuildProductPublication& publication,
                                        const CompletedBuildProduct&   product)
    -> BuildProductResult<empty> {
    auto lock = rstd_try(
        acquire_product_lock(publication.lock.as_path(), rstd::fs::FileLockMode::Exclusive, true));
    auto current    = rstd_try(read_product_state(publication.state.as_path()));
    auto generation = rstd_try(state_generation(current, "building"_str));
    if (generation != publication.generation.as_str()) {
        return product_failure<empty>(
            rstd::format("build product generation '{}' was superseded by '{}'",
                         publication.generation.as_str(),
                         generation.as_str()));
    }
    if (product.base_directory.as_path() != publication.base_directory.as_path()) {
        return product_failure<empty>(
            rstd::format("completed build base directory '{}' does not match publication base '{}'",
                         product.base_directory.as_path(),
                         publication.base_directory.as_path()));
    }
    auto expected_identity = rstd_try(product_identity(product));
    if (product.id != expected_identity.as_str()) {
        return product_failure<empty>(
            rstd::format("completed build identity '{}' does not match payload identity '{}'",
                         product.id.as_str(),
                         expected_identity.as_str()));
    }
    rstd_try(validate_product_file_identities(product));
    auto state = JsonMap::make();
    state.insert(String::make("schema"_str),
                 Json::Number(rstd::json::Number::from_u64(BUILD_PRODUCT_SCHEMA)));
    state.insert(String::make("state"_str), product_string("complete"_str));
    state.insert(String::make("generation"_str), product_string(generation.as_str()));
    state.insert(String::make("product"_str), rstd_try(product_json(product)));
    return write_product_state(publication.state.as_path(), Json::Object(rstd::move(state)));
}

auto load_completed_build_product(ref<rstd::path::Path> project_root,
                                  ref<rstd::path::Path> requested,
                                  ref<str> profile) -> BuildProductResult<CompletedBuildProduct> {
    auto base           = resolve_build_base_directory(project_root, requested, profile);
    auto canonical_base = rstd::fs::canonicalize(base.as_path());
    if (canonical_base.is_err()) {
        return product_io_failure<CompletedBuildProduct>("resolve build product directory"_str,
                                                         base.as_path(),
                                                         rstd::move(canonical_base).unwrap_err());
    }
    base        = rstd::move(canonical_base).unwrap();
    auto layout = product_layout(base.as_path());
    auto lock   = rstd_try(
        acquire_product_lock(layout.lock.as_path(), rstd::fs::FileLockMode::Shared, false));
    auto state = rstd_try(read_product_state(layout.state.as_path()));
    auto state_text =
        rstd_try(product_required_string(state, "state"_str, "build product state"_str));
    if (state_text == "building"_str) {
        rstd_try(state_generation(state, "building"_str));
        return product_failure<CompletedBuildProduct>(rstd::format(
            "build directory '{}' does not contain a completed build", base.as_path()));
    }
    rstd_try(state_generation(state, "complete"_str));
    auto product_value = rstd_try(product_member(state, "product"_str, "build product state"_str));
    auto product       = rstd_try(parse_product(*product_value, base.as_path()));
    if (product.build_directory.as_path().strip_prefix(base.as_path()).is_none()) {
        return product_failure<CompletedBuildProduct>(
            rstd::format("completed build directory '{}' is outside requested build directory '{}'",
                         product.build_directory.as_path(),
                         base.as_path()));
    }
    auto validated = validate_product_file_identities(product);
    if (validated.is_err()) {
        return Err(rstd::move(validated).unwrap_err());
    }
    return Ok(rstd::move(product));
}

auto validate_completed_build_product(const CompletedBuildProduct& product,
                                      const BuildRequest&          request,
                                      ref<str>                     profile,
                                      ref<str> target) -> BuildProductResult<empty> {
    auto project_root = rstd::fs::canonicalize(request.selection.root.as_path());
    if (project_root.is_err()) {
        return product_io_failure<empty>("resolve install project root"_str,
                                         request.selection.root.as_path(),
                                         rstd::move(project_root).unwrap_err());
    }
    if (project_root->as_path() != product.project_root.as_path()) {
        return product_failure<empty>(
            rstd::format("completed build belongs to project '{}', not '{}'",
                         product.project_root.as_path(),
                         project_root->as_path()));
    }
    if (product.profile != profile) {
        return product_failure<empty>(rstd::format(
            "completed build profile is '{}', requested '{}'", product.profile.as_str(), profile));
    }
    if (product.target != target) {
        return product_failure<empty>(rstd::format(
            "completed build target is '{}', requested '{}'", product.target.as_str(), target));
    }
    if (request.configuration.target.is_Default()) {
        if (product.target_kind != "default"_str) {
            return product_failure<empty>(
                rstd::format("completed build target kind is '{}', requested 'default'",
                             product.target_kind.as_str()));
        }
    } else {
        const auto& android = request.configuration.target.as_Android().target;
        if (product.target_kind != "android"_str || product.android_abi != android.abi.as_str() ||
            product.android_minimum_api != android.minimum_api) {
            return product_failure<empty>(
                rstd::format("completed Android target is '{}', API {}; requested '{}', API {}",
                             product.android_abi.as_str(),
                             product.android_minimum_api,
                             android.abi.as_str(),
                             android.minimum_api));
        }
    }
    if (! same_strings(product.requested_packages, request.selection.packages)) {
        return product_failure<empty>("completed build package selection does not match"_str);
    }
    if (product.features.default_features != request.selection.features.default_features ||
        ! same_strings(product.features.enabled, request.selection.features.enabled)) {
        return product_failure<empty>("completed build feature selection does not match"_str);
    }
    return Ok(empty {});
}

} // namespace lito
