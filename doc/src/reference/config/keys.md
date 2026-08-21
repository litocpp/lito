# Configuration keys

## Global data root

Lito keeps reusable Git sources, downloaded archives, and automatically built tools under one
global data root. An absolute `XDG_DATA_HOME` selects `$XDG_DATA_HOME/lito` on every platform.
Without that override, the root is `$HOME/.local/share/lito` on Unix and `%LOCALAPPDATA%\lito` on
Windows.

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

`stdlib` accepts `"auto"`, `"libc++"`, `"libstdc++"`, or `"msvc"` and defaults to `"auto"`.
Automatic selection uses the effective build target after typed target and sysroot options have
been resolved:

- Android and macOS select libc++;
- Linux selects libstdc++;
- Windows with the MSVC environment selects MSVC STL;
- MinGW and targets without a defined policy require an explicit value.

The build setup reports the resolved library. `auto` is only a configuration selection; compiler,
BMI, link, and cache identities use the resolved concrete library.

```toml
[toolchain]
cxx = "clang++"
stdlib = "auto"
```

## `[tools]`

Simple host tool fields accept a searchable name or an absolute path:

- `tar`, default `tar`;
- `bsdtar`, default `bsdtar`;
- `clang-format`, default `clang-format`;
- `curl`, default `curl`;
- `git`, default `git`;
- `strip`, default `llvm-strip`.

```toml
[tools]
clang-format = "clang-format"
git = "/opt/git/bin/git"
```

Tools with provider configuration use nested tables. Their `executable` field also accepts a
searchable name or an absolute path:

```toml
[tools.cmake]
executable = "/opt/cmake/bin/cmake"
generator = "Ninja"
search-path = ["../install/lib/cmake"]

[tools.pkg-config]
executable = "pkg-config"
search-path = []
library-path = []
sysroot = "target/sysroot"
```

When only the executable is configured, CMake and pkg-config also accept a scalar shorthand:

```toml
[tools]
cmake = "/opt/cmake/bin/cmake"
pkg-config = "pkg-config"
```

Each shorthand is exactly an alias for the corresponding `executable` field. Lito normalizes it to
the provider table before merging shared, local, and invocation configuration, so it never removes
generator, search path, sysroot, or override fields supplied elsewhere. Repeated `-c` assignments
also merge provider fields from left to right.

The tools namespace declares executable requests and provider settings; it does not make every tool
a prerequisite. Lito resolves a role only when the selected command, package, external input,
profile, or install entry produces an action that needs it. A configured executable may therefore
be absent when the current invocation does not use its capability.

Archive extraction selects the first available provider. An explicitly configured `bsdtar` or
`tar` is considered first, followed by the other archive tool and `cmake -E tar`. A valid
materialization needs none of them, and a fetch-seed or verified file-cache hit does not require
`curl`.

### `[tools.pkg-config]`

- `executable` defaults to `pkg-config`;
- `search-path` is an array of existing directories used to find package metadata;
- `library-path` is an array of existing directories used for target library metadata;
- `sysroot` is an existing path, canonicalized relative to the project root.

Setting `executable`, `library-path`, or `sysroot` marks pkg-config as explicitly target-configured.
Setting only `search-path` does not.

### `[tools.cmake]`

- `executable` defaults to `cmake`;
- `generator` is a non-empty string and defaults to `Ninja`;
- `search-path` is an array of existing directories;
- `overrides` is a table of local installed-package substitutions.

An override currently supports only `source = "installed"`:

```toml
[tools.cmake.overrides.Vulkan]
source = "installed"
```

`Vulkan` must equal the `package = "Vulkan"` value of the affected
`[external-dependencies.cmake.NAME]` declarations. It is not the manifest-local `NAME` alias. The
override applies to every selected dependency with that CMake package identity.

`tools.cmake.overrides` is forbidden in `lito-config.toml`; use `.lito/config.toml` or `-c`.

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
