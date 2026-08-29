# Rutile Shading Language



## Contents

- [1. Introduction](#1-introduction)
  - [1.1 Scope](#11-scope)
  - [1.2 Conformance](#12-conformance)
  - [1.3 Conventions](#13-conventions)
- [2. Basics](#2-basics)
  - [2.1 Processing Model](#21-processing-model)
  - [2.2 Source Text](#22-source-text)
  - [2.3 Character Set](#23-character-set)
  - [2.4 Lines and Line Continuations](#24-lines-and-line-continuations)
  - [2.5 Translation Units](#25-translation-units)
  - [2.6 Lexical Decomposition](#26-lexical-decomposition)
  - [2.7 Whitespace and Comments](#27-whitespace-and-comments)
  - [2.8 Preprocessing Tokens](#28-preprocessing-tokens)
  - [2.9 Identifiers](#29-identifiers)
  - [2.10 Preprocessing and Language Tokens](#210-preprocessing-and-language-tokens)
  - [2.11 Macro and Conditional Directives](#211-macro-and-conditional-directives)
- [3. Types](#3-types)
  - [3.1 Type Categories](#31-type-categories)
  - [3.2 Built-in Types](#32-built-in-types)
    - [3.2.1 Numeric Types](#321-numeric-types)
    - [3.2.2 Vector Types](#322-vector-types)
    - [3.2.3 Matrix Types](#323-matrix-types)
    - [3.2.4 String Types](#324-string-types)
    - [3.2.5 Resource Types](#325-resource-types)
  - [3.3 Array Types](#33-array-types)
  - [3.4 Pointer Types](#34-pointer-types)
  - [3.5 Reference Types](#35-reference-types)
  - [3.6 Structure Types](#36-structure-types)
  - [3.7 Tuple Types](#37-tuple-types)
  - [3.8 Complete and Incomplete Types](#38-complete-and-incomplete-types)
  - [3.9 Type Identity and Compatibility](#39-type-identity-and-compatibility)
- [4. Declarations](#4-declarations)
  - [4.1 General Form](#41-general-form)
  - [4.2 Attributes](#42-attributes)
  - [4.3 Type Specifiers and Type Names](#43-type-specifiers-and-type-names)
  - [4.4 Object Declarators](#44-object-declarators)
  - [4.5 Pointer Declarators](#45-pointer-declarators)
  - [4.6 Reference Declarators](#46-reference-declarators)
  - [4.7 Array Declarators](#47-array-declarators)
  - [4.8 Parenthesized and Abstract Declarators](#48-parenthesized-and-abstract-declarators)
  - [4.9 Type Alias and Tuple Declarations](#49-type-alias-and-tuple-declarations)
  - [4.10 Structure Declarations](#410-structure-declarations)
  - [4.11 Variable, Member, and Parameter Declarations](#411-variable-member-and-parameter-declarations)
  - [4.12 Function Declarations](#412-function-declarations)
  - [4.13 Parameter Contracts](#413-parameter-contracts)
  - [4.14 Namespace Declarations](#414-namespace-declarations)
  - [4.15 Template Declarations](#415-template-declarations)
  - [4.16 Import and Export Declarations](#416-import-and-export-declarations)
- [5. Definitions](#5-definitions)
  - [5.1 Structure Definitions](#51-structure-definitions)
  - [5.2 Variable Definitions](#52-variable-definitions)
  - [5.3 Function and Operator Definitions](#53-function-and-operator-definitions)
  - [5.4 Destructors](#54-destructors)
  - [5.5 Template Definitions](#55-template-definitions)
  - [5.6 Namespace Definitions](#56-namespace-definitions)
- [6. Expressions and Operators](#6-expressions-and-operators)
  - [6.1 Construction](#61-construction)
  - [6.2 Conversions](#62-conversions)
  - [6.3 Destructuring](#63-destructuring)
  - [6.4 Array-to-Pointer Conversion](#64-array-to-pointer-conversion)
  - [6.5 Subscripting](#65-subscripting)
  - [6.6 Address-Of](#66-address-of)
  - [6.7 Indirection](#67-indirection)
  - [6.8 Pointer Arithmetic](#68-pointer-arithmetic)
  - [6.9 Pointer Comparison](#69-pointer-comparison)
  - [6.10 Reference Binding and Access](#610-reference-binding-and-access)
- [7. Statements and Control Flow](#7-statements-and-control-flow)
  - [7.1 Destruction on Scope Exit](#71-destruction-on-scope-exit)
  - [7.2 Selection Statements](#72-selection-statements)
  - [7.3 Emitting Functions](#73-emitting-functions)
  - [7.4 Named Barriers](#74-named-barriers)
- [8. Templates](#8-templates)
  - [8.1 Template Parameter Kinds](#81-template-parameter-kinds)
  - [8.2 Default Template Arguments](#82-default-template-arguments)
  - [8.3 Constraints](#83-constraints)
  - [8.4 Explicit Specializations](#84-explicit-specializations)
  - [8.5 Template Selection](#85-template-selection)
  - [8.6 Interface and Concrete Content](#86-interface-and-concrete-content)
  - [8.7 Instantiation](#87-instantiation)
- [9. Modules](#9-modules)
  - [9.1 Scopes and Names](#91-scopes-and-names)
  - [9.2 Name Lookup](#92-name-lookup)
  - [9.3 Linkage](#93-linkage)
  - [9.4 Module Interfaces and Concrete Content](#94-module-interfaces-and-concrete-content)
  - [9.5 Exports and Imports](#95-exports-and-imports)
  - [9.6 Circular Dependencies](#96-circular-dependencies)
- [10. Shader Stages and Interfaces](#10-shader-stages-and-interfaces)
  - [10.1 Stage Interfaces](#101-stage-interfaces)
  - [10.2 Stage Order](#102-stage-order)
  - [10.3 Standard Stage Types](#103-standard-stage-types)
  - [10.4 Rasterization Contracts](#104-rasterization-contracts)
  - [10.5 Stage Selection](#105-stage-selection)
- [11. RTSL IR and Artifacts](#11-rtsl-ir-and-artifacts)
  - [11.1 Representation Model](#111-representation-model)
  - [11.2 Physical Encoding](#112-physical-encoding)
  - [11.3 Header and Compatibility](#113-header-and-compatibility)
  - [11.4 Instructions, IDs, and Literals](#114-instructions-ids-and-literals)
  - [11.5 Logical Instruction Order](#115-logical-instruction-order)
  - [11.6 Enumerations](#116-enumerations)
  - [11.7 Symbols and Canonical Link Names](#117-symbols-and-canonical-link-names)
  - [11.8 Type and Constant Instructions](#118-type-and-constant-instructions)
  - [11.9 Module-Interface Instructions](#119-module-interface-instructions)
  - [11.10 Concrete SSA Instructions](#1110-concrete-ssa-instructions)
  - [11.11 Reflection Instructions](#1111-reflection-instructions)
  - [11.12 Artifact Kinds](#1112-artifact-kinds)
  - [11.13 Linking](#1113-linking)
  - [11.14 RTSL Transpilers](#1114-rtsl-transpilers)
  - [11.15 Extensions](#1115-extensions)
  - [11.16 Stripping and Debug Data](#1116-stripping-and-debug-data)
- [12. Debugging](#12-debugging)



## 1. Introduction

The Rutile Shading Language (RTSL) is a shading language for defining data, functions, resources,
and shader-stage computations. This document specifies the syntax and semantics of RTSL and the
requirements placed on RTSL implementations and backends.


### 1.1 Scope

This specification defines RTSL source, preprocessing, *declarations*, *definitions*, execution,
*modules*, compilation artifacts, and target-independent *RTSL IR*.

Target-dependent language rules include the layout of *structure types*, the availability of
*RTSL extensions*, and the bindings assigned to externally supplied resources. Host APIs, backend
representations, and binary encodings are outside this specification unless stated otherwise.


### 1.2 Conformance

An RTSL implementation conforms to this specification when, for each target it supports, it
accepts every valid RTSL program, rejects every program required to be invalid, and preserves the
specified observable behavior.

The specification describes semantic dependencies without prescribing compiler passes, storage
formats, or an internal implementation. An implementation may combine, divide, reorder, or omit
internal work when doing so does not change the required result or observable program behavior.


### 1.3 Conventions

Every occurrence of a term defined by this specification is written in *italics*, including the
occurrence that defines the term.

Inline code identifies literal source text, names, operators, and other exact spellings. Fenced
blocks marked `rtsl` contain RTSL source. Fenced blocks marked `ebnf` contain grammar fragments.

A construct described as invalid does not satisfy the applicable language rule.

In grammar fragments, `=` introduces a production, quoted text denotes literal source characters,
`,` denotes sequence, `|` denotes alternatives, parentheses group elements, braces denote zero or
more repetitions, and `;` ends a production.

Examples illustrate the surrounding rules and do not introduce additional syntax or semantics.
Names used only in an example have no predefined meaning unless the text states otherwise.



---



## 2. Basics

RTSL source is supplied in *translation units*. Preprocessing forms the language *tokens* from
which *declarations* and *definitions* are parsed. The rules in this chapter define that source and
the semantic ordering of its processing.


### 2.1 Processing Model

Preprocessing determines the language *token sequence* of each *translation unit*. The
*declarations* and *definitions* of the *translation unit* are parsed from that sequence.

*Declaration processing* determines the declared entities, their types, and the exported
*declarations* of each required *translation unit*. Within a *scope*, type *declarations* become
available before function and variable *declarations*. All applicable *declarations* become
available before their *definitions* are processed.

*Definition processing* completes the declared entities. It adds exported *template definitions*
to the *module interface* and produces concrete *RTSL IR* from non-template *definitions* and
instantiated *template specializations*. Compiling a *translation unit* consumes the
*module-interface artifacts* required by its imports and produces an *object artifact*. The
compiler also produces a *module-interface artifact* when compiled interface output is requested.

An *object artifact* contains the concrete output of 1 compiled *translation unit*. A
*module-interface artifact* contains compiled *module interfaces* used while compiling source. A
*library artifact* contains concrete output linked from 1 or more *object artifacts* or
*library artifacts*. An *RTSL transpiler* converts a *library artifact* into backend shaders. The
exact artifact representation is defined by the *RTSL IR* rules.

These are dependencies between required results, not prescribed compiler passes. An implementation
may combine or interleave its internal work when the same *declarations*, *definitions*,
*module interfaces*, *RTSL artifacts*, diagnostics, and observable behavior result.


### 2.2 Source Text

An *RTSL source string* is a UTF-8 encoded sequence of *RTSL characters*. Input that is not
well-formed UTF-8 is invalid.

A *source file* is a file that contains Rutile Shading Language code. When a *source file*
participates in a compilation, its contents are supplied as an *RTSL source string*.


### 2.3 Character Set

The *RTSL character set* consists of:

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

Each notation in the table denotes 1 *whitespace* character. Each character in the
*RTSL character set* is an *RTSL character*. An input character that is not an *RTSL character* is
invalid.


### 2.4 Lines and Line Continuations

A *new-line* is a line feed, a carriage return, or a carriage return followed immediately by a
line feed. The 2-character form is 1 *new-line*. An *RTSL source string* is divided into *lines* at
each *new-line*.

A reverse solidus (`\`) immediately followed by a *new-line* forms a *line continuation*. The
preprocessor removes both characters before recognizing *comments*. No *whitespace* is inserted.
The joined text occupies 1 *line* during subsequent processing.

A reverse solidus immediately before the terminating *new-line* of a *line comment* therefore
continues the *line comment* onto the next physical *line*.

For example:

```rtsl
Foo f\
oo;
```

is treated as:

```rtsl
Foo foo;
```


### 2.5 Translation Units

A *translation unit* is an *RTSL source string* supplied under a *canonical name* and processed as
1 unit.

The *canonical name*, rather than the contents of the *RTSL source string*, identifies the
*translation unit*. Different *canonical names* identify different *translation units*, even when
their *RTSL source strings* are identical.

A *canonical name* is a nonempty *string*. *Canonical names* are compared character by character
and case-sensitively. RTSL does not normalize case, path separators, relative components, or any
other part of a *canonical name*.

The *canonical name* of file-backed source is its file name. A *compilation environment* assigns a
*canonical name* to source that is not file-backed before the source participates in a compilation.

A *compilation environment* maps each *canonical name* in a compilation to exactly 1
*RTSL source string*. Each mapping entry supplies 1 *translation unit*. The mapping remains fixed
for the duration of the compilation.


### 2.6 Lexical Decomposition

As part of preprocessing, *line continuations* are removed and an *RTSL source string* is
decomposed from left to right into *preprocessing tokens*, *whitespace*, and *comments*. Each
*whitespace* character is recognized separately. At every other position, the longest valid
*preprocessing token* is taken.

A *comment* delimiter begins a *comment* except within a *character literal* or *string literal*.


### 2.7 Whitespace and Comments

*Whitespace* separates *preprocessing tokens*. Except for *new-lines*, *whitespace* has no meaning
after lexical decomposition. *New-lines* are retained for preprocessing directives.

A *line comment* begins with `//` and continues up to the next *new-line* or the end of the
*RTSL source string*. A *new-line* that terminates a *line comment* is not part of the *comment*.

A *block comment* begins with `/*` and ends with the first subsequent `*/`. *Block comments* do not
nest. A *block comment* that is not terminated before the end of the *RTSL source string* is
invalid.

Within a *line comment*, *comment* delimiters have no special meaning. Within a *block comment*,
`*/` ends the *comment*; any other *comment* delimiter has no special meaning. *Comment* delimiters
are not recognized within *character literals* or *string literals*.

The non-*new-line* characters of each *comment* are replaced by 1 space. *New-lines* within a
*block comment* and the *new-line* following a *line comment* are retained. A *comment* therefore
separates *preprocessing tokens* in the same manner as *whitespace*.

For example, `foo/**/bar` contains the 2 *identifiers* `foo` and `bar`.


### 2.8 Preprocessing Tokens

A *preprocessing token* is a lexical element consumed by the preprocessor. It has 1 of the
following forms:

```ebnf
preprocessing-token = identifier
                    | preprocessing-number
                    | character-literal
                    | string-literal
                    | punctuator ;
```

A *preprocessing number* is provisionally recognized as a numeric spelling. After preprocessing,
it is converted to an *integer literal* or *floating literal*.


### 2.9 Identifiers

An *identifier* is a *preprocessing token* with the following form:

```ebnf
identifier          = identifier-start , { identifier-continue } ;
identifier-start    = letter | "_" ;
identifier-continue = identifier-start | digit ;
```

RTSL is case-sensitive. *Identifiers* that differ only in *letter* case are distinct.

After preprocessing, an *identifier* whose spelling is reserved as a *keyword* is classified as a
*keyword* and cannot name an entity.


### 2.10 Preprocessing and Language Tokens

The preprocessor replaces *comments*, evaluates preprocessing directives, and converts the
remaining *preprocessing tokens* into language *tokens*. These are preprocessing operations, not
separately required compilation passes.

*Comments* are replaced before preprocessing directives are evaluated. A preprocessing directive
cannot create a *comment*.

Preprocessing produces a *token sequence* consumed by the RTSL parser. A *token* has 1 of the
following forms:

```ebnf
token = keyword
      | identifier
      | integer-literal
      | floating-literal
      | character-literal
      | string-literal
      | punctuator ;
```

A *keyword* is an *identifier* spelling reserved by RTSL. Every other *identifier* remains an
*identifier* when converted to a language *token*.

An *integer literal* or *floating literal* is formed from a *preprocessing number*. A
*preprocessing number* that does not form a valid numeric literal is invalid.

A *punctuator* is an operator or delimiter represented by punctuation characters. The
*punctuator* grammar determines whether adjacent punctuation characters form 1 *token* or several
*tokens*.

*Whitespace*, *comments*, and preprocessing directives do not produce language *tokens*.


### 2.11 Macro and Conditional Directives

A *macro definition* defines a *macro*. A *macro* associates an *identifier* with an optional
sequence of *preprocessing tokens*:

```rtsl
#define FOO
#define BAR "replacement text"
```

The first form defines `FOO` without a replacement sequence. The second form defines `BAR` with
the *preprocessing tokens* of the *string literal* as its replacement sequence.

A *conditional preprocessing directive* tests a *preprocessing condition*. A
*preprocessing condition* may be a *macro* name or an *expression* containing *macro* names:

```rtsl
#if FOO
#endif

#if VALUE == 1
#endif
```

A defined *macro* without a replacement sequence evaluates as `true` in a
*preprocessing condition*. A *macro* with a replacement sequence is expanded before its
*preprocessing condition* is evaluated. A *preprocessing condition* that uses an undefined
*macro* name evaluates as `false`.



---



## 3. Types

RTSL provides *built-in types*, permits *declarations* to introduce types, and permits declarators
to derive *array types*, *pointer types*, and *reference types* from other types. This chapter
defines the properties and relationships of those types. The syntax that forms or declares them is
specified with *declarations*.


### 3.1 Type Categories

A *built-in type* is provided by RTSL without a source *declaration*. *Numeric types*,
*vector types*, *matrix types*, *string types*, and *resource types* are *built-in types*.

A *declared type* is introduced by a source *declaration*. Named *structure types* and anonymous
*tuple types* are *declared types*. A *type alias declaration* gives another name to a type; it
does not introduce a new type.

A *derived type* is formed from another type by an *array declarator*, *pointer declarator*, or
*reference declarator*. The type from which an *array type* is formed is its *element type*. The
type from which a *pointer type* or *reference type* is formed is its *referred-to type*.

A *value type* is a type whose values may be stored in ordinary objects. *Numeric types*,
*vector types*, *matrix types*, *string types*, *array types*, *pointer types*,
*reference types*, *structure types*, and *tuple types* are *value types*. A *resource type* is
not a *value type*.


### 3.2 Built-in Types

The following subsections define the types provided without source *declarations*.


#### 3.2.1 Numeric Types

The *signed integer types* are `i08`, `i16`, `i32`, and `i64`. The *unsigned integer types* are
`u08`, `u16`, `u32`, and `u64`. The *floating-point types* are `f32` and `f64`. These types and
`usize` are the *numeric types*. The type `usize` represents sizes and array subscripts.

If an operation on a *signed integer type* produces a result outside that type's representable
range, the behavior is undefined. Arithmetic on an *unsigned integer type* is performed modulo 2
raised to the number of value bits in the type.

The type `f32` uses the IEEE 754 binary32 format. The type `f64` uses the IEEE 754 binary64 format.
Their arithmetic follows IEEE 754.


#### 3.2.2 Vector Types

A *vector type* has 2, 3, or 4 components of one *numeric type*. The corresponding type templates
are `vec2<T>`, `vec3<T>`, and `vec4<T>`.

The *template parameter* `T` accepts a *signed integer type*, *unsigned integer type*, or
*floating-point type*. Each vector template declares `f32` as the default *template argument* for
`T`.


#### 3.2.3 Matrix Types

A *matrix type* has 2, 3, or 4 elements in each of its 2 dimensions. The square matrix templates
are `mat2<T>`, `mat3<T>`, and `mat4<T>`. The non-square matrix templates are `mat2x3<T>`,
`mat2x4<T>`, `mat3x2<T>`, `mat3x4<T>`, `mat4x2<T>`, and `mat4x3<T>`.

The *template parameter* `T` accepts a *signed integer type*, *unsigned integer type*, or
*floating-point type*. Each matrix template declares `f32` as the default *template argument* for
`T`.


#### 3.2.4 String Types

A *string* is an ordered sequence of *RTSL characters*. A *string type* represents *string* values.
A *string literal* produces a *string* value.


#### 3.2.5 Resource Types

A *resource type* describes a *resource entity* rather than an ordinary value. A *resource type*
has no values and cannot be constructed.

The *resource types* are:

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

For a texture resource, `T` is a *numeric type* or *vector type* that specifies how 1 texel is
interpreted. The meaning of `Format` and the second parameters of `rt_uniform` and `rt_storage`
are not defined by this version of the specification.

A function parameter and a non-static data member cannot have a *resource type*. Either may have a
*pointer type* or *reference type* whose *referred-to type* is a *resource type*. A function cannot
return a *resource type* by value.


### 3.3 Array Types

An *array type* contains a fixed positive number of elements of one *element type*. An object of
*array type* is an *array object*. Its elements are distinct objects stored contiguously in
increasing subscript order.

The *element type* is a *complete value type*. A *reference type* may be an *element type*; an
array of *references* contains distinct *references*. A *resource type* and an
*incomplete type* cannot be an *element type*.

The number of elements is the *array bound*. The *array bound* is determined when the
*array type* is formed and is part of the type. An *array bound* that cannot be determined at that
point, or that is not positive, is invalid.

An *array object* is not a *pointer value*. *Array-to-pointer conversion* and subscripting are
defined as *expressions*.


### 3.4 Pointer Types

A *pointer type* may be derived from any type described by this chapter, including an
*array type*, *reference type*, *resource type*, or *incomplete structure type*. A value of
*pointer type* is a *pointer value*.

A *pointer value* may designate an entity of its *referred-to type*. When its *referred-to type*
is a *resource type*, it designates a *resource entity*. When its *referred-to type* is an
*array type* or *reference type*, it designates that *array object* or *reference* rather than an
element or referenced entity.

A *pointer value* may also designate an element of an *array object* whose *element type* is its
*referred-to type*, or the position immediately following the final element. A *pointer value*
designating that final position is a *one-past pointer*.

Values of the same *pointer type* may be copied, assigned, passed to functions, and returned from
functions. The mutability of a pointer variable and the mutability of its designated entity are
independent.

RTSL does not specify or expose the representation of a *pointer value*. An implementation may
materialize, transform, or eliminate that representation while preserving every required pointer
operation.


### 3.5 Reference Types

A *reference type* may be derived from any type described by this chapter, including an
*array type*, *pointer type*, *reference type*, *resource type*, or
*incomplete structure type*. An object of *reference type* is a *reference*.

A *reference* is bound during initialization to an existing entity of its *referred-to type*. It
denotes that entity, cannot be null, and cannot be reseated. When the *referred-to type* is itself a
*reference type*, the outer *reference* is bound to that inner *reference*. Each *reference type*
layer therefore remains a distinct part of the type.

A *reference* may be stored in a variable, array element, or non-static data member. It may also be
passed to or returned from a function. Using a *reference* after the lifetime of the entity to
which it is bound has ended has undefined behavior.

RTSL does not specify or expose the representation of a *reference*. An implementation may
materialize, transform, or eliminate that representation while preserving the binding and every
operation performed through it.


### 3.6 Structure Types

A *structure type* is a nominal *declared type*. One structure entity has one *structure type*.
Distinct structure entities have different types even when their members have identical names,
types, and order.

A *structure declaration* introduces a named *structure type*. The type remains incomplete until
its *structure definition* is processed.

Non-static data members retain their *declaration* order. They are placed at increasing addresses,
and the first non-static data member has offset 0. The target ABI determines member alignment,
padding, offsets, and total structure size without reordering members.


### 3.7 Tuple Types

A *tuple type* is a fresh anonymous nominal *structure type* introduced by one occurrence of tuple
type syntax. Its elements are ordered and may be named or unnamed. Each element becomes one
non-static data member of the anonymous structure in the same position.

Every tuple occurrence introduces a different *tuple type*, even when another occurrence has
identical element types and names. A *type alias declaration* may give one occurrence a reusable
name. Reusing that alias reuses that one *tuple type*.

A *tuple type* has the member access and layout semantics of its anonymous structure. Its elements
retain their *declaration* order. Tuple destructuring is defined as an *expression*.


### 3.8 Complete and Incomplete Types

A *complete type* has all information required to determine the permitted uses of its objects. An
*incomplete type* lacks some of that information.

Every *built-in type*, *pointer type*, and *reference type* is complete. A *pointer type* or
*reference type* is complete even when its *referred-to type* is incomplete. An *array type* is
complete when its *element type* is complete and its *array bound* has been determined. A
*tuple type* is complete when all types required by its non-static data members are complete.

An *incomplete structure type* is a *structure type* whose *structure definition* has not been
processed. It may be the *referred-to type* of a *pointer type* or *reference type*, and it may be
named by a *function declaration*. An operation requiring its members, size, alignment, or layout
is invalid while it remains incomplete.


### 3.9 Type Identity and Compatibility

Each *built-in type* has one identity. Two *array types* are the same type when their
*element types* and *array bounds* are the same. Two *pointer types* or two *reference types* are
the same type when their *referred-to types* are the same.

Each named structure entity and each tuple occurrence has a distinct nominal identity. A
*type alias declaration* preserves the identity of the type it names.

Two types are compatible when they have the same identity. Compatible redeclarations of one entity
use compatible types. Distinct *tuple types* are not compatible and cannot be made compatible by
identical syntax, member names, member types, or layout.



---



## 4. Declarations

A *declaration* introduces or redeclares a language entity. An entity may have multiple compatible
*declarations*. The rules for determining whether *declarations* denote the same entity are part
of *name lookup* and *linkage*.

After the applicable *declaration processing* is complete, each *declaration* is visible throughout
its containing *scope*.


### 4.1 General Form

A *declaration* consists of any applicable *attributes* and *declaration specifiers* followed by
the syntax that names or otherwise introduces its entity. A *declaration specifier* determines the
kind, type, storage, mutability, or external resolution of that entity.

An *object declaration* combines a *type specifier* with an *object declarator*. It declares an
ordinary object or *resource entity*. Variable, non-static data-member, and parameter
*declarations* are *object declarations*. Functions use the separate syntax for a
*function declaration* beginning with `fn`.

Every *definition* contains a *declaration*. A terminating semicolon introduces an entity without
supplying a body or member sequence. Whether an initializer is a *definition* depends on the kind
and storage of the declared entity.

An *external declaration* is a *declaration* containing `extern`. The qualifier states that the
declared entity is supplied outside the current *translation unit*; it is not specific to
resources.


### 4.2 Attributes

An *attribute* is *declaration* metadata introduced by `@`. *Attributes* precede the *declaration*
to which they apply. *Attributes* on a function precede the `fn` *keyword*.

For example:

```rtsl
@stage : vertex
fn main(Point point) -> Vertex;
```

A *stage attribute* declares a function as an entry point for 1 shader stage:

```ebnf
stage-attribute = "@", "stage", ":", stage-name ;
stage-name      = "vertex"
                | "tess_eval"
                | "tess_control"
                | "geometry"
                | "fragment"
                | "compute" ;
```

An *attribute* does not change whether the construct to which it applies is a *declaration* or a
*definition*.


### 4.3 Type Specifiers and Type Names

A *type specifier* identifies the initial type of an *object declaration* or *type-id*. It is a
*built-in type* name, a *type name*, or *tuple type* syntax.

A *type name* is an *identifier* or *qualified name* that denotes a type introduced by a
*declaration*. A *type alias declaration*, *structure declaration*, or applicable
*template specialization* may supply a *type name*.

A *type-id* describes a type without declaring an *identifier*. It consists of a *type specifier*
followed by an optional *abstract declarator*. Function return types, explicit casts, and type
*template arguments* use *type-ids*.

```ebnf
type-id = type-specifier , [ abstract-declarator ] ;
```


### 4.4 Object Declarators

An *object declarator* declares an *identifier* and derives its type from the preceding
*type specifier*. *Pointer declarators*, *reference declarators*, *array declarators*, and
*parenthesized declarators* may be composed.

```ebnf
declarator                 = [ pointer-reference-sequence ] , direct-declarator ;
pointer-reference-sequence = pointer-reference-operator,
                             { pointer-reference-operator } ;
pointer-reference-operator = "*" | "&" ;
direct-declarator          = identifier
                           | "(" , declarator , ")"
                           | direct-declarator , "[" , array-bound , "]" ;
```

The declarator is read outward from its declared *identifier*. An array suffix reads as
"array of", `*` reads as "pointer to", and `&` reads as "reference to". Reading then reaches the
type named by the *type specifier*. Parentheses determine which declarator operator is encountered
next.

The following *declarations* demonstrate the binding order:

| Declaration          | Declared type                   |
| -------------------- | ------------------------------- |
| `Foo value;`         | `Foo`                           |
| `Foo* values[16];`   | array of 16 pointers to `Foo`   |
| `Foo (*values)[16];` | pointer to array of 16 `Foo`    |
| `Foo& values[16];`   | array of 16 references to `Foo` |
| `Foo (&values)[16];` | reference to array of 16 `Foo`  |
| `Foo&* value;`       | pointer to reference to `Foo`   |
| `Foo*& value;`       | reference to pointer to `Foo`   |
| `Foo& & value;`      | reference to reference to `Foo` |

The declarator grammar contains no function suffix and does not declare function-pointer types.


### 4.5 Pointer Declarators

A *pointer declarator* is one `*` in an *object declarator* or *abstract declarator*. It derives a
*pointer type*. Its *referred-to type* is the type reached after that `*` when the declarator is
read outward from its *identifier* or abstract center.

Multiple *pointer declarators* form multiple pointer layers. A *pointer declarator* may be composed
with *reference declarators* and *array declarators*.


### 4.6 Reference Declarators

A *reference declarator* is one `&` in an *object declarator* or *abstract declarator*. It derives a
*reference type*. Its *referred-to type* is the type reached after that `&` when the declarator is
read outward from its *identifier* or abstract center.

Every `&` forms one *reference type* layer. Those layers remain distinct when
*reference declarators* are repeated or composed with *pointer declarators* and
*array declarators*. No reference-collapsing rule changes the type produced by the written
declarators.

Adjacent *reference type* layers are written as separate `&` *punctuators*, as in `Foo& & value`.
Each `&` is a separate *reference declarator*.


### 4.7 Array Declarators

An *array declarator* is a bracketed *array bound* following a *direct declarator*. It derives an
*array type*. Its *element type* is the type reached after that suffix when the declarator is read
outward from its *identifier* or abstract center.

The *array bound* is an integer *expression* evaluable while the *declaration* is processed. Its
value is the element count of the resulting *array type* and is part of that type.

*Array declarators* may be repeated to form nested *array types*. An *array declarator* may also
have a *pointer type* or *reference type* as its *element type*.


### 4.8 Parenthesized and Abstract Declarators

A *parenthesized declarator* encloses an *object declarator* in parentheses. It changes declarator
binding without introducing another type layer.

An *abstract declarator* has the same *pointer declarator*, *reference declarator*,
*array declarator*, and *parenthesized declarator* forms as an *object declarator* but contains no
declared *identifier*:

```ebnf
abstract-declarator        = pointer-reference-sequence
                           | [ pointer-reference-sequence ],
                             direct-abstract-declarator ;
direct-abstract-declarator = "(" , abstract-declarator , ")"
                           | "[" , array-bound , "]"
                           | direct-abstract-declarator,
                             "[" , array-bound , "]" ;
```

For example, these return types are a pointer to an array and a reference to an array:

```rtsl
fn row() -> Foo (*)[16];
fn borrowed_row() -> Foo (&)[16];
```

An *abstract declarator* cannot contain an *identifier* or a function parameter list.


### 4.9 Type Alias and Tuple Declarations

A *type alias declaration* gives a reusable name to the type described by its *type-id*:

```rtsl
using Vertex = (Position position, Data data);
```

A *type alias declaration* has the following form:

```ebnf
type-alias-declaration = "using" , identifier , "=" , type-id , ";" ;
```

*Tuple type* syntax is an ordered, comma-separated sequence of element *declarations* enclosed in
parentheses. An element *declaration* uses a *type specifier* and may contain an
*object declarator* that supplies its name and *derived type*:

```rtsl
(Position position, Data data)
(vec4, vec3)
```

The first form has named elements. The second has unnamed elements. The type identity of each
occurrence and the effect of naming it with a *type alias declaration* are properties of
*tuple types*.

For example, `pos2` and `dim2` name the distinct *tuple types* introduced by their initializers:

```rtsl
using pos2 = (f32, f32);
using dim2 = (f32, f32);
```


### 4.10 Structure Declarations

A *structure declaration* introduces or redeclares a named *structure type* without defining its
members:

```rtsl
struct Foo;
```

Any form of *declaration* may occur as a structure member. Non-function member names are unique
within a structure. Member functions may share a name when they form a valid overload set.

An *access label* has the following form:

```ebnf
access-label = ( "public" | "private" | "protected" ) , ":" ;
```

An *access label* appears between member *declarations*:

```rtsl
struct Foo {
  public:
    fn visible();

  protected:
    fn inherited();

  private:
    i32 value;
}
```

Members preceding the first *access label* are *public members*. Each *access label* applies to the
members that follow it until the next *access label* or the end of the structure. A `public:` label
makes those members *public members*, a `private:` label makes them *private members*, and a
`protected:` label makes them *protected members*.

A *public member* is accessible wherever its name is visible. A *private member* is accessible to
members of its structure. A *protected member* is also accessible to members of a derived
structure.


### 4.11 Variable, Member, and Parameter Declarations

A *variable declaration* declares a variable, its type, and its qualifiers. A
*variable qualifier* changes the storage, mutability, *linkage*, or external resolution of the
declared variable.

A non-static data-member *declaration* and a parameter *declaration* use the same *type specifier*
and *object declarator* syntax. A parameter *declaration* may additionally contain a
*member-contract list*.

```ebnf
variable-declaration = { attribute }, { variable-qualifier }, type-specifier,
                       init-declarator, { ",", init-declarator }, ";" ;
init-declarator       = declarator, [ "=" , initializer ] ;
parameter-declaration = { attribute }, type-specifier, declarator,
                        [ member-contract-list ] ;
```

An unqualified variable has local storage. Each shader invocation has its own instance.

A variable declared `const` cannot be modified after initialization. `const` constrains
mutability rather than selecting a storage region.

A variable declared `uniform` resides in uniform storage. Its value is shared by shader
invocations and is read-only from shader code.

A storage or mutability qualifier on a variable of *pointer type* applies to that variable, not to
an entity designated by its *pointer value*. For example:

```rtsl
uniform Foo* foos;
```

The variable `foos` cannot be assigned from shader code. An assignment through `foos` is permitted
when the designated `Foo` object is modifiable.

A variable declared `storage` resides in storage-backed memory. Its value is shared by shader
invocations and is read-only from shader code.

A variable declared `shared` resides in workgroup-shared storage. Each workgroup has its own
instance.

A *declaration* containing `static` declares storage associated with its containing entity or
restricts its *linkage*, according to the *declaration* context. A static structure member is
accessed through a *qualified name* such as `Foo::bar`.

When `extern` appears in a *variable declaration*, it is a *variable qualifier*.

A *variable declaration* whose type is a *resource type* contains `extern`. A resource-typed
*variable declaration* without `extern` is invalid.

An *external declaration* of a resource-typed variable denotes an *external resource*. The
*external resource* is supplied by the Rutile runtime rather than by a *definition* in another
*translation unit*.

Every *external resource* has an *external resource name*. The name is qualified by every enclosing
namespace and *template specialization* in nesting order. A *qualified name* may be supplied as an
*identifier argument*.

A *variable declaration* of *resource type* may use a structured element type. The tuple occurrence
in this example introduces the anonymous nominal *tuple type* used by the *declaration*:

```rtsl
template<typename T>
extern rt_storage<(Foo foo, Bar bar), T> array;
```


### 4.12 Function Declarations

A *function declaration* begins with `fn`, declares its name and parameters, and may declare a
return *type-id* after `->`:

```rtsl
fn foo(Bar value) const -> Result;
```

Function *attributes* precede `fn`. Function *modifiers* follow the parameter list and precede the
return-type arrow. The function *modifiers* defined by this specification are `const`, `implicit`,
and `emit`.

`const` declares a member function that does not modify the object through which it was called.
`implicit` is valid only on a *constructor declaration*. `emit` declares an *emitting function*.

A *constructor declaration* uses the name of its structure and has no return-type arrow. The
`implicit` modifier may appear on that *declaration*:

```rtsl
fn Foo(Bar value);
fn Foo(Baz value) implicit;
```

A *destructor declaration* declares the behavior associated with ending the lifetime of an object
of its *structure type*. It uses `~` followed by the structure name and does not begin with `fn`:

```rtsl
struct Foo {
    ~Foo();
}
```

An *operator declaration* uses an operator as its declared name. A parameterless `operator()` may
declare a *destructuring operator*:

```rtsl
fn operator() -> FooData;
```


### 4.13 Parameter Contracts

A *contract* constrains a declared entity or supplies semantic information about it. A
*member-contract list* follows a parameter name and assigns *contracts* to named members of the
parameter type:

```ebnf
member-contract-list = "{", member-contract, { ",", member-contract }, "}" ;
member-contract      = ".", identifier, ":", identifier ;
```

For example:

```rtsl
fn shade(Sample sample{ .index : flat, .color : smooth }) -> vec4;
```

An explicitly supplied member *contract* overrides the default *contract* of that member's type
for the parameter *declaration*.


### 4.14 Namespace Declarations

A *namespace declaration* introduces or redeclares a named namespace.


### 4.15 Template Declarations

A *template declaration* is preceded by a *template-parameter clause* and declares a family of
*template specializations*.

```rtsl
template<typename T>
struct Foo;
```

When a templated *declaration* is exported, the *template-parameter clause* precedes `export`:

```rtsl
template<typename T>
export struct Foo;
```


### 4.16 Import and Export Declarations

An *export declaration* contains `export`. A *declaration* is not exported by default.

An *import declaration* has either of the following forms:

```rtsl
import "foo.rtsl";
export import "foo.rtsl";
```

The second form is an *exported import*.



---



## 5. Definitions

A *definition* completely specifies a declared entity. Every *definition* contains a
*declaration*. An entity may have multiple compatible *declarations* but no more than 1
*definition*. A second *definition* of the same entity is invalid.

The *declaration* contained in a *definition* participates in *declaration processing*. Its body,
initializer, or member sequence participates in *definition processing*.


### 5.1 Structure Definitions

A *structure definition* supplies the member sequence of a *structure type*:

```rtsl
struct Foo {
    i32 member;
}
```

The member *declarations* become available before member bodies, initializers, and other
*definition* content are processed. Processing the member sequence completes the
*structure type*.


### 5.2 Variable Definitions

A *variable definition* supplies the initializer or storage required to complete a variable.
An *external declaration* is not a *variable definition*.


### 5.3 Function and Operator Definitions

A *function definition* supplies a function body. A *constructor definition*,
*destructor definition*, and *operator definition* supply the body of the corresponding declared
entity.


### 5.4 Destructors

Every *value type* has a *destructor*. A *destructor* for which no behavior is explicitly defined is
a *trivial destructor*. A *trivial destructor* has no effect and produces no executed operation.

Destruction remains part of the language semantics even when the applicable *destructor* is
trivial.


### 5.5 Template Definitions

A *template definition* supplies a *definition* from which *template specializations* may be
instantiated.


### 5.6 Namespace Definitions

A *namespace definition* supplies the *declaration* and *definition* sequence belonging to a named
namespace.



---



## 6. Expressions and Operators

An *expression* computes a result, identifies an entity, performs an operation, or combines those
effects. Operators, function calls, construction, *conversion*, member access, and assignment are
forms of *expression*.


### 6.1 Construction

A *constructor* initializes an object of its *structure type*. A *constructor* is used only by
explicit construction unless its *declaration* contains `implicit`.


### 6.2 Conversions

A *conversion* changes the type through which an *expression* is used. An *implicit conversion*
requires no *conversion* syntax. An *explicit cast* places a destination *type-id* in parentheses
before its operand:

```rtsl
(Foo)bar
```

A *built-in conversion* may be an *implicit conversion* only when it cannot lose information.
Every other *built-in conversion* requires an *explicit cast*.

Two distinct *tuple types* cannot be converted to one another, either implicitly or explicitly.
This rule applies even when their element types and names are identical.

RTSL defines no *conversion* from a *pointer type* to an integer type. Such a *conversion* is
invalid whether it is implicit or written as an *explicit cast*. The numeric representation of a
*pointer value* is not observable in RTSL.

An integer value may be converted to a *pointer type* by an *explicit cast*. An applicable
*RTSL extension* defines the accepted integer values and the entity designated by each accepted
value. There is no implicit integer-to-pointer *conversion*. After *extension query expressions*
are resolved, an integer-to-pointer *conversion* not supplied by the selected *RTSL transpiler* is
invalid.

*Pointer types*, *pointer values*, and the pointer operations defined by RTSL are core constructs
and do not require an *RTSL extension*. An extension that supplies integer addresses extends how a
*pointer value* enters execution; it does not change the *pointer type* or its operations.


### 6.3 Destructuring

A tuple value *destructures* into its elements in order.

A non-tuple structure is destructurable only when it declares a parameterless
*destructuring operator*. The operator returns the exact *tuple type* produced by destructuring the
structure. A structure without such an operator does not destructure into its data members.

For example:

```rtsl
using FooData = (vec4, vec3);

struct Foo {
    fn operator() -> FooData;
}

fn consume(FooData data);
```

When an *expression* of type `Foo` is supplied where `FooData` is required, its
*destructuring operator* is invoked and the returned `FooData` value is used. No *conversion*
between distinct *tuple types* occurs.


### 6.4 Array-to-Pointer Conversion

Except in a context that operates on an *array object* as a whole, an *expression* that designates
an *array object* is implicitly converted to a *pointer value* designating its first element. This
is the *array-to-pointer conversion*. The *conversion* does not copy any element. An *array object*
cannot be assigned as a whole.

An *array-to-pointer conversion* from an *array type* with *element type* `T` produces a value of
*pointer type* `T*`.


### 6.5 Subscripting

A subscripting *expression* has the form `E1[E2]`. One operand has *pointer type* and the other has
integer type. It has the same behavior as `*((E1) + (E2))`. An *array object* may therefore be
subscripted after its implicit *array-to-pointer conversion*.


### 6.6 Address-Of

Applying unary `&` to an *expression* that designates an object or *resource entity* produces a
*pointer value* designating that entity. When the designated entity has type `T`, the result has
*pointer type* `T*`. The operation does not read the entity and does not perform an
*array-to-pointer conversion* on its operand.

Applying unary `&` to a *reference* produces a *pointer value* designating the entity to which that
*reference* is directly bound. When an outer *reference* is bound to an inner *reference*, the
result has a *pointer type* whose *referred-to type* is the inner *reference type*.


### 6.7 Indirection

Applying unary `*` to a *pointer value* that designates an entity produces an *expression* that
designates that entity. When the entity is an object, reading or modifying that *expression* reads
or modifies the designated object. When the *referred-to type* is a *reference type*, the result
designates that *reference*. When it is a *resource type*, the result designates that
*resource entity* and may be used only by operations defined for that *resource type*.

Indirection through a *one-past pointer* or through a *pointer value* that no longer designates an
entity has undefined behavior.


### 6.8 Pointer Arithmetic

Adding a *pointer value* and an integer `n`, in either operand order, advances by `n` elements of
the *referred-to type*. Subtracting `n` from a *pointer value* retreats by `n` elements. When a
*pointer value* designates element `i` of an *array object*, the result designates element `i + n`
or `i - n`, respectively. The result may instead be the *one-past pointer*. Producing a result
outside that range has undefined behavior.

For pointer arithmetic, an object that is not an element of an *array object* behaves as the only
element of an *array object* of length 1.


### 6.9 Pointer Comparison

Two *pointer values* of the same *pointer type* may be compared for equality. Values that
designate the same storage location compare equal; values that designate different storage
locations compare unequal. Relational comparison orders elements of the same *array object*, with
the *one-past pointer* ordered after every element. A relational comparison has undefined behavior
unless both operands designate elements of, or the position one past, the same *array object*.


### 6.10 Reference Binding and Access

*Reference binding* initializes a *reference* of type `T&` by binding it to an entity of type `T`.
Binding a function parameter and returning a *reference* use the same rule.

If the initializer directly designates a *reference* of type `T`, that *reference* is selected
without accessing the entity to which it is bound. This permits each layer of a nested
*reference type* to bind separately.

Otherwise, an initializer that is a *reference* supplies the entity to which it is directly bound.
This rule is applied again when necessary to reach an entity compatible with `T`.

For example, `outer` binds to the *reference* `inner`, and `pointer` designates `inner`:

```rtsl
Foo object;
Foo& inner = object;
Foo& & outer = inner;
Foo&* pointer = &outer;
```

Except during *reference binding* and the address-of operation, an operation applied to a
*reference* is applied to the entity to which it is directly bound. When that entity is another
*reference*, the same rule applies to the inner reference as required by the operation.

Assignment through a *reference* assigns to the ultimately referenced non-reference entity; it
does not change any *reference binding*. Binding a *reference* to an entity whose lifetime does not
include every use of the *reference* has undefined behavior.



---



## 7. Statements and Control Flow

A *statement* controls execution or evaluates an *expression* for its effects. A *block* is an
ordered sequence of *statements* enclosed by braces and introduces a *block scope*.


### 7.1 Destruction on Scope Exit

Leaving a *block scope* ends the lifetime of the local objects whose storage belongs to that
*block scope*. Their *destructors* are invoked before control finishes leaving the *block scope*.
Objects are destroyed in the reverse order in which their initialization completed.

Whenever control leaves a *block scope*, required destruction occurs before the transfer completes.


### 7.2 Selection Statements

A *selection statement* selects 1 of 2 *statements* according to a condition:

```ebnf
selection-statement = "if", "(", expression, ")", statement
                    | "if", "(", expression, ")", statement, "else", statement ;
```

The condition is contextually converted to a Boolean value. When it is `true`, the first
*statement* is executed. Otherwise, the *statement* following `else` is executed when present.


### 7.3 Emitting Functions

A function that contains an `emit` *statement* has an abstract *emit target*. For an ordinary
function, the *emit target* has the function's return type and is initialized upon entry before
any *statement* in the function body is executed.

Executing `emit expression;` applies `operator<-` to the *emit target* with *expression* as its
argument. Every `emit` *statement* in the function operates on the same *emit target*. The
*emit target* is part of the language semantics but need not be materialized.

Reaching the end of the function returns the *emit target*.

A function declared with `emit` receives its *emit target* from its caller instead of creating a
target of its own. Such a function is implicitly generic over the target type and has an implicit
*reference* parameter bound to the caller's active *emit target*. Calls through multiple
*emitting functions* propagate the same *emit target*.


### 7.4 Named Barriers

A *named barrier* is a workgroup execution barrier. No invocation in the workgroup proceeds beyond
a dynamic occurrence of the *named barrier* until every invocation in that workgroup has reached
the same occurrence. Invocations encounter *named barriers* in the same dynamic order.

Writes to workgroup-shared variables and tessellation-control output variables performed before
the *named barrier* are visible to accesses performed after the *named barrier*.



---



## 8. Templates

A *template specialization* is the member of a template family selected by a particular list of
*template arguments*. A *template argument* supplies a type, value, or *identifier* for the
corresponding *template parameter*.

A *template declaration* may be formed from any *declaration* whose entity depends on
*template parameters*.


### 8.1 Template Parameter Kinds

A *template parameter* is a *type parameter*, a *value parameter*, or an
*identifier parameter*.

A *type parameter* is introduced with `typename` and accepts a type:

```rtsl
typename T
```

A *value parameter* is introduced by a type and an *identifier*. Its argument must be evaluable
while the applicable *template specialization* is determined:

```rtsl
usize N
```

An *identifier parameter* is introduced with `identifier` and accepts an *identifier*:

```rtsl
identifier Name
```

Substitution of an *identifier parameter* replaces its uses in *identifier* positions. The
substituted *identifier* follows the ordinary *scope*, *name lookup*, and collision rules. An
*identifier argument* participates in specialization identity and may be a *qualified name*.

An *identifier parameter* may supply the declared name of an *external resource*:

```rtsl
template<typename T, identifier Name>
extern rt_texture2d<T> Name;
```


### 8.2 Default Template Arguments

A *template parameter* may declare a *default template argument* after `=`:

```rtsl
template<typename T = f32>
struct Foo;
```

When a *template argument* is omitted, its declared default is used. When every
*template parameter* has a default, the template name may be written without a
*template argument list*. In the example above, `Foo` denotes `Foo<f32>`.


### 8.3 Constraints

Any kind of *template parameter* may have a *constraint*. A *constraint* is a Boolean *expression*
written after the parameter and separated from it by `:`:

```rtsl
template<typename T : integral>
fn foo();
```

The *expression* must be evaluable while the substitution is considered. When the *constraint*
names a template, the constrained *template argument* is supplied as the first *template argument*
of that template.

The *associated constraint* of a *template declaration* is the conjunction of its parameter
*constraints*. A *declaration* without parameter *constraints* has no *associated constraint*.

Before satisfaction or ordering is determined, an *associated constraint* is normalized into
conjunctions, disjunctions, and *atomic constraints*. An *atomic constraint* consists of one
*expression* occurrence and the mapping from its *template parameters* to the parameters of the
constrained *declaration*. Expanding a named *constraint* preserves the *expression* occurrences
from the named *constraint's* *definition*.

Two *atomic constraints* are identical only when they originate from the same *expression*
occurrence and have equivalent parameter mappings. *Expressions* that merely produce the same
result are not identical *constraints*.

Substitution replaces the parameters of each *atomic constraint* with the proposed
*template arguments*. If substitution makes the immediate *expression* invalid, that
*atomic constraint* is not satisfied. Otherwise, it is satisfied exactly when its Boolean result
is `true`. The *template declaration* supplies a specialization only when its complete
*associated constraint* is satisfied.

The same syntax applies to *value parameters* and *identifier parameters*:

```rtsl
template<usize N : power_of_two, identifier Name : valid_resource_name>
fn foo();
```


### 8.4 Explicit Specializations

An *explicit specialization* places a *template argument list* after the declared name and
declares exactly the specialization identified by those arguments. A generic
*template declaration* does not place arguments after the declared name.

```rtsl
fn foo<Bar>();
```

An *explicit specialization* does not require a preceding generic *template declaration*. A
matching explicit and generic *declaration* denote the same specialization regardless of their
textual order.


### 8.5 Template Selection

When an *explicit specialization* and a generic *template declaration* apply to the same argument
list, the *explicit specialization* is selected.

*Constraint* subsumption partially orders otherwise matching generic *template declarations*. To
determine whether a *constraint* `P` subsumes a *constraint* `Q`, `P` is placed in disjunctive
normal form and `Q` is placed in conjunctive normal form.

Each disjunctive clause of `P` subsumes each conjunctive clause of `Q` when the disjunctive clause
contains an *atomic constraint* identical to an *atomic constraint* in the conjunctive clause. `P`
subsumes `Q` only when this holds for every such pair of clauses.

A constrained *declaration* is *at least as constrained* as an unconstrained *declaration*. One
constrained *declaration* is *at least as constrained* as another when its *associated constraint*
subsumes the other's *associated constraint*. It is *more constrained* when the reverse relation
does not also hold.

Among otherwise matching generic *template declarations*, the unique *more constrained*
*declaration* is selected. When neither *declaration* is *more constrained*, or more than 1 maximal
*declaration* remains, the use is ambiguous and invalid.


### 8.6 Interface and Concrete Content

An exported generic *template declaration* and its *template definition* are represented in the
*module interface*. The *declarations* of all its *explicit specializations* are represented there
as well. An exported *explicit specialization* is also represented when no matching generic
*template declaration* exists. The *module interface* contains the complete semantic information
required to perform substitution, selection, and instantiation without the original
*RTSL source string*.

A *definition* produced by instantiating or explicitly defining a *template specialization* is
*concrete content*. A serialized instance of that content may occur in an *object artifact* or
*library artifact*. The generic *template definition* remains only in the *module interface* and
its serialized *module-interface artifact*.

An *explicit specialization* is declared and visible before the specialization is instantiated.
This rule prevents an already instantiated generic *definition* from later being replaced by an
*explicit specialization*.


### 8.7 Instantiation

A *template specialization* has 1 identity throughout a linked set of artifacts, regardless of
where it is required. It is semantically instantiated no more than once in that set.

The compiler instantiates every *template specialization* required by a compiled
*translation unit*. Every *declaration* required by an instantiation resolves to a *definition*
before that instantiation completes.

When a required specialization already has *concrete content*, the compiler reuses that content.
Otherwise, it instantiates the specialization from the applicable *template definition* in a
*module interface*. A later compilation may instantiate additional specializations from that
interface. A linker may merge duplicate instances of the same specialization. The
*RTSL transpiler* does not instantiate *template specializations*.

An implementation may perform any work during compilation when doing so does not change the
required compilation artifacts or observable program behavior. RTSL does not classify variables
or values according to whether an implementation happens to evaluate them during compilation.



---



## 9. Modules

A *module* is the semantic content obtained from one *translation unit*. It consists of a
*module interface* and *concrete content*.


### 9.1 Scopes and Names

A *scope* is a region in which a *declaration* may introduce a name. A *translation unit* has a
*translation-unit scope*; it does not implicitly declare a namespace. *Namespace definitions*,
*structure definitions*, *template-parameter clauses*, functions, and *blocks* introduce their
respective *scopes*.

A *declaration* introduces its name into its containing *scope*. *Definitions* do not create a
second entity for a name already declared.

A *qualified name* identifies an entity through its enclosing named *scopes* or
*template specializations*. Unqualified and *qualified names* use the same declared entities and
differ only in how *name lookup* begins.


### 9.2 Name Lookup

*Name lookup* determines which *declaration* a name denotes. Member functions, *constructors*,
operators, static members, and nested types use the *declarations* of the structure *scope*. A
member may refer to any member *declaration* in the same structure, including one that occurs later
in the source.

*Declarations* reached through more than 1 import path continue to denote the same entity. A set
of incompatible *declarations* is invalid.


### 9.3 Linkage

*Linkage* determines whether *declarations* in different *translation units* denote the same
entity. Export does not change *linkage*.

A function or variable declared directly in *translation-unit scope* has *external linkage* unless
declared `static`. A function or variable declared `static` in that *scope* has *internal linkage*
and cannot be named from another *translation unit*.

An *external declaration* of a non-resource function or variable resolves to a matching
*definition* with *external linkage*. That *definition* may occur in another *translation unit*
without being exported or imported.


### 9.4 Module Interfaces and Concrete Content

A *module interface* is the semantic content exported by a *translation unit*, including content
re-exported by its imports. It contains exported *declarations* and the *template declarations* and
*template definitions* classified as part of that interface by the template rules. It contains no
non-template function body, variable initializer, or source text.

*Concrete content* is the processed semantic content of concrete *definitions* belonging to the
*translation unit*. It includes non-template *definitions* and instantiated or explicitly defined
*template specializations*. It does not include a generic *template definition*.


### 9.5 Exports and Imports

An *export declaration* contributes its declared entity to the *module interface*.

The *string* produced by the *string literal* in an *import declaration* is the exact
*canonical name* of the imported *translation unit*. The import makes that unit's
*module interface* visible throughout the importing *translation unit*. A *string* that does not
identify a supplied *translation unit* is invalid.

An *exported import* re-exports the complete imported *module interface*. An ordinary import affects
visibility in the importing *translation unit* but does not re-export imported content.


### 9.6 Circular Dependencies

Dependencies between *import declarations* may contain cycles, including cycles formed by
*exported imports*. A cycle does not by itself make a program invalid.

Each *module interface* in a cycle contains its directly exported content and the content
re-exported through its *exported imports*. A *declaration* reached through more than 1 import path
remains the same *declaration*. Conflicting *declarations* and unresolved names remain invalid.

For example:

```rtsl
// foo.rtsl
export import "bar.rtsl";
export struct Foo {}
export fn foo(Bar bar);
```

```rtsl
// bar.rtsl
export import "foo.rtsl";
export struct Bar {}
export fn bar(Foo foo);
```

Each *exported import* re-exports the other *module interface*. Both *module interfaces* therefore
contain both structures and both functions.



---



## 10. Shader Stages and Interfaces

A *shader-stage entry point* is a function whose *declaration* contains a *stage attribute*. The
*attribute* determines the stage implemented by that function.

Functions for different shader stages may share an *identifier*. Each overload carries the
*stage attribute* for the stage it implements.


### 10.1 Stage Interfaces

The parameters of a *shader-stage entry point* form its *stage input interface*. Its return type
forms its *stage output interface*. For an *emitting function*, the return type is the type of its
*emit target* and therefore determines the *stage output interface*.

The *stage output interface* of each selected graphics stage matches the *stage input interface*
of the next selected graphics stage. The selected and fully instantiated interfaces must be
compatible. An incompatible stage composition is invalid.

The *RTSL transpiler* derives target-specific stage configuration and limits from the selected
interfaces. This includes a maximum vertex count represented by the output type of a
*shader-stage entry point* for the *geometry stage*.


### 10.2 Stage Order

The graphics-stage order is the *vertex stage*, *tessellation-control stage*,
*tessellation-evaluation stage*, *geometry stage*, and *fragment stage*. The stages selected for an
entry-point *identifier* retain their relative order. Adjacent selected stages communicate through
their *stage output interfaces* and *stage input interfaces*.

A *compute-stage entry point* has 3 `usize` parameters that receive the workgroup sizes. The
*compute stage* is separate from the graphics-stage order.


### 10.3 Standard Stage Types

`Position` is the standard vertex-position output type:

```rtsl
struct Position {
    vec4 value;
    f32 point_size;
    f32* clip_distance;
    f32* cull_distance;
}
```

`TriangleStrip<T, N>` is a bounded triangle-strip output type. `T` is its vertex type and `N` is
the maximum number of vertices it can contain. Applying `operator<-` appends a vertex. `size()`
returns the number of appended vertices.

```rtsl
template<typename T, usize N>
struct TriangleStrip {
    fn operator<-(T value);
    fn size() const -> usize;

  private:
    T _vertices[N];
    usize _size;
}
```

A *shader-stage entry point* for the *geometry stage* that returns `TriangleStrip<T, N>` declares
`N` as its maximum output vertex count. Values emitted by the function are appended to that output
object. If one invocation attempts to append more than `N` vertices, its behavior is undefined.


### 10.4 Rasterization Contracts

A *rasterization contract* supplies interpolation information for a stage-interface value. A
floating-point scalar or *vector type* has the default *rasterization contract* `smooth`. An
integer scalar or *vector type* has the default *rasterization contract* `flat`.

A *structure type* is not rasterized as one value and has no direct *rasterization contract*. Its
members contribute their own values to the stage interface.

The *contracts* `flat` and `smooth` select flat and smooth interpolation, respectively. A
*member-contract list* on a parameter in a *stage input interface* overrides the default
*contracts* of its named members. A named member whose type has no *rasterization contract* cannot
receive an override.


### 10.5 Stage Selection

The compiler retains every *shader-stage entry point* in its *object artifact*. Linking retains
those entry points in the resulting *library artifact*. Neither operation selects the entry points
that become backend shaders.

The selected *RTSL transpiler* is backend-specific. It receives a *library artifact*, an
entry-point *identifier*, and a nonempty set of requested shader stages. For each requested stage,
it selects the *shader-stage entry point* with that *identifier* and stage. A requested stage with
no match or more than 1 match is invalid.

Every selected entry point and every entity required by its *definition* is fully instantiated
before transpilation begins. The *RTSL transpiler* validates the selected interfaces and produces a
separate backend shader for each selected stage.



---



## 11. RTSL IR and Artifacts

*RTSL intermediate representation*, abbreviated *RTSL IR*, is the binary, typed,
target-independent representation defined by this chapter. An *RTSL artifact* is one complete
serialized *RTSL IR* word stream.

RTSL defines *object artifacts*, *module-interface artifacts*, and *library artifacts*. Their
conventional file-name extensions are `.rto`, `.rtm`, and `.rtl`, respectively. RTSL defines no
program or executable artifact.


### 11.1 Representation Model

*Static single-assignment form*, abbreviated *SSA*, represents each computed result with one
definition and refers to that result by ID. *Concrete IR* represents executable semantics as typed
*SSA* control flow. Objects that require storage are accessed through explicit operations, and
each function body is a control-flow graph of *basic blocks*. *Object artifacts* and
*library artifacts* contain *concrete IR*.

*Parameterized IR* represents exported declarations, constraints, and generic
*template definitions*. A *template parameter* may occur where its corresponding type, value, or
identifier will occur after substitution. *Module-interface artifacts* contain
*parameterized IR* and no ordinary non-template function body or variable initializer.

Constructors, destructors, operators, methods, reference access, and `emit` behavior are lowered
to the instructions defined by this chapter before *concrete IR* is serialized. Constructors,
destructors, operators, and methods use ordinary function instructions after lowering. Required
destructor calls and `emit`-target operations are explicit calls in *concrete IR*.

*RTSL IR* retains abstract RTSL types. It does not assign physical registers, addresses,
descriptor sets, binding numbers, backend locations, or target layouts. It is not executable and
is never supplied directly to a graphics or compute backend.


### 11.2 Physical Encoding

An *RTSL word* is an unsigned 32-bit integer. An *RTSL artifact* is a nonempty sequence of
*RTSL words*. When stored as bytes, every *RTSL word* is encoded in little-endian byte order. The
byte length of an artifact is therefore a positive multiple of 4.

A partial final word and any bytes following the final complete instruction are invalid.

The first 6 *RTSL words* form the *artifact header*:

| Word | Contents                                                   |
| ---: | ---------------------------------------------------------- |
|    0 | Magic number `0x4c535452`                                  |
|    1 | Major version in bits 31–16; minor version in bits 15–0    |
|    2 | Patch version in bits 31–16; snapshot version in bits 15–0 |
|    3 | Artifact kind                                              |
|    4 | Exclusive upper bound for *result IDs*                     |
|    5 | Reserved; must be `0`                                      |

The initial *RTSL IR version* is `0.1.0.0`. A reader accepts an artifact only when its major and
minor fields exactly equal the major and minor fields supported by the reader. Patch and snapshot
differences are accepted.

A patch or snapshot revision does not add, remove, or reinterpret an *opcode*, operand, enumerant,
validation rule, or physical field. A change to any of those requires a new minor version. A
change that is not compatible with the representation model requires a new major version.

The word following the *artifact header* is the first instruction. Instructions continue without
gaps or an offset directory until the end of the artifact. The format has no physical sections.


### 11.3 Header and Compatibility

The *artifact-kind enumerant* in header word 3 is:

| Value | Artifact kind               | Extension |
| ----: | --------------------------- | --------- |
|     1 | *object artifact*           | `.rto`    |
|     2 | *module-interface artifact* | `.rtm`    |
|     3 | *library artifact*          | `.rtl`    |

Every other value is invalid.

Header word 4 is the *ID bound*. Every nonzero *RTSL ID* in the artifact is less than the
*ID bound*. An artifact with an *ID bound* of 0 is invalid. The *ID bound* need not equal 1 more
than the greatest used ID.

The following compatibility results apply:

| Artifact version | Reader version | Result   |
| ---------------- | -------------- | -------- |
| `0.1.0.0`        | `0.1.0.0`      | accepted |
| `0.1.7.3`        | `0.1.0.0`      | accepted |
| `0.1.0.0`        | `0.1.9.4`      | accepted |
| `0.0.1.0`        | `0.1.0.0`      | rejected |
| `0.2.0.0`        | `0.1.0.0`      | rejected |
| `1.1.0.0`        | `0.1.0.0`      | rejected |


### 11.4 Instructions, IDs, and Literals

An *RTSL instruction* is a contiguous sequence of *RTSL words*. Bits 31–16 of its first word are
its *word count*. Bits 15–0 are its *opcode*. The *word count* includes the first word and is at
least 1. A count that extends beyond the artifact is invalid.

Each *opcode* fixes whether the instruction has a *result type ID*, *result ID*, ID operands,
literal operands, or a variable-length operand sequence. Operand words occur in the order shown in
the instruction tables. An *opcode* not defined by this specification is invalid.

An *RTSL ID* is an unsigned 32-bit instruction operand. ID 0 denotes the absence of an optional ID
and is never a *result ID*. Every nonzero ID denotes exactly 1 result within its artifact. A
*result ID* is defined once. Except for the permitted forward references in Section 11.5, an ID is
defined before use.

An ID operand is nonzero unless its instruction explicitly permits ID 0.

An integer literal wider than 32 bits occupies consecutive words from least significant to most
significant. A floating-point literal contains its IEEE representation and uses the same word
order. Unused high bits in the last word of a literal are zero.

`OpString` defines a *string ID*. Its operands are a *result ID*, total byte count, chunk byte
count, and the chunk's UTF-8 bytes packed 4 per word. The first byte occupies bits 7–0. Bytes after
the chunk are zero padding. Its *word count* is 4 plus the chunk byte count rounded up to words.

When the complete string does not fit in 1 instruction, one or more `OpStringContinued`
instructions immediately follow. Each contains the original *string ID*, a chunk byte count, and
packed bytes. The sum of all chunk byte counts equals the total byte count. A continuation after
the declared count has been reached, an intervening instruction, invalid UTF-8, or nonzero padding
is invalid. A continuation's *word count* is 3 plus its chunk byte count rounded up to words. A
continuation chunk is nonempty. An empty string uses an `OpString` chunk byte count of 0.

Each chunk byte count is no greater than the capacity implied by the instruction's 16-bit
*word count*, and the *word count* equals the fixed operands plus the exact number of packed-byte
words.

`OpNop` has *opcode* 0, a *word count* of 1, and no operands. `OpString` has *opcode* 1.
`OpStringContinued` has *opcode* 2. *Opcodes* 12–31, 46–63, 70–95, 107–127, 147–191, and every
*opcode* greater than 204 are reserved and invalid in version `0.1`.

`OpNop` may occur between complete instructions and has no semantic effect. It does not interrupt
the logical order, but it cannot occur between an `OpString` and its required continuation.

The notation used below is:

- `type-id` — an ID defined by a type instruction;
- `result-id` — the ID defined by the instruction;
- `string-id` — an ID defined by `OpString`;
- `symbol-id` — an ID defined by `OpSymbol`;
- `module-id` — an ID defined by `OpModule`;
- `value-id` — an ID that denotes a typed value;
- `block-id` — an ID defined by `OpBlock`;
- `literal` — one unsigned literal word;
- `...` — repetition determined by a preceding count.


### 11.5 Logical Instruction Order

An *RTSL artifact* contains instructions in this order:

1. `OpString` groups;
2. `OpModule` and `OpImport` instructions;
3. `OpSymbol` and `OpAlias` instructions;
4. type instructions;
5. constant instructions;
6. contract and reflection instructions;
7. template instructions in a *module-interface artifact*;
8. global `OpVariable` instructions in an *object artifact* or *library artifact*;
9. function instruction sequences.

Within item 6, `OpContract` instructions precede `OpStageInterface`, `OpStageElement`,
`OpResource`, and `OpEntryPoint` instructions, in that order.

Within item 2, every `OpModule` precedes every `OpImport`.

Instructions nested between `OpTemplate` and `OpTemplateEnd` remain together in item 7, including
their parameterized type, constant, variable, and function instructions. A function-local
`OpVariable` remains within its function sequence in item 9.

All strings, modules, and symbols are defined before use. A symbol contains its complete
*canonical type encoding* and therefore does not forward-reference an artifact-local type ID.

A type is normally defined before use. To represent a recursive nominal member type, the
referred-to type of `OpTypePointer` or `OpTypeReference`, or the member type of `OpTypeStruct`, may
forward-reference an `OpTypeStruct` result. The referenced result is defined before the first
constant, reflection, template, variable, or function instruction. The ordinary completeness
rules still determine whether the member type itself is valid.

An `OpBranch`, `OpBranchConditional`, `OpSelectionMerge`, or `OpLoopMerge` may reference a later
block in the same function. An `OpPhi` may reference a value defined in a later block when that
value is paired with that block. Calls may reference a declared `OpSymbol` for which the artifact
contains no definition. Every forward-referenced ID is defined in its required region before the
artifact ends. No other forward reference is permitted.


### 11.6 Enumerations

The *symbol-kind enumerants* are:

| Value | Kind     |
| ----: | -------- |
|     0 | type     |
|     1 | variable |
|     2 | function |
|     3 | template |

The *linkage-kind enumerants* are `0` for no linkage, `1` for internal linkage, and `2` for
external linkage.

The *stage-kind enumerants* are:

| Value | Stage                   |
| ----: | ----------------------- |
|     0 | vertex                  |
|     1 | tessellation control    |
|     2 | tessellation evaluation |
|     3 | geometry                |
|     4 | fragment                |
|     5 | compute                 |

The *interface-role enumerants* are `0` for input and `1` for output. The
*resource-access enumerants* are `0` for read-only, `1` for write-only, and `2` for read-write.

The *storage-kind enumerants* are:

| Value | Storage          |
| ----: | ---------------- |
|     0 | function-local   |
|     1 | static           |
|     2 | uniform          |
|     3 | storage-backed   |
|     4 | workgroup-shared |

The *resource-kind enumerants* are:

| Value | Resource type family |
| ----: | -------------------- |
|     0 | `rt_sampler`         |
|     1 | `rt_image1d`         |
|     2 | `rt_image2d`         |
|     3 | `rt_image3d`         |
|     4 | `rt_texture1d`       |
|     5 | `rt_texture2d`       |
|     6 | `rt_texture3d`       |
|     7 | `rt_uniform`         |
|     8 | `rt_storage`         |

The *contract-kind enumerants* are `0` for no contract, `1` for `smooth`, and `2` for `flat`.
The *member-access enumerants* are `0` for public, `1` for protected, and `2` for private.

The *unary-operation enumerants* are:

| Value | Operation               |
| ----: | ----------------------- |
|     0 | floating negation       |
|     1 | signed-integer negation |
|     2 | logical negation        |
|     3 | bitwise complement      |

The *binary-operation enumerants* are:

| Values | Operations, in value order                                                             |
| ------ | -------------------------------------------------------------------------------------- |
| 0–4    | floating add, subtract, multiply, divide, remainder                                    |
| 5–11   | integer add/subtract/multiply, signed/unsigned divide, signed/unsigned remainder       |
| 12–16  | vector-scalar, matrix-scalar, matrix-vector, matrix-matrix multiplication, dot product |
| 17–22  | ordered floating equal, not equal, less, less-or-equal, greater, greater-or-equal      |
| 23–24  | integer equal, not equal                                                               |
| 25–28  | signed less, less-or-equal, greater, greater-or-equal                                  |
| 29–32  | unsigned less, less-or-equal, greater, greater-or-equal                                |
| 33–34  | logical and, logical or                                                                |
| 35–37  | bitwise and, bitwise or, bitwise exclusive-or                                          |
| 38–43  | pointer equal, not equal, less, less-or-equal, greater, greater-or-equal               |

The *conversion-kind enumerants* are:

| Value | Conversion                    |
| ----: | ----------------------------- |
|     0 | floating to unsigned integer  |
|     1 | floating to signed integer    |
|     2 | signed integer to floating    |
|     3 | unsigned integer to floating  |
|     4 | signed-integer width change   |
|     5 | unsigned-integer width change |
|     6 | floating-point width change   |

The *symbol flags* are a bit mask:

|  Bit | Meaning                         |
| ---: | ------------------------------- |
|    0 | a definition is present         |
|    1 | exported                        |
|    2 | declared `extern`               |
|    3 | declared `const`                |
|    4 | constructor declared `implicit` |
|    5 | function declared `emit`        |
|    6 | constructor                     |
|    7 | destructor                      |
|    8 | operator                        |
|    9 | declared `static`               |

All unassigned enumerant values and flag bits are invalid.


### 11.7 Symbols and Canonical Link Names

`OpModule` has *opcode* 3 and the operands:

```text
result-id, canonical-name-string-id, content-present-literal
```

The final operand is 1 when the artifact contains content owned by that module and 0 when the
instruction records only a referenced module identity. Every other value is invalid.

`OpImport` has *opcode* 4 and the operands:

```text
importing-module-id, imported-module-id, exported-literal
```

The final operand is 0 for an ordinary import and 1 for an exported import. Both *module IDs* are
defined by `OpModule`, and their *canonical names* are different.

No 2 `OpModule` instructions in one artifact have the same *canonical name*. An `OpImport` records
one directed import edge and does not copy or flatten the imported module's symbols.

`OpSymbol` has *opcode* 5 and the operands:

```text
result-id, module-id, link-name-string-id, display-name-string-id,
symbol-kind, linkage-kind, flags, type-byte-count, packed-type-bytes...
```

The display-name ID may be 0 only when no later operation requires the source spelling. The packed
type bytes use the *canonical type encoding* defined below. Padding bytes are zero.

A template symbol records the complete type of the entity declared by the template. A namespace
template has a type-byte count of 0. Every non-template symbol has a nonempty type encoding.

`OpAlias` has *opcode* 6 and the operands:

```text
module-id, display-name-string-id, flags, path-component-count,
path-component-string-id..., type-byte-count, packed-type-bytes...
```

The path is the alias's complete qualified name. Its final component equals its display name. The
only flag defined for `OpAlias` is bit 1, which records export. Its packed type bytes use the same
encoding and zero-padding rules as `OpSymbol`.

A *canonical symbol key* is a byte sequence containing, in order:

1. one byte containing the *symbol-kind enumerant*;
2. one byte containing the *linkage-kind enumerant*;
3. one byte stating whether a module name follows;
4. the module's *canonical name* when the preceding byte is 1;
5. a `u32` path-component count and that many strings;
6. a `u32` template-argument count and that many *template arguments*;
7. for a function or function template, a `u32` parameter count and that many
   *canonical type encodings*.

The template-argument sequence contains the arguments of every enclosing
*template specialization* followed by the entity's own arguments, in nesting order.

A string within a *canonical symbol key* is a little-endian `u32` byte count followed by that many
UTF-8 bytes. Integers in the key are little-endian. A symbol path contains at least 1 nonempty
component. The key ends immediately after the fields required by its symbol kind and, for a
template symbol, the kind of entity declared by the template. Trailing bytes are invalid.

The module-name-present byte is 1 for an entity with internal linkage and for a type or template
whose identity belongs to a module. It is 0 for a function or variable with external linkage.

A *canonical link name* is the characters `_RT` followed by 2 lowercase hexadecimal characters
for every byte of the *canonical symbol key*, in byte order. A *canonical link name* contains no
other characters. Two symbols are link candidates exactly when their *canonical link names* are
identical. Their complete symbol records are then required to be compatible.

The key decoded from an `OpSymbol` link-name string has the same *symbol kind* and *linkage* as the
record. Its module field follows the module-presence rule and, when present, equals the
*canonical name* of the owning module. Its function parameter encodings equal the parameters of
the record's *complete type*. Any mismatch is invalid.

A return type and function modifiers do not occur in the *canonical symbol key*. They occur in the
*complete type* and flags of `OpSymbol`, so incompatible declarations collide and are diagnosed
rather than becoming different overloads.

The *canonical type encoding* begins with 1 byte containing one of these tags:

|  Tag | Type                    | Following data                                                  |
| ---: | ----------------------- | --------------------------------------------------------------- |
|    0 | void                    | none                                                            |
|    1 | Boolean                 | none                                                            |
|    2 | signed integer          | `u16` bit width                                                 |
|    3 | unsigned integer        | `u16` bit width                                                 |
|    4 | floating point          | `u16` bit width                                                 |
|    5 | `usize`                 | none                                                            |
|    6 | string                  | none                                                            |
|    7 | vector                  | `u32` count, element type                                       |
|    8 | matrix                  | `u32` rows, `u32` columns, element type                         |
|    9 | array                   | `u64` bound, element type                                       |
|   10 | pointer                 | referred-to type                                                |
|   11 | reference               | referred-to type                                                |
|   12 | named structure         | canonical module string, canonical type-symbol link-name string |
|   13 | tuple                   | canonical module string, `u32` tuple-occurrence index           |
|   14 | function                | return type, `u32` parameter count, parameter types             |
|   15 | resource                | *resource kind*, `u32` argument count, arguments                |
|   16 | template type parameter | zero-based parameter index                                      |

A qualified-name path is a `u32` component count followed by that many strings. The link name in a
named-structure encoding identifies the type symbol, including any *template arguments*. A
tuple's index is its zero-based position among tuple-type occurrences in the preprocessed
*translation unit*.

Every string embedded in a *canonical type encoding* uses a little-endian `u32` byte count
followed by that many UTF-8 bytes.

A type named through a *type alias declaration* uses the encoding of the aliased type. The alias
name does not participate in type identity.

A resource or *template argument* begins with 1 byte: 0 for a type, 1 for a value, or 2 for an
identifier. A type argument contains a *canonical type encoding*. A value argument contains its
*canonical type encoding* followed by a `u32` byte count and its *canonical constant encoding*. An
identifier argument contains a qualified-name path.

A *canonical constant encoding* uses 1 byte for a Boolean. An unsigned integer uses its exact type
width in little-endian order, and a signed integer uses its exact type width in little-endian
two's-complement form. A floating-point value uses its IEEE bit pattern in little-endian byte order.
A *string* uses its UTF-8 bytes without padding, and a `usize` value uses an unsigned 64-bit
little-endian encoding. A composite contains, in element or member order, a `u32` byte count
followed by the *canonical constant encoding* of each constituent. `OpUndef` and types without a
form in this paragraph have no *canonical constant encoding* and cannot supply a value
*template argument*.

For example, an external function named `foo` with 1 `i32` parameter begins with a key containing
*symbol kind* 2, *linkage* 2, no module, the single path component `foo`, no
*template arguments*, and the *canonical type encoding* of `i32`. Its complete
*canonical link name* is:

```text
_RT0202000100000003000000666f6f0000000001000000022000
```

Changing the parameter to `f32` gives the overload this name:

```text
_RT0202000100000003000000666f6f0000000001000000042000
```

The following additional examples use the *canonical name* `a.rtsl`.

The internal variable `cache` has this *canonical link name*:

```text
_RT01010106000000612e7274736c0100000005000000636163686500000000
```

The named type `ns::Foo` has this *canonical link name*:

```text
_RT00000106000000612e7274736c02000000020000006e7303000000466f6f00000000
```

Tuple occurrence 3 has this identity string:

```text
_RT0d06000000612e7274736c03000000
```

The type specialization `Box<i32>` has this *canonical link name*:

```text
_RT00000106000000612e7274736c0100000003000000426f780100000000022000
```

The tuple string is the `_RT` prefix followed by the hexadecimal *canonical type encoding* with tag
13. A *template specialization* places its type, value, and identifier arguments in the
template-argument sequence before any function parameter types.


### 11.8 Type and Constant Instructions

The type instructions are:

| *Opcode* | Instruction       | Operands after the first word                         |
| -------: | ----------------- | ----------------------------------------------------- |
|       32 | `OpTypeVoid`      | `result-id`                                           |
|       33 | `OpTypeBool`      | `result-id`                                           |
|       34 | `OpTypeInt`       | `result-id, bit-width, signed-literal`                |
|       35 | `OpTypeFloat`     | `result-id, bit-width`                                |
|       36 | `OpTypeUSize`     | `result-id`                                           |
|       37 | `OpTypeString`    | `result-id`                                           |
|       38 | `OpTypeVector`    | `result-id, element-type-id, component-count`         |
|       39 | `OpTypeMatrix`    | `result-id, element-type-id, row-count, column-count` |
|       40 | `OpTypeArray`     | `result-id, element-type-id, bound-low, bound-high`   |
|       41 | `OpTypePointer`   | `result-id, referred-to-type-id`                      |
|       42 | `OpTypeReference` | `result-id, referred-to-type-id`                      |
|       43 | `OpTypeStruct`    | see below                                             |
|       44 | `OpTypeFunction`  | see below                                             |
|       45 | `OpTypeResource`  | see below                                             |

`OpTypeInt` accepts widths 8, 16, 32, and 64. Its final operand is 0 for unsigned and 1 for
signed. `OpTypeFloat` accepts widths 32 and 64. A vector or matrix element type is defined by
`OpTypeInt` or `OpTypeFloat`. A vector component count and both matrix dimension counts are from 2
through 4.

The 2 bound operands of `OpTypeArray` form one unsigned 64-bit value, with the low-order word
first. The bound is positive and representable by `usize`. Pointer and reference type instructions
retain the referred-to type exactly, including another pointer, reference, array, resource, or
incomplete nominal structure type.

`OpTypeStruct` has the operands:

```text
result-id, identity-string-id, tuple-literal, member-count,
member-name-string-id, member-access, member-type-id, ...
```

The identity string is the named type's *canonical link name* or the `_RT` hexadecimal encoding of
the tuple's *canonical type encoding*. The tuple operand is 1 for a *tuple type* and 0 otherwise. A
member name may be 0 only for an unnamed tuple element. Members occur in declaration order. Two
structure type instructions with the same identity string have identical tuple flags and member
sequences.

`OpTypeFunction` has the operands:

```text
result-id, return-type-id, parameter-count, parameter-type-id...
```

The return type is `OpTypeVoid` for a function without a returned value.

`OpTypeResource` has the operands:

```text
result-id, resource-kind, argument-count, argument...
```

Each argument begins with an argument kind. Kind 0 is followed by a type ID. Kind 1 is followed by
a type ID, a byte count, and that many bytes of *canonical constant encoding* packed into words.
Kind 2 is followed by an ID denoting an identifier argument. In *concrete IR*, that ID is a
*string ID* containing the components of its *qualified name* joined by `::`, without whitespace.
In *parameterized IR*, it may instead be an identifier-parameter or dependent-name ID. Padding
bytes are zero. The argument kinds and count match the selected resource family. A concrete
resource type contains no unresolved *template argument*.

The constant instructions are:

| *Opcode* | Instruction           | Operands after the first word            |
| -------: | --------------------- | ---------------------------------------- |
|       64 | `OpConstantBool`      | `type-id, result-id, value-literal`      |
|       65 | `OpConstantInt`       | `type-id, result-id, value-words...`     |
|       66 | `OpConstantFloat`     | `type-id, result-id, value-words...`     |
|       67 | `OpConstantString`    | `type-id, result-id, string-id`          |
|       68 | `OpConstantComposite` | `type-id, result-id, count, value-id...` |
|       69 | `OpUndef`             | `type-id, result-id`                     |

The *word count* of an integer or floating constant is fixed by its type. A signed integer uses
two's-complement representation. A Boolean value literal is 0 or 1. Every constituent of a
composite constant is a constant whose type matches the corresponding component or member.


### 11.9 Module-Interface Instructions

*Opcodes* 96 through 106 occur only in a *module-interface artifact*.

| *Opcode* | Instruction                     | Operands after the first word                                |
| -------: | ------------------------------- | ------------------------------------------------------------ |
|       96 | `OpTemplate`                    | `result-id, module-id, symbol-id, parameter-count`           |
|       97 | `OpTemplateTypeParameter`       | `result-id, name-string-id, default-type-id`                 |
|       98 | `OpTemplateValueParameter`      | `type-id, result-id, name-string-id, default-value-id`       |
|       99 | `OpTemplateIdentifierParameter` | `result-id, name-string-id, default-name-id`                 |
|      100 | `OpTemplateConstraint`          | `template-id, expression-symbol-id`                          |
|      101 | `OpTemplateSpecialization`      | see below                                                    |
|      102 | `OpDependentName`               | `result-id, base-id, component-count, component-id...`       |
|      103 | `OpDependentCall`               | `type-id, result-id, callee-id, argument-count, value-id...` |
|      104 | `OpDependentConstruct`          | `type-id, result-id, argument-count, value-id...`            |
|      105 | `OpDependentMember`             | `type-id, result-id, base-id, member-name-id`                |
|      106 | `OpTemplateEnd`                 | `template-id`                                                |

A missing default is ID 0. An identifier name operand denotes either a *string ID* or an
`OpTemplateIdentifierParameter` result as required by the instruction.

`OpTemplate` is immediately followed by exactly its declared number of template-parameter
instructions, in parameter order. Their *result IDs* are the parameter IDs. Each default refers only
to an earlier result. An `OpTemplateConstraint` references a function symbol whose return type is
Boolean. Its parameterized definition computes the constraint by using the applicable
template-parameter IDs.

`OpTemplateSpecialization` has the operands:

```text
result-id, template-id, symbol-id, argument-count,
argument-kind, argument-id, ...
```

Argument kind 0 denotes a type ID, 1 denotes a constant ID, and 2 denotes a string or identifier
parameter ID. The template ID is 0 for an explicit specialization that has no matching generic
template declaration in the artifact.

Instructions between `OpTemplate` and its matching `OpTemplateEnd` are *parameterized IR*. The
parameter results may be used by type, constant, symbol, dependent, and function instructions in
that range. An `OpDependentName` base ID is 0 for an unqualified name and otherwise denotes a
symbol, type, *template parameter*, or another dependent name. Its components are *string IDs* or
identifier-parameter IDs. `OpDependentName`, `OpDependentCall`, `OpDependentConstruct`, and
`OpDependentMember` defer the corresponding lookup or selection until substitution. Every other
operation has the same meaning and validation rules as in *concrete IR* after replacing its
*template parameters*.

Every `OpTemplate` has exactly 1 matching `OpTemplateEnd`. Nested template ranges are properly
nested, and a parameter result is used only within its declaring range and nested ranges.

A *module-interface artifact* contains all information required to normalize and evaluate
constraints, select a declaration, substitute arguments, and emit *concrete IR* without the
original *RTSL source string*.

The result of `OpTemplateTypeParameter` may be used as a `type-id` within its template. In a
*canonical type encoding*, tag 16 followed by the parameter's zero-based index denotes that type.


### 11.10 Concrete SSA Instructions

The data and value instructions are:

| *Opcode* | Instruction            | Operands after the first word                                          |
| -------: | ---------------------- | ---------------------------------------------------------------------- |
|      128 | `OpVariable`           | `type-id, result-id, storage-kind, symbol-id, initializer-id`          |
|      129 | `OpLoad`               | `type-id, result-id, pointer-or-reference-id`                          |
|      130 | `OpStore`              | `pointer-or-reference-id, value-id`                                    |
|      131 | `OpAccessChain`        | `type-id, result-id, base-id, index-count, index-id...`                |
|      132 | `OpArrayToPointer`     | `type-id, result-id, array-id`                                         |
|      133 | `OpPointerOffset`      | `type-id, result-id, pointer-id, offset-id`                            |
|      134 | `OpReferenceBind`      | `type-id, result-id, entity-id`                                        |
|      135 | `OpCompositeConstruct` | `type-id, result-id, count, value-id...`                               |
|      136 | `OpCompositeExtract`   | `type-id, result-id, composite-id, count, index-literal...`            |
|      137 | `OpCompositeInsert`    | `type-id, result-id, object-id, composite-id, count, indices...`       |
|      138 | `OpVectorShuffle`      | `type-id, result-id, first-id, second-id, count, component-literal...` |
|      139 | `OpConvert`            | `type-id, result-id, conversion-kind, operand-id`                      |
|      140 | `OpBitcast`            | `type-id, result-id, operand-id`                                       |
|      141 | `OpIntegerToPointer`   | `type-id, result-id, integer-id`                                       |
|      142 | `OpCall`               | `type-id, result-id, symbol-id, argument-count, value-id...`           |
|      143 | `OpExtensionQuery`     | `type-id, result-id, extension-name-string-id`                         |
|      144 | `OpSelect`             | `type-id, result-id, condition-id, true-id, false-id`                  |
|      145 | `OpUnary`              | `type-id, result-id, unary-operation, operand-id`                      |
|      146 | `OpBinary`             | `type-id, result-id, binary-operation, left-id, right-id`              |

An absent initializer or global symbol is ID 0. A void call uses `OpTypeVoid` as its type and 0 as
its *result ID*. Every other value-producing instruction has a nonzero *result ID*.

The result type of `OpVariable` is a pointer to the variable's declared object type. Its result
designates that variable's storage. A global variable has a nonzero *symbol ID* and a *storage kind*
other than function-local. A local variable has *symbol ID* 0, uses function-local storage, and
occurs at the start of a block after its `OpPhi` instructions. An initializer has the declared
object type and is absent only when the source semantics permit an uninitialized object.

For a global variable, the pointer's referred-to type is the complete variable type recorded by
its symbol. A global initializer is a constant. A nonconstant local initializer is represented by
the operations that compute it followed by `OpStore`.

An external resource is represented by `OpSymbol`, `OpTypeResource`, and `OpResource`, not by
`OpVariable`. A resource type is not loaded, stored, or constructed as a value.

`OpLoad` produces the type referred to by its pointer or reference operand. `OpStore` requires a
value of that type and a modifiable designated object. `OpAccessChain` begins with a pointer or
reference and applies its integer indices in order to structures, tuples, arrays, vectors, or
matrices. A structure or tuple index is a constant. Its result has the same indirection category as
the base and refers to the selected element type.

`OpArrayToPointer` takes an operand that designates an array and produces a pointer to its first
element. `OpPointerOffset` takes an integer offset and returns the same pointer type as its pointer
operand. `OpReferenceBind` returns a reference to the entity designated by its final operand. These
instructions obey the array bounds, reference binding, lifetime, and indirection rules of the
source language without exposing a pointer or reference representation.

`OpCompositeConstruct` has 1 constituent of the corresponding type for every component, element,
or member of its result. `OpCompositeExtract` follows its literal index path through the composite
type and returns the selected type. `OpCompositeInsert` returns the original composite type after
replacing the selected component with its object operand. Every index is in range.

The 2 inputs of `OpVectorShuffle` are vectors with the same component type. Its result is a vector
of that component type whose length equals the literal count. Each component literal is less than
the sum of the 2 input lengths and selects from the first vector followed by the second.

`OpVariable` is emitted only for an object that requires storage. A local value whose address is
not observed may remain entirely in *SSA* values. Taking an address causes the required storage and
access operations to exist in the IR; it does not expose their representation.

Floating unary and binary operations use matching floating scalar, vector, or matrix element
types. Integer operations use matching integer scalar or vector element types. Logical operations
use Boolean operands. Bitwise operations use integer operands. Comparison instructions produce an
`OpTypeBool` scalar or the corresponding Boolean vector. Pointer comparisons use 2 values of the
same pointer type and remain subject to the pointer-comparison validity rules.

Vector and matrix multiplication operands have the dimensions required by the result type.
`OpConvert` uses the enumerant matching its source and destination numeric categories.
`OpBitcast` operates only on numeric scalar, vector, or matrix types with equal total bit width and
does not perform a numeric conversion.
`OpIntegerToPointer` has an integer operand and a pointer result. There is no corresponding
pointer-to-integer *opcode*.

`OpCall` references a function symbol. Its argument count and argument types equal those of that
symbol's complete function type. Its result type is the function return type. Resource operations,
standard-library operations, and extension-supplied operations use `OpCall`; this version defines
no dedicated *opcode* for them.

`OpExtensionQuery` produces a Boolean scalar. `OpSelect` requires a Boolean scalar condition and
2 values of its result type.

The control-flow instructions are:

| *Opcode* | Instruction           | Operands after the first word                             |
| -------: | --------------------- | --------------------------------------------------------- |
|      192 | `OpFunction`          | `result-id, symbol-id, function-type-id`                  |
|      193 | `OpFunctionParameter` | `type-id, result-id`                                      |
|      194 | `OpBlock`             | `result-id`                                               |
|      195 | `OpPhi`               | `type-id, result-id, pair-count, value-id, block-id, ...` |
|      196 | `OpBranch`            | `target-block-id`                                         |
|      197 | `OpBranchConditional` | `condition-id, true-block-id, false-block-id`             |
|      198 | `OpSelectionMerge`    | `merge-block-id`                                          |
|      199 | `OpLoopMerge`         | `merge-block-id, continue-block-id`                       |
|      200 | `OpReturn`            | none                                                      |
|      201 | `OpReturnValue`       | `value-id`                                                |
|      202 | `OpBarrier`           | `barrier-literal`                                         |
|      203 | `OpUnreachable`       | none                                                      |
|      204 | `OpFunctionEnd`       | none                                                      |

A *basic block* is the instruction sequence beginning with 1 `OpBlock` and ending with 1
terminating instruction. That final instruction is the block's *terminator*. A *predecessor* of a
*basic block* is a *basic block* whose *terminator* branches to it.

An `OpFunction` references a defined function symbol or, within *parameterized IR*, a defined
function-template symbol. It uses the same complete function type recorded by that symbol. It is
followed by exactly the parameters declared by that type, in order, then its *basic blocks* and 1
`OpFunctionEnd`. No parameter occurs after the first *basic block*. Each function has at least 1
*basic block*. The first is the *entry block* and has no *predecessors*.

A *basic block* `A` *dominates* a *basic block* `B` when every control-flow path from the
*entry block* to `B` passes through `A`. A value definition *dominates* an ordinary use when it
occurs earlier in the same *basic block* or its *basic block* *dominates* the use's *basic block*.
A function parameter *dominates* every *basic block* in its function. An `OpPhi` incoming value is
instead available on its corresponding *predecessor* edge.

`OpPhi` instructions occur first in a *basic block*. They contain exactly 1 value for every
*predecessor*, and each listed *basic block* is a *predecessor*. The incoming value has the phi
result type and is available at the end of its listed *predecessor*.

Each *basic block* ends with exactly 1 *terminator*: `OpBranch`, `OpBranchConditional`, `OpReturn`,
`OpReturnValue`, or `OpUnreachable`. No instruction follows the *terminator* in that *basic block*.
Every branch target is a *basic block* in the same function. The condition of
`OpBranchConditional` is a Boolean scalar.

`OpSelectionMerge` occurs in a selection header immediately before its conditional branch. Every
path leaving either selected region reaches its merge block. `OpLoopMerge` occurs in a loop header
immediately before its conditional branch. Its merge block is the only normal exit from the loop
construct, and every back edge targets either the header or its continue block. The continue block
reaches the loop header without passing through the merge block. All merge and continue targets
belong to the same function.

`OpReturn` is valid only for a void function. `OpReturnValue` supplies a value exactly matching the
function return type. `OpBarrier` identifies the source named barrier by a nonzero function-local
literal. Equal literals within one function denote the same named barrier.


### 11.11 Reflection Instructions

The reflection instructions are:

| *Opcode* | Instruction        | Operands after the first word                                                                    |
| -------: | ------------------ | ------------------------------------------------------------------------------------------------ |
|        7 | `OpEntryPoint`     | `symbol-id, identifier-string-id, stage-kind, input-id, output-id`                               |
|        8 | `OpStageInterface` | `result-id, symbol-id, interface-role, value-type-id`                                            |
|        9 | `OpStageElement`   | `interface-id, path-count, member-index..., query-name-string-id, element-type-id, contract-id ` |
|       10 | `OpResource`       | `symbol-id, binding-name-string-id, resource-type-id, access-kind`                               |
|       11 | `OpContract`       | `result-id, contract-kind`                                                                       |

`OpStageElement` has the operands:

```text

```

The path identifies the value within the interface type. An unnamed element has query-name ID 0.
Member indices follow declaration order. The element type equals the type reached through the
path. A contract ID is 0 when the element has no contract.

An entry point's input or output ID is 0 when that interface is absent. Otherwise, it denotes an
`OpStageInterface` for the same entry-point symbol and applicable role.

An `OpEntryPoint` symbol is a function with the stage recorded by the instruction. It has a
definition in an *object artifact* or *library artifact*; a *module-interface artifact* may record
only its exported declaration. Its identifier string is the exact source entry-point *identifier*.
An `OpStageInterface` value type is the complete input or output type of that function for the
recorded role.

The binding name in `OpResource` is the complete external resource name after namespace and
template qualification. The instruction records no descriptor set, binding number, register,
backend location, or target layout. Its symbol denotes an external resource, and its resource type
ID is the *complete type* recorded for that symbol.


### 11.12 Artifact Kinds

An *object artifact* has exactly 1 `OpModule` whose content-present operand is 1. That instruction
identifies the compiled *translation unit*. Additional `OpModule` instructions have a
content-present operand of 0 and identify modules referenced by its imports or symbols. The
artifact contains the concrete definitions, instantiated *template specializations*, symbols,
imports, entry points, resources, and reflection produced from the compiled *translation unit*. It
may contain unresolved symbol declarations. It contains no *opcode* from 96 through 106.

A *module-interface artifact* has 1 or more `OpModule` instructions whose content-present operand
is 1. It contains the exported declarations and re-export relationships of each represented
*module*, plus the parameterized definitions required to instantiate exported templates. A module
identity referenced but not represented has a content-present operand of 0. Each represented
*module* retains its own *canonical name* and symbols. The artifact contains no ordinary
non-template variable or function definition and no global `OpVariable`.

A compiler may consume a *module-interface artifact* while compiling source without reprocessing
the represented *RTSL source strings*. A *module-interface artifact* is not a linker input.

A *library artifact* contains linked *concrete IR* from 1 or more *object artifacts* or
*library artifacts*. A module whose content is represented has a content-present operand of 1; a
referenced-only module has an operand of 0. The artifact retains every linked
*shader-stage entry point*, resource requirement, and required reflection record. It may contain
unresolved symbol declarations. It contains no *opcode* from 96 through 106.

The following hexadecimal word streams are normative encoding examples. Spaces and line breaks
are not part of an artifact. Each group of 8 hexadecimal digits denotes the numeric value of 1
*RTSL word*, which is written to a file in little-endian byte order.

This is a complete *object artifact* for an empty translation unit named `empty.rtsl`:

```text
4c535452 00000001 00000000 00000001 00000003 00000000 00070001 00000001
0000000a 0000000a 74706d65 74722e79 00006c73 00040003 00000002 00000001
00000001
```

ID 1 contains the *canonical name*, and ID 2 is the module identity. The final instruction begins
with `0x00040003`, which encodes a *word count* of 4 and *opcode* 3.

This complete *module-interface artifact* contains one exported generic identity function with
one *type parameter*. Its *canonical name* is `m`; its function name is `id`:

```text
4c535452 00000001 00000000 00000002 0000000d 00000000 00050001 00000001
00000001 00000001 0000006d 00050001 00000002 00000002 00000002 00006469
00150001 00000003 00000041 00000041 3054525f 30323033 30313031 30303030
30643630 30303031 30303030 30303032 36303030 30343639 30303030 30303030
30303031 31303030 30303030 30303030 00000030 00050001 00000004 00000001
00000001 00000054 00040003 00000005 00000001 00000001 000d0005 00000006
00000005 00000003 00000002 00000003 00000002 00000003 0000000f 0000100e
00010000 00100000 00000000 00050060 00000007 00000005 00000006 00000001
00040061 00000008 00000004 00000000 0005002c 00000009 00000008 00000001
00000008 000400c0 0000000a 00000006 00000009 000300c1 00000008 0000000b
000200c2 0000000c 000200c9 0000000b 000100cc 0002006a 00000007
```

Its *result IDs* are 1 through 12, so its *ID bound* is 13. ID 7 is `OpTemplate`, ID 8 is its type
parameter, ID 9 is the dependent function type, and IDs 10 through 12 form the parameterized
function body. The template symbol's *canonical link name* is:

```text
_RT030201010000006d0100000002000000696400000000010000001000000000
```

This is a complete *library artifact* obtained by linking 2 empty modules named `a.rtsl` and
`b.rtsl`:

```text
4c535452 00000001 00000000 00000003 00000005 00000000 00060001 00000001
00000006 00000006 74722e61 00006c73 00060001 00000002 00000006 00000006
74722e62 00006c73 00040003 00000003 00000001 00000001 00040003 00000004
00000002 00000001
```

When a call remains unresolved in a nonempty *library artifact*, the artifact contains its
`OpSymbol` declaration and the `OpCall` that references that *symbol ID*, but no matching
`OpFunction`.


### 11.13 Linking

An *RTSL linker* consumes *object artifacts* and *library artifacts* and produces 1
*library artifact*. It does not consume *module-interface artifacts*.

The *RTSL linker* assigns artifact-local output *RTSL IDs* and remaps every input *RTSL ID*. IDs
that denote a merged module, symbol, type, or constant may map to the same output ID; all other
input results remain distinct. Module identities with the same *canonical name* are merged.

The *RTSL linker* merges symbols with equal *canonical link names*, validates compatible
declarations, and associates every available definition with its symbol. It merges duplicate
instantiations only when they denote the same *template specialization* and have equivalent
*concrete IR*. Its output follows the canonical instruction order independently of input order.

A second definition of the same entity, incompatible declarations with the same
*canonical link name*, or conflicting nominal type definitions are invalid. A symbol for which no
input supplies a definition remains unresolved in the output. The validity and interpretation of
the *library artifact* do not depend on the order in which its inputs were provided.


### 11.14 RTSL Transpilers

*RTSL transpilers* are independently developed for their backends. They are not generated or built
by the RTSL compiler.

An *RTSL transpiler* receives a *library artifact*, an entry-point *identifier*, and a
nonempty set of requested shader stages. It selects the matching *shader-stage entry points*,
resolves *extension query expressions*, validates their interfaces, and produces a separate backend
shader for each selected stage. It does not instantiate *template specializations*.

The *RTSL transpiler* assigns backend resource locations, stage configuration, geometry limits,
layouts, and other target-specific representations. It may restructure, specialize, combine, or
eliminate *concrete IR* as long as required observable behavior is preserved.

The consequences of an unresolved ordinary symbol for a selected entry point are outside the
artifact-format contract defined by this chapter.


### 11.15 Extensions

An *RTSL extension* is an optional capability implemented by an *RTSL transpiler*. An
*extension name* is the *string* that identifies 1 extension. Extension availability is Boolean;
RTSL assigns no versions or dependencies to extensions. *Extension names* are compared character
by character and case-sensitively.

An *extension query expression* has the following form:

```ebnf
extension-query-expression = "extension", "(", string-literal, ")" ;
```

The expression is represented by `OpExtensionQuery`. Its result type is `OpTypeBool`, and its
string operand contains the exact *extension name*. The query remains an ordinary Boolean *SSA*
value in an *object artifact* or *library artifact*.

The selected *RTSL transpiler* replaces every `OpExtensionQuery` with `true` or `false` according
to the capabilities it implements. Ordinary control-flow processing determines which operations
remain. Querying an extension does not import or activate it.

An operation supplied by an extension is represented as an ordinary call to an unresolved
`OpSymbol` with a *canonical link name*. RTSL reserves no extension *opcode* range and stores no
extension table in an artifact.


### 11.16 Stripping and Debug Data

A compiler or *RTSL linker* may remove an instruction or entity only when no remaining semantic
content or later consumer requires it.

The *canonical link name* of every retained symbol, every *canonical name* required by a retained
module, import, or symbol, every retained entry-point identifier, the names and types required to
query retained stage interfaces, and every retained resource binding name and type are required
semantic data. A *module-interface artifact* retains every declaration, constraint,
specialization declaration, and generic definition required for lookup, substitution, selection,
and instantiation.

Source text, source positions, lexical scopes, macro-expansion ancestry, local source names, and
inlining ancestry are not part of the formats defined by this chapter. A separate debug companion
format may associate such information with artifact-local *RTSL IDs*. This specification does not
define that format.



---



## 12. Debugging

A *debugging utility* is a construct whose only purpose is debugging. *String* values, formatting
operations, and diagnostic operations may be *debugging utilities* when used for that purpose.
*Debugging utilities* do not affect required non-debug program behavior.

The *module* whose *canonical name* is `format.rtsl` provides `std::format`. It is imported with:

```rtsl
import "format.rtsl";
```

RTSL *string* formatting follows the replacement-field, format-specifier, and formatting rules of
`std::format`.

The compiler may remove any *debugging utility*, including its *string* values and formatting
operations, before producing an *RTSL artifact*. A retained *debugging utility* is represented by
ordinary *RTSL IR* and may be removed during later compilation, linking, transpilation, or backend
processing.

No *debugging utility* is required to survive RTSL compilation, transpilation, backend production,
or execution.
