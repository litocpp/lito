# Dependencies

Dependency table keys are package names and must match the package found at the declared source.

## `[dependencies.NAME]`

A normal package dependency has exactly one source. `NAME` is always the provider package name;
package aliases are not supported:

- `path = "RELATIVE"`;
- `git = "URL"`, optionally with exactly one of `branch`, `tag`, `rev`, or `commit`;
- `version = "REQUIREMENT"` for a Registry package, optionally with `registry = "NAME"`;
- `workspace = true` to reuse `[workspace.dependencies.NAME]`.

A dependency from the default Registry may use its version requirement directly as a string:

```toml
[dependencies]
geometry = "^1.4"
```

This is equivalent to `geometry = { version = "^1.4" }`. Use the table form when selecting a
named Registry or declaring `pub`, `usage`, `features`, or `default-features`. The string form is not
accepted by `dev-dependencies` or `runtime-dependencies`.

`commit` is a full 40-digit hexadecimal Git object ID. Git URLs and selectors must be non-empty, may
not start with `-`, and URLs may not contain a fragment.

After resolving the provider package, Lito classifies the dependency as a C/C++ library, script,
plugin, or pmacro contract. A C/C++ library edge accepts two independent settings:

- `usage = "compile" | "link"`, or a non-empty unique array containing those facets. Omitting it
  selects both compile and link usage;
- `pub = true | false`, defaulting to `false`. A public edge propagates the selected facets through
  the consuming library.

`usage = "compile"` and `usage = ["compile"]` are equivalent. Array order is not significant.
An empty, duplicate, or unknown facet is rejected. Public link-only dependencies are valid: they
propagate link requirements without exposing headers, definitions, or modules.

The deprecated `visibility = "public" | "private" | "link"` spelling is still read for existing
packages, but cannot be mixed with `pub` or new usage syntax and is never emitted. Plugin, pmacro,
and script dependencies do not accept `pub` or `usage` because their lifecycle is determined by
the provider target kind.

`features` is an optional array of provider feature names. `default-features` is a boolean and
defaults to `true`.

Examples:

```toml
[dependencies.geometry]
path = "../geometry"

[dependencies.rstd-std]
git = "https://github.com/litocpp/rstd.git"
branch = "main"
pub = true

[dependencies.geometry-codec]
version = "^1.4"
registry = "internal"
usage = "link"
```

## `[dev-dependencies.NAME]`

Development dependencies use the same package sources, feature fields, and compile/link usage
selection. They are always private, so `pub = true` is rejected. They are considered when selected
targets are tests, benchmarks, or compile tests.

## Pmacro dependencies

A normal dependency that resolves to a package containing `[pmacro]` is a compiler-host input. Its
dependency key is the provider package name and is the left side of an invocation identity:

```toml
[dependencies.model-macros]
path = "../model-macros"
features = ["diagnostics"]
default-features = false
```

```cpp
struct [[pmacro::attr("model-macros::validate")]] Model {};
struct [[pmacro::derive("model-macros::equal")]] Value {};
```

Source, feature, and workspace fields match other dependencies. A pmacro dependency does not
accept `pub` or `usage` and never enters target compile or link usage. A `[pmacro]` provider may
depend on host C/C++ libraries through ordinary dependencies, but cannot recursively depend on
another `[pmacro]` provider.

## `[runtime-dependencies.NAME]`

Runtime dependencies use the same package sources and do not accept `pub`, `usage`, `features`, or
`default-features`. They belong to runtime/install planning rather than compilation.

## `[workspace.dependencies.NAME]`

The workspace declaration provides exactly one package source. It does not contain `pub`, `usage`,
or feature requests:

```toml
[workspace.dependencies]
geometry = "^1.4"
```

The string form declares a package from the default Registry and is equivalent to
`geometry = { version = "^1.4" }`. Use a dependency table for path, Git, builtin, or named Registry
sources:

```toml
[workspace.dependencies.geometry]
path = "geometry"
```

Members opt in and own edge-local settings:

```toml
[dependencies.geometry]
workspace = true
pub = true
features = ["simd"]
```

For a workspace pmacro dependency, the member may set features and `default-features`, but omits
`pub` and `usage`. Workspace development dependencies may select usage but remain private.
Workspace runtime dependencies additionally omit feature fields.

## Source and package conflicts

Lito resolves the complete graph by package name. Two requirements that resolve the same name from
incompatible sources are a package conflict; they do not become two hidden identities. A child
package's relative path and external declarations remain owned by the package source that declared
them.

See [Dependencies and the lock file](../../guide/dependencies-and-lock.md).
