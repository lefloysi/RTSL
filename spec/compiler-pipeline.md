# RTSL Compiler Pipeline Specification

## Compiler Pipeline

```text
.rtsl source
  -> preprocessor
  -> lexer
  -> parser
  -> semantic analysis
  -> RTIR lowering
  -> artifact writer
```

## Linker Pipeline

```text
.rtslo / .rtsll inputs
  -> artifact reader
  -> module merge and id remap
  -> cross-module call inlining
  -> link validation
  -> .rtsll or .rtslp artifact writer
```

## Ownership

- preprocessor: conditional source inclusion and supported macro definitions
- lexer: tokenization and source offsets
- parser: syntax and AST shape
- semantic analysis: names, types, overloads, resources, layouts, stages,
  imports, exports, and source diagnostics
- RTIR lowering: backend-neutral IR
- artifact code: serialized format
- linker: artifact merging, import/export checks, unresolved calls, program
  validation, and linked artifact emission

## Inputs And Outputs

Compiling emits `.rtslo`. Sources with exports also emit `.rtslm`.

Library linking emits `.rtsll`.

Program linking emits `.rtslp`.

Dumping prints textual RTIR.

## Imports

Compilation resolves `import "path";` through import search paths. Source
import cycles are diagnosed.

## Program Validation

A program link requires at least one stage entry point. A graphics program must
contain exactly one vertex entry and exactly one fragment entry.

Program links reject unresolved calls, duplicate exported function identities,
and stale imported interfaces.

## Open Questions

- The complete preprocessor directive set is not specified.
- Dependency-file output is not specified.
- Diagnostic stability is not specified.
