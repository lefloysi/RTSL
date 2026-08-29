# RTSL Documentation Guidance

This file records the working rules for maintaining the RTSL specification.


## Document Roles

- `specification.md` is normative. It contains confirmed syntax, semantics, validity rules, and
  required observable behavior.
- `notes.md` is a non-normative scratchpad. It contains unresolved decisions, incomplete grammar,
  provisional syntax and examples, future ideas, and intentionally non-normative implementation
  reminders.
- `agent.md` records stable document organization and cross-document invariants.

Remove a note after its confirmed content has been incorporated into the specification. Do not
maintain a second copy of settled specification prose in `notes.md`.


## Working Method

Read the current versions of all three files before editing them. The user may edit them while work
is in progress, so re-read a file before replacing a large section and preserve new user material.

Extract the smallest semantic rule from conversational material and write it as direct
specification prose. Preserve the user's exact syntax and confirmed behavior. Do not invent missing
semantics or import familiar language rules that the user did not select.

Keep unresolved choices in `notes.md`. Keep backend strategies and host-API details out of
normative language rules unless the specification intentionally makes them observable.

Perform structural, semantic, and editorial reviews after a broad rewrite.


## Specification Organization

Order the normative chapters by semantic dependency:

1. Introduction
2. Basics
3. Types
4. Declarations
5. Definitions
6. Expressions and Operators
7. Statements and Control Flow
8. Templates
9. Modules
10. Shader Stages and Interfaces
11. RTSL IR and Artifacts
12. Debugging

Types defines type categories, identity, completeness, and semantic properties. Declarations
defines type specifiers, declarators, attributes, qualifiers, and the syntax that introduces
entities. Expressions and Operators defines conversions and operations on those types.

Arrays, pointers, and references are derived types, not built-in types. Their declarator syntax
belongs in Declarations. Array conversion, subscripting, pointer operations, and reference binding
belong in Expressions and Operators.

Attributes and qualifiers remain with declarations. Definitions describe bodies, initializers,
member sequences, and instantiated concrete content. Names, scopes, linkage, imports, exports, and
module interfaces remain together under Modules.


## Declarator and Addressing Invariants

- Object declarators compose pointer, reference, array, and parenthesized forms. Functions retain
  the dedicated `fn` syntax; object declarators do not introduce function pointers.
- Arrays are distinct contiguous objects. Ordinary expression use converts an array to a pointer
  to its first element when the context does not require the array itself.
- Arrays of references, pointers to references, references to arrays, references to pointers, and
  nested references are valid type compositions.
- Every `&` declarator forms one distinct reference layer. RTSL has no reference collapsing or
  separate rvalue-reference category.
- References bind existing entities, cannot be null or reseated, and may be stored, passed, or
  returned.
- Pointer arithmetic is array-bounded and may produce a non-dereferenceable one-past pointer.
- Unary `&` is the core address-of operator for objects, resource entities, and references.
- A qualifier on a pointer variable does not propagate to the designated entity.
- Integer-to-pointer conversion is explicit and exists only where an applicable RTSL extension
  defines the accepted address. Pointer-to-integer conversion does not exist.
- Pointer and reference representations are not observable.
- Pointer types and operations are core language constructs. Supplying addresses of externally
  bound storage is an extension capability.

Keep virtual addresses, storage maps, provenance-based devirtualization, and target instruction
details non-normative. RTSL IR retains abstract pointer operations. A backend may lower known
provenance directly, resolve an unchanged pointer once, or use a virtual mapping when required.


## Templates and Compiled Interfaces

- Exported generic template declarations, their complete definitions, and the declarations needed
  to select their explicit specializations are module-interface content.
- Generic template definitions are serialized only in `.rtm` module-interface artifacts.
- Concrete template instantiations and explicitly defined specializations are serialized in
  `.rto` object artifacts and `.rtl` library artifacts.
- An explicit specialization is declared and visible before that specialization is instantiated.
- The compiler instantiates every specialization required while compiling a translation unit.
- A linker may merge equivalent concrete instances of the same specialization.
- The transpiler does not instantiate template specializations.
- One specialization has one identity throughout a linked set of artifacts.
- `.rtm` uses parameterized IR; `.rto` and `.rtl` use concrete typed SSA.
- The compiler retains every shader-stage entry point in its `.rto` output.
- The linker retains those entry points in `.rtl`.
- The transpiler receives `.rtl`, an entry-point identifier, and requested shader stages. It
  selects the matching stage entry points, resolves extensions, validates interfaces, and produces
  backend shaders.
- Extension queries remain ordinary Boolean IR expressions until the transpiler resolves them.


## RTSL IR and Artifacts

- `.rto` is concrete typed SSA for exactly 1 translation unit.
- `.rtm` is a compiled interface bundle used by the compiler. It contains exported declarations
  and parameterized template IR, and it is not a linker input.
- Compiling source consumes the `.rtm` artifacts required by its imports and emits `.rto`. The
  compiler emits `.rtm` when compiled interface output is requested.
- `.rtl` is a linked library of concrete SSA. The linker consumes `.rto` and `.rtl` and may leave
  symbols unresolved.
- RTSL defines no program or executable artifact. Only a transpiler produces backend shaders.
- The artifact format is a canonical little-endian stream of 32-bit words with a fixed header,
  fixed opcode and operand tables, and exact major/minor compatibility.
- The initial format version is `0.1.0.0`. Major and minor must match exactly. Patch and snapshot
  differences are compatible and cannot change the binary grammar.
- Every unknown opcode is invalid. Unassigned opcode and enumerant values remain reserved.
- Cross-artifact symbols use canonical `_RT` link names formed from hexadecimal canonical symbol
  keys. Do not replace them with hashes or artifact-local identities.
- Resource records contain logical binding names and complete RTSL types. Backend binding numbers,
  descriptor sets, registers, layouts, and target locations do not belong in RTSL IR.
- Stage-interface and resource names required for later queries are semantic data, not debug data.
- Source text, locations, lexical scopes, macro ancestry, local source names, and inlining ancestry
  belong in a future debug companion format, not `.rto`, `.rtm`, or `.rtl`.
- Constructors, destructors, operators, methods, reference access, and `emit` behavior are lowered
  before concrete SSA is serialized.
- Pointer and reference operations remain abstract and representation-independent in concrete IR.
- Extension-supplied operations use ordinary unresolved canonical symbols. No extension-specific
  opcode range or extension table belongs in an artifact.


## Formatting and Verification

Keep prose at 100 characters or fewer. Direct Markdown links in the table of contents and aligned
Markdown table rows may exceed 100 characters. Use ASCII `...`, never a Unicode ellipsis.
Italicize every occurrence of a term defined by the specification, including its defining
occurrence.

Use direct Markdown links in the table of contents. Do not replace them with reference-link
definitions. Regenerate and validate the table after changing headings.

In `specification.md`, place 3 empty lines before and after each top-level `---` separator. Place
2 empty lines before a subsection heading and 1 empty line after it. Keep raw Markdown readable.

After editing:

- validate representative object and abstract declarators;
- confirm that no function-pointer syntax was introduced;
- check every table-of-contents target;
- check line lengths, trailing whitespace, Unicode ellipses, Markdown fences, and heading order;
- search all three files for duplicated, contradictory, and stale terminology;
- confirm compiler, module, template, stage, extension, and transpiler responsibilities agree;
- verify that every surviving note is unresolved, provisional, future-facing, or intentionally
  non-normative;
- run `git diff --check` and inspect the complete documentation diff;
- preserve unrelated working-tree changes and leave documentation changes uncommitted unless the
  user asks otherwise.
