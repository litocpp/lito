# Lito

Lito is a module-first C++ build tool with manifest.  
It uses `lito.toml` manifest to build packages.  

## Requirements

- LLVM/Clang 22 with libc++ and LLD
- CMake 4.0 or newer

## Bootstrap

```sh
cmake --preset release
cmake --build --preset release
cmake --install build/cmake-release --prefix build/install

./build/install/bin/lito
```

## Usage

Create a `lito.toml`:

```toml
[package]
name = "hello"
version = "0.1.0"

[[bin]]
name = "hello"
sources = ["src/main.cpp"]
```

Declaring `sources` makes the target use those sources explicitly.
If it is not declared, Lito will discover `src/lib.cppm` or `src/main.cppm`.

Then run:

```sh
lito build
lito test
lito format --check
```

See the [manifest schema](schema/lito.schema.json) for the complete configuration contract.

## License

[MIT](LICENSE-MIT) OR [Apache-2.0](LICENSE-APACHE)
