# Rutile Shading Language

Rutile Shading Language, or RTSL, is the shader language and shader toolchain
for Rutile. It lets engine code describe shader interfaces once, compile that
source into backend-neutral artifacts, and hand the final linked program to a
Rutile backend for target-specific emission.

RTSL exists to keep shader source, reflection data, resource bindings, stage
interfaces, and link-time shader composition in one coherent model. Backends do
not parse source and do not reconstruct language meaning from text. They consume
linked program artifacts and lower the normalized representation to SPIR-V,
HLSL, MSL, WGSL, or another target format.

The project is intended for Rutile itself, backend authors, tooling authors,
and engine code that needs a stable shader compilation boundary.

## Design Goals

- Keep source language meaning out of backend code.
- Make resource and stage-interface reflection a compiler product, not a
  convention duplicated by each renderer backend.
- Preserve authored shader structure where it matters for diagnostics,
  debugging, linking, and reflection.
- Use a backend-neutral intermediate representation for validation, linking,
  and artifact transport.
- Expose stable integration surfaces through the CLI, C ABI, CMake helpers, and
  serialized artifacts.
- Keep the shared `rtsl-sdk` core usable by both the compiler and transpilers.

## Architecture

RTSL is split around compiler ownership boundaries. Each layer owns one kind of
decision and passes normalized data to the next layer.

```text
.rtsl source
  -> frontend          lexing, parsing, AST construction
  -> semantic analysis name/type resolution, stage rules, resource metadata
  -> RTIR              backend-neutral typed IR and reflection bridges
  -> artifact writer   .rtslo object and .rtslm interface artifacts

.rtslo / .rtsll + .rtslm
  -> linker            symbol, type, resource, stage-interface resolution
  -> .rtsll library or .rtslp final program

.rtslp
  -> Rutile backend    target-specific shader output
```

The major systems are:

- `frontend`: preprocessing, tokenization, parsing, and AST construction. This
  layer owns syntax only.
- `sema`: source-language meaning, including name lookup, type checking,
  overload selection, stage validation, resource classification, and layout
  metadata.
- `ir`: RTIR, a typed backend-neutral representation used by lowering,
  validation, linking, artifact writing, and backend consumption.
- `artifact`: binary artifact containers and shader-specific linking.
- `driver`: orchestration for compilation, imports, and module interfaces.
- `api`: the C-compatible embedding boundary and language-service entry points.
- `rtslc`: the command-line driver around the compiler and linker.
- `bindings`: public ABI headers for tools and host integrations.
- `rtsl-sdk`: shared artifact and type model used by the compiler and by
  transpilers or backends that consume RTSL artifacts.

Backend policy lives outside this repository. A backend may reject a linked
program that uses unsupported stages, resource kinds, primitive operations, or
target capabilities, but it should not reinterpret RTSL source-language rules.

## Core Concepts

**RTSL source** is the authored shader language. Stage entries are ordinary
functions marked with `@stage : name`; the function name itself is not special.

**RTIR** is the backend-neutral typed intermediate representation. It is not
RTSL syntax and it is not a target shader dialect. It carries instructions,
types, constants, functions, decorations, and reflection bridges.

**Artifacts** are the binary interchange format between compilation, linking,
loading, reflection, and backend lowering:

- `.rtslo`: compiled object for one source file.
- `.rtslm`: module interface containing exported declarations.
- `.rtsll`: linked library that can be used as a later link input.
- `.rtslp`: final linked shader program consumed by Rutile backends.

**Stage interfaces** describe data crossing shader stage boundaries. The
compiler records payload fields, interpolation, built-in placement, and assigned
locations so backends can synthesize target-specific inputs and outputs without
recovering that information from instruction streams.

**Resources** are declared in source and assigned concrete binding metadata by
the compiler and linker. Reflection reports source-facing names and backend
binding data from artifacts.

