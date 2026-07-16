# RTSL Backend Contract Specification

Rutile backends consume linked `.rtslp` program artifacts.

## Responsibilities

Backends own target shader emission, entry point declarations, descriptor
layout emission, stage declarations, interpolation and built-in mappings,
capability checks, and target-specific rejection.

## Required Artifact Data

Backends consume:

- RTIR type and constant pool
- global variables
- function bodies
- decorations
- resource reflection
- stage interface reflection
- entry reflection

Stage interface reflection is the authoritative source for payload fields,
interpolation, user locations, clip-position placement, and struct member
indices.

## Entry Points

Linked graphics programs expose:

- `vert` for the vertex stage
- `frag` for the fragment stage

The authored stage identifier is preserved in entry reflection.

## Resources

Resource reflection carries source binding type, access, descriptor group, and
member. Buffer resource layout is carried by compiler-lowered metadata and
decorations.

## Rejection

A backend may reject a valid RTSL program when the target cannot support a
stage, resource kind, layout rule, operation, or capability used by the program.

## Open Questions

- Target capability reporting is not specified.
- The minimum RTIR opcode subset required by every Rutile backend is not yet
  specified.
