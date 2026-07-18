# RTSL

RTSL is Rutile's shader language. The compiler produces linked `.rtslp`
programs; backend libraries load those programs through a small C++ SDK and
transpile individual shader stages.

## Features

- complete vertex and fragment program model
- typed stage inputs and outputs derived from ordinary structs
- imports, object files, module interfaces, libraries, and linked programs
- `rtslc` command-line compiler and linker
- immutable C++23 SDK for backend authors
- separate HLSL and SPIR-V transpilers
- CMake program building and embedding

## Minimal Shader

```rtsl
struct Point {
    vec3 position;
};

struct Vertex {
    vec4 position;
};

@stage : vertex
fn vertex_entry(Point p) -> Vertex : position(clip) {
    return Vertex(vec4(p.position, 1.0));
}

@stage : fragment
fn fragment_entry(Vertex v) -> vec4 {
    return vec4(1.0, 0.0, 1.0, 1.0);
}
```

## Build And Consume A Program

```powershell
rtslc compile shader.rtsl -o shader.rtslo
rtslc link-program shader.rtslo -o shader.rtslp
```

Backend code loads only the linked program and requests one target stage:

```cpp
#include <rtsl/sdk.hpp>
#include <rtsl/spirv.hpp>

auto program = rtsl::load_program(program_bytes);
if (!program) {
    report(program.error().message);
    return;
}

auto vertex = rtsl::spirv::transpile(*program, rtsl::Stage::vertex);
auto fragment = rtsl::spirv::transpile(*program, rtsl::Stage::fragment);
```

The backend links the SDK and whichever transpilers it uses. It never links the
RTSL compiler.

```cmake
target_link_libraries(my_backend PRIVATE RTSL::sdk RTSL::hlsl RTSL::spirv)
```

## Build RTSL

```powershell
cmake -S . -B out/build
cmake --build out/build --config Debug
ctest --test-dir out/build -C Debug --output-on-failure
```

## Targets

- `rtsl`: compiler/linker library
- `rtslc`: command-line compiler
- `RTSL::sdk`: linked-program loading, validation, reflection, and typed RTIR
- `RTSL::hlsl`: stage-to-HLSL transpiler used before DXC; depends only on the SDK
- `RTSL::spirv`: separate stage-to-SPIR-V transpiler; depends only on the SDK
- `rtsl-tests` and `rtsl-sdk-tests`: integration and dependency-boundary tests

## Documentation

- [Language](docs/language.md)
- [Artifacts and SDK](docs/artifacts-sdk.md)
- [Command line](docs/cli.md)
- [CMake](docs/cmake.md)
- [Compiler C ABI](docs/c-abi.md)

## License

RTSL is distributed under the MIT License. See [LICENSE](LICENSE).
