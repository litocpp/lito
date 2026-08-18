# Command-line Reference

## Invocation

```text
lito [GLOBAL OPTIONS] COMMAND [COMMAND OPTIONS] [ARGUMENTS]
```

Lito requires a subcommand. `-h/--help` prints generated help; `-V/--version` prints the installed
package version.

## Global options

`-C DIRECTORY` changes the effective working directory before manifest discovery. It defaults to
`.`.

`--no-config` ignores `.lito/config.toml` but still reads `lito-config.toml`.

`-c/--config KEY=VALUE` overrides one config value for the invocation and may be repeated.

`--use-env-flags` appends ambient `CFLAGS`, `CXXFLAGS`, and `LDFLAGS` to the C, C++, and link option
domains. It is valid only for `build`, `install`, `test`, `bench`, `doc`, and `scan`; the variables
are otherwise ignored.

Toolchain values are configuration keys rather than dedicated CLI options. Override one invocation
with `-c toolchain.cxx=/opt/llvm/bin/clang++` or persist the value in a config file.

The `config` subcommand cannot be combined with `--no-config` or `--config`; it reads or mutates the
persisted local config directly.

## Commands

- [`build` and `install`](build-and-install.md)
- [`test` and `bench`](test-and-bench.md)
- [`doc`, `scan`, and `format`](doc-scan-and-format.md)
- [`update`, `lock`, and `config`](update-lock-and-config.md)

Lito reports errors as a cause chain on stderr and returns a non-zero exit status. Commands that
emit JSON or config values reserve stdout for that result.
