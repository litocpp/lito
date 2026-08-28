# Targets and sources

## `[lib]`

A package has at most one library target.

- `name` is required and is a package-style target name.
- Static output uses `archive`; shared output uses `kind = "shared"` and `artifact`. Output basenames
  are safe names; `.` and `..` are rejected.
- `linker-options` is an optional array for a shared library's own link action. It is rejected for a
  static library and never becomes consumer usage.
- `module` is optional for an explicit source list and required for module convention discovery.
- `sources` is an optional non-empty array of package-relative paths. Presence selects explicit
  source ownership.
- `source-groups` is an optional non-empty array naming `[source-groups.NAME]` entries.
- `when` is an optional non-empty array of target source conditions.

With discovery, the library entry is `src/lib.cppm` and provides `module`.

For a C package selected by `[package].standard`, `module` is forbidden and either `sources` or
`source-groups` is required. The same restriction applies to C runnable targets.

## `[plugin]`

A package has at most one compiler plugin target and cannot also declare `[lib]` or `[pmacro]`.
The package name is also the registered Clang plugin name. The target accepts `module`, `sources`,
`source-groups`, and `when` with the same source ownership rules as a C++ library, and requires a
C++ package standard.

Lito builds plugin sources with the host toolchain as PIC objects. The resulting support archive
is linked into a loadable library and retained with the action and host compiler identity as one
compiler plugin product. Plugin targets may use ordinary static host C/C++ and external
dependencies, but cannot depend on another compiler plugin.

An ordinary dependency that resolves to `[plugin]` is a host-only compiler dependency. Lito builds
it before target compilation and attaches the completed product to the dependent target's compile
invocations. It does not contribute target include directories, modules, archives, shared
libraries, or install artifacts. `visibility` is therefore rejected for a plugin dependency.

## `[pmacro]`

A package has at most one pmacro target and cannot also declare `[lib]`. The target accepts
`module`, `sources`, `source-groups`, and `when` with the same source ownership rules as a C++
library. It does not accept output names, linker options, install fields, or runtime resources.

Lito always builds this target for the build host as a PIC archive. It then generates registration
objects and links only the providers selected by a consumer into that consumer's aggregate Clang
plugin. The target automatically receives Lito's `pmacro` compiler-support package. A pmacro
provider may use ordinary C/C++ library dependencies, but cannot depend on another pmacro provider.
An override for the builtin `pmacro` package must still provide `[plugin]`.

Provider functions use `[[pmacro::define]]`. `AttributeInput` definitions are invoked through
`pmacro::attr`; `DeriveInput` definitions are invoked through `pmacro::derive`. Both receive and
return token streams. Function-like macros, Clang AST access, semantic query APIs, and side outputs
are not part of this interface.

`pmacro::attr` replaces its annotated item with the returned tokens. `pmacro::derive` retains the
original complete record, union, or enum and appends each derive provider's output in argument
order. A fixed `pmacro::helper("provider::derive", ...)` attribute may carry provider-owned tokens
on that item or its members; Lito removes claimed helpers before authoritative compilation.
Invocations and helpers are supported only at main-source spelling locations. Active macros apply
to replaceable file/namespace declarations and record members, not statements, expressions,
parameters, or type positions.

## `[[bin]]`

- `name` is required.
- `module`, `sources`, `source-groups`, and `when` have the same roles as for a library.
- `link-stdlib` is a boolean and defaults to `true`.
- `resources` is a non-empty array of generated runtime resources when present.

With discovery, the runnable entry is `src/main.cppm`. When the entry imports the runnable target's
declared root module, that root module uses `src/mod.cppm`. A convention-discovered `main.cppm`
must itself declare a named module. If the target declares `module`, the provided name must match.

## `[[test]]` and `[[bench]]`

Tests and benchmarks accept the runnable fields `name`, `module`, `sources`, `source-groups`, `when`,
and `link-stdlib`.

A test also accepts `attach`, a non-empty array. Each attachment requires:

- `package`: the name of a direct package dependency with a library target;
- `sources`: a non-empty array of sources compiled only into that library's test attachment archive.

Attachment sources never enter the production library archive or a normal build.

## `[compile-test]`

`cases` is required and non-empty. Declare cases as `[[compile-test.cases]]`:

- `name` is required and identifies the case;
- `source` is a required relative source path;
- `outcome` is `"success"` or `"failure"`;
- `options` is an optional array of additional case compiler options;
- `diagnostic-contains` requires every listed substring for a failure;
- `diagnostic-contains-any` requires at least one listed substring for a failure.

Success cases cannot declare diagnostic expectations.

## `[source-groups.NAME]`

`sources` is required and non-empty.

Choose one root form:

- omit `root` for package-relative sources, or set `root = "package"`;
- set `root = "generated"` for the current owner's build-script result;
- set `external-source = "NAME"` for a package external source.

`external-source` cannot be combined with a generated root.

## Target source conditions

Each inline item in a target's `when` array requires:

- `condition`: a non-empty condition expression;
- `source-groups`: a non-empty array of group names.

Example:

```toml
[source-groups.linux]
sources = ["src/linux.cpp"]

[[bin]]
name = "viewer"
sources = ["src/main.cpp"]
when = [
  { condition = 'target.os == "linux"', source-groups = ["linux"] },
]
```

## Module discovery

For a declared root module, Lito maps the remaining logical module/partition segments to `src/`.
It tries `NAME.cppm` before `NAME/mod.cppm`, and then includes a same-name `.cpp` companion when it
exists. See [Modules and source discovery](../../guide/modules-and-source-discovery.md).
