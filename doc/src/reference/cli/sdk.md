# `sdk`

The `sdk` command manages LLVM SDKs in Lito's global data directory. Available versions and host
artifacts come from the catalog embedded in the installed Lito executable; the command does not
query GitHub for a mutable version list.

## `lito sdk list`

```text
lito sdk list
```

List catalog entries supported by the current host and merge them with locally installed SDK
descriptors. Status is one of `available`, `installed`, `installed, unavailable`, or `invalid`.
This command does not require a project, network access, or host tools.

## `lito sdk install VERSION`

```text
lito sdk install 22.1.8
```

`VERSION` is required and must be an exact `major.minor.patch` entry in the embedded catalog for the
current host. Lito downloads the catalog archive through the verified global file cache, checks its
size and SHA-256 digest, extracts and certifies the LLVM tools, and atomically publishes it at:

```text
<LitoDataRoot>/llvm/<version>
```

Each installed version contains a deterministic `sdk.json` descriptor. A matching installation is
re-certified and reused without requiring the download cache. An existing directory with a missing,
invalid, or mismatched descriptor is reported as a conflict and is not overwritten.

The command does not change `PATH`, project configuration, or the active toolchain. Host-tool
providers such as curl and an archive extractor can be selected through `[tools]` configuration or
`-c tools.<name>=...`. `--no-config` ignores `.lito/config.toml`; `--use-env-flags` is not valid for
SDK commands.
