# Usage and linking

`[usage]` declares package compile and link requirements. `[[when]].usage` accepts the same fields.

## Include directories

`public-include-directories` and `private-include-directories` are arrays. Each entry is one of:

- a relative string path, based at the package source root;
- `{ path = "...", root = "package" }`;
- `{ path = "...", root = "generated" }`;
- `{ path = "...", external-source = "NAME" }`.

Public include directories require a library target because they must have a public consumer.
Private directories apply only while compiling the package.

## Definitions

`public-definitions` and `private-definitions` are arrays of compiler definition strings. Public
definitions propagate through public usage; private definitions do not.

Lito owns profile and package metadata macros. A manifest cannot override settings such as
`NDEBUG`, exception/RTTI state, `LITO_PKG_VERSION`, or `LITO_FEAT_*` through raw definitions.

## Compiler and linker options

`options` is an array of compiler options local to the package. Lito parses the array in the
language selected by `[package].standard`, so C packages retain C vendor options without exposing
them to C++ targets, and vice versa. Known options enter typed compatibility domains. Conflicting
standard library, threading, language-standard, exception, RTTI, visibility, LTO, and other owned
settings are diagnosed.

`linker-options` is an array applied to the package's own link actions. It is not a public usage
requirement and is never copied to a consuming target. A package without a shared library, binary,
test, or benchmark cannot declare linker options.

Use `[lib].linker-options` when an option belongs only to the shared library link action, such as an
ELF symbol version script. Package `[usage].linker-options` remains appropriate only when every link
action owned by the package requires the option.

A shared library is a native link boundary. Its private and link-only dependencies are consumed
while producing the shared object, but their link arguments and typed link requirements are not
replayed when another target links that shared object. Dependencies declared public remain part of
the shared library's explicit consumer interface. Static libraries retain their native link closure
because the final linker must resolve archive members.

Raw `-pthread` is not accepted as a substitute for `threads`.

Lito normalizes supported linker runtime-search forms, including common `-Wl,-rpath,...`,
`-Wl,--rpath=...`, and `-Xlinker` forms, into typed requirements. It emits ELF `RUNPATH` with new
dynamic tags. `--disable-new-dtags` is rejected because it requests legacy `DT_RPATH`. Install
recipes may replace these paths only in a distinct install-specific binary variant; see
[Install and resources](install-and-resources.md).

Project-wide compiler and linker arguments belong to config `[build]`, where C, C++, and link
inputs are separate. They do not turn package-private `options` into public usage requirements.

## Threads

`threads` is a boolean. It selects the platform threading compile/link policy as one typed setting,
so a provider and consumer cannot silently use incompatible models.

## System libraries

`system-libraries` is an array of logical operating-system or SDK library names. Entries are
non-empty names such as `dl` or `user32`; they cannot be paths, `-l` options, whitespace-containing
arguments, or colon-qualified values.

## Visibility and standard library linking

Lito builds with hidden symbol visibility by default and derives exported behavior from module/API
definitions and explicit compiler policy.

Runnable target `link-stdlib` defaults to `true`. Set it to `false` only when the target owns a
freestanding/no-standard-library link closure. The selected toolchain standard library is configured
outside the manifest; see [Configuration keys](../config/keys.md).
