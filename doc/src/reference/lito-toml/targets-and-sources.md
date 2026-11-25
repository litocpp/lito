# Targets and sources

## `[lib]`

A package has at most one library target.

- `name` is a package-style target name and defaults to the package name.
- Static output uses `archive`, which defaults to the library target name. `kind = "shared"`
  specifies a shared library with an optional `artifact` basename that has the same default. Output
  basenames are safe names; `.` and `..` are rejected.
- `linker-options` is an optional array for a shared library's own link action. It is rejected for a
  static library and never becomes consumer usage.
- `module` is optional for an explicit source list and required for module convention discovery.
- `sources` is an optional non-empty array of package-relative paths. Presence selects explicit
  source ownership.
- `source-groups` is an optional non-empty array naming `[source-groups.NAME]` entries.
- `when` is an optional non-empty array of target source conditions.
- `path` and `source-root` override the discovery entry and module directory. They can be combined
  with `source-groups` and `when`, but not with explicit `sources`.

With discovery, the library entry is `src/lib.cppm` and provides `module`.
Direct `source-groups` and active `when` groups supplement discovery without disabling it. Discovery
still follows active imports and companion implementations, rather than compiling every file in
the module directory. Setting `sources` selects explicit mode instead.

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

Lito reports unknown attributes from its source scan and disables Clang's duplicate
`-Wunknown-attributes` warnings. Its known spellings cover LLVM 23 attributes; support
queries still use the active compiler, language mode, and target. Plugin package scopes
are handled by their plugins, and the unscoped `unsafe` attribute never produces an
unknown-attribute warning. `-Wno-unknown-attributes` disables Lito's warnings too.

### Project safety checks

Lito provides the `safety` checker as a project build option. Enable it in `lito-config.toml`:

```toml
[build]
safety = true
```

Use `-c build.safety=true` or `-c build.safety=false` to override the setting for one
invocation. Lito runs the checker on workspace C++ targets, including sources without any
annotations. For external packages compiled by Lito, the plugin registers `[[unsafe]]` so
function annotations survive in BMI files. External CMake projects use their own build settings.
Safety checks require a Clang SDK matching the compiler, and Lito builds and caches its bundled
host plugin on demand.

The checker diagnoses raw-pointer operations and calls to unsafe functions. An unsafe function
declaration imposes a requirement on its callers, and its body still needs explicit unsafe blocks.

```cpp
[[unsafe]] int load(const int* pointer);

int consume(const int* pointer) {
    [[unsafe]] {
        return load(pointer);
    }
}
```

Build the producer and consumer modules with the same safety setting. With safety enabled,
function contracts are preserved in BMI files. With safety disabled, Lito ignores `[[unsafe]]`.
Unsafe blocks do not authorize the bodies of nested lambdas or local-class methods.

The initial checker does not insert bounds checks or implement ownership, borrow, or
`Send`/`Sync` analysis. Raw indirect function calls require an unsafe block; unsafe destructors
are rejected. System-header and imported implementation bodies remain library trust boundaries.

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
through `pmacro::derive`; `FunctionInput` definitions are invoked through `pmacro::invoke`. All three
receive and return token streams. Provider code and the public pmacro module use C++23 and `import std`.

A function macro is a standalone attribute declaration at global or namespace scope:

```cpp
[[pmacro::invoke("entity::entity_input", AlbumInput, AlbumFields, album, {
    name: String,
    sort_name: Option<String>,
    track_count: i32
})]];
```

Its provider is an ordinary `[[pmacro::define("entity")]]` function named `entity_input`, taking
`FunctionInput` and `Context&`, returning `host::MacroResult`, and declared `noexcept`. Lito selects
providers from the consumer's declared pmacro dependencies; the public scope does not replace the
provider package identity. Raw scanning recognizes `pmacro::invoke` without interpreting the DSL or
mistaking its argument tokens for module declarations or imports.

The first argument must be the macro's string-literal identity. The remaining balanced tokens are
passed as `FunctionInput::arguments`, excluding that identity and its separating comma. They need not
be C++ expressions or declarations; their syntax belongs to the provider. `attribute_span` identifies
the control attribute, while `invocation_span` includes the standalone declaration and its terminating
semicolon. `scope` and `after_item_export` describe the real lexical and export context, without a
host declaration. `Context::declaration()` returns `UnsupportedDeclaration` for this callback.

The returned tokens replace the invocation with zero or more declarations. This does not use a
preprocessor macro wrapper or pragma. Class members, statements, expressions and type positions are
outside the function-macro invocation boundary.

Write the invocation and its arguments directly in source. Combining it with preprocessing macros,
whether as an outer wrapper or inside its arguments, is unsupported. Such combinations have no
expansion or diagnostic guarantees; source-spelling checks may reject them.
Conditional directives such as `#if` and `#ifdef` may surround an invocation; Clang selects the active
branch before expansion. Conditional directives inside an invocation are unsupported.

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

