export module lito.driver:package.builtin;

import rstd;
import lito.core;

using namespace rstd::prelude;

export namespace lito::package
{

struct BuiltinPackage {
    String                          source_identity;
    String                          digest;
    lito::manifest::PackageManifest manifest;
    lito::source::SourceTree        source;
};

auto load_builtin_package(ref<str> id) -> PackageResult<BuiltinPackage>;

} // namespace lito::package
