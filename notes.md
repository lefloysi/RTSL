# RTSL Design Notes

This document records non-normative design decisions, examples, and unresolved questions for
RTSL. The normative language rules belong in `specification.md`.

## 1. Source Model

The source representation is specified in `specification.md`. The current design decisions are:

- source text is encoded using UTF-8;
- the language is case-sensitive;
- the permitted character set is intentionally restricted;
- whitespace and comments both separate preprocessing tokens;
- a reverse solidus followed immediately by a new-line forms a line continuation;
- line continuations are removed before comments are recognized, as in C;
- the language has character literals but no string types or string literals.

## 2. Types

### 2.1 Scalars

`usize` is the canonical size type and serves a role comparable to `size_t`.

The signed integer types are:

```text
i08 i16 i32 i64
```

The unsigned integer types are:

```text
u08 u16 u32 u64
```

The floating-point types are:

```text
f32 f64
```

The exact lowering of these types may depend on the target hardware.

### 2.2 Pointers and References

RTSL supports pointer and reference types. Their complete type and memory semantics remain to be
specified.

### 2.3 Resources

Resource values are constructed by the compilation environment rather than by RTSL source code.
Resource variables are therefore declared with `extern`.

Resource values cannot be copied or moved. References and pointers to resource values are
permitted.

The planned resource types are:

```rtsl
rt_sampler

rt_image1d<Format>
rt_image2d<Format>
rt_image3d<Format>

rt_texture1d<T>
rt_texture2d<T>
rt_texture3d<T>

rt_uniform<T, U>
rt_storage<T, U>
```

A resource may contain structured data:

```rtsl
rt_storage<(Foo foo, Bar bar), T> array;
```

### 2.4 Structures

A structure may be declared without being defined:

```rtsl
struct Foo;
```

A structure definition provides its members:

```rtsl
struct Foo {
    i32 member;
}
```

Both forms may be exported:

```rtsl
export struct Foo;

export struct Bar {
    i32 member;
}
```

The identity, completeness, member, layout, and redeclaration rules for structures remain to be
specified.

### 2.5 Tuples

A tuple type is an unnamed structure type with ordered, named members. Every member has a name:

```rtsl
(Position position, Data data)
```

A tuple has the same layout as a structure containing the same members in the same order.

A type alias may name a tuple without introducing a nominal structure type:

```rtsl
using Vertex = (Position position, Data data);
```

Member names, member types, and member order participate in tuple type identity. Tuples with
different member names are different types.

The exact structure and tuple layout and alignment rules remain to be specified.

### 2.6 Compile-Time Values

RTSL has no `comptime` qualifier. Whether a value is available at compile time is inferred from
the values and operations on which it depends.

Literals are available at compile time. An expression is available at compile time when its inputs
are compile-time values and its operations can be evaluated during compilation. A variable
initialized with such an expression is also a compile-time value.

## 3. Variable Qualifiers

The qualifier categories are not all equivalent. Some select storage, some constrain mutability,
and some affect linkage.

- An unqualified variable has local storage. Each shader invocation has its own instance, which
  may be read and written normally.
- A `const` variable cannot be modified after initialization. `const` constrains mutability
  rather than selecting a storage region.
- A `uniform` variable resides in uniform storage. Its value is shared by all shader invocations
  and is read-only from shader code.
- A `storage` variable resides in storage-backed memory. Its value is shared by all shader
  invocations and is read-only from shader code.
- A `shared` variable resides in workgroup-shared storage. Each workgroup has its own instance.
- An `extern` declaration refers to an externally provided entity.

The runtime may implement uniform storage using a ring-buffered uniform buffer. This is an
implementation detail and does not belong in the language specification.

## 4. Declarations

### 4.1 Declaration Order

Structure declarations are resolved before function declarations. A function declaration may
therefore refer to a structure declared later in the same scope.

### 4.2 Linkage

Namespace-scope functions and variables have external linkage unless declared `static`.
Namespace-scope functions and variables declared `static` have internal linkage.

An `extern` declaration may refer to an entity defined in another translation unit without
importing or exporting that entity, provided that the entity has external linkage.

