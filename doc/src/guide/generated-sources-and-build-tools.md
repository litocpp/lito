# Generated sources and build tools

Generated inputs are owned by the package or workspace that declares the build script. Lito runs
that script before source discovery, then passes a typed generated tree into the normal build
pipeline. Consumers do not inspect a build-script directory or reconstruct generated paths.

## Build scripts

A fixed `build.lua` at a package or workspace root declares generation actions through Lito's Lua
API. Common operations include running a declared tool, configuring a text file, and embedding a
file into a C++ source.

Generated files live under the generated root for that owner. Reference them through a source group
or runtime resource rather than a path that escapes the package:

```toml
[source-groups.generated]
root = "generated"
sources = ["src/version.cpp"]

[lib]
name = "core"
module = "core"
archive = "core"
source-groups = ["generated"]
```

The complete build receives the generated tree directly. Source discovery, scan cache identity,
compile actions, and resource publication therefore share one generation result.

## Host build tools

`[build-tools.ALIAS]` declares an executable downloaded for the host, not the target. Each host
platform/architecture archive has an HTTPS URL and SHA-256 digest:

```toml
[build-tools.litobook]
version = "0.1.0"
executable = "litobook-linux-x86_64/bin/litobook"

[build-tools.litobook.archives.linux-x86_64]
url = "https://github.com/litocpp/litodoc/releases/download/v0.1.0/litobook-linux-x86_64.tar.gz"
sha256 = "29ebc2b57803df70c2dfe80936336ec1e15bead3adea50e1c4a2a22c745b77a0"
```

The alias is used by `build.lua`; the script does not discover an arbitrary executable from a
download directory. Host tool downloads participate in source acquisition and offline/fetch-seed
policy.

## Generated versus external roots

`root = "generated"` selects the build-script result owned by this package. `external-source =
"NAME"` selects a prepared external source owned by the package manifest. These roots are mutually
exclusive; a caller should never guess which filesystem path backs either root.

See [build tools and external inputs](../reference/lito-toml/build-tools-and-external-inputs.md).