`Declaration::scope` describes the lexical declaration scope. `Declaration::after_item_export`
describes whether declarations emitted after the item are already exported, require an explicit
`export`, or are not exported. These fields change the host C++ ABI in pmacro 0.8.0; providers must
be rebuilt with the matching builtin pmacro package.

`FunctionInput`, `host::MacroKind::Function`, and `host::FunctionCallback` extend the host C++ ABI in
pmacro 0.9.0. Providers and aggregate plugins must be rebuilt with the matching builtin package;
Lito's aggregate recipe identity invalidates previous plugin artifacts.

`TokenStream::slice(begin, end)` copies the half-open range of top-level tokens into a new stream.
It preserves spans, spacing, and group boundaries. Nested groups retain their original child
streams. Empty ranges are valid. Invalid streams and out-of-bounds or reversed ranges return
`Error::InvalidArgument`.

`TokenStream::append` separates the input from preceding tokens and preserves the spacing inside
an appended stream. Use `Context::parse` to create compounds such as `::` as single tokens.
`Context::splice(input, replacements)` accepts `TokenSplice` bindings from identifier names to
individual tokens or streams. It replaces matching identifiers inside nested groups and preserves
source locations on both the surrounding tokens and the replacements. Inserted tokens are copied
as supplied, without applying the replacement rules to them. An empty replacement stream removes
the identifier. Duplicate or empty binding names and invalid or foreign inputs return
`Error::InvalidArgument`.

These changes extend the host C++ ABI in pmacro 0.10.0. Build providers and aggregate plugins with
the matching builtin package.

`TypeInfo::display` is diagnostic text. Available `type_id` fragments are C++ code representations,
not stable type identities. `Context::tokens(fragment)` validates the fragment's source context and
preserves its available diagnostic location. The model does not expose Clang objects, global type
lookup, target layout, or arbitrary template instantiation.

`Context::emit(OutputLocation::AfterItem, tokens)` appends declarations after the complete declaration
group. `NamespaceEnd` inserts them before the enclosing lexical namespace block closes, where they
can refer to later declarations in that block. It requires an enclosing namespace. A derive's
returned tokens precede its `AfterItem` emissions; emissions from a failed callback are discarded.
These outputs enter the same compilation and do not write external files.

Invocations use supported declaration positions and source spelling locations. Statement/expression/
type-position macros, public mutable AST access, and arbitrary side files are not supported. The
expansion depth is limited to 64 and generated output to 16 MiB per compilation.

## `[[bin]]`

A package can declare multiple binaries.

- `name` is required.
- `module`, `sources`, `source-groups`, and `when` have the same roles as for a library.
- `target` is an optional [platform predicate](workspace-and-package.md#package) that further
  restricts the package's supported platforms.
- `link-stdlib` is a boolean and defaults to `true`.
- `resources` is a non-empty array of generated runtime resources when present.

With discovery, the runnable entry is `src/main.cppm`. When the entry imports the runnable target's
declared root module, that root module uses `src/mod.cppm`. A convention-discovered `main.cppm`
must itself declare a named module. If the target declares `module`, the provided name must match.

## `[[cdylib]]`

A package can declare multiple shared libraries intended for loading by an external host. Binaries
and cdylibs can use the package's `[lib]`, which remains the library exposed to package dependencies.

- `name` is a required target name. `artifact` is an optional output basename and defaults to `name`.
- `module`, `sources`, `source-groups`, and `when` specify the target's sources.
- `target` is an optional platform predicate with the same behavior as for `[[bin]]`.
- `linker-options` applies to this target's link action. `link-stdlib` defaults to `true`.

Module discovery uses `src/lib.cppm` by default and requires `module`. Use `path` to choose a
distinct entry when the package declares more than one library or cdylib entry.

The target's sources define its exported ABI, including exported C entry points where the host
requires them. The native toolchain determines the supported output format and filename suffix.
An Android target with `artifact = "viewer_native"` produces `libviewer_native.so`.

```toml
[package]
name = "viewer"
version = "0.1.0"

[lib]
module = "viewer"

[[bin]]
name = "viewer"
target = { os = ["linux", "windows", "macos"] }
sources = ["src/desktop.cpp"]

[[cdylib]]
name = "viewer-android"
target = { os = "android" }
artifact = "viewer_native"
sources = ["src/android.cpp"]
```

The current target must satisfy both the package predicate and any target predicate. By default,
`lito build` selects supported production targets. An explicit selector for a disabled target is an
error. Use `--target bin:NAME` or `--target cdylib:NAME` to select a particular entry. `when` selects
additional source groups within an enabled target.

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

An optional `standard` fixes the group's actual language and standard, such as `"c99"` or
`"c++20"`. Unlike the package's minimum standard, this value is not raised by dependency package
requirements. Without it, the group inherits the package's effective language and standard.
A C++ package can use a C-standard group, whose sources compile as C. A C package cannot declare a
C++-standard group, even when that group is not selected.

The same physical file cannot belong to both discovery and a selected source group, or to two
selected groups. Lito reports the conflicting sources even when their standards match. A file
outside the actual discovery closure does not conflict merely because it is in the module directory.

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
