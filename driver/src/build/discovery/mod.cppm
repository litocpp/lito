export module lito.driver:build.discovery;

import rstd;
import lito.core;
import lito.cpp;

using namespace rstd::prelude;

export namespace lito
{

auto discover_explicit_sources(const cpp::ResolvedTarget& target)
    -> cpp::SourceDiscoveryResult<cpp::ResolvedSourceSet>;
auto discover_format_sources(const lito::manifest::PackageManifest& manifest)
    -> cpp::SourceDiscoveryResult<cpp::ResolvedSourceSet>;
auto resolve_source_target(const cpp::PackageMetadata&          package,
                           const cpp::ResolvedNativeTargetPlan& discovery,
                           ref<rstd::path::Path>                source)
    -> cpp::SourceDiscoveryResult<cpp::TargetId>;

} // namespace lito
