# Rutile Shading Language

RTSL is Rutile's shader language and compiler. v0.1 targets graphics shaders:
vertex and fragment stages, resource declarations, stage payloads, linking,
reflection, and backend-ready program artifacts.

RTSL source describes the shader contract. The compiler assigns concrete
resource bindings and stage locations, then exposes them through reflection.
Projects that need backend-side artifact handling should depend on the RTSL SDK
layer (`rtsl-sdk`) rather than compiler internals.

## Minimal Program

```rtsl
struct Point {
    vec3 position;
    vec2 uv;
};

struct Vertex {
    vec4 position;
    vec2 uv;
};

@stage : vertex
fn vertex_entry(Point p) -> Vertex : position(clip), uv(smooth) {
    return Vertex(vec4(p.position, 1.0), p.uv);
}

@stage : fragment
fn fragment_entry(Vertex v) -> vec4 {
    return vec4(v.uv, 0.0, 1.0);
}
```

Entry functions are selected by function attributes. v0.1 recognizes
`@stage : name`; their function names are ordinary names, and `main` is only a
convention.

The bare fragment `vec4` return is the default single color target. Structured
vertex returns use a return boundary to mark clip-space position and
interpolated fields.

## Artifacts

- `.rtslo`: compiled source object
- `.rtslm`: exported module interface
- `.rtsll`: linked library
- `.rtslp`: linked program consumed by Rutile backends

## SDK

`rtsl-sdk` is shared code for the compiler and backend/transpiler code that
consumes RTSL artifacts. Its public header is C; its implementation is C++.
It exposes artifact data backend headers need; it does not choose or implement
a backend.

## Documentation

- [Language](docs/language.md)
- [RTIR](docs/rtir.md)
- [Artifacts](docs/artifacts.md)
- [Linking](docs/linking.md)
- [Backend contract](docs/backend-contract.md)
- [Compiler architecture](docs/compiler-architecture.md)
- [C binding](bindings/README.md)

## Build

```powershell
cmake -S . -B out/build
cmake --build out/build --config Debug
ctest --test-dir out/build -C Debug --output-on-failure
```

On Windows, run those commands from a Visual Studio developer shell.

## CMake Integration

```cmake
list(APPEND CMAKE_MODULE_PATH "/path/to/RTSL/cmake")
include(Rtsl)

rtsl_add_program(game_shaders
    RTSLC rtslc
    SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/world.rtsl"
    EMBED
)
```
