# RTSL transpiler SDK replacement

Status: IN PROGRESS

## Objective

Completely replace the existing backend-facing SDK with a concise, coherent interface optimized for writing Rutile shader transpilers; migrate every in-repository consumer and remove the obsolete surface.

## Architectural rule

The compiler owns source syntax, language semantics, validation, and linking. The SDK owns the stable serialized-artifact model and a safe, normalized, backend-neutral read interface. Transpilers depend only on the SDK and never reconstruct frontend policy or include compiler-internal headers.

## Documentation consulted

- `AGENTS.md`
- `README.md`
- `docs/language.md`
- `docs/artifacts-sdk.md`
- `docs/c-abi.md`
- `docs/cli.md`
- `docs/cmake.md`

## Impact map

### Confirmed violations

- `rtsl-sdk` is an `INTERFACE` target, but all old SDK symbols are implemented by the compiler in `rtsl/src/api/rtsl.cpp`; a backend cannot link only the SDK.
- `rtsl-sdk/include/rtsl.h` exposes an opaque mutable-lifetime C handle and a getter-per-index matrix with raw numeric opcodes/decorations, no useful load error, and no complete program view.
- Entry reflection drops the function ID and stage-interface reflection is not exposed at all.
- Resource reflection is source-shaped, drops the global ID, calls set/binding `group/member`, and reconstructs kind from frontend type spellings in the API layer.
- Free-floating stage interfaces are keyed by source type name and role. Linked programs omit source struct declarations, so a backend cannot robustly join them to IR or entries.
- Sampled/storage image dimensions are collapsed during lowering; the remaining source spelling is the only shape information.
- Artifact parsing lives in the compiler, accepts unbounded encoded counts before allocation, and does not validate the complete linked-program graph.
- Compiler/linker metadata, authored tags, semantic records, normalized reflection, and backend-facing IR are mixed in shared structs.
- `merge_module` shifts IR IDs but not reflected resource type IDs; stage interfaces are silently deduplicated by source `(role, type_name)`.
- Semantic code hardcodes backend entry spellings (`vert`/`frag`) instead of leaving emitted target names to transpilers.
- `function_type_id`, instruction source locations, and function debug tables are serialized/exposed despite never being populated.
- The direct DX12 consumer already disagrees with the current header (`binding` versus `group/member`) and links the entire compiler; Vulkan has no real artifact reader yet.

### Suspicious related locations

- `rtsl-sdk/include/rtsl.h`
- `rtsl-sdk/src/`
- compiler artifact, IR, linker, ABI, tests, build/export, and documentation layers
- parent Rutile DX12/Vulkan backend consumers (read-only integration impact; outside this writable repository)

### Inspected and ruled out

- CLI command ownership and Rutile backend registration do not belong in the SDK.
- A C SDK is not required by the mixed-language Vulkan target because it already enables C++23 and can use a narrow C++ translation unit.
- Object, module-interface, and library artifacts are compiler/linker inputs, not backend SDK concepts.

## Invariants

### Before change

- Backend loading is manual (`load`/many indexed getters/`destroy`) and errors collapse to null/zero.
- The target named SDK is not independently linkable.
- Correct transpilation requires private compiler knowledge that is absent from the public contract.

### After change

- Backend authors receive a complete linked-program view through SDK-owned types and functions.
- The SDK validates untrusted artifact bytes and exposes no unchecked raw serialized pointers.
- Backend code needs no compiler parser, semantic-analysis, linker, or internal IR headers.
- Artifact serialization has one SDK-owned schema and compiler emission conforms to it.
- No superseded public SDK entry points remain.
- Backend input is an immutable owning `rtsl::sdk::Program` returned as `std::expected<Program, LoadError>`.
- Entries own typed input/output interfaces and reference their IR functions directly.
- Resources carry normalized kind/image shape/access/set/binding/global/value-type data.
- Types, constants, globals, functions, blocks, instructions, and decorations have typed public records and direct ID lookup.
- Program loading bounds allocations, rejects non-program artifacts, validates enums/IDs/CFG/reflection relationships, and reports structured errors.
- The compiler C ABI is independent of the C++ transpiler SDK.

## Checklist

- [x] Complete architectural search and consumer impact map.
- [x] Record affected layers/files and concrete pre-change invariants.
- [x] Design one coherent replacement interface around transpiler use cases.
- [ ] Implement the SDK without feature-specific workarounds in generic code.
- [ ] Migrate compiler/artifact/ABI/test/build consumers.
- [ ] Add tests for the general artifact-view rule and malformed input.
- [ ] Align README and relevant docs with the released v0.1 surface.
- [ ] Repeat repository search for old APIs and forbidden internal dependencies.
- [ ] Review the final diff for special cases, transitive includes, and incomplete stubs.
- [ ] Run repository-defined build and tests and record results.

## Verification

- Pending.

## Final repository search

- Pending.

## Blockers

- None.

## Continuation notes

- Parallel read-only analyses completed documentation/language boundaries, the current SDK surface, and all backend/build/test consumers.
- Chosen public boundary: compiled C++23 SDK; move-only immutable `Program`; typed IR graph and entry-centric reflection; no legacy C SDK shim.
- Compiler-only data excluded from the public model: imports/exports/interface hashes, AST declarations, authored tags/layout spellings, semantic resource records, mangled names, unresolved calls, source manager/diagnostics.
