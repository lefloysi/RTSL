# Language

RTSL files define shader stages, shared structs and functions, uniforms,
layouts, imports, and exported module interfaces.

## Imports

```rtsl
import "shared/math";
```

Imports use quoted paths. Exported imports are re-exported through the compiled
module interface.

## Exports

```rtsl
export fn saturate_color(vec4 color) -> vec4 {
    return clamp(color, vec4(0.0), vec4(1.0));
}
```

Exported declarations are written to the `.rtslm` module interface next to the
compiled object.

## Stages

Stages are functions marked with `@stage`. The supported graphics stages are
`vertex` and `fragment`; those names are identifiers, not keywords.

```rtsl
uniform {
    UniformBuffer mvp;
}

uniform albedo {
    Sampler2D texture;
    UniformBuffer tint;
}

layout mvp : mat4;
layout albedo::tint : vec4;

struct Point {
    vec3 position;
    vec2 uv;
};

struct Vertex {
    vec4 position;
    vec2 uv;
};

@stage : vertex
fn vertex_entry(Point p) -> Vertex : position(clip), uv(smooth) {
    return Vertex(mvp * vec4(p.position, 1.0), p.uv);
}

@stage : fragment
fn fragment_entry(Vertex v) -> vec4 {
    return sample(albedo::texture, v.uv) * albedo::tint;
}
```

The fields after `:` map returned struct fields to stage outputs.
`position(clip)` routes to clip position; `uv(smooth)` becomes an interpolated
varying.

RTSL source does not declare `input`, `output`, or `varying` blocks. After
linking, `rtsl::Program::entries()` exposes each stage's validated input
and output interface directly to transpilers.

## Storage-buffer arrays

A storage-buffer layout can expose a runtime-sized array. The array is the
entire buffer payload and supports dynamic `i32` or `u32` indexing.

```rtsl
uniform terrain {
    readonly StorageBuffer cells;
}

layout terrain::cells : uvec4[];

fn read_cell(u32 index) -> uvec4 {
    return terrain::cells[index];
}
```

Runtime-array layouts are valid only for `StorageBuffer` bindings. Their
physical layout is `std430`; element stride is reflected in linked RTIR.

## Standard library

RTSL provides these scalar and float-vector operations:

- `abs`, `floor`, `fract`, and `sqrt`
- `min`, `max`, and `mod`
- `mix` and `smoothstep`
- `float_bits_to_uint` for width-preserving float-to-unsigned bit casts
- `texture_size` for base-mip texture dimensions
- `sample` for implicit-LOD texture sampling

Scalar edge/factor arguments are accepted where the corresponding operation
has a vector result. Numeric scalar and equal-width vector constructors perform
explicit conversions, such as `f32(value)` and `vec2(texture_size(texture))`.

Signed and unsigned integer values support arithmetic and the `&`, `|`, `^`,
and `~` bitwise operators.
