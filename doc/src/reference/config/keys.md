# Configuration keys

## `[environment]`

`append-path` is an array of existing directories. Lito canonicalizes them relative to the project
root and appends them to the process search path used for tools and build-script actions.

```toml
[environment]
append-path = ["tools/bin"]
```

## `[toolchain]`

Executable fields accept a searchable name or an absolute path:

- `cc`, default `clang`;
- `cxx`, default `clang++`;
- `ld`, default `ld.lld`;
- `ar`, default `llvm-ar`.

`stdlib` is `"libc++"` or `"libstdc++"` and defaults to `"libc++"`.

```toml
[toolchain]
cxx = "clang++"
stdlib = "libstdc++"
```

## `[tools]`

Host tool fields accept a searchable name or an absolute path:

- `cmake`, default `cmake`;
- `tar`, default `tar`;
- `bsdtar`, default `bsdtar`;
- `clang-format`, default `clang-format`;
- `curl`, default `curl`;
- `git`, default `git`;
- `pkg-config`, default `pkg-config`;
- `strip`, default `llvm-strip`.

```toml
[tools]
cmake = "/opt/cmake/bin/cmake"
clang-format = "clang-format"
```

This table maps tool roles to executable requests; it does not make every tool a prerequisite.
Lito resolves a role only when the selected command, package, external input, profile, or install
entry produces an action that needs it. A configured executable may therefore be absent when the
current invocation does not use its capability.

Archive extraction selects the first available provider. An explicitly configured `bsdtar` or
`tar` is considered first, followed by the other archive tool and `cmake -E tar`. A valid
materialization needs none of them, and a fetch-seed or verified file-cache hit does not require
`curl`.

## `[build]`

Global build options are split by language and link domain:

```toml
[build]
options = ["-Wall", "-DPROJECT_CPP=1"]
linker-options = ["-Wl,--as-needed"]

[build.c]
options = ["-Wstrict-prototypes", "-DPROJECT_C=1"]
```

- `build.options` is an array of non-empty C++ compiler option strings;
- `build.c.options` is an array of non-empty C compiler option strings;
- `build.linker-options` is an array of non-empty options for link actions.

The C and C++ streams are independent: a C option does not enter C++ compilation and a C++ option
does not enter C compilation. Link options do not change compile identities. Lito parses known
options into its typed policy, includes relevant values in scan/compile/link identities, and reports
the originating config key when an option conflicts with the selected target, profile, standard
library, or another Lito-owned setting.

Ambient `CFLAGS`, `CXXFLAGS`, and `LDFLAGS` are ignored by default. The global `--use-env-flags`
option appends them to the C, C++, and link streams respectively for build-oriented commands. Their
contents use command-fragment tokenization; malformed quoting or non-UTF-8 values are errors that
name the variable.

## `[lock]`

`path` is a non-empty file path and defaults to `lito.lock` in the project root. A relative path is
based at the project root. Its parent must exist; if the target exists, it must be a file.

```toml
[lock]
path = ".lito/lito.lock"
```

## `[install]`

`root` is a non-empty directory path used as the default Lito-managed install root. A relative value
is based at the project root. Managed-root precedence is `--root`, `LITO_INSTALL_ROOT`, this config
value, `LITO_HOME`, then `$HOME/.lito`.

## `[doc]`

`litodoc-path` is an existing directory containing a local Litodoc project. Lito canonicalizes it
when loading config.

## `[patch."GIT-URL"]`

Every patch entry requires `path`, an existing local directory. The Git URL must be non-empty, must
not start with `-`, and must not contain `#`.

```toml
[patch."https://github.com/litocpp/rstd.git"]
path = "../rstd"
```

## `[pkg-config]`

- `search-path` is an array of existing directories used to find package metadata.
- `library-path` is an array of existing directories used for target library metadata.
- `sysroot` is an existing path, canonicalized relative to the project root.

Setting `tools.pkg-config`, `library-path`, or `sysroot` marks pkg-config as explicitly
target-configured.

## `[cmake]`

- `generator` is a non-empty string; default `Ninja`.
- `search-path` is an array of existing directories.
- `overrides` is a table of local installed-package substitutions.

An override currently supports only `source = "installed"`:

```toml
[cmake.overrides.Vulkan]
source = "installed"
```

`cmake.overrides` is forbidden in `lito-config.toml`; use `.lito/config.toml` or `-c`.