**Primitives** are reserved backend-lowered operations identified by canonical
symbols. Ordinary language functions are not backend intrinsics unless they
lower to reserved primitives.

## Repository Structure

- `rtsl/`: compiler library project. Its `src/` tree contains the frontend,
  semantic analysis, RTIR lowering, artifact writer/linker, diagnostics, driver,
  and ABI implementation. Its `include/` tree contains the public C ABI.
- `rtsl-sdk/`: shared core for the compiler and transpilers. It owns artifact
  and type model definitions that must stay consistent across producers and
  consumers of RTSL artifacts.
- `rtslc/`: command-line compiler and linker driver project.
- `docs/`: normative design documents for the language, compiler architecture,
  RTIR, artifacts, linking, and backend contract.
- `tests/`: parser, compiler, linker, ABI, artifact, and shader fixture tests.
- `cmake/`: CMake integration helpers for building RTSL shader programs as part
  of a host project.
- `scripts/`: development and validation helpers.
- `workspace/`: scratch shader inputs and local development artifacts.
- `tools/`: editor and IDE tooling experiments related to RTSL.

Build outputs, generated fixtures, local package installs, and IDE state belong
under `out/`, `.vs/`, or tool-specific `bin/` and `obj/` directories.

## Build And Development

RTSL is a CMake project that requires a C++23 compiler. The command-line driver
uses CLI11, and the test target uses Catch2.

On Windows, run the build from a Visual Studio developer shell:

```powershell
cmake -S . -B out/build
cmake --build out/build --config Debug
ctest --test-dir out/build -C Debug --output-on-failure
```

The primary CMake targets are:

- `rtsl`: static compiler library.
- `rtsl-sdk`: interface target for shared artifact/type model headers.
- `rtslc`: command-line compiler driver.
- `rtsl-tests`: Catch2 test executable.

The normal development loop is:

1. Update the language, compiler, artifact, or backend-contract documentation
   that defines the intended behavior.
2. Change the owning compiler layer.
3. Add focused tests at the nearest meaningful boundary.
4. Run the targeted test executable and the CLI smoke tests.
5. Re-check that source, artifacts, reflection, and documentation still describe
   the same model.

## Command-Line Usage

Compile an RTSL source file to an object:

```powershell
rtslc compile shader.rtsl -o shader.rtslo
```

Link one or more objects or libraries into a backend-facing program:

```powershell
rtslc link-program shader.rtslo -o shader.rtslp
```

Build a reusable linked library:

```powershell
rtslc link-library lighting.rtslo materials.rtslo -o shading.rtsll
```

Inspect an artifact as textual RTIR:

```powershell
rtslc dump shader.rtslp
```

Use `-I` with `compile` to add module-interface search paths for imports:

```powershell
rtslc compile material.rtsl -I out/build/shaders -o material.rtslo
```

## CMake Integration

Host projects can build shader programs through the RTSL CMake helpers:

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

The helper compiles each source file to `.rtslo`, links a `.rtslp`, wires local
module-interface dependencies, and can embed linked program bytes into the host
target.

## Example

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

The vertex return boundary marks `position` as clip-space output and `uv` as a
smooth varying. The fragment entry returns a bare `vec4`, which represents the
default color attachment.

## Contributing

Treat RTSL as a language and artifact contract, not only as a set of accepted
syntax cases. Changes should preserve the compiler ownership boundaries:

- syntax belongs in the frontend
- source-language meaning belongs in semantic analysis
- backend-neutral representation belongs in RTIR
- serialization belongs in artifact code
- target-specific lowering belongs in Rutile backends
- ABI lifetime and error conversion belong in the API layer

When changing behavior, update the relevant documentation and add tests that
exercise the general rule. Prefer structural fixes over isolated feature
branches, and keep reflection, linking, and backend contracts aligned with the
language model.

## License

RTSL is distributed under the MIT License. See [LICENSE](LICENSE).