Export and linkage are independent. `export` controls visibility through a module interface; it
does not determine whether an entity has external linkage.

For a structure member, `static` denotes storage associated with the structure rather than with
an instance. A static member is accessed through a qualified name such as `Foo::bar`.

## 5. Templates

### 5.1 Generic Declarations

A generic template declaration uses a leading template-parameter clause:

```rtsl
template<typename T>
fn foo();
```

The declaration describes a family containing one specialization for every type that may be
substituted for `T`. It does not eagerly instantiate every specialization.

Templates may be applied to functions, structures, and namespaces:

```rtsl
template<typename T>
fn foo();

template<typename T>
struct Foo {};

template<typename T>
namespace Foo {}
```

For an exported template, the template-parameter clause precedes `export`:

```rtsl
template<typename T>
export struct Foo;
```

The form `export template<typename T> struct Foo;` is not part of the language.

### 5.2 Template Parameter Kinds

Templates support type, value, and identifier parameters:

```rtsl
template<typename T, usize N, identifier Name>
```

- `typename T` accepts a type.
- `usize N` accepts a compile-time value of type `usize`. Other suitable value types may be used
  in place of `usize`.
- `identifier Name` accepts an identifier and substitutes it into identifier positions.

Identifier parameters are particularly useful for resources because a resource's declared name is
part of its externally visible identity:

```rtsl
template<typename T, identifier Name>
extern rt_texture2d<T> Name;
```

An identifier argument participates in specialization identity. Different identifier arguments
produce different specializations. Substituted identifiers follow the ordinary scope, lookup, and
collision rules.

### 5.3 Constraints

A template parameter may have a constraint:

```rtsl
template<typename T : integral>
fn foo();
```

A constraint is a compile-time expression that evaluates to a Boolean value. A constrained
template declares a specialization only for arguments that satisfy its constraint. If `Bar`
does not satisfy `integral`, the declaration above does not declare `foo<Bar>`.

Constraints may use compile-time functions, literals, variables, and previously declared
constraints. When a constraint names a template, the constrained template argument is passed
implicitly as its first template argument.

Constraints apply to all template parameter kinds:

```rtsl
template<usize N : power_of_two, identifier Name : valid_resource_name>
fn foo();
```

### 5.4 Explicit Specializations

Only an explicit specialization places template arguments after the declared name:

```rtsl
fn foo<Bar>();
```

This declares exactly `foo<Bar>`. No preceding generic declaration of `foo` is required.

When an explicit specialization and a generic template both apply to the same arguments, the
explicit specialization is selected.

An explicit specialization and a matching generic declaration refer to the same specialization,
regardless of which declaration occurs first.

If several generic declarations apply, the most constrained declaration is selected. The ordering
of constraints remains to be specified.

### 5.5 Instantiation

A specialization is instantiated at most once for the complete program, rather than once per
translation unit.

The intended semantic order is:

1. Resolve imports, declarations, and linkage.
2. Determine the required template specializations.
3. Instantiate each required specialization once.
4. Extract and compile the required shader stages.

This ordering describes language semantics. It does not prescribe compiler passes or an
implementation strategy.

## 6. Modules

### 6.1 Module Interfaces

A module interface contains declarations but not implementations. A declaration is not exported
by default. `export` makes a declaration visible through the module interface.

An ordinary import makes another module interface visible within the importing translation unit.
An exported import also includes that interface in the importing module interface.

An `extern` declaration can name an entity with external linkage without requiring the entity to
be present in an imported module interface.

### 6.2 Circular Dependencies

Ordinary imports and exported imports may form cycles. A cycle is not invalid by itself.

For a circular re-export group, exported structure declarations are resolved throughout the group
before exported function declarations. This permits declarations such as:

```rtsl
// foo.rtsl
export import <bar.rtsl>;
export struct Foo {};
export fn foo(Bar bar);
```

```rtsl
// bar.rtsl
export import <foo.rtsl>;
export struct Bar {};
export fn bar(Foo foo);
```

The specification defines the resulting visibility and validity. It does not prescribe multiple
compilation passes.

### 6.3 Compilation Artifacts

