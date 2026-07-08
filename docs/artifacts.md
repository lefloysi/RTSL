# RTSL Artifact Formats

RTSL artifacts are binary files using one sectioned container format. Textual
RTIR exists for disassembly and inspection, but the canonical toolchain
interchange format is binary.

This document is the byte-level specification of what
`src/artifact/artifact.cpp` actually reads and writes today (container version
0.4). If the code and this document disagree, one of them is a bug.

## Artifact Kinds

| kind | extension | u16 value | contents |
|------|-----------|-----------|----------|
| object  | `.rtslo` | 1 | one compiled source file: bodies, imports, exports, unresolved calls |
| module  | `.rtslm` | 2 | public interface of an object/library: exported signatures, no private bodies |
| library | `.rtsll` | 3 | merged objects; internal references resolved, no entry points required |
| program | `.rtslp` | 4 | final linked module consumed by Rutile backends |

A `.rtsld` sidecar (debug artifact) uses the same container; it currently
carries no sections.

## Encoding primitives

All multi-byte values are **little-endian**. The building blocks (implemented
in `src/support/binary.hpp`) are:

```text
uN       fixed-width little-endian integer (enums encode as their underlying type)
bool     one byte; readers accept only 0 or 1
string   u32 length + that many bytes (no null terminator)
vec<T>   u32 count + `count` contiguous T records
```

## Container layout

```text
+------------------+ offset 0
| header           | 48 bytes (zero-padded)
+------------------+ offset 48
| section table    | 32 bytes per section
+------------------+ offset 48 + 32 * section_count
| section payloads | concatenated, in section-table order
+------------------+ = file_size
```

### Header (48 bytes)

```text
offset size field                 value
0      u32  magic                 0x4c535452 (bytes "RTSL")
4      u16  version_major         0
6      u16  version_minor         4
8      u16  artifact_kind         see kind table above
10     u8   endianness            1 (little-endian)
11     u8   reserved              0
12     u32  reserved              0
16     u32  header_size           48
20     u32  section_count
24     u64  section_table_offset  48
32     u64  file_size             total bytes, validated on read
40     u64  (zero padding to header_size)
```

Readers reject a wrong magic, a different `version_major`, `endianness != 1`,
`header_size != 48`, or a `file_size` that does not match the input length.

### Section table entry (32 bytes)

```text
offset size field
0      u32  section_kind
4      u32  reserved (0)
8      u64  payload offset (from start of file)
16     u64  payload size in bytes
24     u32  flags (currently always 1)
28     u32  reserved (0)
```

### Section kinds

```text
1  string_table            5  decoration_table        9  entry_table
2  ir_module               6  struct_table           10  debug_table (empty today)
3  import_table            7  resource_table         11  imported_export_table
4  export_table            8  stage_interface_table
```

Which sections a writer emits depends on the artifact kind: **programs omit**
`import_table`, `export_table`, `imported_export_table`, and `struct_table` —
a linked program never re-links, so only the reflection surface the runtime
reads survives. All other kinds write every section.

## Section payloads

### string_table (1)

`vec<string>`. Entry 0 is the module's source name. `StringId` fields elsewhere
(function display/mangled names, debug parameter names) index into this table.

### ir_module (2)

```text
next_id             u32                   id allocator high-water mark
type_constant_pool  vec<instruction>
global_variables    vec<instruction>
function_count      u32
functions           function * function_count
call_target_names   vec<string>           pending FunctionCall targets (empty in programs)
debug_count         u32
function_debug      debug_info * debug_count
```

One `instruction` record (also the wire form inside function bodies):

```text
op          u16        IROp enum value — see src/ir/ops.def; ORDER IS WIRE FORMAT
result_id   u32        0 = no result
type_id     u32        0 = untyped
operands    vec<u32>   ids
literals    vec<u32>   inline data (constant bits, indices, storage class, ...)
loc_file    u32        debug location (0 when absent)
loc_line    u32
loc_column  u32
```

One `function` record:

```text
result_id         u32
function_type_id  u32
return_type_id    u32
stage             u8          StageKind (0 none, 1 vertex, 2 fragment)
generated         u8          bool
exported          u8          bool
display_name      u32         StringId
mangled_name      u32         StringId
source_name       string      inliner/linker resolution key ("" in programs)
parameter_ids     vec<u32>
body              vec<instruction>
```

Constructor functions (`Foo::Foo`) are dead after inlining and are **not**
serialized. One `debug_info` record: `display_name u32`, `param_count u32`,
then `param_count` `u32` StringIds.

### import_table (3)

`vec<string>` of imported module names.

### export_table (4) and imported_export_table (11)

`vec<export_symbol>` where `export_symbol` = `name string, kind string,
type string`.

### decoration_table (5)

`vec<decoration>`:

```text
target        u32   decorated id
kind          u16   IRDecorationKind
member_index  u32   0xffffffff = decorate the target itself, else struct member
literals      vec<u32>
```

### struct_table (6)

`vec<struct_decl>` where `struct_decl` = `name string` + `vec<field>` and
`field` = `type string, name string`.

### resource_table (7)

`count u32`, then per uniform:

```text
scope_name          string   "" for anonymous / unscoped
name                string
type_id             u32      IR type pool id of the value type
access              u8       AccessKind (0 rw, 1 ro, 2 wo)
set                 u32      descriptor set
member              u32      binding within the set
is_anonymous        u8       bool
anonymous_block_id  u32
field_count         u32      inline-struct payload field names
field_name          string * field_count
```

### stage_interface_table (8)

Only host-visible roles (input, output) are serialized; varyings are
pipeline-internal and matched by location at link time. `vec<interface>`:

```text
role    u8              StageRole (0 input, 2 output)
fields  vec<io_field>

io_field:
  name           string
  interpolation  u8      InterpolationKind (0 none, 1 smooth, 2 flat, 3 clip)
  builtin        u8      BuiltinSlot (0 none; then frontend/builtins.def order + 1)
  location       u32     0xffffffff = no location (builtin drives placement)
```

### entry_table (9)

`vec<entry>` where `entry` = `name string, mangled_name string, stage u8,
function_id u32`.

### debug_table (10)

Reserved. Written empty today.

## Versioning

Any change to the byte layout above — including reordering `ir/ops.def` or
`frontend/builtins.def`, whose enum values are wire encodings — must bump
`version_minor` (compatible additions) or `version_major` (breaking changes).

## Naming and debug preservation

Artifacts store both canonical identities (mangled names) and display names.
Canonical identities drive linking; display names drive diagnostics,
reflection, disassembly, and debugging. The linker preserves user-authored
names; only compiler-generated temporaries use deterministic synthetic names.
