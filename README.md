# Rutile Shading Language

RTSL is Rutile's shader language and shader compilation toolchain.

It compiles `.rtsl` source files into backend-neutral artifacts that can be
linked, inspected, embedded, loaded through the C ABI, and consumed by Rutile
backends.

## Build

```powershell
cmake -S . -B out/build
cmake --build out/build --config Debug
ctest --test-dir out/build -C Debug --output-on-failure
```

Primary targets:

- `rtsl`: compiler library
- `rtsl-sdk`: shared artifact model
- `rtslc`: command-line compiler
- `rtsl-tests`: test executable

## Basic Use

```powershell
rtslc compile shader.rtsl -o shader.rtslo
rtslc link-program shader.rtslo -o shader.rtslp
rtslc dump shader.rtslp
```

## Repository Layout

```text
rtsl/          compiler library and public C ABI
rtslc/         command-line compiler
rtsl-sdk/      shared artifact headers
cmake/         CMake integration helper
tests/         compiler and toolchain tests
docs/          practical guides
spec/          v0.1 contracts
workspace/     scratch inputs
tools/         editor and tooling experiments
```

## Documentation

- [Architecture](architecture.md)
- [Practical docs](docs/README.md)
- [Specifications](spec/README.md)

## License

RTSL is distributed under the MIT License. See [LICENSE](LICENSE).
