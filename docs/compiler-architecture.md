# RTSL Compiler Architecture

RTSL is compiled by a C++ frontend with a stable C ABI. The command line tool
wraps the compiler library. The RTSL SDK is a separate embeddable layer for
project/runtime tooling that consumes linked artifacts.

## Pipeline

The compiler pipeline is:

```text
RTSL source
  -> lexer
  -> parser
  -> AST
  -> semantic analysis
  -> RTIR generation
  -> artifact writer
```

The linker pipeline is:

```text
rtslo/rtsll inputs + rtslm interfaces
  -> symbol and type resolution
  -> RTIR merge
  -> resource/stage metadata resolution
  -> rtsll or rtslp artifact writer
```

Backends or project runtime tools consume `rtslp`, inspect its ordered payloads
and RTIR, then lower to SPIR-V, HLSL, MSL, or another target format. Shared
artifact inspection belongs in `rtsl-sdk`; target-specific lowering belongs in
backend headers, not in the compiler library or CLI.

## Frontend Stages

The lexer turns source text into tokens and records source offsets. It recognizes
keywords, identifiers, literals, comments, operators, punctuation, and end of
file.

The parser consumes tokens and produces an AST. It should not perform final type
resolution, overload resolution, resource layout, or backend lowering.

Semantic analysis resolves names, validates declarations, checks types, selects
overloads, validates stage signatures, records exports, and creates the semantic
model used by RTIR generation.

RTIR generation lowers semantic AST nodes into typed backend-neutral IR while
preserving source-facing symbol names and debug mappings.

## Ownership Boundaries

The public ABI lives in `include/` and remains C-compatible. Implementation
headers and C++ types live under `rtsl/` or implementation-only subdirectories.

C++ exceptions must not escape any exported ABI function. ABI entry points catch
allocation failures and internal exceptions, convert them to `rtsl_result`, and
return failure handles or status codes.

Compiler objects own their internal memory. Output blobs returned by the ABI are
owned by the object that produced them and remain valid until that object is
destroyed.

## Diagnostics

Diagnostics must include a stable code, severity, source file identity, byte
offset, line, column, and human-readable text. The compiler supports both
recoverable diagnostics and fatal errors.

Recoverable syntax or semantic errors should continue when doing so improves
diagnostic quality. Artifact emission only succeeds when no fatal errors and no
blocking semantic errors remain.

Debug metadata is not an optional afterthought. It is emitted into binary
artifacts when enabled and maps functions, symbols, values, and instruction
ranges back to source files and spans.

## Artifacts

The compiler emits `rtslo` for every compiled source file. If the source exports
symbols, it also emits `rtslm`.

The linker may emit `rtsll` for a linked library without required entry points.
If that library exports symbols, it also emits `rtslm`.

The linker emits `rtslp` for a final linked program with entry points and final
backend-facing metadata.

## CLI Responsibilities

For v0.1, the command line tool supports:

- compiling source to `rtslo` and `.rtslm` when the source exports symbols
- linking objects and libraries into `rtsll` or `rtslp`
- disassembling binary artifacts into textual RTIR
- import search paths for `.rtslm` module interfaces

Exact option names are part of the v0.1 command-line contract.

## C ABI Responsibilities

The C ABI should expose the same logical pipeline:

- create and destroy compiler contexts
- compile source buffers into artifact blobs
- create and destroy linker contexts
- add object, library, or module blobs
- link to library or program outputs
- read artifact blobs and diagnostic results
- load an existing artifact blob for reflection (`rtslLoadModule`)
- reflect a module's uniforms, stage interface variables, and entry points

Reflection is available on any loaded artifact (`rtslo`, `rtslm`, `rtsll`,
`rtslp`). Uniform queries report the scope, name, type, set, binding, and the
mangled backend binding name. Stage-interface queries report each varying field
with its payload type, interpolation, built-in slot, and assigned location.
Entry queries report backend entry names and authored stage identifiers.

For v0.1, this ABI is focused on compilation, linking, loading, and reflection.

The ABI should not expose C++ types, STL containers, exceptions, or ownership
that depends on C++ destructors in user code.
