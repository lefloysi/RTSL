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
