# Agent Instructions

This repository values concise, explicit C++ code. Fix correctness first, but
do not ignore design quality. When writing or reviewing code, apply these rules.

RTSL v0.1 target:

- vertex and fragment shader support must be complete
- source syntax should match the documented language surface
- backend behavior should match the documented backend contract
- compiler changes should move the release surface forward, not just tidy code
- treat RTSL as a coherent language design, not a pile of accepted syntax

## Design Rules

- Prefer free functions for stateless behavior.
- Use classes only when they own state, protect invariants, manage lifetime, or
  provide a meaningful interface.
- Use structs only for real data concepts. Do not create wrapper structs just to
  avoid passing a few clear arguments.
- Use `std::string_view` for borrowed read-only text.
- Use `std::span<T>` / `std::span<const T>` for borrowed contiguous ranges.
- Use `std::string` and `std::vector<T>` when ownership, mutation, resizing, or
  storage of that exact container is required.
- Do not use `const std::string&` for read-only text.
- Do not use `const std::vector<T>&` for read-only iteration.
- Use brace initialization for local object construction.
- Include what each file uses. Do not rely on transitive includes.

## Formatting Rules

- Use braces for local object construction and aggregate initialization.
- Prefer explicit initialization over declaration-style parentheses.
- Keep indentation and layout consistent with the existing file.
- Keep line breaks where they improve readability of complex expressions or
  argument lists.
- Avoid formatting changes that do not improve clarity or correctness.

## Release Discipline

- Keep implementation and documentation aligned with the intended v0.1 scope in
  `spec/language.md`, `spec/backend-contract.md`, and `spec/compiler-pipeline.md`.
  When user-directed design changes conflict with current docs, update the docs
  or the implementation so they converge on the intended behavior.
- When a feature is listed as part of v0.1, treat missing support as a bug.
- When a feature is documented as a goal but not a v0.1 guarantee, do not add
  it unless it helps complete the release surface or unblocks a required path.
- Prefer finishing the vertex/fragment pipeline end to end before widening to
  extra stage families or backend-specific expansion.
- Keep tests on the released path dense enough to catch regressions in syntax,
  lowering, reflection, and linking.

## Codex Operating Workflow

For every nontrivial implementation or bug-fix task, treat a user-provided
file, function, test, diagnostic, or line number as a starting clue, not a
scope boundary. Before editing, infer the general rule or invariant implicated
by the example, read the relevant v0.1 documentation, inspect the named
location, search the full repository for analogous implementations and
violations, and inspect both upstream and downstream layers.

Create or update an agent-scoped task ledger for nontrivial work at
`.codex/tasks/<agent-id>.md`, where `<agent-id>` is stable for the current
worker and distinct from other concurrent agents. The ledger must record the
objective, architectural rule, documentation consulted, impact map, pre-change
and post-change invariants, checklist, verification commands and results, final
repository search, blockers, and continuation notes. Keep the impact map
separated into confirmed violations, suspicious related locations, and
inspected locations ruled out.

Prefer structural fixes over symptom patches. Before adding feature-specific
conditionals, flags, parser branches, enum cases, or isolated workarounds,
determine whether the behavior should follow from a more general
representation, named semantic rule, corrected phase boundary, centralized
classification, shared table, earlier invariant, or normalization before later
compiler stages. Direct checks for individual surface-language feature names in
generic code are suspicious; if the next related feature would require another
parallel branch, redesign first.

Maintain the compiler ownership boundary explicitly: syntax is preserved by the
parser rather than prematurely interpreted; contextual language meaning belongs
to semantic analysis; lowering consumes validated meaning; backend code receives
normalized backend-neutral information and must not reconstruct frontend
policy. A stage-specific semantic decision may live in semantic analysis when it
represents a real language distinction, but not in generic syntax parsing merely
because the parser sees the token.

For nontrivial work, the checklist must include the architectural search,
affected layers and files, important invariants, coherent implementation items,
tests for the general rule with at least one case beyond the motivating example
where appropriate, a repeated repository search after implementation, final
diff review for newly introduced special-case logic, repository-defined
verification, and ledger evidence.

Do not claim completion while known checklist items remain. A task is complete
only when all accepted checklist items are complete, relevant verification
succeeds, the final architectural search is recorded, the diff has been
reviewed, no known required stub/TODO/bypass/temporary workaround remains, and
the task ledger records the result. If genuinely blocked, state the exact
blocker, preserve evidence, complete every independent item first, and mark the
ledger `BLOCKED`, never `DONE`.

## Language And Documentation

- Reason from the intended RTSL language model before changing syntax,
  lowering, docs, or tests.
- Keep documentation concise and normative. Document the language shape and
  observable behavior, not the rationale for every design decision.
- Do not copy every review correction into prose. Apply the correction to the
  grammar, examples, implementation, and tests where it belongs.
- Remove obsolete syntax and examples instead of explaining compatibility with
  a model that is no longer intended.
- When docs and source disagree, make them converge on the intended design.
- Examples must use the real v0.1 syntax and should be short enough to reveal
  the rule they demonstrate.

## Layering Rules

- Lexing and parsing own syntax.
- Semantic analysis owns source-language meaning and validation.
- IR lowering owns backend-neutral program representation.
- Artifact code owns serialization format.
- Backend code owns backend-specific names, formatting, layout restrictions, and
  target quirks.
- ABI code owns C-compatible handles, errors, and lifetime bridges.

Do not put backend policy in frontend, semantic, or generic IR code. Do not hide
language policy in unnamed lowering helpers. If a rule changes language behavior,
name it, isolate it in the owning layer, and test it.

## Abstraction Rules

Good abstractions remove real complexity by protecting invariants, clarifying
ownership, centralizing a repeated rule, or isolating a real boundary.

Bad abstractions add ceremony without meaning. Reject stateless classes, junk
drawer context objects, local lambdas that encode domain policy, and helper
types that only obscure straightforward parameters.

## Lookup and Constants

- Small fixed mappings can be `constexpr` tables.
- Shared language mappings should come from one source of truth, such as a
  `.def` file.
- Use maps when data is dynamic, large, or repeatedly indexed enough to justify
  it.
- Do not choose a hashmap just because lookup exists.
- Hardcoded values are acceptable only for local facts. Hardcoded language,
  binary-format, ABI, or backend policy must be named and owned by the correct
  layer.

## Error Handling

- Compiler stages report diagnostics.
- Serialization code returns structured errors.
- ABI functions convert failures to C status objects and never leak exceptions.
- Do not silently turn real failures into empty containers unless the caller can
  distinguish empty success from failure.

## Review Rejection Checklist

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
