# v0.1 API/ABI Agent Ledger

## Objective

Inspect public headers, SDK model headers, artifact/linker serialization, and CLI output for v0.1 API/ABI release hygiene. Prioritize separating shared/public SDK model from compiler internals without broad churn.

## Architectural Rule

Shared artifact container identifiers and layout constants belong in the public SDK model when external tools or backends need to parse emitted artifacts. Compiler internals may own rich `Artifact` and IR structures, but must consume the same shared constants rather than duplicating public binary policy.

## Documentation Consulted

- `AGENTS.md`
- `spec/artifact-format.md`
- `spec/sdk-model.md`
- `docs/c-abi.md`
- `spec/c-abi.md`

## Impact Map

### Confirmed Violations

- `rtsl/src/artifact/artifact.cpp` privately defined payload-kind IDs even though payload kinds are part of the documented artifact format consumed by SDK/backends.
- `rtsl-sdk/include/rtsl/artifact.hpp` exposed artifact kind/version but not the binary container record sizes or payload-kind IDs needed by SDK consumers.

### Suspicious Related Locations

- `rtsl/include/rtsl.h` C ABI reflection names changed in the worktree from stage variables to stage outputs; verify implementation and tests continue to match.
- `rtsl/src/api/rtsl.cpp` maps compiler artifact/stage/resource models into the C ABI; verify no stale names remain.
- `rtslc/src/rtslc.cpp` writes `.rtslm` sidecars and links artifacts using artifact extensions from the compiler artifact API.

### Inspected Locations Ruled Out

- `rtsl/src/support/basic_types.hpp` already forwards to `rtsl-sdk/include/rtsl/basic_types.hpp`; no duplicate integer aliases remain.
- `rtsl/src/artifact/linker.hpp` still owns compiler linker behavior, which is not yet a public SDK runtime model.
- `tests/compiler.cpp` already covers C ABI stage-output reflection on object and loaded program artifacts.

## Pre-Change Invariants

- Artifact kind values remain object=1, module=2, library=3, program=4.
- Artifact version remains 0.1.
- Current writer/reader use a 40-byte header and 32-byte payload records after the in-progress serialization cleanup.
- C ABI stage-output reflection exposes only output interfaces, not varyings.

## Checklist

- [x] Inspect SDK headers versus support/internal artifact types.
- [x] Inspect artifact serialization and linker exposure.
- [x] Inspect C ABI stage-output API and related tests.
- [x] Move low-risk shared artifact IDs/constants into SDK.
- [x] Update compiler serialization to consume SDK-owned payload kind.
- [x] Add focused tests for SDK/container constants.
- [x] Run repository-defined build/tests.
- [x] Repeat repository search for stale duplicate payload/layout policy.
- [x] Review final diff for special-case API churn.

## Verification Commands and Results

- `cmake --build out\build --config Debug`: required escalation for Windows SDK metadata access; built `rtsl.lib` and `rtsl-tests.exe`, then failed `rtslc` with transient MSVC PDB contention `C1041`.
- `cmake --build out\build --config Debug --target rtslc -- /m:1`: passed.
- `ctest --test-dir out\build -C Debug --output-on-failure`: first run used a stale/raced test binary and failed one obsolete test name.
- `cmake --build out\build --config Debug --target rtsl-tests -- /m:1`: passed.
- `ctest --test-dir out\build -C Debug --output-on-failure`: passed, 5/5 tests.

## Final Repository Search

- `rg "enum class PayloadKind|ArtifactHeaderSize|PayloadRecordSize|RTSL_STAGE_ROLE|rtsl_stage_variable|rtslModuleGetStageVariable|rtslModuleGetStageLocation|header size|payload record" rtsl rtsl-sdk tests rtslc spec docs`
- Result: no stale C ABI stage-variable names; `PayloadKind` now only in SDK; header/record constants referenced by SDK, artifact serialization, tests, and spec prose.

## Post-Change Invariants

- SDK owns artifact kind IDs, payload kind IDs, artifact magic/version, and emitted container header/record sizes.
- Artifact writer/reader consumes SDK `PayloadKind` and checks serializer record sizes against SDK constants at the container boundary.
- Artifact tests assert emitted bytes match SDK header-size and payload-record placement constants.
- C ABI stage-output reflection remains host-facing output-only reflection.

## Blockers

- None currently.

## Continuation Notes

- Avoid reverting concurrent edits already present in `rtsl-sdk/include/rtsl/artifact.hpp`, `rtsl/include/rtsl.h`, `rtsl/src/artifact/artifact.cpp`, `rtsl/src/api/rtsl.cpp`, `rtslc/src/rtslc.cpp`, and tests.
- SDK still does not own a backend-ready decoded artifact reader/view. Moving `Artifact`, `IRModule`, `IRInstruction`, or linker behavior into SDK is too risky now because those types still carry compiler lowering/linker state.
- Reflection structs are now present in `rtsl-sdk/include/rtsl/artifact.hpp` from concurrent work; this pass only validated that no duplicate internal definitions remained and that tests still pass.
- The artifact header size is now 40 bytes in the current writer. The old SDK constant was 64 bytes in `HEAD`; this is a format change that is acceptable before v0.1 only if coordinated with any existing consumers.