The abstract module interface contains no source text or implementation code. Its serialized
representation remains undecided.

Possible artifact designs include:

- a separate module-interface file and object file;
- a single combined compilation artifact;
- an artifact that retains source or an intermediate representation needed for templates.

Name resolution and linkage are intended to complete before template instantiation. A referenced
entity that cannot be resolved makes the program invalid.

## 7. Template and Yield Example

The following types illustrate template structures and incremental return construction:

```rtsl
struct Position {
    vec4 value;
    f32 point_size;
    f32* clip_distance;
    f32* cull_distance;
}

template<typename T, usize N>
struct TriangleStrip {
    fn operator<-(T value);
    fn size() const -> usize;

  private:
    T _vertices[N];
    usize _size;
}

template<typename T, usize N>
fn TriangleStrip<T, N>::operator<-(T value) {
    _vertices[_size++] = value;
}
```

## 8. Shader-Stage Example

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
    yield triangle[0];
    yield triangle[1];
    yield triangle[2];
}

@stage : fragment
fn main(Vertex vertex) -> vec4 {
    return vertex.data.color;
}
```

## 9. Yielding Functions

When a function executes `yield`, an implicit return object of the function's return type is
conceptually constructed. For a function returning `TriangleStrip<Vertex, 3>`, that object is
equivalent to:

```rtsl
TriangleStrip<Vertex, 3> __return{};
```

The statement:

```rtsl
yield value;
```

has semantics equivalent to:

```rtsl
__return<-value;
```

All `yield` statements in the function operate on the same implicit return object. Reaching the
end of the function or executing `return;` returns that object. An explicit return value returns
the specified value instead.

The implicit return object is an abstract semantic object. It need not be materialized in storage
if the implementation preserves the observable behavior of the program.

A function may be declared as yielding:

```rtsl
fn foo() yield -> Foo;
```

A yielding function inherits the yield target of its caller rather than constructing its own
implicit return object. Calls between yielding functions propagate the current target through any
number of calls.

## 10. Named Barriers

A named barrier is syntactically similar to a label:

```rtsl
@stage : compute
fn main(usize x, usize y, usize z) {
    some_barrier:
}
```

The synchronization and control-flow semantics of named barriers remain to be specified.

## 11. Planned Specification Areas

The specification will eventually require dedicated sections for:

- types and conversions;
- declarations and definitions;
- expressions and operators;
- statements and control flow;
- templates and constraints;
- shader stages and interfaces;
- resource and storage semantics;
- the standard library.

## 12. Open Questions

- How are overlapping constrained generic templates ordered when more than 1 constraint succeeds?
- How are template definitions represented across module and compilation artifacts?
- Which declarations require definitions before instantiation or stage extraction?
- What exact artifact format represents module interfaces and compiled implementations?
- What are the layout and target-lowering requirements for scalar and resource types?
- What are the identity, completeness, member, layout, and redeclaration rules for structures?
- What are the remaining compatibility rules for tuple types?
- Which operations are permitted on pointers and references to resource values?
- May a qualified name be supplied as an identifier template argument?
- How do namespaces affect the external names of resources produced by identifier substitution?
- Which declarations other than types are visible before their textual occurrence?
- Which operations and functions may be evaluated at compile time?
- What happens when a compile-time variable is later assigned a runtime value?
- What synchronization behavior does a named barrier provide?
- What restrictions apply to `yield`, yielding calls, and explicit returns?



you can export structs by saying export struct Foo; or definition inline whatever you want



Structs...
basically you declare a struct by saying
struct Foo;
and you define a struct by saying 
struct Foo {
    i32 member;
}
you can export structs by saying export struct Foo; or definition inline whatever you want

templates. it should be template<typename T> export struct. not export template<typename T> struct. okay?
structs have the same specilization etc rules as function templates. structs are just like functions and variables etc
that they define some compiler thing that later gets accessed. just like in c etc. struct Foo {}; declares a well not variable
but something called Foo, that can then be accessed. its the same system. because its simpler.
right what else
uh padding.
i dont know padding rules tbh
i think they should be similar to c and c++. or maybe even be undefined ?? i mean there is no raw byte access anyway.
