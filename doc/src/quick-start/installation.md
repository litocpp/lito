# Install Lito

## Required tools

Install these tools before building Lito or a Lito project:

- LLVM/Clang 22: `clang`, `clang++`, `ld.lld`, `llvm-ar`, and `llvm-strip`;
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

Lito defaults to libc++. Select libstdc++ for a project with a shared configuration file:

```toml
[toolchain]
stdlib = "libstdc++"
```

Save this as `lito-config.toml` in the project root when the choice is a project contract. Use
`.lito/config.toml` for a machine-local choice, or pass
`-c toolchain.stdlib=libstdc++` for one invocation. See
[configuration precedence](../reference/config/precedence-and-locations.md).
