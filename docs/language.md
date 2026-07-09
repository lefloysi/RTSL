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
- `using uniform scope;`
- `uniform name? { ... }`
- `layout path : rule? Type;`
- `fn name(...) ...`
- `@vertex fn name(...) ...`
- `@fragment fn name(...) ...`

`layout` is a reserved keyword. `readonly`, `writeonly`, `smooth`,
`flat`, `clip`, `std140`, `std430`, and `scalar` are contextual words.

## Types

A type is a builtin type, qualified name, alias, or inline `struct` body.

```rtsl
struct Vertex {
    vec4 position;
    vec2 uv;
};

layout camera : struct {
    mat4 view_proj;
};
```

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

`clip` marks the clip-space position. `smooth` and `flat` select interpolation.
The compiler assigns concrete locations.

A fragment entry returning a bare `vec4` is the default single color target.
It reflects as one output field named `color` at location `0`.

## Exports

`export` makes declarations visible through the emitted `.rtslm` interface.
`export import <path>;` republishes the imported module's public interface.
Stage entries do not need to be exported unless another module is meant to link
to that function as a normal symbol.

## Builtins

Backend globals are not source syntax. Stage builtins are compiler-owned
metadata produced during lowering and backend emission.
