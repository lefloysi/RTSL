# RTSL SDK Model Specification

`rtsl-sdk` defines shared artifact-facing types and constants. It is not the
compiler API.

## Basic Types

- `u08`, `u16`, `u32`, `u64`
- `i08`, `i16`, `i32`, `i64`

These aliases are fixed-width integer types.

## Artifact Constants

Artifact kinds:

- object = `1`
- module = `2`
- library = `3`
- program = `4`

Artifact version:

- magic = `0x4c535452`
- major = `0`
- minor = `1`

## Open Questions

- The complete SDK surface for third-party backends is not yet specified.
- Ownership of future reflection structs between `rtsl-sdk` and compiler
  internals needs a clear migration rule.
