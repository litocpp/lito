# Profiles, features, and conditions

Profiles choose code-generation policy. Features choose package capabilities. Conditions apply
usage or source groups for the selected target environment. They are separate inputs and remain in
the build and BMI identities when they affect compilation.

## Built-in profiles

`debug` has no optimization, full debug information, no stripping, and no LTO. It is the default
for build-oriented commands other than `install`. `release` uses optimization level 3, no debug
information, no stripping, and no LTO. It also defines `NDEBUG` and is the default for `install`.

`plain` delegates optimization, debug information, LTO, `NDEBUG`, and link-time stripping to global
build inputs. An unspecified field uses the compiler default: Lito does not translate it to `-O0`,
`-g0`, or `-fno-lto`. `plain` does not delegate the language standard, target, sysroot, standard
library, exceptions, RTTI, BMI policy, or PIC.

Every selectable profile inherits exception and RTTI policy from the non-selectable `base` profile:

```toml
[profile.base]
exceptions = false
rtti = false
```

`base` cannot be selected or used as an explicit `inherits` target. A built-in or custom profile
may override either setting; the selected result still applies consistently to the complete build
graph. The legacy `profile.exceptions` and `profile.rtti` spellings remain accepted for this
version but are deprecated.

Create a named profile by inheriting an existing one:

```toml
[profile.perf]
inherits = "release"
debug = "line-tables-only"
lto = "thin"
```

Built-in `debug`, `release`, and `plain` inherit `base`; they may be customized directly but cannot
declare `inherits`.
Every other profile must inherit another profile, and inheritance cycles are rejected. A packaging
profile can keep selected fields fixed while delegating the others:

```toml
[profile.packaging]
inherits = "plain"
lto = "thin"
```

## Project build options

Profiles own optimization, debug information, stripping, LTO, exceptions, and RTTI. Project config
can add options without moving that policy into package manifests:

```toml
[build]
options = ["-DPROJECT_CPP=1"]
linker-options = ["-Wl,--as-needed"]

[build.c]
options = ["-DPROJECT_C=1"]
```

Lito keeps C, C++, and link options in separate domains. It parses them before planning, rejects an
attempt to override profile- or toolchain-owned settings, and includes relevant options in cache and
compatibility identities. Use `--use-env-flags` to append `CFLAGS`, `CXXFLAGS`, and `LDFLAGS` for
one invocation; ambient values are otherwise ignored. With `plain`, these inputs can provide the
delegated code-generation fields:

```sh
CFLAGS="-O2 -g" CXXFLAGS="-O2 -g" LDFLAGS="-Wl,-z,relro" \
  lito --use-env-flags build --profile plain
```

For `debug`, `release`, or a fixed field in a custom profile, an equal typed value is normalized and
a different value remains an error. Package `usage.options` cannot provide delegated profile fields.

## Package features

Declare features in a package:

```toml
[features.tracing]
default = false
```

Enable them with `--features tracing`; repeat the option or use a comma-separated value. Disable all
root default features with `--no-default-features`.

For each declared feature, Lito owns a package-local integer macro. The default spelling uppercases
the feature, changes `-` to `_`, and prefixes `LITO_FEAT_`, so `fast-path` becomes
`LITO_FEAT_FAST_PATH`. Its value is available while preprocessing that package and does not escape
across module/package boundaries.

`LITO_PKG_VERSION` similarly expands to the current package version when a preprocessor expression
queries it. Lito records these semantic macro queries during scanning and passes matching values to
the compiler.

## Conditional usage

Use `[[when]]` to add usage requirements after a condition evaluates true:

```toml
[[when]]
condition = 'target.os == "linux" && feature.tracing'

[when.usage]
private-definitions = ["HAVE_LINUX_TRACING=1"]
system-libraries = ["dl"]
```

Conditions support booleans, feature names, and target family/OS comparisons. Conflicting settings
from multiple active conditions are diagnosed with both declaration sources.

Targets may instead use their `when` array to add named `source-groups`. See the exact
[profile, feature, and condition fields](../reference/lito-toml/profiles-features-and-conditions.md).
