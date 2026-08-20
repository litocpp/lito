# Build tools and external inputs

## `[build-tools.NAME]`

Each host build tool requires:

- `version`: a non-empty identity string;
- `executable`: a relative path inside the acquired archive;
- `archives`: one or more host platform entries.

An archive key has the form `OS-ARCHITECTURE`, for example `linux-x86_64`. Each entry requires an
HTTPS `url` and a full hexadecimal `sha256` digest.

Build tools are available to `build.lua`; they are host tools and do not become target package
dependencies.

## `[external-sources.NAME]`

A package external source is one of:

- `path = "RELATIVE"`;
- `git = "URL"` and at most one selector: `branch`, `tag`, `rev`, or `commit`;
- `archive = "URL"` plus `sha256`;
- `archives.ARCHITECTURE` entries, each with `archive` and `sha256`;
- `workspace = true`, referring to `[workspace.external-sources.NAME]`.

Archive URLs must be safe for the generated CMake project and may not contain fragments, quotes,
backslashes, semicolons, or newlines. Architecture keys must use Lito's canonical spelling; aliases
such as `amd64`, `x64`, and `arm64` are rejected in favor of canonical names.

Workspace external sources use the same concrete recipes but cannot themselves use
`workspace = true`.

## pkg-config external dependencies

`[external-dependencies.pkg-config.NAME]` requires:

- `module`: a non-empty pkg-config module name that does not start with `-`;
- optional `version`, beginning with `=`, `<`, `>`, `<=`, or `>=`;
- optional boolean `static`;
- required `visibility = "public" | "private" | "link"`.

`[workspace.external-dependencies.pkg-config.NAME]` declares `module`, `version`, and `static` but
omits visibility. A member reference sets `workspace = true` and required `visibility`.

## CMake external dependencies

`[external-dependencies.cmake.NAME]` requires `package` and a non-empty `targets` array:

```toml
[external-dependencies.cmake.vulkan]
package = "Vulkan"
targets = [
  { name = "Vulkan::Vulkan", visibility = "private" },
]
```

`NAME` is a manifest-local dependency alias. `package` is the actual CMake package identity. Generic
integration passes it to `find_package`; an adapter receives it through
`LITO_CMAKE_DEPENDENCY_PACKAGE`. It is also the identity matched by
`tools.cmake.overrides.PACKAGE`, independently of the local alias. Every target entry has `name`
and `visibility`.

Optional fields are:

- `source`: the name of a package external source;
- `adapter`: a relative adapter source used by the generated consumer project;
- `cache`: CMake cache values whose keys contain letters, digits, or `_` and whose values are
  strings, booleans, or integers;
- `config-directory`: a relative package configuration directory.

`adapter` and `config-directory` are mutually exclusive. `cache` is used only when building a path
or Git source.

`[workspace.external-dependencies.cmake.NAME]` declares `package`, `source`, `adapter`, `cache`, and
`config-directory` but omits targets. A package reference uses `workspace = true` and supplies its
selected target names and visibility. The package identity comes from the workspace declaration;
the member reference does not repeat it.

### CMake asset sets

During Lito's CMake query, a package configuration or adapter may call the injected function:

```cmake
lito_export_asset_set(
  NAME runtime
  ROOT "${PACKAGE_PREFIX_DIR}/lib"
  FILES libexample.so resources/data.bin
)
```

`NAME` and `ROOT` are required. Every `FILES` entry is a normal relative path below `ROOT` and must
resolve to an existing regular non-symlink file owned by the dependency source, its staged install
prefix, or the CMake query build. Set names are unique within one dependency alias. Lito stores the
logical path and canonical source for later selection by `install.lua`; the function does not make
the files compile or link inputs.

## Owner boundary

External source paths are resolved by the package or workspace that declares them. CMake,
source-group, and include-directory consumers receive that resolved source; they do not reinterpret
the declaration relative to the root application.
