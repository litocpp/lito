module;
#include <rstd/macro.hpp>

module lito.driver;

import rstd;
import lito.core;
import lito.pack;
import :package.builtin;
import :registry.blob;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace
{

template<typename T>
auto embedded_failure(String message) -> lito::registry::RegistryGraphResult<T> {
    return Err(lito::registry::RegistryGraphError { .message = rstd::move(message) });
}

template<typename T>
auto embedded_failure(ref<str> message) -> lito::registry::RegistryGraphResult<T> {
    return embedded_failure<T>(String::make(message));
}

auto registry_config(const lito::config::LitoBootstrapConfig& config,
                     const lito::registry::RegistryId&        identity)
    -> Option<ref<lito::config::NamedRegistryConfig>> {
    for (const auto& registry : *config.registries()) {
        if (registry.identity == identity) {
            return Some(
                ref<lito::config::NamedRegistryConfig>::from_raw_parts(rstd::addressof(registry)));
        }
    }
    return None();
}

auto descriptor_error(ref<str> id, ref<str> message) -> String {
    return rstd::format("embedded package '{}': {}", id, message);
}

} // namespace

auto lito::package::EmbeddedRegistryPackages::resolve(ref<str> id)
    -> lito::registry::RegistryGraphResult<lito::registry::BuiltinRegistryPackage> {
    if (provider_.resolve == nullptr) {
        return embedded_failure<lito::registry::BuiltinRegistryPackage>(
            rstd::format("builtin package '{}' has no embedded provider", id));
    }
    auto input = provider_.resolve(provider_.context, id);
    if (input.is_none()) {
        return embedded_failure<lito::registry::BuiltinRegistryPackage>(
            rstd::format("builtin package '{}' is not provided by this executable", id));
    }
    auto descriptor = lito::registry::parse_verified_publish_candidate(input->descriptor);
    if (descriptor.is_err()) {
        return embedded_failure<lito::registry::BuiltinRegistryPackage>(
            descriptor_error(id, rstd::move(descriptor).unwrap_err().message.as_str()));
    }
    auto definition = lito::registry::BuiltinRegistryPackage {
        .package = descriptor->package.clone(),
        .version = descriptor->version.clone(),
    };
    for (const auto& index : indices_) {
        if (index.package() == descriptor->package) return Ok(rstd::move(definition));
    }
    auto configured = registry_config(*config_, descriptor->package.registry);
    if (configured.is_none()) {
        return embedded_failure<lito::registry::BuiltinRegistryPackage>(
            rstd::format("builtin package '{}' uses Registry '{}' which is not configured",
                         id,
                         descriptor->package.registry.as_str()));
    }
    auto cache =
        lito::registry::RegistryBlobCache(cache_root_.clone(),
                                          (*configured)->effective_endpoints()->blob.clone(),
                                          lito::registry::RegistryNetworkPolicy::Offline,
                                          {});
    auto blob = cache.publish(descriptor->package, input->archive);
    if (blob.is_err()) {
        return embedded_failure<lito::registry::BuiltinRegistryPackage>(
            descriptor_error(id, rstd::move(blob).unwrap_err().message.as_str()));
    }
    if (blob->checksum != descriptor->archive.checksum ||
        blob->size != descriptor->archive.size.value()) {
        return embedded_failure<lito::registry::BuiltinRegistryPackage>(
            descriptor_error(id, "archive does not match its verified descriptor"_str));
    }
    auto inspected = lito::registry::PackageArchiveInspector::inspect_candidate(
        *blob, descriptor->package, descriptor->version);
    if (inspected.is_err()) {
        return embedded_failure<lito::registry::BuiltinRegistryPackage>(
            descriptor_error(id, rstd::move(inspected).unwrap_err().message.as_str()));
    }
    if (inspected->archive.checksum != descriptor->archive.checksum ||
        inspected->archive.size != descriptor->archive.size ||
        inspected->archive.format != descriptor->archive.format ||
        inspected->candidate.file_count != descriptor->file_count ||
        inspected->candidate.unpacked_size != descriptor->unpacked_size ||
        ! lito::registry::registry_dependencies_match(inspected->candidate.dependencies.as_slice(),
                                                      descriptor->dependencies.as_slice())) {
        return embedded_failure<lito::registry::BuiltinRegistryPackage>(
            descriptor_error(id, "descriptor does not describe the embedded archive"_str));
    }
    auto dependencies = Vec<lito::registry::RegistryDependencyProjection>::with_capacity(
        descriptor->dependencies.len());
    for (const auto& dependency : descriptor->dependencies) {
        dependencies.push(dependency.clone());
    }
    auto index = lito::registry::RegistryPackageIndex::single(descriptor->package.clone(),
                                                              descriptor->version.clone(),
                                                              descriptor->archive.checksum.clone(),
                                                              rstd::move(dependencies));
    if (index.is_err()) {
        return embedded_failure<lito::registry::BuiltinRegistryPackage>(
            rstd::format("embedded package '{}': {}", id, rstd::move(index).unwrap_err()));
    }
    indices_.push(rstd::move(index).unwrap());
    return Ok(rstd::move(definition));
}

auto lito::package::EmbeddedRegistryPackages::add_indices(
    lito::registry::RegistryGraphClient& client) const -> void {
    for (const auto& index : indices_) client.add_index(index.clone());
}
