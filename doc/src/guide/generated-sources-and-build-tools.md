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

## Staged Qt QML modules

The built-in Qt script package stages a module under `lito-qml/<URI as directories>` by default.
Its `qmldir`, QML files, resources, and generated `module.qmltypes` are target metadata that can be
selected by `install.lua`. Generator intermediates live in the sibling `<output>.build` directory.
Use returned output handles for generated C++ and resource inputs instead of their physical paths.

To allow another process to load the staged module from disk:

```lua
local qt = require("@lito.qt")
local target = lito.target({ kind = "lib", name = "ui" })
qt.qml_module({
  target = target,
  qt = lito.external_dependency(target, "qt6"),
  uri = "Example.Ui",
  qml_files = { "qml/StatusDot.qml" },
  resources = { "qml/status.svg" },
  prefer_resources = false,
  plugin = "none",
})
```

`prefer_resources` defaults to `true`. Setting it to `false` omits `prefer` only from the staged
`qmldir`; the embedded resource version still prefers the module's resource URL. The return value's
`qmldir` refers to the staged copy and `resource_qmldir` to the embedded copy. Cache generation
continues to use the resource version.

The corresponding `install.lua` is:

```lua
lito.install({
  generated_files = {{
    target = { kind = "lib", name = "ui" },
    source = "lito-qml/Example/Ui",
    destination = "share/qml/Example/Ui",
  }},
})
```

Add the installed `share/qml` directory to the consumer's QML import path. `module.qmltypes` is
generated when `moc_files` are provided; it describes C++ types for tooling, but does not implement
or register them at runtime. A consuming process must provide any backend types required by the
QML files. Pure-QML modules do not need a `.qmltypes` file.

## Host build tools

`[build-tools.ALIAS]` declares an executable for the host, not the target. To use an installed tool:

```toml
[build-tools.glslang]
path = "glslangValidator"
```

The build script consumes the same `lito.tool("glslang")` handle as a downloaded tool.
Lito resolves the effective host PATH and includes the executable path and content in the action
cache identity. A local tool is a host prerequisite, not part of a source bundle.

Alternatively, declare a fixed downloaded tool. Each host
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
download directory. Host tool downloads participate in source acquisition and offline/source-bundle
policy.

## Generated versus external roots

`root = "generated"` selects the build-script result owned by this package. `external-source =
"NAME"` selects a prepared external source owned by the package manifest. These roots are mutually
exclusive; a caller should never guess which filesystem path backs either root.

See [build tools and external inputs](../reference/lito-toml/build-tools-and-external-inputs.md).
