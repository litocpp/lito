# Configuration precedence and locations

Lito builds one typed project configuration in this order:

```text
built-in defaults
  -> lito-config.toml
  -> .lito/config.toml
  -> repeated -c/--config assignments
```

Later values replace earlier scalar/array values. Nested tables merge by key.

`--use-env-flags` is an opt-in append operation after configuration decoding rather than another
config file. It adds `CFLAGS`, `CXXFLAGS`, and `LDFLAGS` after configured build-option arrays while
preserving each source as a separate diagnostic input. Without the option, those ambient variables
do not affect Lito compile or link actions.

## Shared project config

`lito-config.toml` is read from the resolved project root. It expresses choices that every checkout
should share, such as the standard library contract or a project-wide CMake generator.

The shared file may not contain `cmake.overrides`. Installed-package substitution is local machine
state; put it in `.lito/config.toml` or pass it with `--config`.

## Local persisted config

`.lito/config.toml` is read after the shared file. It is suitable for absolute tool paths, SDK
locations, local Git patches, and installed CMake package overrides.

`--no-config` ignores only `.lito/config.toml`. It still loads `lito-config.toml`, then applies
command-line overrides.

The `config` command operates on the local file:

```sh
lito config path
lito config get
lito config get toolchain.stdlib
lito config set toolchain.stdlib libstdc++
lito config unset toolchain.stdlib
```

`set` parses `VALUE` as TOML when possible and otherwise stores it as an unquoted string. It
validates the complete resulting config before writing atomically. `unset` errors when the key is
missing. Dotted and quoted TOML key paths are supported.

## Invocation overrides

`-c KEY=VALUE` applies only to the current process and may be repeated:

```sh
lito -c toolchain.stdlib=libstdc++ -c build.options='["-Wall"]' build
```

Assignments are applied left to right. As with `config set`, the value is parsed as TOML when
possible, then falls back to a string.

Toolchain keys use the same override path as every other config value:

```sh
lito -c toolchain.cxx=/opt/llvm/bin/clang++ \
     -c toolchain.stdlib=libc++ build
```

Lito has no separate `--toolchain.*` option family. Repeated `-c` assignments are the highest
precedence configuration input and are applied left to right.

For build-oriented commands, ambient language and linker flags can be appended explicitly:

```sh
CFLAGS='-DPROJECT_C=1' \
CXXFLAGS='-DPROJECT_CPP=1' \
LDFLAGS='-Wl,--as-needed' \
lito --use-env-flags build
```

The option is accepted by `build`, `install`, `test`, `bench`, `doc`, and `scan`. Lito removes the
three ambient variables from CMake subprocess environments, so the opt-in flags affect Lito-owned
targets but do not leak into external CMake configuration or builds.

## Relative paths

Relative directories and files in either config file or `-c` are resolved against the project root.
Directory search fields are canonicalized and must already exist. Executable fields accept either a
searchable executable name or an absolute path; a relative path containing directories is rejected.
