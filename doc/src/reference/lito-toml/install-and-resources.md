# Install and resources

Installation is owned by the resolved package graph and an optional package-root `install.lua`.
The manifest declares runtime dependencies and generated build resources, while the install recipe
selects artifacts and maps materialized inputs to destinations.

## Binary `resources`

`[[bin]].resources` is an optional non-empty array. Every entry requires:

- `name`: a package-style logical name;
- `root = "generated"`;
- `path`: a relative path below the binary owner's generated root.

```toml
[[bin]]
name = "viewer"
resources = [
  { name = "frontend", root = "generated", path = "frontend/default" },
]
```

The resource path is not a source list and does not add files to compilation. It publishes a typed
build result for documentation and other integrations. It is not an install recipe input and Lito
does not copy it during installation.

## `[runtime-dependencies.NAME]`

Runtime dependencies use the package source forms documented in [Dependencies](dependencies.md).
They do not accept visibility or feature fields. Installation resolves them as packages, installs
them dependency-first using their own recipes, and records their exact source identities in the
managed package catalog. They do not enter the compile or link closure merely because they are
runtime dependencies.

## Default recipe

A package without `install.lua` receives one artifact entry for every selected `[[bin]]`. The
destination is `bin/` followed by the binary's artifact name. Repeated `lito install --bin NAME`
filters binary targets only for directly selected packages using this default recipe.

Every installable package must resolve a package version. A package without an install script must
also have at least one binary target.

## `install.lua`

An `install.lua` regular non-symlink file at the package root is discovered automatically. It
replaces the default recipe and must call `lito.install(TABLE)` exactly once. A direct package with
a custom script cannot be combined with `--bin`, because the script owns its complete recipe.

The `lito` Lua module exposes these values:

- `package_name` and `package_version`;
- `profile`;
- `target`, containing the effective target triple;
- `target_arch`, containing its canonical architecture.

It exposes these functions:

- `env(NAME)`, returning a UTF-8 string or `nil` when the variable is unset;
- `render_template({ input = PATH, values = TABLE })`, returning rendered text;
- `install(RECIPE)`, declaring the complete package recipe.

Template values are strings, integers, or booleans. Placeholder names must satisfy Lito's
configure-template name rules. Template inputs are regular non-symlink files inside the package.

## Recipe fields

The top-level recipe accepts only `artifacts`, `external_assets`, `files`, `templates`, and
`inventories`. Each is an optional array of tables.

### Artifacts

An artifact selects a binary owned by the recipe package:

```lua
artifacts = {{
  target = { kind = "bin", name = "viewer" },
  destination = "bin/viewer",
}}
```

`kind` currently accepts only `bin`. Targets and destinations cannot be repeated within the
recipe. Only selected artifact targets are built.

On Linux, an artifact may request an install-specific ELF `RUNPATH` through declared external
asset sets:

```lua
artifacts = {{
  target = { kind = "bin", name = "viewer" },
  destination = "bin/viewer",
  runtime_search = {{
    external_asset = { dependency = "cef", set = "runtime" },
  }},
}}
```

Every reference must match exactly one `external_assets` entry in the same recipe. Lito derives an
origin-relative path from the artifact and asset destinations, then links a separate install
variant. The normal build artifact is unchanged.

### External assets

External assets select a named asset set exported by a CMake dependency:

```lua
external_assets = {{
  dependency = "cef",
  set = "runtime",
  destination = "lib/cef",
  strip = {
    mode = "debuginfo",
    files = { "libcef.so" },
  },
}}
```

`strip` is optional. Its mode is `debuginfo` or `symbols`, and `files` is a non-empty list of
logical paths within the asset set. Lito runs the configured LLVM strip tool only on staged
copies, never on dependency-owned source files.

### Package files and templates

Package files copy a regular non-symlink source inside the package root:

```lua
files = {{ source = "LICENSE", destination = "share/viewer/LICENSE" }}
```

Templates render a package-owned input with scalar values and materialize the result:

```lua
templates = {{
  input = "viewer.conf.in",
  destination = "share/viewer/viewer.conf",
  values = { package = lito.package_name, version = lito.package_version },
}}
```

### Inventories

An inventory writes the installed file list to `destination`. `relative_to` selects the relative
base within the package's published layout and may be an empty string for its root:

```lua
inventories = {{
  destination = "share/viewer/files.txt",
  relative_to = "",
}}
```

## Path and destination rules

Recipe paths are normalized relative paths without `.` or `..` components. They are non-empty
except that inventory `relative_to` may select the package root with `""`. They cannot target
`.lito`. Package file and template sources must remain within the package root. Asset sources must
remain within dependency-owned source, install, or query-build roots.

`lito install --root DIRECTORY` selects the managed store; `--prefix DIRECTORY` writes an untracked
prefix tree and conflicts with `--root`. Managed-root precedence is:

1. `--root`;
2. `LITO_INSTALL_ROOT`;
3. config `[install].root`;
4. `LITO_HOME`;
5. `$HOME/.lito`.

`--force` permits replacements allowed by the selected destination mode, but does not make
ambiguous ownership valid. See the [`install` command](../cli/build-and-install.md) and
[configuration keys](../config/keys.md).
