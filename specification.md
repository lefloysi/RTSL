# Rutile Shading Language

## 1. Introduction

This document specifies the syntax and semantics of the Rutile Shading Language (RTSL).

### 1.1 Conventions

Terms defined by this specification are written in *italics* when they are introduced.

In grammar fragments, `=` introduces a production, quoted text denotes literal source characters,
`,` denotes sequence, `|` denotes alternatives, parentheses group elements, braces denote zero or
more repetitions, and `;` ends a production.

## 2. Source Text

### 2.1 Source Encoding

RTSL source text is encoded using UTF-8. Source text that is not well-formed UTF-8 is invalid.

### 2.2 Character Set

The RTSL character set consists of:

- the 52 uppercase and lowercase *letter* characters of the Latin alphabet:
`a` `b` `c` `d` `e` `f` `g` `h` `i` `j` `k` `l` `m` `n` `o` `p` `q` `r`
`s` `t` `u` `v` `w` `x` `y` `z`
`A` `B` `C` `D` `E` `F` `G` `H` `I` `J` `K` `L` `M` `N` `O` `P` `Q` `R`
`S` `T` `U` `V` `W` `X` `Y` `Z`;

- the 10 decimal *digit* characters:
`0` `1` `2` `3` `4` `5` `6` `7` `8` `9`;

- the 30 graphic *symbol* characters:
`!` `"` `#` `%` `&` `'` `(` `)` `*` `+` `,` `-` `.` `/` `:`
`;` `<` `=` `>` `?` `@` `[` `\` `]` `^` `_` `{` `|` `}` `~`;

- the 4 *whitespace* characters listed below.

| Notation | Code point | Name            |
| -------- | ---------- | --------------- |
| ` `      | U+0020     | Space           |
| `\t`     | U+0009     | Horizontal tab  |
| `\n`     | U+000A     | Line feed       |
| `\r`     | U+000D     | Carriage return |

Each backslash sequence in the table denotes the corresponding single whitespace character.

An *RTSL source string* is a sequence of RTSL characters.

Any decoded character that is not an RTSL character is invalid.

### 2.3 Lines and New-Lines

A *new-line* is a line feed or a carriage return. A carriage return immediately followed by a line
feed forms a single new-line.

A source string is divided into lines at each new-line.

### 2.4 Line Continuations

A reverse solidus (`\`) immediately followed by a new-line forms a *line continuation*. The reverse
solidus and the new-line are removed before lexical decomposition. Their removal does not insert
whitespace, and the joined source is treated as a single line during all subsequent processing.

Line continuations are removed before comments are recognized. Consequently, a line comment whose
last character is a reverse solidus continues onto the following physical line.

For example:

```rtsl
Foo f\
oo;
```

is treated as:

```rtsl
Foo foo;
```

## 3. Translation Units

A *translation unit* is an RTSL source string supplied as a unit of compilation.

### 3.1 Compilation Environments

A *compilation environment* supplies the translation units that participate in a compilation. It
also associates each translation unit with a canonical name.

### 3.2 Canonical Names

Every translation unit has a *canonical name*. Canonical names are unique within a compilation
environment and are compared case-sensitively.

For source read from a file, the canonical name is normally the file name. Source supplied through
another mechanism, such as an inline source string, is assigned a canonical name by the
compilation environment.

## 4. Lexical Structure

After line continuations have been removed, a source string is decomposed into preprocessing
tokens, whitespace characters, and comments. The decomposition proceeds from left to right. A
whitespace character is taken as whitespace. A comment delimiter begins a comment except within a
character literal. At any other position, the longest sequence of characters that forms a
preprocessing token is taken.

Whitespace and comments separate adjacent preprocessing tokens. They are not themselves
preprocessing tokens.

### 4.1 Whitespace

The *whitespace* characters are the 4 characters classified as whitespace in Section 2.2.

Whitespace separates preprocessing tokens. Except for new-lines, whitespace has no meaning after
token separation. New-lines are retained for use by the preprocessor.

### 4.2 Comments

A *line comment* begins with `//` and continues up to the next new-line or the end of the source
string. A new-line that terminates a line comment is not part of the comment.

A *block comment* begins with `/*` and ends with the first subsequent `*/`. Block comments do not
nest. A block comment that is not terminated before the end of the source string is invalid.

Within a line comment, comment delimiters have no special meaning. Within a block comment, `*/`
ends the comment; any other comment delimiter has no special meaning. Comment delimiters are not
recognized within character literals.

