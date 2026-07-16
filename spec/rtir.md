# RTIR Specification

RTIR is RTSL's backend-neutral typed intermediate representation.

## Ids

RTIR ids share one module-local id space. Id `0` is reserved. Producers allocate
ids monotonically. Linkers may remap ids when merging modules.

## Module Contents

An RTIR module contains:

- source name
- imports and imported exports
- exports
- type and constant pool
- global variables
- decorations
- functions
- function debug records
- struct reflection
- resource reflection
- stage interface reflection
- pending call targets
- next id

Reflection records are artifact metadata, not SSA instructions.

## Instructions

Each instruction contains:

- opcode
- optional result id
- optional result type id
- ordered id operands
- ordered integer literals
- optional source debug location

Opcode numeric values are stable artifact values and are defined by the RTIR
opcode table.

## Opcode Families

- types
- constants
- function variables and memory
- composites
- arithmetic
- vectors and matrices
- comparisons and logical operations
- conversions
- control flow
- functions and calls
- images and samplers
- standard-library extended instructions

## Storage Classes

- `Function`
- `Uniform`
- `UniformConstant`
- `StorageBuffer`
- `PushConstant`
- `Private`

## Functions And Calls

An RTIR function records its identity, signature, parameters, body, and optional
stage tag. Function bodies start with a label and end with a terminator.

Calls may target module-local functions or pending imported call targets.
Program linking resolves all pending calls.

## Stage Interfaces

RTIR stores stage interface reflection as metadata. Stage input, output, and
varying declarations are not RTIR instructions.

## Open Questions

- Per-op operand and literal tables need normative wording.
- Verification rules for dominance, structured control flow, and type
  consistency need normative wording.
