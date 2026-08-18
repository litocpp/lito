# Profiles, features, and conditions

Profiles choose code-generation policy. Features choose package capabilities. Conditions apply
usage or source groups for the selected target environment. They are separate inputs and remain in
the build and BMI identities when they affect compilation.

## Built-in profiles

`debug` has no optimization, full debug information, no stripping, and no LTO. It is the default
for build-oriented commands other than `install`. `release` uses optimization level 3, no debug
information, no stripping, and no LTO. It also defines `NDEBUG` and is the default for `install`.

Project-wide exception and RTTI policy is declared at the top of `[profile]`:

```toml
[profile]
exceptions = false
rtti = false
```

Create a named profile by inheriting an existing one:

```toml
[profile.perf]
inherits = "release"
debug = "line-tables-only"
lto = "thin"
```

Built-in `debug` and `release` may be customized directly but cannot declare `inherits`. Every other
profile must inherit another profile, and inheritance cycles are rejected.

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
one invocation; ambient values are otherwise ignored.

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