Before preprocessing, the non-new-line characters of each comment are replaced by a single space.
New-lines within a block comment and the new-line following a line comment are retained. A comment
therefore separates preprocessing tokens in the same manner as whitespace. For example,
`foo/**/bar` contains the 2 identifier tokens `foo` and `bar`.

### 4.3 Preprocessing Tokens

A *preprocessing token* is a lexical element consumed by the preprocessor. Preprocessing tokens
include identifiers, preprocessing numbers, character literals, and punctuators.

The forms of preprocessing numbers, character literals, and punctuators are to be specified.

### 4.4 Identifiers

An *identifier* is a preprocessing token that may name a language entity. It has the following
form:

```ebnf
identifier = ( letter | "_" ) , { letter | digit | "_" } ;
```

In this grammar, `letter` and `digit` denote the character classes defined in Section 2.2.

An identifier begins with a *letter* character or an underscore. Each subsequent character is a
*letter* character, a *digit* character, or an underscore.

RTSL is case-sensitive. Identifiers that differ only in letter case are distinct identifiers.

After preprocessing, an identifier whose spelling is reserved as a keyword is a keyword token and
cannot name an entity.

## 5. Preprocessing

The preprocessor operates on the preprocessing tokens produced by lexical decomposition. Comments
are recognized and replaced before preprocessing directives are evaluated. Preprocessing therefore
cannot create a comment.

The preprocessing directives and their effects are to be specified.

## 6. Types

### 6.1 Structures

A structure declaration introduces a structure name without defining its members:

```rtsl
struct Foo;
```

The declaration introduces `Foo` as a named language entity. The name is subject to the ordinary
declaration and lookup rules.

A structure definition provides the members of the structure:

```rtsl
struct Foo {
    i32 member;
}
```

A structure declaration or definition may be exported:

```rtsl
export struct Foo;

export struct Bar {
    i32 member;
}
```

The remaining rules for structure declarations and definitions are to be specified.

### 6.2 Tuples

A *tuple type* is an unnamed structure type with an ordered sequence of named members. Every member
has a name. For example:

```rtsl
(Position position, Data data)
```

contains a member named `position` of type `Position`, followed by a member named `data` of type
`Data`.

A type alias may name a tuple type:

```rtsl
using Vertex = (Position position, Data data);
```

The alias does not introduce a nominal structure type. Member names, member types, and member order
participate in the identity of a tuple type. Tuples with different member names are different types.

A tuple has the same layout as a structure containing the same members in the same order.

### 6.3 Compile-Time Values

RTSL has no qualifier that marks a value as compile-time. Compile-time availability is inferred
from the values and operations on which an expression depends.

A literal is a compile-time value. An expression whose inputs are compile-time values is itself a
compile-time value when its operations can be evaluated during compilation. A variable initialized
with such an expression is also a compile-time value. For example, `foo` is compile-time in:

```rtsl
i32 foo = 5;
```

### 6.4 Absence of Strings

RTSL does not define string types or string literals.

## 7. Declarations

A *declaration* introduces or redeclares a language entity. The forms of declarations are
specified in later sections.

### 7.1 Declaration Order

Within a scope, structure declarations are resolved before function declarations. A type
declaration is visible throughout its containing scope, including before its textual occurrence.
Consequently, a declaration may refer to a type declared later in the same scope.

### 7.2 Linkage

Export and linkage are independent properties.

A namespace-scope function or variable has external linkage unless declared `static`. A
namespace-scope function or variable declared `static` has internal linkage and cannot be named
from another translation unit.

An `extern` declaration may refer to an entity with external linkage that is defined in another
translation unit. The entity does not need to be exported or imported.

The other forms and effects of linkage are to be specified.

## 8. Templates

A declaration preceded by a template-parameter clause is a *template declaration*. A template
declaration describes a family of declarations. It does not instantiate every member of that
family.

For example:

```rtsl
template<typename T>
fn foo();
```

declares a specialization of `foo` for every type that may be substituted for `T`.

A template-parameter clause precedes `export` when the templated declaration is exported:

```rtsl
template<typename T>
export struct Foo;
```

The form `export template<typename T> struct Foo;` is invalid.

The specialization, redeclaration, constraint, and selection rules for templates apply equally to
function templates and structure templates.

### 8.1 Template Parameter Kinds

