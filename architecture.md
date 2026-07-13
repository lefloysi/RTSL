# RTSL architecture

RTSL compiles source files into backend-neutral binary artifacts that the
[Rutile](https://www.github.com/lefloys/Rutile) runtime consumes:

```
.rtsl  source        --[ compiler ]--> .rtslo  object
                                        .rtslm  module/interface (when it exports)
.rtslo + .rtsll      --[ linker  ]--> .rtsll  library
                                        .rtslp  final program (consumed by Rutile backends)
```

`rtslc` is the CLI driver wrapping the compiler/linker library. `rtsl-sdk` is
the embeddable SDK layer projects use to read RTSL artifacts and get the data
backend headers need for target-specific lowering.

## Repository layout

The repository is split into top-level project folders. The compiler library
project lives under `rtsl/`, the CLI project lives under `rtslc/`, and shared
SDK model headers live under `rtsl-sdk/`. The public C ABI header belongs to the
compiler project at `rtsl/include/rtsl.h`.

```
RTSL/
  rtsl/
    include/              # public C ABI: rtsl.h
    src/
      support/            # compiler plumbing: source manager, diagnostics, binary io
      frontend/           # preprocessor, lexer, parser, AST
                          #   (+ tokens.def, directives.def, builtins.def, resource_types.def)
      sema/               # semantic analysis + type checking, mangling, uniform lowering
      ir/                 # SSA RTIR: lowering, verification, disassembly (+ ops.def)
      artifact/           # artifact container serialization and the linker
      driver/             # compiler orchestration
      api/                # the C ABI implementation (rtsl.cpp) and the language service
  rtslc/
    src/                  # CLI executable sources
  rtsl-sdk/
    include/rtsl/         # SDK model headers shared by compiler and backends
  tests/                  # rtsl-tests + workspace shaders
  cmake/                  # Rtsl.cmake integration helpers
  docs/                   # language, RTIR, artifacts, linking, backend-contract specs
  scripts/                # build.{sh,bat}, test.{sh,bat}
  workspace/              # scratch shaders for development
```

## Build graph

The build is intentionally layered: compiler library, SDK, CLI, and tests.

```
rtsl        STATIC   rtsl/src support + frontend + sema + ir + artifact + driver + api
rtsl-sdk    INTERFACE rtsl-sdk/include/rtsl/*.hpp   -> shared SDK headers
rtslc       EXE      rtslc/src/rtslc.cpp            -> rtsl (+ CLI11)
rtsl-tests  EXE      tests/*.cpp                     -> rtsl (+ Catch2)
```

Ad hoc runtime package readers are not part of this repository. Shared artifact
model definitions belong in `rtsl-sdk`; target-specific transpilation belongs
in backend headers that consume the SDK instead of compiler internals.

## Pipeline

```
RTSL source
  -> preprocessor        (#define / #ifdef)
  -> lexer               (tokens + source offsets)
  -> parser              (AST; owns syntax only)
  -> semantic analysis   (name/type resolution, type checking, resource + stage
                          layout assignment, exports)
  -> RTIR generation     (typed SSA, backend-neutral)
  -> artifact writer     (.rtslo, and .rtslm when the source exports)
```

The linker merges `.rtslo`/`.rtsll` inputs (id-remapping + cross-module
inlining) and emits `.rtsll` or a final `.rtslp` program.

## Layer ownership

- **frontend** owns syntax (lexing, parsing).
- **sema** owns source-language meaning: name/type resolution, type checking,
  overload/stage validation, and the resource/stage layout assignment.
- **ir** owns the backend-neutral program model and its verification.
- **artifact** owns the serialized container format.
- **api** owns the C-compatible handles, errors, and lifetime bridges.

There are **no backends in this repository.** Turning an `.rtslp` into SPIR-V,
HLSL, MSL, or WGSL — including synthesizing each target's stage input/output
variables from the stage-interface metadata — is backend policy that lives in
Rutile. RTIR therefore never represents stage input/output as instructions; a
stage entry is a plain typed function carrying only its stage tag.

## Boundary rules

- Rutile backends consume `.rtslp` (and reflect other artifacts) only. They must
  never link the RTSL compiler, frontend, or linker.
- The public C ABI, CLI, SDK, CMake helpers, and serialized artifact format are
  the intended RTSL surfaces for code outside this repo.
- Artifact wire-format constants live in `rtsl-sdk`; compiler artifact code
  and backend/transpiler code consume that shared SDK model.
- Nothing in `bindings/` may include from `rtsl/`.

## Related specs

- [docs/compiler-architecture.md](docs/compiler-architecture.md)
- [docs/language.md](docs/language.md)
- [docs/artifacts.md](docs/artifacts.md)
- [docs/rtir.md](docs/rtir.md)
- [docs/linking.md](docs/linking.md)
- [docs/backend-contract.md](docs/backend-contract.md)
