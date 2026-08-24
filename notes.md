



Rutile source text is encoded using UTF-8. The language is case-sensitive.

The following whitespace characters are permitted:

| Character | Value | Name            |
| --------- | ----- | --------------- |
| ` `       | 0x20  | Space           |
| `\t`      | 0x09  | Horizontal tab  |
| `\n`      | 0x0A  | Line feed       |
| `\r`      | 0x0D  | Carriage return |

The following non-whitespace characters are permitted:

| Character    | Name              |
| ------------ | ----------------- |
| `a-z`, `A-Z` | Letters           |
| `0-9`        | Digits            |
| `_`          | Underscore        |
| `.`          | Period            |
| `+`          | Plus              |
| `-`          | Hyphen-minus      |
| `*`          | Asterisk          |
| `/`          | Solidus           |
| `%`          | Percent sign      |
| `<` `>`      | Angle brackets    |
| `[` `]`      | Square brackets   |
| `(` `)`      | Parentheses       |
| `{` `}`      | Braces            |
| `^`          | Circumflex accent |
| `|`          | Vertical line     |
| `&`          | Ampersand         |
| `~`          | Tilde             |
| `=`          | Equals sign       |
| `!`          | Exclamation mark  |
| `:`          | Colon             |
| `;`          | Semicolon         |
| `,`          | Comma             |
| `?`          | Question mark     |
| `\`          | Reverse solidus   |


| Storage Qualifier | Meaning |
| ----------------- | ------- |
| *none*            | The variable has local storage. Each shader invocation has its own instance. The variable may be read and written normally. |
| `const`           | The variable cannot be modified after initialization. `const` constrains mutability rather than selecting a separate storage region. |
| `uniform`         | The variable is placed in the shader's uniform block. A single value is shared by all invocations of the shader and is read-only from shader code. |
| `storage`         | The variable is placed in the shader's storage block. A single value is shared by all invocations of the shader and is read-only from shader code. |
| `shared`          | The variable is placed in workgroup-shared storage. A single instance is shared by all invocations in the same workgroup. |
| `extern`          | The variable refers to an externally provided symbol.  |

## 5. Operators and Expressions

## 6. Statements and Structure

## 7. Standard Library


Value Types:

usize : the canonical size type. comparable to size_t
i08 i16 i32 i64 : may lower to different things depending on hardware
u08 u16 u32 u64 : may lower to different things depending on hardware
f32 f64



Resource Types:
Resource types can only be constructed by the compiler. what this means is, you can only declare
them extern. because you cant construct them. they also dont have a copy or move constructor, you 
can however take references and pointers to them.

rt_sampler

rt_image1d<Format>
rt_image2d<Format>
rt_image3d<Format>

rt_texture1d<T>
rt_texture2d<T>
rt_texture3d<T>

rt_uniform<T, U>
rt_storage<T, U>



rt_storage<(Foo foo, Bar bar), T> array;


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

```rtsl
// Every shader has one ring-buffered uniform buffer. This is managed by Rutile and should not
// appear as an implementation requirement in the language specification. The specification only
// defines that variables declared uniform are placed in uniform storage.

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
// simple tuple
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

// A yield statement incrementally constructs the return value of a function.
//
// When a function uses yield, an implicit return object of the function's return type is
// conceptually constructed. For example, in a function returning TriangleStrip<Vertex, 3>:
//
//     TriangleStrip<Vertex, 3> __return{};
//
// A statement:
//
//     yield value;
//
// has semantics equivalent to:
//
//     __return<-value;
//
// All yield statements within the function operate on the same implicit return object. Reaching the
// end of the function or executing return; returns this object. An explicit return value instead
// returns the specified value.
//
// The implicit return object is an abstract semantic object. It is not required to be materialized
// in storage if the implementation can preserve the observable behavior of the program.
//
// A function may be declared as yielding:
//
//     fn foo() yield -> Foo;
//
// A yielding function does not construct its own implicit return object. Instead, it inherits the
// yield target of its caller through an implicit templated reference. Yield statements within the
// yielding function operate on this inherited object.
//
// Calling a yielding function therefore propagates the current yield target through the call. This
// may continue through any number of yielding function calls without requiring the yield target to
// be materialized in storage.


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

```


@stage : compute
fn main(usize x, usize y, usize) {
    // Although syntactically similar to a label, this is a named barrier.
    some_barrier:
}
```