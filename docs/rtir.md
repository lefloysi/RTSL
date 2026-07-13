# RTSL Intermediate Representation

RTIR is the compiler's backend-neutral, typed SSA representation. It is produced
by lowering and consumed by artifact writing, linking, and backend emission.
RTIR is not RTSL source syntax.

## What RTIR Contains

- ids for values, types, constants, and functions
- flat instruction streams for functions and type/constant pools
- decorations for reflected metadata
- reflection bridges for uniforms, stage boundaries, structs, and entries

RTIR is shaped to be easy to lower to and easy to validate. It is not the
source language, and it is not a backend-specific shader dialect.

## Type And Value Model

Types, constants, and SSA values all live in one id space. A module's ids are
unique within that module. Id `0` means "no id".

RTIR values are built from a small set of operations:

- scalar types
- vector types
- matrix types
- struct types
- function types
- backend storage/resource handles

The source language does not expose pointer types. If lowering needs a storage
handle for variables or resources, that is an implementation detail of the IR
and backend contract, not an RTSL type-system feature.

## Instructions

An `IRInstruction` holds:

- an opcode
- an optional result id
- an optional type id
- operand ids
- literal data
- source debug location

The instruction stream is flat. Basic blocks are represented by labels and
terminators. Control flow is explicit in the stream.

## Decorations

Decorations attach metadata to existing ids without changing the SSA stream.
They are used for:

- resource binding metadata
- stage and payload reflection
- built-in slot mapping
- layout offsets and strides
- interpolation metadata

## Reflection Bridges

RTIR carries source-facing reflection data for:

- uniform scopes and resource bindings
- stage interface boundaries
- struct declarations
- entry points

This is what the C API and the artifact writer read back. The compiler does not
force clients to reconstruct those concepts from raw instructions.

## Lowering Model

Lowering turns source declarations into RTIR functions and metadata. The
lowerer resolves:

- names
- types
- constructors
- varying boundaries
- resource metadata

The source function remains the authored entry point. The compiler may add
stage wrappers or inline constructor bodies during lowering.
