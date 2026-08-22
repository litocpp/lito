# Dependencies

Dependency table keys are package names and must match the package found at the declared source.

## `[dependencies.NAME]`

A normal package dependency has exactly one source. `NAME` is always the provider package name;
package aliases are not supported:

- `path = "RELATIVE"`;
- `git = "URL"`, optionally with exactly one of `branch`, `tag`, `rev`, or `commit`;
- `version = "REQUIREMENT"` for a Registry package, optionally with `registry = "NAME"`;
- `workspace = true` to reuse `[workspace.dependencies.NAME]`.

`commit` is a full 40-digit hexadecimal Git object ID. Git URLs and selectors must be non-empty, may
not start with `-`, and URLs may not contain a fragment.

Normal dependencies require `visibility = "public" | "private" | "link"`.

`features` is an optional array of provider feature names. `default-features` is a boolean and
defaults to `true`.

Examples:

```toml
[dependencies.geometry]
path = "../geometry"
visibility = "private"

[dependencies.rstd-std]
git = "https://github.com/litocpp/rstd.git"
branch = "main"
visibility = "public"

[dependencies.geometry-codec]
version = "^1.4"
registry = "internal"
visibility = "private"
```

## `[dev-dependencies.NAME]`

Development dependencies use the same package sources and feature fields but do not
accept `visibility`. They are considered when selected targets are tests, benchmarks, or compile
tests.

## `[runtime-dependencies.NAME]`

Runtime dependencies use the same package sources and do not accept `visibility`, `features`, or
`default-features`. They belong to runtime/install planning rather than compilation.

## `[workspace.dependencies.NAME]`

The workspace declaration provides exactly one package source. It does not contain visibility or
feature requests:

```toml
[workspace.dependencies.geometry]
path = "geometry"
```

Members opt in and own edge-local settings:

```toml
[dependencies.geometry]
workspace = true
visibility = "public"
features = ["simd"]
```

For a workspace dev dependency, the member omits visibility. For a workspace runtime dependency, it
also omits feature fields.

## Source and package conflicts

Lito resolves the complete graph by package name. Two requirements that resolve the same name from
incompatible sources are a package conflict; they do not become two hidden identities. A child
package's relative path and external declarations remain owned by the package source that declared
them.

See [Dependencies and the lock file](../../guide/dependencies-and-lock.md).
