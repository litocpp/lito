# External dependencies

Use package dependencies for software built as Lito packages. External dependencies adapt
installed SDK metadata or a CMake project into typed compile and link requirements.

## pkg-config

A pkg-config dependency names the module queried by `pkg-config`:

```toml
[external-dependencies.pkg-config.openssl]
module = "openssl"
version = ">= 3.0"
visibility = "private"
```

`static = true` requests static metadata. Configure the `pkg-config` host tool, search paths,
library paths, and sysroot in [project config](../reference/config/keys.md), not by inserting shell
variables into the manifest.

## CMake packages

A CMake dependency names the package and the targets Lito consumes:

```toml
[external-dependencies.cmake.vulkan]
package = "Vulkan"
targets = [
  { name = "Vulkan::Vulkan", visibility = "private" },
]
```

The provider may use an installed package or build a declared external source. `source` names a
package-owned `[external-sources.NAME]`. `cache` applies only while building a path or Git source.
`adapter` and `config-directory` are alternative ways to locate/shape package configuration and
cannot be combined.

Lito keeps provider metadata as raw compile options until the consuming package language is known,
then parses it in the C or C++ option domain selected by that package. Ambient `CFLAGS`, `CXXFLAGS`,
and `LDFLAGS` are removed from CMake subprocess environments; even an invocation using
`--use-env-flags` does not leak those values into the external CMake project.

Workspace declarations omit target visibility. A member referencing the workspace declaration
supplies its selected targets and visibility.

A CMake package or adapter can also publish runtime files as a named asset set while Lito queries
the dependency:

```cmake
if(COMMAND lito_export_asset_set)
  lito_export_asset_set(
    NAME runtime
    ROOT "${PACKAGE_PREFIX_DIR}/lib"
    FILES libexample.so data/registry.bin
  )
endif()
```

`FILES` are logical relative paths below `ROOT`. Lito validates their ownership and records the
set under the CMake dependency alias. A package `install.lua` can materialize that set and use its
destination to derive an install-only Linux `RUNPATH`; exporting it does not copy files by itself.

## External source roots

An external source has exactly one recipe:

- `path`;
- `git` with at most one selector;
- `archive` plus `sha256`;
- architecture-specific `archives` with a digest for each canonical architecture.

The same prepared source can back a CMake dependency, source group, or include directory. The
package owner resolves it once and passes typed roots to each consumer.

Archive acquisition checks an existing materialization, read-only fetch seeds, and the global file
cache before resolving download or extraction tools. Fresh downloads use `tools.curl`; extraction
tries `tools.bsdtar`, `tools.tar`, then the public `cmake -E tar` interface. An archive used only as
a source group does not require a CMake package query.

## System libraries and threads

Use `usage.system-libraries` for logical OS/SDK names such as `dl` or `user32`. Do not place `-l`,
paths, or linker options in that list. Use `usage.threads = true` for the platform threading model;
do not spell it as raw `-pthread` in compile or link options.

See [external input fields](../reference/lito-toml/build-tools-and-external-inputs.md) and
[usage fields](../reference/lito-toml/usage-and-linking.md). Installation is covered by
[Install and resources](../reference/lito-toml/install-and-resources.md).
