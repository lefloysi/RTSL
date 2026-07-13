# RTSL architecture

RTSL compiles source files into backend-neutral binary artifacts that the
[Rutile](https://www.github.com/lefloys/Rutile) runtime consumes:

```
.rtsl  source        --[ compiler ]--> .rtslo  object
                                        .rtslm  module/interface (when it exports)
.rtslo + .rtsll      --[ linker  ]--> .rtsll  library
                                        .rtslp  final program (consumed by Rutile backends)
```

`rtslc` is the CLI driver wrapping the compiler/linker library.

## Repository layout

Sources are grouped by pipeline layer under `src/`; includes are rooted at
`src/` (e.g. `#include "frontend/lexer.hpp"`). The public C ABI header is the
only split-out surface and lives at `bindings/c/include/rtsl.h`.

```
RTSL/
  bindings/c/             # public C ABI: include/rtsl.h (+ CMakeLists)
  src/
    support/              # shared plumbing: types, source manager, diagnostics, binary io
    frontend/             # preprocessor, lexer, parser, AST
                          #   (+ tokens.def, directives.def, builtins.def, resource_types.def)
    sema/                 # semantic analysis + type checking, mangling, uniform lowering
    ir/                   # SSA RTIR: lowering, verification, disassembly (+ ops.def)
    artifact/             # artifact container serialization and the linker
    driver/               # compiler orchestration (compiler.cpp) and the rtslc CLI (rtslc.cpp)
    api/                  # the C ABI implementation (rtsl.cpp) and the language service
    runtime/              # .rtslp reader for Rutile backends (NOT built by this repo)
  tests/                  # rtsl-tests + workspace shaders
  cmake/                  # Rtsl.cmake integration helpers
  docs/                   # language, RTIR, artifacts, linking, backend-contract specs
  scripts/                # build.{sh,bat}, test.{sh,bat}
  workspace/              # scratch shaders for development
```

## Build graph

The build is intentionally simple: one static library plus the CLI and tests.

```
rtsl        STATIC   support + frontend + sema + ir + artifact + driver + api
rtslc       EXE      driver/rtslc.cpp                -> rtsl (+ CLI11)
rtsl-tests  EXE      tests/*.cpp                     -> rtsl (+ Catch2)
```

`src/runtime/package.{hpp,cpp}` is deliberately **excluded** from this build. It
is a standalone `.rtslp` reader that Rutile backends compile themselves against
their own `rutile.h`; keeping it out of the RTSL library inverts the dependency
correctly — RTSL knows nothing about Rutile. Its `ir_op` enum mirrors
`src/ir/ops.def` and must be kept in lockstep (opcode values are wire format).

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
- `src/runtime/` and the serialized artifact format are the only RTSL surfaces
  code outside this repo should depend on.
- Nothing in `bindings/` may include from `src/`.

## Related specs

- [docs/compiler-architecture.md](docs/compiler-architecture.md)
- [docs/language.md](docs/language.md)
- [docs/artifacts.md](docs/artifacts.md)
- [docs/rtir.md](docs/rtir.md)
- [docs/linking.md](docs/linking.md)
- [docs/backend-contract.md](docs/backend-contract.md)
