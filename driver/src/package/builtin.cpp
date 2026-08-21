module;
#include <rstd/macro.hpp>

module lito.driver;

import rstd;
import lito.core;
import :package.builtin;

using namespace rstd::prelude;
using namespace rstd::literals;

static constexpr unsigned char QT_PACKAGE_MANIFEST[] = {
#embed "../../../data/script-packages/qt/lito.toml"
};
static constexpr unsigned char QT_PACKAGE_MODULE[] = {
#embed "../../../data/script-packages/qt/lib.lua"
};
static constexpr unsigned char QT_PACKAGE_MOC[] = {
#embed "../../../data/script-packages/qt/qt/moc.lua"
};
static constexpr unsigned char QT_PACKAGE_QML[] = {
#embed "../../../data/script-packages/qt/qt/qml.lua"
};

template<rstd::size_t Size>
auto embedded_text(const unsigned char (&contents)[Size]) noexcept -> ref<str> {
    return ref<str>::from_raw_parts_unchecked(reinterpret_cast<const byte*>(contents), usize(Size));
}

template<typename T>
auto builtin_failure(String message) -> lito::package::PackageResult<T> {
    return Err(lito::package::PackageError::Message(rstd::move(message)));
}

auto qt_source_tree() -> lito::package::PackageResult<lito::source::SourceTree> {
    auto tree = lito::source::SourceTree::make();
    const struct EmbeddedFile {
        ref<str> path;
        ref<str> contents;
    } files[] = {
        { "lito.toml"_str, embedded_text(QT_PACKAGE_MANIFEST) },
        { "lib.lua"_str, embedded_text(QT_PACKAGE_MODULE) },
        { "qt/moc.lua"_str, embedded_text(QT_PACKAGE_MOC) },
        { "qt/qml.lua"_str, embedded_text(QT_PACKAGE_QML) },
    };
    for (const auto& file : files) {
        auto added = tree.add_text(file.path, file.contents);
        if (added.is_err()) {
            return builtin_failure<lito::source::SourceTree>(
                rstd::format("cannot add '{}' to builtin build package '{}': {}",
                             file.path,
                             "qt",
                             rstd::move(added).unwrap_err()));
        }
    }
    return Ok(rstd::move(tree));
}

auto builtin_digest(ref<str> id, const lito::source::SourceTree& tree) -> String {
    auto identity = rstd::format("lito-builtin-package-v2\n{}", id);
    for (const auto& entry : tree.entries()) {
        identity.push_ascii('\n');
        identity.push_str(entry.path().as_str());
        identity.push_ascii('\n');
        identity.push_str(rstd::crypto::sha256_hex(entry.contents()).as_str());
    }
    return rstd::crypto::sha256_hex(identity.as_str());
}

auto lito::package::load_builtin_package(ref<str> id) -> PackageResult<BuiltinPackage> {
    if (id != "qt"_str) {
        return builtin_failure<BuiltinPackage>(
            rstd::format("unknown builtin build package '{}'", id));
    }
    auto tree     = rstd_try(qt_source_tree());
    auto digest   = builtin_digest(id, tree);
    auto manifest = lito::manifest::load_package_manifest_from_source_tree(id, tree);
    if (manifest.is_err()) {
        return Err(rstd::into<PackageError>(rstd::move(manifest).unwrap_err()));
    }
    auto package = rstd::move(manifest).unwrap();
    if (! package.targets.is_empty() || ! package.compile_tests.is_empty() ||
        package.install_script.is_some() || ! package.dependencies.is_empty() ||
        ! package.dev_dependencies.is_empty() || ! package.runtime_dependencies.is_empty() ||
        package.script.is_none() || ! package.build_tools.is_empty() ||
        ! package.external_sources.is_empty() ||
        ! package.pkg_config_external_dependencies.is_empty() ||
        ! package.cmake_external_dependencies.is_empty()) {
        return builtin_failure<BuiltinPackage>(
            rstd::format("builtin package '{}' must contain only a script contract", id));
    }
    if (package.version.value.is_none()) {
        return builtin_failure<BuiltinPackage>(
            rstd::format("builtin package '{}' must declare a version", id));
    }
    return Ok(BuiltinPackage {
        .source_identity = rstd::format("builtin:{}:{}", id, digest.as_str()),
        .digest          = rstd::move(digest),
        .manifest        = rstd::move(package),
        .source          = rstd::move(tree),
    });
}
