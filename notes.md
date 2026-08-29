# RTSL Working Notes

This file is the non-normative working scratchpad for material that is not ready for
`specification.md`. Confirmed rules are removed from this file after they are incorporated into the
specification.



---



## Lexical and Preprocessing Work

The following grammar is incomplete:

- preprocessing numbers;
- integer and floating-literal syntax and typing;
- character and string-literal syntax;
- the complete punctuator and keyword sets;
- the remaining preprocessing-directive grammar;
- macro expansion outside preprocessing conditions;
- the complete preprocessing-expression grammar.



---



## Type System Questions

- What are the Boolean and character type spellings and value rules?
- For `matNxM<T>`, which dimension denotes rows and which denotes columns?
- What are the exact meanings of `Format`, `U`, and the remaining resource template parameters?
- Which operations belong to each resource type?


### Pointer and Reference Questions

- What syntax creates a null pointer, and which operations are valid on one?
- What is the result type and exact validity rule for subtracting two pointers?
- What are the complete pointer provenance, aliasing, alignment, and lifetime rules?


### Pointer-Lowering Reminder

This is an implementation strategy, not an RTSL semantic rule.

Pointer operations remain abstract in RTSL IR. A transpiler can lower a pointer directly when its
storage provenance is known. When direct lowering is unavailable, it can represent the pointer as
a virtual `u64` address whose range maps to a storage target and offset. The virtual address is not
a physical device address and does not reserve bits in one.

An external-buffer range can resolve to the buffer's current physical storage plus an offset. An
address-taken local that must remain accessible across a call can be materialized in
transpiler-managed, per-invocation storage.

The transpiler should preserve provenance for as long as possible. A pointer with known provenance
can bypass virtual lookup. An unchanged pointer can be resolved once and reused throughout its
live range. A reference cannot be reseated, so the same resolution can be reused for its lifetime.
Runtime lookup remains only where the actual storage provenance remains runtime-dependent.


### Future Resource Allocation

Shader-side resource allocation may be added later. No allocation interface, resource lifetime,
ownership model, or interaction with external resource declarations has been selected.



---



## Declaration and Definition Questions

- What are the declaration, lookup, composition, and nesting rules for contracts?
- How are backend resource locations described when explicit binding control is needed?
- What is the explicit destructor-definition syntax?
- What is the complete syntax for namespace definitions and out-of-structure member definitions?
- When are parameters, return objects, temporaries, and partially initialized objects destroyed?



---



## Expression Questions

- What are the complete construction and initializer syntaxes?
- What syntax consumes a destructured sequence outside parameter conversion?



---



## Control-Flow Questions

- Which explicit return forms are permitted in a function that contains `emit`?
- Which explicit return forms are permitted in a function declared with the `emit` modifier?
- What is the final named-barrier syntax?
- In which shader stages may a named barrier appear?

The following named-barrier syntax is provisional:

```rtsl
@stage : compute
fn main(usize x, usize y, usize z) {
    some_barrier:
}
```



---



## Provisional Shader-Stage Example

This example intentionally contains undefined stage types and incomplete bodies. It remains here
until those constructs have complete normative rules.

```rtsl
struct Material {
    vec4 tint;
}

struct Point {
    vec3 position;
    vec4 color;
    u32 material;
}

struct Data {
    fn Data(Point point);

    vec4 color;
    u32 material;
}

using Vertex = (Position position, Data data);

fn Data::Data(Point point) {
    color = point.color;
    material = point.material;
}

@stage : vertex
fn main(Point point) -> Vertex {
    return { { .value = frame.mvp * point.position }, Data(point) };
}

using Patch = patch<Vertex, 16, quad_tess>;

@stage : tess_control
fn main(Patch& patch, usize index) -> Vertex {
    ...
}

@stage : tess_eval
fn main(Patch& patch, Patch::coord coord) -> Vertex {
    ...
}

@stage : geometry
fn main(triangle<Vertex> triangle) -> TriangleStrip<Vertex, 3> {
    emit triangle[0];
    emit triangle[1];
    emit triangle[2];
}

@stage : fragment
fn main(Vertex vertex) -> vec4 {
    return vertex.data.color;
}
```


### Stage Questions

- What are the exact matching rules for compound stage interfaces?
- What are the definitions and semantics of `patch<T, N, Mode>`, `triangle<T>`, topology modes,
  and patch coordinate types?
- What are the lengths and storage semantics of `Position::clip_distance` and
  `Position::cull_distance`?
- What is the parameter ordering and complete meaning of compute-stage workgroup values?
- What are the complete semantics and applicability of `flat`, `smooth`, and future contracts?


### Rutile Integration Reminder

The Rutile runtime object called a program is not an RTSL executable or program artifact.
`rtProgramSource` supplies an entry-point identifier, requested stages, and one `.rtl` library to
the selected transpiler. The host API remains outside the RTSL language specification.



---



## Extension Question

- What is the name and exact capability boundary of the extension that supplies addresses of
  externally bound buffer storage?



---



## Debugging Questions

- What are the exact names and signatures of the print and related diagnostic operations?
- A future debug companion format remains to be designed, including its file-name extension and
  its source, scope, macro-expansion, template-origin, and inlining records.
