# Rutile Shading Language (RTSL)

Rutile Shading Language is a shader language and compiler for the Rutile
graphics API.

RTSL keeps a shader contract in one source file: resources, stage payloads,
helper types, and entry points live together in a C-style language that lowers
to backend-neutral Rutile shader IR.

## Documentation

The specification is split by concern:

- [Language semantics](docs/language.md)
- [Compiler architecture](docs/compiler-architecture.md)
- [Artifact formats](docs/artifacts.md)
- [RTIR](docs/rtir.md)
- [Linking](docs/linking.md)
- [Backend contract](docs/backend-contract.md)

## Examples

The `workspace` directory contains sample shader files:

- `default.rtsl`
- `graphics.rtsl`
- `compute.rtsl`
- `advanced.rtsl`

These samples demonstrate graphics stages, compute structure, uniform resource
access, varying payloads, and proposed advanced stage families such as
tessellation, mesh, and ray tracing.

## Current Status

RTSL is still a design and tooling project. The compiler architecture and binary
artifact model are being specified before the full parser, semantic analyzer,
RTIR writer, linker, and backend contracts are implemented.

Editor tooling scaffolding lives under `tools/vs-rtsl-ext/`.

