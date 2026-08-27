# Install Lito

## Required tools

Install these tools before building Lito or a Lito project:

- LLVM/Clang 22: `clang`, `clang++`, LLD (`ld.lld` or `lld-link`), `llvm-ar`, and `llvm-strip`;
- `clang-format` for `lito format`;
- libc++ or libstdc++ for C++ targets that link the standard library;
- CMake and Ninja when bootstrapping Lito or consuming CMake external dependencies;
- Git when a manifest uses Git sources.

Confirm the executable and version:

```sh
lito --version
```

## Bootstrap from source

The CMake build is Lito's bootstrap path. From a Lito source checkout:

```sh
cmake --preset release
cmake --build --preset release
cmake --install build/cmake-release --prefix build/install
build/install/bin/lito --version
```

The repository presets select the Clang toolchain and may point at sibling rstd and luato
checkouts used by Lito development. A packaged source build may instead let CMake fetch the pinned
dependencies from the repositories declared by the root `CMakeLists.txt`.

These commands build Lito itself. A normal Lito package does not need a CMake file.

## Select the C++ standard library

Lito defaults to automatic selection: Linux uses libstdc++, Android and macOS use libc++, and a
Windows MSVC target uses MSVC STL. The selection follows the effective target rather than the host
machine. The build setup prints the concrete result before scanning.

Most projects can omit `toolchain.stdlib`. Select a concrete library when the target has no
automatic policy or the project requires a particular library:

```toml
[toolchain]
stdlib = "libstdc++"
```

Save this as `lito-config.toml` in the project root when the choice is a project contract. Use
`.lito/config.toml` for a machine-local choice, or pass
`-c toolchain.stdlib=libstdc++` for one invocation. See
[configuration precedence](../reference/config/precedence-and-locations.md).

Selecting a standard library controls compilation and linking. Lito does not copy a managed LLVM
SDK's libc++ runtime or add a runtime search path during ordinary build or install.
