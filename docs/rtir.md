# Rutile Shader Intermediate Representation

RTIR is the backend-neutral intermediate representation stored in RTSL
artifacts. The binary form is canonical. The textual form exists for
disassembly, assembly, tests, and manual inspection.

## Design

RTIR is typed and SSA-like. Functions contain basic blocks. Basic blocks contain
instructions and end with terminators. Values have explicit types. Instructions
use ids for values, symbols, functions, types, constants, blocks, and debug
spans.

RTIR must preserve user-facing symbol names through artifact tables. The binary
instruction stream may use numeric ids, but disassembly resolves those ids back
to stored names when available.

## Functions

A function has:

- symbol id
- function id when locally defined or linked
- parameter list
- return type
- basic block range
- source/debug origin
- linkage and export state through the symbol table

Before linking, calls to external functions reference symbol ids. After final
program linking, direct calls reference function ids. Calls to reserved
primitives remain identifiable as primitive symbols.

## Basic Blocks

A basic block has:

- block id
- optional display label
- instruction range
- predecessor and successor metadata when available
- debug/source origin when useful

Every block ends in one terminator such as `return`, `branch`, or conditional
branch.

## Values And Types

Instruction results define value ids. Value ids are scoped to a function.
Parameter values are assigned ids before the first instruction.

Types are referenced through the artifact type table. Types include scalar,
vector, matrix, struct, resource, array, generic, function, and stage helper
types.

## Instruction Records

The binary instruction stream uses records with:

```text
opcode          u16
flags           u16
result_value    u32  optional, invalid when no result
result_type     u32  optional, invalid when no result
operand_count   u16
debug_span      u32  optional
operands        operand[operand_count]
```

Operands are tagged values:

```text
kind   u8
data   u32/u64 depending on kind
```

Operand kinds include value id, type id, symbol id, function id, block id,
constant id, string id, immediate integer, and immediate float.

## Core Opcode Families

Structural: `parameter`, `debug_value`, `nop`

Constants and construction: `const`, `construct`, `splat`, `undef`

Memory and aggregates: `load`, `store`, `extract`, `insert`, `field`, `index`

Arithmetic and logic: `add`, `sub`, `mul`, `div`, `mod`, `neg`, `not`, `and`,
`or`, `xor`, `cmp`

Calls and resources: `call`, `primitive_call`, `load_resource`, `store_resource`

Control flow: `branch`, `branch_cond`, `return`, `discard`

The opcode set can grow, but unknown required opcodes make an artifact
unreadable by older tools.

## Primitives

Primitives are reserved functions under `rt::__primitive`. They represent
operations that require backend-specific lowering, such as texture sampling,
barriers, derivatives, discard, subgroup operations, and ray tracing operations.

The standard library should expose normal RTSL functions that call primitives
only when necessary. Backends lower primitive calls by canonical primitive
identity, not by display name alone.

## Textual RTIR

Textual RTIR is a readable assembly of the binary artifact. It must round-trip
semantically with the binary format.

Example:

```rtir
artifact rtslo 1.0

strings {
  #0 "graphics.rtsl"
  #1 "graphics::vert_main"
  #2 "mvp"
}

types {
  %t0 = f32
  %t1 = vec4
  %t2 = struct Vertex
}

symbols {
  @0 = export fn "graphics::vert_main"(Point) -> Vertex
  @1 = resource "mvp" : mat4
}

functions {
  fn @0 {
    block ^0:
      %0 : %t1 = load_resource @1
      return %0
  }
}
```

Disassembly should prefer stored display names, emit ids where ambiguity exists,
and include debug comments when requested. Assembly may accept labels and names,
but the writer canonicalizes references into table ids.

## Debug Mapping

Each instruction may reference a debug span. A span maps to source file id, byte
offset, line, column, and length. Function, symbol, and type records also carry
source origins.

Disassembly modes should support:

- compact IR without source comments
- annotated IR with source locations
- verbose IR with table ids, hashes, and section offsets
