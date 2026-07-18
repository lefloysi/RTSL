# OoO Shader Language Surface

## Objective

Extend RTSL v0.1 with the general-purpose operations required to translate
Outposts of Odyssey's existing vertex and fragment shaders without changing
their rendering algorithm.

## Architectural rule

Surface syntax and overload rules are owned by semantic analysis, lowering
produces backend-neutral typed IR operations, and SPIR-V/HLSL transpilers map
those operations without reconstructing frontend policy.

## Documentation consulted

- `AGENTS.md`
- `README.md`
- `docs/language.md`
- `docs/cmake.md`
- `rtsl/src/sema/stdlib.def`
- Existing compiler, SDK, SPIR-V, and HLSL tests

## Impact map

### Confirmed violations

- The standard library exposes only `sample`.
- Existing OoO shaders require common scalar/vector math intrinsics.
- Source indexing parses but is not typed or lowered.
- Storage-buffer layouts cannot currently express a runtime array payload.
- Float bit reinterpretation has IR/backend support but no coherent source API.
- Texture dimensions have no source, IR, SDK, or backend operation.

### Suspicious related locations

- Scalar/vector constructor conversion and unsigned literal handling.
- Integer arithmetic and bitwise operators used by packed terrain data.
- Artifact validation shapes for new normalized instructions.
- Resource stage reflection through indexed storage-buffer loads.

### Inspected locations ruled out

- Graphics stage interfaces already cover the required vertex/fragment flow.
- Storage-buffer resources and std430 layout metadata already exist.
- Both backends already consume normalized typed instructions and reflected resources.

## Invariants

### Pre-change

- Existing RTSL programs and artifact version remain valid.
- `sample` behavior and current graphics interfaces remain unchanged.

### Post-change

- Required math calls are consistently typed for scalar and float-vector operands.
- Runtime indexing is valid only for indexable values and uses integer indices.
- Bit reinterpretation is explicit and width preserving.
- Texture queries return dimensions matching the sampled image shape.
- New operations serialize, load, validate, and transpile through both backends.

## Checklist

- [x] Read instructions and map frontend, semantic, IR, SDK, and backend ownership.
- [x] Define the complete required operation/signature table.
- [x] Implement semantic validation and type inference.
- [x] Implement indexing and intrinsic IR lowering.
- [x] Update artifact/SDK normalization and validation.
- [x] Implement SPIR-V lowering and validation.
- [x] Implement HLSL lowering.
- [x] Document the released language surface.
- [x] Add general and OoO-shaped regression tests.
- [x] Run repository verification and repeat the architectural search.
- [x] Review the final diff for feature-name special casing and temporary workarounds.

## Verification

- `cmake --build out\\build --config Debug` — passed.
- `ctest --test-dir out\\build -C Debug --output-on-failure` — 6/6 passed.
- Terrain-surface HLSL passed DXC validation.
- Terrain-surface SPIR-V passed `spirv-val --target-env vulkan1.3`.
- `git diff --check` — passed.

## Final repository search

- No `TODO`, `FIXME`, or `HACK` markers were introduced.
- Source intrinsic names are centralized in `rtsl/src/sema/stdlib.def`.
- Runtime-array handling remains structural in the SDK and backends; no OoO shader names leaked into the compiler.

## Blockers

None.

## Continuation notes

DONE.
