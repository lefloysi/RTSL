# RTSL Artifact Formats

RTSL artifacts are binary files produced by the compiler and linker. They are
the interchange format between compilation, linking, loading, and reflection.

This document describes the container and the ordered payloads it carries. The
file format is fixed by the shared SDK artifact model and
`rtsl/artifact/artifact.cpp`.

## Artifact Kinds

| kind    | extension | value | contents |
|---------|-----------|-------|----------|
| object  | `.rtslo`  | 1     | a compiled source file |
| module  | `.rtslm`  | 2     | exported interface only |
| library | `.rtsll`  | 3     | linked objects with internal references resolved |
| program | `.rtslp`  | 4     | final linked shader program |

## Encoding Primitives

The artifact writer uses the RTSL binary helpers:

- fixed-width values are little-endian
- strings are length-prefixed
- vectors are length-prefixed contiguous sequences
- enums serialize as their underlying type

## Container Layout

An artifact is laid out as:

1. a fixed-size header
2. a linear run of 32-byte payload records
3. payload bytes, concatenated in record order

The header stores the magic, version, artifact kind, payload count, payload
record offset, and file size.

## Payload Records

Each payload record stores:

- the payload kind
- the payload offset
- the payload size
- flags

The writer emits records in a fixed order. Payloads follow the same order.

## Payloads

The artifact payload order is:

- strings
- lowered RTIR
- imports
- exports
- imported exports
- struct declarations
- decorations
- resources
- stage interfaces
- entry points
- debug data

Programs omit the import and export payloads that are no longer needed after
linking. The other artifact kinds keep the full metadata surface.

## IR Module Payload

The `ir_module` payload stores:

- the next id value
- the type/constant pool
- global variables
- functions
- pending call-target names
- debug function records

Function records carry the authored name, the mangled name, the stage, the
result id, the parameter ids, and the body instruction stream.

## Reflection Payloads

Uniform and stage-interface payloads are written in resolved form. The compiler
assigns concrete metadata and writes it into the artifact so reflection can read
it back directly.

That means the artifact carries the values the runtime needs, not the raw source
syntax that produced them.

Each stage-interface field records its name, interpolation, assigned location,
and the index of the struct member it maps to. Because the
RTIR type pool carries no member names, that member index is what lets a backend
extract or insert the field when synthesizing the target's stage I/O. A member
index of `0xFFFFFFFF` means the field is not a struct member — the entry's whole
parameter or return value is the payload (for example a bare `vec4` fragment
color).

## Versioning

Changes to the byte layout require a version bump. Additive changes bump the
minor version. Breaking changes bump the major version.
