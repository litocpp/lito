module;

#include <rstd/macro.hpp>

export module lito.test.support.install;

import rstd;
import lito;
import lito.lock;
import lito.package;
import lito.package.graph_contract;
import lito.workspace.contract;
import lito.workspace.resolver;
import lito.platform;
import lito.dependency;
import lito.dependency.cmake;
import lito.source;
import lito.manifest;
import lito.toolchain;
import lito.build.discovery;
import lito.build.layout;
import lito.system.environment;
import lito.system.process;
import lito.system.storage;
import lito.test.base_support;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

export namespace lito_test
{
auto local_provenance(ref<rstd::path::Path> root) -> lito::InstallSourceProvenance {
    return lito::InstallSourceProvenance::Local(PathBuf::from(root),
                                                lito::path_source_identity(root));
}

auto managed_destination(ref<rstd::path::Path> root) -> lito::InstallDestination {
    return lito::InstallDestination::Managed(lito::InstallRoot { .path = PathBuf::from(root) });
}

auto install_script_input(const lito::PackageManifest& manifest) -> lito::PackageInstallInput {
    auto root = manifest.root.clone();
    return lito::PackageInstallInput {
        .name          = manifest.name.clone(),
        .version       = manifest.version.value->clone(),
        .root          = manifest.root.clone(),
        .manifest_path = manifest.manifest_path.clone(),
        .script        = Some(manifest.install_script->clone()),
        .source =
            lito::ResolvedPackageSource {
                .identity       = lito::path_source_identity(root.as_path()),
                .kind           = lito::PackageSourceKind::Path,
                .root_directory = rstd::move(root),
                .path           = PathBuf::from("."_str),
            },
        .direct = true,
    };
}

} // namespace lito_test
