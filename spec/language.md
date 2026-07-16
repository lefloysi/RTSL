# RTSL Language Specification

This file defines the RTSL v0.1 source language.

## Translation Units

A translation unit is an ordered sequence of declarations:

- `import "path";`
- `export import "path";`
- `namespace name { ... }`
- `struct Name { ... };`
- `using Alias = Type;`
- `using qualified::symbol;`
- `using namespace qualified;`
- `export using namespace qualified;`
- `uniform [scope] { ... }`
- `layout path : [rule] type;`
- `fn name(parameters) [-> return-type] body-or-semicolon`
- `export fn ...`

## Names

Qualified names use `::`. Namespaces qualify contained structs, functions,
uniform scopes, and layout paths.

`using qualified::symbol;` imports one symbol. `using namespace qualified;`
imports a namespace or uniform scope for unqualified lookup.

## Types

Built-in value types:

- `void`, `bool`, `f32`, `i32`, `u32`
- `vec2`, `vec3`, `vec4`
- `ivec2`, `ivec3`, `ivec4`
- `uvec2`, `uvec3`, `uvec4`
- `mat2`, `mat3`, `mat4`

Resource types:

- `UniformBuffer`, `StorageBuffer`
- `Sampler`, `Sampler2D`, `Sampler3D`, `SamplerCube`, `Sampler2DArray`
- `Image2D`, `Image3D`

Structs define named aggregate value types. Struct member functions may be
declared inside the struct and defined as qualified functions.

Function parameters may be `const T&`. References are valid only for function
parameters. Stage entry parameters must be value types.

## Functions

```rtsl
fn name(T value) -> R { return R(value); }
fn forward(T value) -> R;
```

Omitting `->` means `void`. A semicolon terminates a forward declaration.
Functions may be overloaded. Exported functions appear in module interfaces.

Standard library:

- `sample(image_or_texture, coordinates) -> vec4`

## Statements And Expressions

Function bodies support blocks, conditionals, loops, local declarations,
assignments, returns, and call expression statements.

Expressions support names, integer literals, floating literals, boolean
literals, calls, member access, qualified access, indexing, unary operators,
binary operators, logical operators, comparisons, and assignment left-hand
sides.

`true` and `false` are boolean literals.

## Resources

```rtsl
uniform albedo {
    readonly Sampler2D texture;
    UniformBuffer tint;
}

layout albedo::tint : vec4;
```

Bindings default to read-write access. `readonly` and `writeonly` qualify
resource access.

Named uniform scopes are referenced by qualified path or imported with `using`.
Anonymous `uniform { ... }` blocks are valid and produce distinct descriptor
sets.

`layout path : [rule] type;` attaches a payload type to a buffer resource.
Valid rules are `std140`, `std430`, and `scalar`. If omitted, `UniformBuffer`
uses `std140` and `StorageBuffer` uses `std430`.

## Stage Entries

A stage entry is a function with `@stage : value`.

```rtsl
@stage : vertex
fn vertex_entry(Point p) -> Vertex : position(clip), uv(smooth) { ... }

@stage : fragment
fn fragment_entry(Vertex v) -> vec4 { ... }
```

Supported stages are `vertex` and `fragment`.

Linked graphics programs use backend entry names:

- `vertex` -> `vert`
- `fragment` -> `frag`

## Stage Signatures

Stage parameters and return values are ordinary RTSL types.

A vertex stage that returns a struct declares its output fields with a return
boundary:

```rtsl
fn vertex_entry(Point p) -> Vertex : position(clip), uv(smooth) { ... }
```

The boundary grammar is:

```text
: field(tag, ...) (, field(tag, ...))*
```

Each field must name a member of the return struct. A field requires at least
one tag.

v0.1 tags:

- `clip`
- `smooth`
- `flat`

A fragment parameter of struct type must match the vertex return-boundary
payload. A fragment returning bare `vec4` produces a single color result.

## Imports And Exports

`import "path";` imports a module interface. `export import "path";`
re-exports it. Exported declarations contribute to `.rtslm` module interfaces.

## Open Questions

- Literal grammar, numeric conversions, and operator precedence need a full
  table.
- Full overload ranking is not yet specified.
- Layout byte offsets and alignment rules need a normative table.