A template parameter is a type parameter, a value parameter, or an identifier parameter.

A *type parameter* is introduced with `typename` and accepts a type:

```rtsl
typename T
```

A *value parameter* is introduced by a type and an identifier. It accepts a compile-time value of
that type:

```rtsl
usize N
```

Any type may be used for a value parameter if its argument is available at compile time.

An *identifier parameter* is introduced with `identifier` and accepts an identifier:

```rtsl
identifier Name
```

Substitution of an identifier parameter replaces each use of the parameter in an identifier
position. The substituted identifier follows the ordinary scope, lookup, and collision rules. In
particular, it can determine the externally visible name of a resource:

```rtsl
template<typename T, identifier Name>
extern rt_texture2d<T> Name;
```

An identifier argument participates in specialization identity. Different identifier arguments
produce different specializations.

### 8.2 Constraints

Any kind of template parameter may have a constraint. The constraint is written after the
parameter name, separated by a colon:

```rtsl
template<typename T : integral>
fn foo();
```

The constraint is evaluated for each substitution of `T`. The template declares a specialization
only when the constraint evaluates to `true`. If `Bar` does not satisfy `integral`, the declaration
above does not declare `foo<Bar>`.

A call to a specialization for which no declaration exists is invalid.

The same syntax applies to value and identifier parameters:

```rtsl
template<usize N : power_of_two, identifier Name : valid_resource_name>
fn foo();
```

### 8.3 Explicit Specializations

An *explicit specialization* uses template arguments in place of template parameters. It declares
exactly the specialization named by those arguments.

A template-argument list following a declared name denotes an explicit specialization. A generic
template declaration does not place template arguments after the declared name.

For example:

```rtsl
fn foo<Bar>();
```

An explicit specialization does not require a preceding generic template declaration. The example
above declares `foo<Bar>` even if no generic declaration of `foo` exists.

If a generic declaration of `foo` is introduced before or after `foo<Bar>`, both declarations refer
to the same specialization.

### 8.4 Template Selection

If both an explicit specialization and a generic template declaration apply to the same template
arguments, the explicit specialization is selected.

If more than 1 generic template declaration applies, the most constrained declaration is selected.
The ordering of constraints is to be specified.

## 9. Modules

Every translation unit has a *module interface*. It consists of the declarations exported directly
by that translation unit and the declarations made available through its exported imports.

A module interface describes declarations, but not their implementations. It exposes neither the
translation unit's source text nor its non-exported declarations.

A module interface is an abstract language construct. This specification does not prescribe a file
format or serialization for it.

### 9.1 Exports

A declaration is not exported by default.

The `export` keyword makes a declaration part of the translation unit's module interface.

### 9.2 Import Declarations

An import declaration has either of the following forms:

```rtsl
import <foo.rtsl>;
export import <foo.rtsl>;
```

The nonempty sequence of characters between `<` and `>` is the *import name*. An import name cannot
contain `>` or a new-line. An import name is not an identifier.

An import name denotes the translation unit whose canonical name is the same sequence of
characters. The import makes that translation unit's module interface visible throughout the
importing translation unit.

An import name that does not denote a translation unit is invalid.

An exported import adds every declaration in the imported module interface to the importing module
interface. An ordinary import affects visibility within the importing translation unit but does not
re-export any declarations.

### 9.3 Circular Dependencies

Import dependencies may contain cycles, including cycles formed by exported imports. A cycle does
not by itself make a program invalid.

A *circular re-export group* is a maximal set of translation units in which every member is
reachable from every other member by following 1 or more exported imports.

The module interfaces of a circular re-export group are determined together. The exported
structure declarations of every member are resolved and made available throughout the group before
any exported function declarations are resolved. An exported function declaration may therefore
refer to a structure exported by any member of the group.

Within the group, each module interface is the smallest set containing its directly exported
declarations and all declarations in the interfaces it imports with `export import`.

A declaration reached through more than 1 import path remains a single declaration. Conflicting
declarations and unresolved names remain invalid even when they occur in a circular group.

These rules specify the semantic result. They do not require an implementation to use a particular
number or order of compilation passes.

For example, the following circular re-export dependency is valid:

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

`Foo` and `Bar` are resolved before `foo` and `bar`. The module interface of each translation unit
therefore contains both structures and both functions.

## 10. Compilation Products

The representation of compiled implementations and module interfaces is to be specified. RTSL does
not currently require them to be stored in separate files.

