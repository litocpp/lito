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
C++ package standard. `attribute-only` is an optional boolean and defaults to `false`.

Lito builds plugin sources with the host toolchain as PIC objects. The resulting support archive
is linked into a loadable library and retained with the action and host compiler identity as one
compiler plugin product. Plugin targets may use ordinary static host C/C++ and external
dependencies, but cannot depend on another compiler plugin.

An ordinary dependency that resolves to `[plugin]` is a host-only compiler dependency. Lito builds
it before target compilation and attaches the completed product to the dependent target's compile
invocations. It does not contribute target include directories, modules, archives, shared
libraries, or install artifacts. `pub` and `usage` are therefore rejected for a plugin dependency.

By default, the plugin is loaded for every source in the dependent target. Opt in to scope-based
loading with:

```toml
[package]
name = "example_plugin"
version = "0.1.0"

[plugin]
module = "example.plugin"
attribute-only = true
```

An attribute-only plugin must register its C++ attributes under its exact package name, such as
`[[example_plugin::marker]]` or `[[using example_plugin: marker]]`. The attribute name does not
affect selection. No custom prefix or name normalization is supported: `example-plugin` cannot
opt in, and it is never mapped to `example_plugin`. The package name must start with an ASCII
letter and contain only ASCII letters, digits, and underscores, without `__`. The reserved scopes
`clang`, `gnu`, `msvc`, `gsl`, `omp`, and `std` cannot opt in.

Lito uses frontend facts from the source, expanded macros, and included headers to choose plugins
per compile unit. A scoped `__has_cpp_attribute` query also keeps that plugin loaded. Missing or
incomplete facts, module imports, and `#pragma clang attribute` keep plugins loaded conservatively.
This controls loading only; plugin dependencies are still resolved and built on the host.
Attribute-only plugins must not require execution on sources without their scope. Lito validates
the manifest name, but does not inspect or enforce attribute registrations inside a plugin's code.

## `[pmacro]`

A package has at most one pmacro target and cannot also declare `[lib]`. The target accepts
`module`, `sources`, `source-groups`, and `when` with the same source ownership rules as a C++
library. It does not accept output names, linker options, install fields, or runtime resources.

Lito always builds this target for the build host as a PIC archive. The bootstrap pmacro plugin
generates registrations during normal provider compilation; callbacks and registrations belong to
the same source objects in that archive. Lito links the support archive and the providers selected
by a consumer into its aggregate Clang plugin. The target automatically receives Lito's `pmacro`
compiler-support package. A provider may use ordinary C/C++ library dependencies, but cannot depend
on another pmacro provider. An override for the builtin `pmacro` package must still provide `[plugin]`.

Provider functions use `[[pmacro::define]]`. An optional string literal selects their public macro
scope: `[[pmacro::define("wasmto")]]` on `bindgen` registers `wasmto::bindgen`. Without an argument,
the scope is the provider package name. The scope must be non-empty and must not contain `::`.
Registration and dependency selection retain the actual provider package identity.
`AttributeInput` definitions are invoked through `pmacro::attr`; `DeriveInput` definitions are invoked
through `pmacro::derive`. Both receive and return token streams. Provider code and the public pmacro
module use C++23 and `import std`.

Expansion runs inside the consumer's normal Clang compilation. The active Parser and Sema consume
generated tokens, and that compilation produces the object and BMI. Lito scans the original source
dependencies and prepares the plugins; it does not receive rewritten C++ or rescan macro output.
Macros must preserve the module declaration and direct imports. Preprocessing within macro-handled
source fragments is unsupported and remains the caller's responsibility.

An attribute macro returns a complete item. Record transformations retain the existing declaration,
type identity, and declaration head, and replace the unparsed definition contents. Renaming or
deleting the record is rejected. Ordinary free-function declarations can produce a matching
definition and auxiliary declarations; function-definition transformations retain the parsed head.
This is not a general rollback interface for declarations already processed by Sema.

`pmacro::derive` retains the original complete record, union, or enum and appends each provider's
output in argument order. A fixed `pmacro::helper("scope::derive", ...)` attribute carries
provider-owned arguments on that item or its members. The pmacro plugin validates helper ownership
and consumes its control attributes during compilation.

`Context::declaration()` returns a shared, immutable, owned snapshot whose `data` is a variant of
`RecordInfo`, `EnumInfo`, and `FunctionInfo`. It includes fields, enumerators or function parameters,
types, attributes, and source information; record/enum helpers retain their existing ownership. A
record attribute can request an analysis of its input before its members enter the main Sema;
derive reads the completed declaration. Repeated queries reuse the snapshot, and a changed record
input gets a new snapshot.

