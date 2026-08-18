# Troubleshooting

Lito errors form a cause chain. Start at the outer operation, then read each `caused by` line until
the message names the owning package, target, source, config key, or tool invocation.

## Confirm the effective tools

A build prints resolved tool paths before scanning. Override one invocation through config keys:

```sh
lito -c toolchain.cxx=/opt/llvm/bin/clang++ build
lito -c toolchain.stdlib=libstdc++ build
```

Persist project-wide values in `lito-config.toml` and local values in `.lito/config.toml`. Run
`lito config path` to see the local file. `--no-config` ignores only that local file; it does not
ignore `lito-config.toml`.

The build setup also lists configured C, C++, and link options together with their source key or
environment variable. If an integration expects `CFLAGS`, `CXXFLAGS`, or `LDFLAGS`, remember that
Lito ignores them unless the command uses `--use-env-flags`.

## Module source cannot be resolved

Check all three identities together:

- the target's `module` field;
- the logical name in `export module` or `import`;
- the convention path derived from dots and partitions.

Lito checks the same-name `.cppm` before `mod.cppm`. A partition also requires a provider for its
primary module and a compatible compile/BMI context.

Use `lito scan SOURCE` to inspect the frontend result for one file.

## BMI or option conflicts

Exceptions, RTTI, standard library, threading, language standard, public requirements, target, and
other compiler policy can affect compatibility. Declare them through profile, typed usage, and
toolchain config. Avoid passing a second spelling through raw options: Lito rejects or diagnoses
owned-setting conflicts instead of silently producing incompatible BMIs.

`build.options` is C++-only, `build.c.options` is C-only, and `build.linker-options` is link-only.
Use the reported source label to move a misplaced flag to the correct domain. A target flag that
disagrees with Lito's selected target is rejected before compilation rather than being allowed to
produce a mixed C/C++ graph.

## Lock and source failures

- remove `--locked` only when you intend to update `lito.lock`;
- use `lito update` to separate resolution from compilation;
- use `--offline` to verify that all required sources are already available;
- use repeated `--fetch-seed` for read-only CI or Flatpak inputs;
- keep machine-local Git patches in `.lito/config.toml`.

Do not hand-edit a high-version or incompatible lock into acceptance. Lito validates the lock
format and uses the current manifest to compute the desired graph.

## Performance and compiler failures

Use `--verbose` for exact build events and `--timing-file FILE` for detailed scan, compile, archive,
and link timings. When Clang crashes, rerun the printed compiler invocation outside Lito and keep
Clang's preprocessed source/run script. Lito cannot recover from a frontend process crash, but the
invocation identifies whether the failing source and BMI set are reproducible.
