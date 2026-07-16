# Architecture

RTSL is organized as a small compiler pipeline plus a linker.

```text
.rtsl source
  -> compiler
  -> .rtslo object
  -> linker
  -> .rtslp program
  -> Rutile backend
```

Sources that export declarations also produce `.rtslm` module interfaces.
Libraries use `.rtsll`.

## Compiler

```text
preprocessor -> lexer -> parser -> semantic analysis -> RTIR -> artifact writer
```

- preprocessor: source inclusion and macro handling
- lexer: tokens and source offsets
- parser: syntax and AST
- semantic analysis: names, types, resources, stages, layouts, imports, exports,
  and diagnostics
- RTIR lowering: backend-neutral program representation
- artifact writer: serialized objects and module interfaces

## Linker

The linker consumes `.rtslo` objects and `.rtsll` libraries. It merges modules,
resolves calls, validates program shape, and writes either `.rtsll` or `.rtslp`.

## Public Surfaces

- `rtslc`: command-line tool
- `rtsl/include/rtsl.h`: C ABI
- `rtsl-sdk/include/rtsl/`: shared artifact model
- `cmake/Rtsl.cmake`: CMake helper
- `spec/`: v0.1 contracts

## Backends

Shader backends live outside this repository. A Rutile backend consumes a
linked `.rtslp` program and emits the target shader format.
