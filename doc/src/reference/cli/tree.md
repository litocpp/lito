# `lito tree`

```text
lito tree [-p PACKAGE ...] [--purpose build|test|bench|example|install]
          [--profile PROFILE]
          [--features FEATURES] [--all-features] [--no-default-features]
          [--locked] [--offline] [--frozen] [--source-bundle DIRECTORY ...]
          [--depth N] [--no-dedupe] [--charset utf8|ascii]
```

Display the active Lito package dependencies for the selected configuration and purpose. Roots
follow the project's default package selection; repeated `-p` selects local root packages.
Script-only packages can be inspected with the default build purpose.

The default purpose is `build`, which selects production targets. `test`, `bench`, and `example`
include development dependencies allowed by their selected targets. `install` includes the active
runtime installation chain. Features and dependency conditions use the same selection as build;
an inactive edge stays hidden even when another edge selects its provider.

Profiles use the corresponding command defaults: debug for build, test, and example; release for
bench and install. Custom profiles are resolved from the project profile declarations.

Nodes show the package name and its declared version, when present. Path packages also show
their resolved package directory. Script, plugin, and pmacro edges have type labels; host
instances carry `[host]`. Development and runtime edges appear in their respective groups.
Host and target instances of the same package remain distinct.

Shared subtrees are expanded once per root, with later occurrences marked `(*)`. Leaves are not
marked. `--no-dedupe` repeats shared subtrees. `--depth 0` prints only roots; `--depth 1` includes
direct dependencies. Group headings do not count as package depth. The default character set
is `utf8`; use `--charset ascii` for plain terminal characters.

Tree resolves package metadata and may query Clang or require an installed SDK to determine
the condition environment. It does not scan C++ sources, compile, link, execute build or install
scripts, probe pkg-config packages, configure CMake projects, or build Cargo dependencies.
It displays Lito packages, not source modules, build actions, external packages, or their internal
dependency graphs. Platforms are selected through the existing configuration keys; tree does
not accept an artifact `--target` selector.

Source acquisition and lock synchronization follow the normal project resolution policy.
Tree may create or update `lito.lock`; inactive candidates still participate in the complete
lock. `--locked` rejects a missing or stale lock, `--offline` forbids network acquisition, and
`--frozen` combines both. Source bundles retain their read-only behavior.

Only the tree is written to stdout. Acquisition progress, tool reports, and errors use stderr.

```sh
lito tree
lito tree -p media --features image --depth 2
lito tree -p media --purpose test --frozen
lito tree -p player --purpose install --charset ascii
```
