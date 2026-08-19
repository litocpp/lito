# Lito

Lito is a module-first build tool for C++.   
A fixed `lito.toml` describes packages, workspaces, targets, dependencies, and build policy.  
Lito discovers named module sources and drives Clang, LLD, and LLVM tools.  

[Documentation](https://lito.litocpp.org/)

## Requirements

- LLVM/Clang 22: `clang`, `clang++`, `ld.lld`, `llvm-ar`, and `llvm-strip`.
- A supported C++ standard library: libc++ or libstdc++ on Unix-like targets, and MSVC STL or a
  separately supplied shared libc++ on Windows MSVC targets.
- `clang-format` for `lito format`.
- CMake 4 and Ninja when bootstrapping Lito or building a CMake dependency.

## Bootstrap

```sh
cmake --preset release
cmake --build --preset release
cmake --install build/cmake-release --prefix build/install

./build/install/bin/lito --version
```

## First project

Create this layout:

```text
hello/
├── lito.toml
└── src/
    └── main.cppm
```

`lito.toml`:

```toml
[package]
name = "hello"
version = "0.1.0"

[[bin]]
name = "hello"
module = "hello"
```

`src/main.cppm`:

```cpp
module;

#include <cstdio>

export module hello;

auto main() -> int {
    std::puts("Hello from Lito");
    return 0;
}
```

Build and run:

```sh
lito build
./build/debug/bin/hello/hello
```

Because the target omits `sources`, Lito uses the runnable entry convention at `src/main.cppm` and
discovers its module dependencies. Use explicit `sources` for targets that do not follow the module
convention.

See the [manifest schema](schema/lito.schema.json) for the machine-readable structure and the
[documentation](https://lito.litocpp.org/) for package, workspace, configuration, and CLI details.

## License

[MIT](LICENSE-MIT) OR [Apache-2.0](LICENSE-APACHE)
