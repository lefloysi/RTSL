# RTSL Language Semantics

This document defines the source language accepted by the RTSL compiler. RTSL is
a C-style shader language for Rutile. A source file is always an implementation
file; importable interfaces are produced by the compiler as `rtslm` artifacts.

## Lexical Model

RTSL source is UTF-8 text. The initial compiler implementation may restrict
identifiers to ASCII, but the format must preserve original source spelling for
diagnostics and debug output.

Whitespace separates tokens and is otherwise insignificant outside literals.
Line comments start with `//` and end at the next line break. Block comments
start with `/*` and end with `*/`.

Identifiers start with a letter or `_` and continue with letters, digits, or
`_`. Identifiers are case-sensitive. Keywords cannot be used as ordinary
identifiers.

Numeric literals include integer and floating-point forms. String literals use
double quotes. Character literals may be added later if needed by the standard
library or compile-time evaluation.

The core punctuators and operators include:

- grouping: `(`, `)`, `{`, `}`, `[`, `]`
- separators: `,`, `;`, `.`, `::`
- arithmetic: `+`, `-`, `*`, `/`, `%`
- assignment and comparison: `=`, `==`, `!=`, `<`, `<=`, `>`, `>=`
- logical and bitwise: `!`, `&&`, `||`, `&`, `|`, `^`, `~`
- type and function syntax: `->`, `<`, `>`

## Keywords

Modules and visibility: `import`, `export`

Declarations and scopes: `namespace`, `struct`, `using`, `uniform`, `varying`,
`entry`

Functions and types: `fn`, `const`, `auto`, `void`

Control flow: `if`, `else`, `while`, `do`, `for`, `return`

Varying qualifiers: `clip`, `smooth`, `flat`

Resource access qualifiers: `readonly`, `writeonly`

Boolean literals: `true`, `false`

Parameter and payload qualifiers may include `inout` for stages or APIs that
require writable payload parameters.

## Translation Units

Every `.rtsl` file is a translation unit and may define private symbols,
exported symbols, entry points, resource scopes, and stage payload contracts.
There are no user-authored header files.

Forward declarations are allowed for local ordering convenience. They do not
form an import interface. Import interfaces come from `rtslm` files.

## Imports And Exports

Imports are file-oriented:

```rtsl
import <math.rtsl>;
import <lighting/brdf.rtsl>;
```

The source-like import path resolves to a compiled module interface:

```text
math.rtsl -> math.rtslm
lighting/brdf.rtsl -> lighting/brdf.rtslm
```

`export` marks a symbol as visible through the emitted `rtslm` interface. A
source file with no exports still emits `rtslo`, but does not need to emit
`rtslm`.

## Namespaces And Name Lookup

`namespace` introduces a qualified scope. `::` selects a member of a namespace,
uniform scope, type scope, or other qualified symbol container.

Name lookup first searches the innermost lexical scope, then outer lexical
scopes, then imported/exported scopes made visible by `using`.

Function identity is not the short name alone. The canonical identity is:

```text
qualified_name + parameter_types + return_type
```

The original display name is preserved for diagnostics, disassembly, and debug
symbols.

The namespace `rt::__primitive` is reserved. User code cannot define, shadow,
alias, import over, or export symbols in that namespace.

## Type Declarations

`struct` defines aggregate data and may declare constructors or methods:

```rtsl
struct Vertex {
    Vertex(Point p);
    vec4 position;
    vec2 uv;
};
```

Methods are defined with qualified names:

```rtsl
fn Vertex::Vertex(Point p) {
    position = vec4(p.position, 1.0);
}
```

Builtin scalar, vector, matrix, resource, and stage helper types are provided by
the compiler and standard library. Examples include `f32`, `u32`, `vec2`,
`vec3`, `vec4`, `mat4`, `Buffer<T>`, `Sampler2D`, `Triangle<T>`, `Patch<T>`,
`Geometry<T, N>`, and `Mesh<V, P, T>`.

## Functions And Statements

Functions use `fn`:

```rtsl
fn saturate(f32 x) -> f32 {
    return clamp(x, 0.0, 1.0);
}
```

If no return type is written, the function returns `void`.

Statements include declarations, expression statements, blocks, `if`, `else`,
`while`, `do`, `for`, and `return`.

Expressions include literals, names, qualified names, calls, constructors, field
access, indexing, unary operators, binary operators, assignment, and compound
assignment.

## Uniforms And Resources

`uniform` declares a resource binding scope:

```rtsl
uniform albedo {
    Sampler2D texture;
    vec4 tint;
}
```

Named uniform members are accessed through the uniform scope:

```rtsl
sample(albedo::texture, uv) * albedo::tint
```

Anonymous uniform scopes expose their members directly in the surrounding
namespace. Reopened named uniform scopes are merged. Nested `uniform` scopes are
invalid.

Resource declarations may use access qualifiers:

```rtsl
uniform particles {
    readonly Buffer<Particle> src;
    writeonly Buffer<Particle> dst;
}
```

The compiler assigns backend binding numbers and records the reflected resource
layout in artifacts.

## Varyings And Stage Payloads

`varying` describes interpolation and stage-interface metadata for a payload
type:

```rtsl
varying Vertex {
    clip position;
    smooth uv;
    flat material;
};
```

`clip` marks clip-space position. `smooth` and `flat` control interpolation.
The final `rtslp` stores resolved stage-interface metadata for backend lowering.

## Entry Points

`entry fn` marks an executable shader entry point:

```rtsl
entry fn vert_main(Point p) -> Vertex {
    return Vertex(p);
}
```

Entry point stage classification is derived from the signature and stage types,
then recorded in the linked `rtslp`. The compiler must preserve the authored
entry name for backend reflection and debugging.

## Compute And Advanced Stages

The compiler provides a `Compute` input shape for compute-like stages:

```rtsl
struct Compute {
    vec3 group;
    vec3 local;
    vec3 global;
    u32 local_index;
};
```

The language model also allows future stage families such as tessellation, mesh,
and ray tracing through typed entry signatures and stage helper types. These
features must lower through the same RTIR and artifact model.

## Standard Library And Primitives

The standard library should be written in RTSL where possible. Functions such as
`sample`, `normalize`, `saturate`, and `mix` are ordinary callable functions
unless they need primitive backend behavior.

Only operations that cannot be expressed in RTSL should use reserved primitives:
texture sampling, barriers, derivatives, discard, subgroup operations, and ray
tracing operations.