Ordinary named free functions reuse the host's current declaration without a second parse. Parameters
retain optional names, original and adjusted types, default-argument states, attributes and source
locations. The function includes its return type, declaration/definition form, language linkage,
default calling convention flag, exception state and storage/template flags. This is a snapshot at
the attribute callback: the body has not been analyzed, inherited defaults may not yet be merged,
and an undeduced return type reports `RepresentationError::UndeducedType`. Dependent or deferred
exception information is kept explicit. Methods and special function names return
`UnsupportedDeclaration`. A function input changed by an earlier attribute returns `ChangedInput`
on later model queries instead of pairing new tokens with the old AST. The existing snapshot remains
readable. Function attributes require an unconsumed semicolon or body; deleted/defaulted forms are
outside this invocation boundary. A completed declaration can still describe `FunctionForm::Deleted`.

The function model changes the host C++ ABI in pmacro 0.6.0. Providers must be rebuilt; Lito's aggregate
recipe identity invalidates previous plugin artifacts.

`TypeInfo::display` is diagnostic text. Available `type_id` fragments are C++ code representations,
not stable type identities. `Context::tokens(fragment)` validates the fragment's source context and
preserves its available diagnostic location. The model does not expose Clang objects, global type
lookup, target layout, or arbitrary template instantiation.

`Context::emit(OutputLocation::AfterItem, tokens)` appends declarations after the complete declaration
group. `NamespaceEnd` inserts them before the enclosing lexical namespace block closes, where they
can refer to later declarations in that block. It requires an enclosing namespace. A derive's
returned tokens precede its `AfterItem` emissions; emissions from a failed callback are discarded.
These outputs enter the same compilation and do not write external files.

Invocations use supported declaration positions and source spelling locations. Function-like macros,
statement/expression/type-position macros, public mutable AST access, and arbitrary side files are
not supported. The expansion depth is limited to 64 and generated output to 16 MiB per compilation.

## `[[bin]]`

- `name` is required.
- `module`, `sources`, `source-groups`, and `when` have the same roles as for a library.
- `link-stdlib` is a boolean and defaults to `true`.
- `resources` is a non-empty array of generated runtime resources when present.

With discovery, the runnable entry is `src/main.cppm`. When the entry imports the runnable target's
declared root module, that root module uses `src/mod.cppm`. A convention-discovered `main.cppm`
must itself declare a named module. If the target declares `module`, the provided name must match.

## `[[example]]`

Examples are package-owned executables, separate from normal binaries. They accept `name`,
`module`, `path`, `source-root`, `sources`, `source-groups`, `when`, `link-stdlib`, and `resources`.
They use the package's library, ordinary dependencies, and development dependencies. They do not
accept `attach`, `host-tool`, or library output settings.

```toml
[[example]]
name = "gallery"
path = "gallery/main.cppm"
source-root = "gallery"
link-stdlib = false
```

By default, Lito discovers `examples/NAME.cppm`, `.cpp`, `.cc`, or `.cxx`, and
`examples/NAME/main` with those extensions. A directory without a main entry is not a target.
Nested packages are not traversed. Set `[package].autoexamples = false` to disable automatic
discovery while keeping explicit declarations. A same-name explicit declaration overrides an
automatic target; without an explicit path or source list, it uses the unique conventional entry.
Ambiguous file/directory names and multiple main entries are errors.

Module examples follow imports from their entry, with its parent directory as the default module
source root. Ordinary C++ entries are single-source targets; use `sources` for multiple translation
units. A virtual workspace cannot directly own example targets.

Use `lito build --example gallery`, `lito build --examples`, or `lito run --example gallery`.
Normal build, test, install, and documentation selection do not include examples. Examples from
dependency packages are not built implicitly.

## `[[test]]` and `[[bench]]`

Tests and benchmarks accept the runnable fields `name`, `module`, `sources`, `source-groups`, `when`,
and `link-stdlib`.

Tests and benchmarks also accept `attach`. Each attachment requires:

- `package`: the name of a direct package dependency with a library target;
- `sources`: a non-empty array of sources, relative to the declaring package, compiled into an
  attachment archive for the selected test or benchmark.

Attachment sources never enter the production library archive or a normal build.
They inherit the library's private compile settings and can import its internal module partitions.
The selected executable links the attachment archive in whole; test and benchmark attachments
remain isolated even when their targets have the same name.

```toml
[[bench]]
name = "runtime"
sources = ["benches/main.cpp"]

[[bench.attach]]
package = "runtime-lib"
sources = ["benches/runtime_access.cppm"]
```

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
