# RTSL Artifact Format Specification

RTSL artifacts are binary files for compiler and linker output.

## Artifact Kinds

- object: `.rtslo`
- module interface: `.rtslm`
- library: `.rtsll`
- program: `.rtslp`

Debug artifacts use `.rtsld`.

## Stream

Every artifact serializes these fields in order:

- magic: `0x4c535452`
- version major: `0`
- version minor: `1`
- artifact kind
- endian marker: `1`
- strings
- IR module
- imports
- exports
- decorations
- structs
- resources
- stage interfaces
- entries
- debug
- imported exports

Readers reject invalid magic, unsupported major version, invalid kind,
unsupported endian marker, malformed field data, or trailing bytes.

String data stores the artifact string pool.

IR module data stores ids, types, constants, globals, functions, pending call
targets, and function debug records. Linked programs do not carry pending call
targets.

Non-program artifacts carry imports, exports, imported exports, and struct
declarations. Program artifacts omit relink-only metadata.

Reflection data carries decorations, resources, stage interfaces, and entries.

## Module Interfaces

A `.rtslm` artifact contains exported signatures and type declarations needed
by importers. Function bodies are not part of the module interface.

## Open Questions

- Primitive encoding for strings, vectors, enums, and integers needs normative
  wording.
- Minor-version compatibility is not specified.
