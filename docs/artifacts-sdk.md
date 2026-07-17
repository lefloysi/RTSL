# Artifacts And SDK

Artifacts are the boundary between the RTSL compiler and backend code.

## Artifact Files

- `.rtslo`: object produced from one source file
- `.rtslm`: compiler module interface used by imports
- `.rtsll`: linked compiler library
- `.rtslp`: linked program consumed by the SDK and backends

Only `.rtslp` is backend input. `rtsl::load_program` rejects every other
artifact kind.

```text
source --compiler/linker--> .rtslp --SDK--> Program --transpiler--> target shader
```

The artifact format version is 1.0. This is independent of the RTSL v0.1
language release. Programs written with older artifact encodings must be rebuilt.

## Dependency Boundary

```text
backend ──> RTSL::sdk
backend ──> RTSL::spirv
RTSL::spirv ──> RTSL::sdk

rtsl compiler ──> RTSL::sdk
```

The SDK and `rtsl-spirv` include no parser, semantic-analysis, linker, driver, or
compiler C ABI headers. The compiler produces programs; backend libraries only
consume them.

## Loading

```cpp
#include <rtsl/sdk.hpp>

std::span<const std::byte> bytes = read_file("shader.rtslp");
auto program = rtsl::load_program(bytes);
if (!program) {
    log(program.error().context, program.error().message);
    return;
}
```

`Program` is owning, immutable, move-only, and safe to pass by reference between
backend helpers. Spans and pointers returned by it remain valid until that
program is destroyed.

`LoadError` reports a stable code, byte offset when available, context, and a
human-readable message. Loading validates the artifact version and kind, ID
graph, type and constant shapes, instructions, control-flow blocks, stage
entries and interfaces, resources, decorations, and the absence of unresolved
calls.

## Program Model

The high-level backend contract is entry-centric:

- `entry(Stage)` resolves the authored stage name, function, input, and output
- each interface element has a type ID, optional struct member, location or
  built-in, and interpolation mode
- resources carry normalized kind, image shape, access, descriptor set and
  binding, global/value IDs, and stage-use mask
- `find_type`, `find_constant`, `find_global`, and `find_function` provide
  direct ID lookup
- functions contain typed parameters, basic blocks, and public RTIR operations
- decorations are available globally or by target ID

Correct transpilation depends on enums and IDs. Names are diagnostic metadata,
not backend naming policy.

## Embedded Programs

`rtsl_add_program(... EMBED)` defines an SDK-owned `ProgramBytes` symbol:

```cpp
extern "C" const rtsl::ProgramBytes game_world_shader;

auto program = rtsl::load_program(game_world_shader);
```

`ProgramBytes` is a borrowed view. `load_program` copies and normalizes the
program into its own storage.

## SPIR-V Transpiler

SPIR-V is a separate project and SDK consumer:

```cpp
#include <rtsl/spirv.hpp>

auto vertex = rtsl::spirv::transpile(program, rtsl::Stage::vertex);
if (!vertex) {
    log(vertex.error().context, vertex.error().message);
    return;
}

create_shader_module(vertex->words, vertex->entry_point);
```

Each call emits one module with one `main` entry. The transpiler synthesizes
physical input/output variables and a void wrapper around the selected typed
RTSL entry function. It returns structured errors for a missing stage, invalid
entry contract, unsupported type or operation, and allocation failure.
