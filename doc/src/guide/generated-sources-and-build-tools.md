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

## Version comparisons

Both `build.lua` and `install.lua` expose version utilities through `lito` or `require("@lito")`:

```lua
assert(lito.version_compare("6.10.0", "6.9.0") == 1)
assert(lito.version_matches("6.8.3", ">=6.8.0, <7.0.0"))
```

`version_compare(left, right)` returns `-1`, `0`, or `1`. `version_matches(version, requirement)`
uses the same version requirements as package dependencies. Both use Lito's existing semantic
version parser, not lexical string comparison or CMake's version grammar. Versions require
`major.minor.patch`; a leading `v`, leading zeroes in numeric components, and build metadata
(`+...`) are rejected. Invalid versions or requirements raise a Lua error rather than returning
`false`. Prereleases compare below the corresponding stable version; range matching excludes
prereleases unless the requirement explicitly admits their base version.

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

## Tool dependency files

`lito.run` accepts a Make-style dependency file through `depfile`. A compiler that emits its
dependency file together with its output uses `depfile = { output = 2 }`, where `2` indexes
the declared `outputs` array.

A separate dependency-scanning action can instead pass its generated output to another action:

```lua
local compiled = lito.run({
  tool = compiler,
  cwd = ".",
  args = { "@INPUT:1@", "-o", "@OUTPUT@" },
  inputs = { "source.txt", scanned.outputs[1] },
  outputs = { "compiled.bin" },
  depfile = { input = 2, roots = { "include" } },
})
```

Exactly one of `input` and `output` is required. `input` is a one-based index into `inputs`
and must select a generated output handle. Its producer runs before the consuming action.
Relative dependency paths are resolved against the consuming action's `cwd`; input dependency
files cannot be combined with `output_cwd`. Optional `roots` extends the allowed dependency
directories beyond the package and generated roots, using the same rules as output dependency
files. Dependencies are content-tracked by the consuming action even when the dependency file's
list of paths does not change. The scanner must track its own dependencies as well.

`format` defaults to `"make"`, for compiler-generated Make dependency rules. It supports
multiple targets and rules, empty phony rules (`-MP`), comments, LF/CRLF continuations,
escaped spaces and `#`, and `$$` for a literal dollar sign. Backslashes before ordinary
characters are preserved; escaping follows compiler output rather than a full Make interpreter.
Continuations separate paths, and only prerequisites are tracked, not rule targets.
Variables, recipes and order-only prerequisites are not supported.

Use `format = "nmake"` for Clang's `-MV` output: double quotes protect paths containing
spaces or `#`, while backslashes and dollar signs remain literal. In `"make"` mode,
quotes are ordinary filename characters. Formats are never inferred from existing files.

Use `format = "dxc"` on both actions when consuming DXC's
`-M/-MF` output: DXC writes one raw path per continuation line without Make-style escaping.
The explicit format preserves spaces and backslashes instead of guessing from existing files.

## Generated versus external roots

`root = "generated"` selects the build-script result owned by this package. `external-source =
"NAME"` selects a prepared external source owned by the package manifest. These roots are mutually
exclusive; a caller should never guess which filesystem path backs either root.

See [build tools and external inputs](../reference/lito-toml/build-tools-and-external-inputs.md).
