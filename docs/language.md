# RTSL Language

RTSL is a graphics shader language for Rutile. v0.1 supports vertex and
fragment programs.

## Declarations

Top-level declarations:

- `import <path>;`
- `export import <path>;`
- `export fn name(...) ...`
- `namespace name { ... }`
- `struct Name { ... };`
- `using Alias = Type;`
- `using path::name;`
- `using namespace scope;`
- `uniform name? { ... }`
- `layout path : rule? Type;`
- `fn name(...) ...`
- `@vertex fn name(...) ...`
- `@fragment fn name(...) ...`

Function attributes use `@name` syntax. v0.1 recognizes `@vertex` and
`@fragment` on function declarations. `layout` is a reserved keyword.
`readonly`, `writeonly`, `smooth`, `flat`, `clip`, `std140`, `std430`, and
`scalar` are contextual words.

## Types

A type is a builtin type, resource binding type, qualified name, alias, or
inline `struct` body.

```rtsl
struct Vertex {
    vec4 position;
    vec2 uv;
};

layout camera : struct {
    mat4 view_proj;
};
```

Builtin value types:

- `void`, `bool`
- `f32`, `i32`, `u32`
- `vec2`, `vec3`, `vec4`
- `ivec2`, `ivec3`, `ivec4`
- `uvec2`, `uvec3`, `uvec4`
- `mat2`, `mat3`, `mat4`

Resource binding types:

- `UniformBuffer`, `StorageBuffer`
- `Sampler`
- `Sampler2D`, `Sampler3D`, `SamplerCube`, `Sampler2DArray`
- `Image2D`, `Image3D`

Function parameters may be passed by value or by reference:

```rtsl
fn shade(const Surface& surface) -> vec4 {
    return surface.albedo;
}
```

References are parameter qualifiers in v0.1. Struct fields, local variables,
uniform bindings, layout payloads, aliases, and return types are value types.
Pointers are not RTSL source syntax.

## Structs

Structs are aggregate data. Constructors may be declared and defined inline or
out of line.

```rtsl
struct Vertex {
    vec4 position;
    fn Vertex(vec3 p) {
        position = vec4(p, 1.0);
    }
};

fn Vertex::Vertex(Point p) {
    position = vec4(p.position, 1.0);
}
```

## Resources

Resources are declared with `uniform`. Concrete set, binding, and location
values are compiler-assigned.

```rtsl
uniform camera {
    UniformBuffer data;
}

layout camera::data : std140 mat4;
```

## Stage Entries

Stage entries are ordinary functions with a stage attribute. The function name
is not special.

```rtsl
@vertex
fn vertex_entry(Point p) -> Vertex : position(clip), uv(smooth) {
    return Vertex(vec4(p.position, 1.0), p.uv);
}

@fragment
fn fragment_entry(Vertex v) -> vec4 {
    return vec4(v.uv, 0.0, 1.0);
}
```

Return boundaries describe payload fields crossing a stage boundary:

```rtsl
-> Vertex : position(clip), uv(smooth), material(flat)
```

On vertex return varyings, `clip` marks the clip-space position. `smooth` and
`flat` select interpolation. These are contextual tag names, not reserved
keywords. The compiler assigns concrete locations.

Stage globals such as vertex index, instance index, fragment coordinate, front
facing, and fragment depth are not return-boundary tags. When a stage needs
pipeline-provided values, they are passed explicitly through the entry
function's parameters.

## Fragment Outputs

A fragment entry's return type describes its color attachments — slots in the
bound framebuffer. Attachment *formats* are owned by the pipeline, not the
shader; the shader only names which value goes to which slot.

A bare `vec4` return is the default single color target: it reflects as one
output field named `color` at attachment location `0`.

A struct return is multiple render targets. Each member is a color attachment
and the compiler assigns attachment locations `0, 1, 2, …` in field order. No
per-member tags are required, because color outputs carry no interpolation.

```rtsl
struct GBuffer {
    vec4 albedo;    // attachment 0
    vec4 normal;    // attachment 1
};

@fragment
fn gbuffer_entry(Surface s) -> GBuffer {
    return GBuffer(s.albedo, s.normal);
}
```

## Exports

`export` makes declarations visible through the emitted `.rtslm` interface.
`export import <path>;` republishes the imported module's public interface.
Stage entries do not need to be exported unless another module is meant to link
to that function as a normal symbol.

## Builtins

Backend globals are not source syntax. Stage-global payloads are explicit entry
parameters; backend-specific names are produced during lowering and backend
emission.

## Standard Library

The v0.1 standard-library surface is the set of builtin value constructors,
ordinary user functions, and the graphics sampling primitive:

```rtsl
sample(texture, uv)
```

`sample` returns `vec4`. A fuller standard library may add declarations for
math, construction helpers, and additional texture operations without changing
the core syntax. Standard-library declaration and type-checking support is part
of the language surface; complete lowering for every future library function is
not required before the library surface can be described.
