# Code Style and Design Rules

This document defines how RTSL C++ code should be written. These are design
rules, not formatting rules. The goal is code that exposes ownership, keeps
compiler stages separated, and avoids abstraction that does not carry its own
weight.

## Default Shape

Prefer the simplest construct that accurately models the problem.

- Use a free function when behavior has no state.
- Use a struct when a group of fields is real data with a clear meaning.
- Use a class when there is state, invariants, lifetime, or an interface worth
  protecting.
- Use templates only when multiple concrete overloads would be worse.

A type should exist because it owns data, protects invariants, groups a real
concept, or provides a stable interface. Do not create a class only to group one
stateless function.

## Function Interfaces

Function parameters should describe what the callee actually needs.

- Use `std::string_view` for borrowed text that is not stored.
- Use `std::span<T>` or `std::span<const T>` for borrowed contiguous ranges.
- Use `std::filesystem::path` for filesystem paths.
- Use `std::vector<T>` when the function owns, mutates, resizes, or stores that
  exact container.
- Use `std::string` when the function owns, mutates, or stores text.

Do not take `const std::string&` for read-only text. Do not take
`const std::vector<T>&` for read-only iteration. Those types over-specify the
caller and hide the ownership contract.

Parameter objects are for real concepts, not for avoiding argument lists. A
parameter struct is appropriate when fields are reused together, have meaningful
defaults, are passed through several layers as one concept, or would otherwise
make call sites less clear. It is not appropriate when it merely wraps two or
three unrelated arguments for one function.

## Construction

Use brace initialization for local objects and aggregate construction.

```cpp
Parser parser{ sources, diagnostics, file_id, tokens };
```

Avoid parenthesized local object construction in new code. Braces make object
construction visually distinct and avoid C++ declaration ambiguities.

## Ownership and Lifetime

Ownership should be visible from the type.

- Value members own.
- References and pointers borrow.
- `std::span` and `std::string_view` borrow and must not outlive the source.
- Returned views must have an obvious owner with a longer lifetime.
- Moving from a value should happen at ownership boundaries, not as a habit.

Do not store views into temporary strings, temporary vectors, or data whose owner
is not clearly tied to the receiving object.

## Layering

Keep policy in the layer that owns it.

- Lexing and parsing own syntax.
- Semantic analysis owns source-language meaning and validation.
- IR lowering owns backend-neutral program representation.
- Artifact code owns serialization format.
- Backend code owns backend-specific spelling, layout restrictions, and target
  quirks.
- ABI code owns C-compatible handles, error conversion, and lifetime bridges.

Do not put backend-specific naming, formatting, or target restrictions in
frontend, semantic, or generic IR code. Do not hide language rules inside
generic lowering mechanics. If a rule is part of the language, name it and test
it as a language rule.

## Abstraction

An abstraction must remove real complexity.

Good abstractions usually do at least one of these:

- protect an invariant
- centralize a rule used in multiple places
- make ownership or lifetime clearer
- isolate a stage boundary
- replace duplicated logic with one named concept

Bad abstractions usually do one of these:

- wrap one function in a stateless class
- hide unrelated dependencies in a context object
- move code away from its owning layer
- make call sites shorter but less explicit
- exist only because code looked long

Do not create a context object as a junk drawer. If several values are always
used together and form a real concept, name that concept. Otherwise pass the
specific dependencies.

## Local Helpers

Local lambdas are for local mechanics. They are not for domain rules.

Use a local lambda for short, obvious code that is only meaningful inside the
current function. Promote it to a named function when it encodes language
behavior, compiler-stage policy, serialization rules, backend behavior, or any
logic that needs direct tests.

## Tables and Lookup

Choose lookup structures by semantics first, then by measured need.

- Use `constexpr` tables for small fixed mappings.
- Use generated tables when the data belongs to a shared `.def` source.
- Use maps when the data is dynamic, large enough to matter, or looked up often
  enough that indexing improves clarity or performance.

Do not use a hashmap just because lookup is involved. Do not use a hand-written
linear table when the same mapping is already defined elsewhere.

## Hardcoded Values

Hardcoded values are acceptable only when they are local facts, not hidden
policy.

A hardcoded value is suspicious when changing it would change language behavior,
backend behavior, binary format, or ABI behavior. Those values need a named
constant, a named rule, or a documented table in the owning layer.

Temporary hardcoding must be marked as temporary and isolated so it can be
deleted without changing unrelated code.

## Error Handling

Errors should travel through the layer's normal reporting mechanism.

- Compiler stages report diagnostics.
- Serialization code returns structured errors.
- ABI functions convert failures to C status objects and never leak exceptions.
- Internal helpers should not silently swallow errors unless their contract says
  absence is valid.

Do not turn real failures into empty containers unless the caller can reliably
distinguish empty success from failure.

## Includes

Each file includes what it uses. Do not rely on transitive includes.

If a file uses `std::span`, it includes `<span>`. If it uses `std::move`, it
includes `<utility>`. Header moves should not break unrelated compilation units.

## Review Checklist

Reject or revise code when:

- a stateless class replaces a function
- a wrapper struct does not represent a real concept
- read-only text is passed as `const std::string&`
- read-only contiguous data is passed as `const std::vector<T>&`
- parenthesized local object construction appears in new code
- backend policy appears outside backend-owned code
- language policy is buried in an unnamed helper
- a context object mixes unrelated dependencies
- a local lambda implements a rule that deserves a name
- a data structure is chosen without matching the problem shape
- a file compiles only because of transitive includes

