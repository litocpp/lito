# Lito

Lito is a module-first C++ build tool with manifest.  
It uses `lito.toml` manifest to build packages.  

## Requirements

- LLVM/Clang 22 with libc++ and LLD
- CMake 4.0 or newer

## Bootstrap

```sh
cmake -S . -B build/cmake-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang-22 \
  -DCMAKE_CXX_COMPILER=clang++-22
cmake --build build/cmake-release --target lito
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
discovery = "explicit"
sources = ["src/main.cpp"]
```

Then run:

```sh
lito build
lito test
lito format --check
```

See the [manifest schema](schema/lito.schema.json) for the complete configuration contract.

## License

[MIT](LICENSE)
